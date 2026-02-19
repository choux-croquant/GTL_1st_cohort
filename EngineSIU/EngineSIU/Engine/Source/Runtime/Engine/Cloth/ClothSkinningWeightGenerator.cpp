/**
 * Cloth Skinning Weight Generator Implementation
 */

#include "ClothSkinningWeightGenerator.h"
#include <cmath>
#include <algorithm>

// Spatial Hash Implementation
void FClothSkinningWeightGenerator::FSimpleSpatialHash::Build(
    const TArray<FVector>& Positions,
    float InCellSize)
{
    Grid.Empty();
    CellSize = InCellSize;
    
    if (Positions.Num() == 0)
        return;
    
    // Compute bounds
    MinBounds = Positions[0];
    MaxBounds = Positions[0];
    
    for (const FVector& pos : Positions)
    {
        MinBounds.X = (pos.X < MinBounds.X) ? pos.X : MinBounds.X;
        MinBounds.Y = (pos.Y < MinBounds.Y) ? pos.Y : MinBounds.Y;
        MinBounds.Z = (pos.Z < MinBounds.Z) ? pos.Z : MinBounds.Z;
        
        MaxBounds.X = (pos.X > MaxBounds.X) ? pos.X : MaxBounds.X;
        MaxBounds.Y = (pos.Y > MaxBounds.Y) ? pos.Y : MaxBounds.Y;
        MaxBounds.Z = (pos.Z > MaxBounds.Z) ? pos.Z : MaxBounds.Z;
    }
    
    // Insert vertices into grid
    for (int32 i = 0; i < Positions.Num(); ++i)
    {
        uint64 key = GetCellKey(Positions[i]);
        Grid.FindOrAdd(key).Add(static_cast<uint32>(i));
    }
}

void FClothSkinningWeightGenerator::FSimpleSpatialHash::FindNearestNeighbors(
    const FVector& QueryPos,
    uint32 K,
    const TArray<FVector>& Positions,
    TArray<uint32>& OutIndices,
    TArray<float>& OutDistances) const
{
    OutIndices.Empty();
    OutDistances.Empty();
    
    struct FCandidate
    {
        uint32 Index;
        float DistanceSquared;
        
        bool operator<(const FCandidate& Other) const
        {
            return DistanceSquared < Other.DistanceSquared;
        }
    };
    
    TArray<FCandidate> candidates;
    
    // Search in 3x3x3 neighborhood of cells
    int32 qx, qy, qz;
    GetCellCoords(QueryPos, qx, qy, qz);
    
    for (int32 dx = -1; dx <= 1; ++dx)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            for (int32 dz = -1; dz <= 1; ++dz)
            {
                int32 cx = qx + dx;
                int32 cy = qy + dy;
                int32 cz = qz + dz;
                
                // Compute cell key
                uint64 key = (static_cast<uint64>(cx) & 0xFFFF) |
                            ((static_cast<uint64>(cy) & 0xFFFF) << 16) |
                            ((static_cast<uint64>(cz) & 0xFFFF) << 32);
                
                const TArray<uint32>* cellVertices = Grid.Find(key);
                if (cellVertices)
                {
                    for (uint32 idx : *cellVertices)
                    {
                        FVector diff = Positions[idx] - QueryPos;
                        float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
                        
                        FCandidate candidate;
                        candidate.Index = idx;
                        candidate.DistanceSquared = distSq;
                        candidates.Add(candidate);
                    }
                }
            }
        }
    }
    
    // Sort candidates by distance
    candidates.Sort();
    
    // Take K nearest
    uint32 numToTake = (candidates.Num() < static_cast<int32>(K)) ? candidates.Num() : K;
    for (uint32 i = 0; i < numToTake; ++i)
    {
        OutIndices.Add(candidates[i].Index);
        OutDistances.Add(sqrtf(candidates[i].DistanceSquared));
    }
}

uint64 FClothSkinningWeightGenerator::FSimpleSpatialHash::GetCellKey(const FVector& Pos) const
{
    int32 x, y, z;
    GetCellCoords(Pos, x, y, z);
    
    return (static_cast<uint64>(x) & 0xFFFF) |
           ((static_cast<uint64>(y) & 0xFFFF) << 16) |
           ((static_cast<uint64>(z) & 0xFFFF) << 32);
}

void FClothSkinningWeightGenerator::FSimpleSpatialHash::GetCellCoords(
    const FVector& Pos,
    int32& X,
    int32& Y,
    int32& Z) const
{
    X = static_cast<int32>(floorf((Pos.X - MinBounds.X) / CellSize));
    Y = static_cast<int32>(floorf((Pos.Y - MinBounds.Y) / CellSize));
    Z = static_cast<int32>(floorf((Pos.Z - MinBounds.Z) / CellSize));
}

// Main Generation Function
bool FClothSkinningWeightGenerator::GenerateSkinningWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const FClothSkinningParams& Params,
    FClothSkinningResult& OutResult)
{
    OutResult.bSuccess = false;
    OutResult.NumRenderVertices = RenderPositions.Num();
    OutResult.NumSimVertices = SimPositions.Num();
    OutResult.VerticesWithoutInfluences = 0;
    
    if (RenderPositions.Num() == 0 || SimPositions.Num() == 0)
    {
        OutResult.ErrorMessage = "Invalid input: empty mesh";
        return false;
    }
    
    // Build spatial hash for fast nearest neighbor search
    FSimpleSpatialHash spatialHash;
    
    // Compute average edge length for cell size
    float avgEdgeLength = 1.0f;  // Default
    if (RenderPositions.Num() > 1)
    {
        float sum = 0.0f;
        int32 count = 0;
        int32 sampleCount = (RenderPositions.Num() > 100) ? 100 : RenderPositions.Num();
        
        for (int32 i = 0; i < sampleCount && i + 1 < RenderPositions.Num(); ++i)
        {
            FVector diff = RenderPositions[i + 1] - RenderPositions[i];
            float dist = sqrtf(diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z);
            if (dist > 1e-6f)
            {
                sum += dist;
                count++;
            }
        }
        
        if (count > 0)
        {
            avgEdgeLength = sum / static_cast<float>(count);
        }
    }
    
    spatialHash.Build(SimPositions, avgEdgeLength * 2.0f);
    
    // Generate weights for each render vertex
    OutResult.Weights.SetNum(RenderPositions.Num());
    
    for (int32 renderIdx = 0; renderIdx < RenderPositions.Num(); ++renderIdx)
    {
        const FVector& renderPos = RenderPositions[renderIdx];
        FClothSkinningWeight& weight = OutResult.Weights[renderIdx];
        
        // Find K nearest simulation vertices
        TArray<uint32> nearestIndices;
        TArray<float> nearestDistances;
        
        spatialHash.FindNearestNeighbors(
            renderPos,
            Params.MaxInfluences,
            SimPositions,
            nearestIndices,
            nearestDistances
        );
        
        // Check if we found any influences
        if (nearestIndices.Num() == 0)
        {
            OutResult.VerticesWithoutInfluences++;
            
            // Fallback: find absolute nearest (brute force)
            float minDist = FLT_MAX;
            int32 nearestIdx = 0;
            
            for (int32 simIdx = 0; simIdx < SimPositions.Num(); ++simIdx)
            {
                FVector diff = SimPositions[simIdx] - renderPos;
                float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
                
                if (distSq < minDist)
                {
                    minDist = distSq;
                    nearestIdx = simIdx;
                }
            }
            
            nearestIndices.Add(nearestIdx);
            nearestDistances.Add(sqrtf(minDist));
        }
        
        // Filter influences by max distance
        TArray<uint32> filteredIndices;
        TArray<float> filteredDistances;
        
        for (int32 i = 0; i < nearestIndices.Num(); ++i)
        {
            if (nearestDistances[i] <= Params.MaxDistance)
            {
                filteredIndices.Add(nearestIndices[i]);
                filteredDistances.Add(nearestDistances[i]);
            }
        }
        
        // If no influences within max distance, use nearest
        if (filteredIndices.Num() == 0 && nearestIndices.Num() > 0)
        {
            filteredIndices.Add(nearestIndices[0]);
            filteredDistances.Add(nearestDistances[0]);
        }
        
        // Compute weights from distances
        TArray<float> blendWeights;
        ComputeWeightsFromDistances(filteredDistances, Params, blendWeights);
        
        // Store influences
        weight.NumInfluences = (filteredIndices.Num() < CLOTH_MAX_SKINNING_INFLUENCES) 
            ? filteredIndices.Num() 
            : CLOTH_MAX_SKINNING_INFLUENCES;
        
        for (uint32 i = 0; i < weight.NumInfluences; ++i)
        {
            weight.SimVertexIndices[i] = filteredIndices[i];
            weight.Weights[i] = blendWeights[i];
        }
    }
    
    // Validate weights
    if (!ValidateWeights(OutResult.Weights, OutResult.ErrorMessage))
    {
        return false;
    }
    
    OutResult.bSuccess = true;
    return true;
}

void FClothSkinningWeightGenerator::ComputeWeightsFromDistances(
    const TArray<float>& Distances,
    const FClothSkinningParams& Params,
    TArray<float>& OutWeights)
{
    OutWeights.SetNum(Distances.Num());
    
    if (Distances.Num() == 0)
        return;
    
    if (Distances.Num() == 1)
    {
        OutWeights[0] = 1.0f;
        return;
    }
    
    // Compute inverse distance weights
    float totalWeight = 0.0f;
    
    for (int32 i = 0; i < Distances.Num(); ++i)
    {
        float dist = Distances[i];
        
        if (dist < 1e-6f)
        {
            // Very close - give it full weight
            OutWeights.SetNum(Distances.Num());
            for (int32 j = 0; j < OutWeights.Num(); ++j)
            {
                OutWeights[j] = 0.0f;
            }
            OutWeights[i] = 1.0f;
            return;
        }
        
        if (Params.bUseInverseDistanceWeighting)
        {
            // Inverse distance weighting: w = 1 / dist^power
            float weight = 1.0f / powf(dist, Params.WeightPower);
            OutWeights[i] = weight;
            totalWeight += weight;
        }
        else
        {
            // Uniform weighting
            OutWeights[i] = 1.0f;
            totalWeight += 1.0f;
        }
    }
    
    // Normalize weights
    if (Params.bNormalizeWeights && totalWeight > 1e-6f)
    {
        for (int32 i = 0; i < OutWeights.Num(); ++i)
        {
            OutWeights[i] /= totalWeight;
        }
    }
}

bool FClothSkinningWeightGenerator::ValidateWeights(
    const TArray<FClothSkinningWeight>& Weights,
    FString& OutErrorMessage)
{
    for (int32 i = 0; i < Weights.Num(); ++i)
    {
        const FClothSkinningWeight& weight = Weights[i];
        
        if (weight.NumInfluences == 0)
        {
            OutErrorMessage = "Render vertex has no influences";
            return false;
        }
        
        // Check weight sum
        float sum = 0.0f;
        for (uint32 j = 0; j < weight.NumInfluences; ++j)
        {
            sum += weight.Weights[j];
        }
        
        if (fabs(sum - 1.0f) > 0.01f)  // Allow small tolerance
        {
            OutErrorMessage = "Weights do not sum to 1.0";
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Triangle-Based Skinning Weight Generation (Tangent-Space Offset Method)
// ============================================================================

/**
 * Compute barycentric coordinates of point P in triangle (A, B, C)
 * Returns (u, v, w) where P = u*A + v*B + w*C and u+v+w=1
 */
FVector FClothSkinningWeightGenerator::ComputeBarycentricCoordinates(
    const FVector& P,
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    FVector v0 = B - A;
    FVector v1 = C - A;
    FVector v2 = P - A;
    
    float d00 = FVector::DotProduct(v0, v0);
    float d01 = FVector::DotProduct(v0, v1);
    float d11 = FVector::DotProduct(v1, v1);
    float d20 = FVector::DotProduct(v2, v0);
    float d21 = FVector::DotProduct(v2, v1);
    
    float denom = d00 * d11 - d01 * d01;
    if (fabs(denom) < 1e-6f)
    {
        // Degenerate triangle - return vertex A
        return FVector(1.0f, 0.0f, 0.0f);
    }
    
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;
    
    return FVector(u, v, w);
}

/**
 * Find closest triangle to a point using brute force search
 * Returns triangle index and closest point on that triangle
 */
int32 FClothSkinningWeightGenerator::FindClosestTriangleBruteForce(
    const FVector& Point,
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    FVector& OutClosestPoint,
    float& OutMinDistance)
{
    OutMinDistance = FLT_MAX;
    OutClosestPoint = Point;
    int32 closestTriIdx = -1;
    
    uint32 numTriangles = Indices.Num() / 3;
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = Indices[triIdx * 3 + 0];
        uint32 i1 = Indices[triIdx * 3 + 1];
        uint32 i2 = Indices[triIdx * 3 + 2];
        
        if (i0 >= (uint32)Positions.Num() || i1 >= (uint32)Positions.Num() || i2 >= (uint32)Positions.Num())
            continue;
        
        FVector v0 = Positions[i0];
        FVector v1 = Positions[i1];
        FVector v2 = Positions[i2];
        
        // Find closest point on this triangle (using existing ClosestPointOnTriangle from decimator)
        // For now, use simple projection
        FVector ab = v1 - v0;
        FVector ac = v2 - v0;
        FVector ap = Point - v0;
        
        float d1 = FVector::DotProduct(ab, ap);
        float d2 = FVector::DotProduct(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
        {
            // Vertex region A
            FVector diff = Point - v0;
            float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (distSq < OutMinDistance)
            {
                OutMinDistance = distSq;
                OutClosestPoint = v0;
                closestTriIdx = triIdx;
            }
            continue;
        }
        
        FVector bp = Point - v1;
        float d3 = FVector::DotProduct(ab, bp);
        float d4 = FVector::DotProduct(ac, bp);
        if (d3 >= 0.0f && d4 <= d3)
        {
            // Vertex region B
            FVector diff = Point - v1;
            float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (distSq < OutMinDistance)
            {
                OutMinDistance = distSq;
                OutClosestPoint = v1;
                closestTriIdx = triIdx;
            }
            continue;
        }
        
        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            // Edge region AB
            float v = d1 / (d1 - d3);
            FVector projected = v0 + ab * v;
            FVector diff = Point - projected;
            float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (distSq < OutMinDistance)
            {
                OutMinDistance = distSq;
                OutClosestPoint = projected;
                closestTriIdx = triIdx;
            }
            continue;
        }
        
        FVector cp = Point - v2;
        float d5 = FVector::DotProduct(ab, cp);
        float d6 = FVector::DotProduct(ac, cp);
        if (d6 >= 0.0f && d5 <= d6)
        {
            // Vertex region C
            FVector diff = Point - v2;
            float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (distSq < OutMinDistance)
            {
                OutMinDistance = distSq;
                OutClosestPoint = v2;
                closestTriIdx = triIdx;
            }
            continue;
        }
        
        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            // Edge region AC
            float w = d2 / (d2 - d6);
            FVector projected = v0 + ac * w;
            FVector diff = Point - projected;
            float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (distSq < OutMinDistance)
            {
                OutMinDistance = distSq;
                OutClosestPoint = projected;
                closestTriIdx = triIdx;
            }
            continue;
        }
        
        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            // Edge region BC
            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            FVector projected = v1 + (v2 - v1) * w;
            FVector diff = Point - projected;
            float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
            if (distSq < OutMinDistance)
            {
                OutMinDistance = distSq;
                OutClosestPoint = projected;
                closestTriIdx = triIdx;
            }
            continue;
        }
        
        // Inside face region
        float denom = 1.0f / (va + vb + vc);
        float v = vb * denom;
        float w = vc * denom;
        FVector projected = v0 + ab * v + ac * w;
        FVector diff = Point - projected;
        float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
        if (distSq < OutMinDistance)
        {
            OutMinDistance = distSq;
            OutClosestPoint = projected;
            closestTriIdx = triIdx;
        }
    }
    
    OutMinDistance = sqrtf(OutMinDistance);
    return closestTriIdx;
}

/**
 * Compute tangent frame for a triangle
 * Tangent = normalized edge0, Bitangent = cross(Normal, Tangent), Normal = cross(edge0, edge1)
 */
void FClothSkinningWeightGenerator::ComputeTangentFrame(
    const FVector& V0,
    const FVector& V1,
    const FVector& V2,
    FVector& OutTangent,
    FVector& OutBitangent,
    FVector& OutNormal)
{
    FVector edge1 = V1 - V0;
    FVector edge2 = V2 - V0;
    
    OutNormal = FVector::CrossProduct(edge1, edge2);
    float normalLen = sqrtf(OutNormal.X * OutNormal.X + OutNormal.Y * OutNormal.Y + OutNormal.Z * OutNormal.Z);
    if (normalLen > 1e-6f)
        OutNormal = OutNormal / normalLen;
    else
        OutNormal = FVector(0.0f, 0.0f, 1.0f); // Degenerate triangle fallback
    
    OutTangent = edge1;
    float tangentLen = sqrtf(OutTangent.X * OutTangent.X + OutTangent.Y * OutTangent.Y + OutTangent.Z * OutTangent.Z);
    if (tangentLen > 1e-6f)
        OutTangent = OutTangent / tangentLen;
    else
        OutTangent = FVector(1.0f, 0.0f, 0.0f); // Degenerate edge fallback
    
    OutBitangent = FVector::CrossProduct(OutNormal, OutTangent);
    float bitangentLen = sqrtf(OutBitangent.X * OutBitangent.X + OutBitangent.Y * OutBitangent.Y + OutBitangent.Z * OutBitangent.Z);
    if (bitangentLen > 1e-6f)
        OutBitangent = OutBitangent / bitangentLen;
    else
        OutBitangent = FVector(0.0f, 1.0f, 0.0f); // Fallback
}

/**
 * Generate triangle-based skinning weights with tangent-space offsets
 * This method fixes edge curling and UV distortion by preserving local surface detail
 */
bool FClothSkinningWeightGenerator::GenerateTriangleSkinningWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const TArray<uint32>& SimIndices,
    const FClothSkinningParams& Params,
    TArray<FClothSkinningWeightTriangle>& OutWeights,
    FString& OutError)
{
    OutWeights.Empty();
    
    if (RenderPositions.Num() == 0 || SimPositions.Num() == 0 || SimIndices.Num() < 3)
    {
        OutError = "Invalid input: empty mesh or no triangles";
        return false;
    }
    
    uint32 numSimTriangles = SimIndices.Num() / 3;
    if (numSimTriangles == 0)
    {
        OutError = "Simulation mesh has no triangles";
        return false;
    }
    
    OutWeights.SetNum(RenderPositions.Num());
    
    // Process each render vertex
    for (int32 renderIdx = 0; renderIdx < RenderPositions.Num(); ++renderIdx)
    {
        const FVector& renderPos = RenderPositions[renderIdx];
        FClothSkinningWeightTriangle& weight = OutWeights[renderIdx];
        
        // 1. Find closest simulation triangle
        FVector closestPoint;
        float minDist;
        int32 closestTriIdx = FindClosestTriangleBruteForce(
            renderPos, SimPositions, SimIndices, closestPoint, minDist);
        
        if (closestTriIdx < 0)
        {
            OutError = "Failed to find closest triangle for render vertex";
            return false;
        }
        
        // 2. Get triangle vertices
        uint32 i0 = SimIndices[closestTriIdx * 3 + 0];
        uint32 i1 = SimIndices[closestTriIdx * 3 + 1];
        uint32 i2 = SimIndices[closestTriIdx * 3 + 2];
        
        if (i0 >= (uint32)SimPositions.Num() || i1 >= (uint32)SimPositions.Num() || i2 >= (uint32)SimPositions.Num())
        {
            OutError = "Invalid triangle indices in simulation mesh";
            return false;
        }
        
        FVector v0 = SimPositions[i0];
        FVector v1 = SimPositions[i1];
        FVector v2 = SimPositions[i2];
        
        // 3. Compute barycentric coordinates of closest point
        FVector bary = ComputeBarycentricCoordinates(closestPoint, v0, v1, v2);
        
        // 4. Compute tangent frame (rest pose)
        FVector tangent, bitangent, normal;
        ComputeTangentFrame(v0, v1, v2, tangent, bitangent, normal);
        
        // 5. Compute offset in tangent space
        FVector offsetWorld = renderPos - closestPoint;
        FVector offsetTangent;
        offsetTangent.X = FVector::DotProduct(offsetWorld, tangent);
        offsetTangent.Y = FVector::DotProduct(offsetWorld, bitangent);
        offsetTangent.Z = FVector::DotProduct(offsetWorld, normal);
        
        // 6. Store result
        weight.SimTriangleIndices[0] = i0;
        weight.SimTriangleIndices[1] = i1;
        weight.SimTriangleIndices[2] = i2;
        weight.BarycentricCoords[0] = bary.X;
        weight.BarycentricCoords[1] = bary.Y;
        weight.BarycentricCoords[2] = bary.Z;
        weight.TangentSpaceOffset = offsetTangent;
    }
    
    return true;
}
