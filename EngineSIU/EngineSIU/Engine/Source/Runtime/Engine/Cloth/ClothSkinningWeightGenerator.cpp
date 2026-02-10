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

// ========== Main Generation Functions ==========

// New overload with triangle indices (supports both methods)
bool FClothSkinningWeightGenerator::GenerateSkinningWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const TArray<uint32>& SimIndices,
    const FClothSkinningParams& Params,
    FClothSkinningResult& OutResult)
{
    // Method dispatch
    if (Params.WeightingMethod == EClothWeightingMethod::Barycentric)
    {
        if (SimIndices.Num() == 0)
        {
            OutResult.bSuccess = false;
            OutResult.ErrorMessage = "Barycentric method requires triangle indices";
            return false;
        }
        return GenerateBarycentricWeights(RenderPositions, SimPositions, SimIndices, Params, OutResult);
    }
    else
    {
        return GenerateInverseDistanceWeights(RenderPositions, SimPositions, Params, OutResult);
    }
}

// Legacy overload for backward compatibility (uses inverse distance)
bool FClothSkinningWeightGenerator::GenerateSkinningWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const FClothSkinningParams& Params,
    FClothSkinningResult& OutResult)
{
    // Force inverse distance method for legacy calls
    FClothSkinningParams modifiedParams = Params;
    modifiedParams.WeightingMethod = EClothWeightingMethod::InverseDistance;
    
    TArray<uint32> emptyIndices;
    return GenerateSkinningWeights(RenderPositions, SimPositions, emptyIndices, modifiedParams, OutResult);
}

// ========== Inverse Distance Method (Legacy) ==========

bool FClothSkinningWeightGenerator::GenerateInverseDistanceWeights(
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

// ========== Barycentric Method Implementation ==========

/**
 * Calculate barycentric coordinates for a point relative to a triangle
 */
FBarycentricCoordinates FClothSkinningWeightGenerator::CalculateBarycentricCoordinates(
    const FVector& Point,
    const FVector& TriV0,
    const FVector& TriV1,
    const FVector& TriV2)
{
    FBarycentricCoordinates result;
    
    // Compute vectors
    FVector v0 = TriV1 - TriV0;
    FVector v1 = TriV2 - TriV0;
    FVector v2 = Point - TriV0;
    
    // Compute dot products
    float d00 = v0.X * v0.X + v0.Y * v0.Y + v0.Z * v0.Z;
    float d01 = v0.X * v1.X + v0.Y * v1.Y + v0.Z * v1.Z;
    float d11 = v1.X * v1.X + v1.Y * v1.Y + v1.Z * v1.Z;
    float d20 = v2.X * v0.X + v2.Y * v0.Y + v2.Z * v0.Z;
    float d21 = v2.X * v1.X + v2.Y * v1.Y + v2.Z * v1.Z;
    
    // Compute barycentric coordinates
    float denom = d00 * d11 - d01 * d01;
    
    if (fabs(denom) < 1e-8f)
    {
        // Degenerate triangle
        result.bIsValid = false;
        return result;
    }
    
    float invDenom = 1.0f / denom;
    result.V = (d11 * d20 - d01 * d21) * invDenom;
    result.W = (d00 * d21 - d01 * d20) * invDenom;
    result.U = 1.0f - result.V - result.W;
    
    result.bIsValid = true;
    
    // Calculate distance to triangle plane
    FVector normal = FVector::CrossProduct(v0, v1);
    float normalLength = sqrtf(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
    
    if (normalLength > 1e-8f)
    {
        normal.X /= normalLength;
        normal.Y /= normalLength;
        normal.Z /= normalLength;
        
        result.DistanceToTriangle = fabs(normal.X * v2.X + normal.Y * v2.Y + normal.Z * v2.Z);
    }
    else
    {
        result.DistanceToTriangle = 0.0f;
    }
    
    return result;
}

/**
 * Project point onto triangle and return closest point
 */
FVector FClothSkinningWeightGenerator::ProjectPointOntoTriangle(
    const FVector& Point,
    const FVector& TriV0,
    const FVector& TriV1,
    const FVector& TriV2,
    FBarycentricCoordinates& OutBarycentrics)
{
    // First calculate barycentric coordinates
    OutBarycentrics = CalculateBarycentricCoordinates(Point, TriV0, TriV1, TriV2);
    
    if (!OutBarycentrics.bIsValid)
    {
        // Degenerate triangle - return nearest vertex
        float dist0Sq = (Point - TriV0).SizeSquared();
        float dist1Sq = (Point - TriV1).SizeSquared();
        float dist2Sq = (Point - TriV2).SizeSquared();
        
        if (dist0Sq <= dist1Sq && dist0Sq <= dist2Sq)
        {
            OutBarycentrics.U = 1.0f;
            OutBarycentrics.V = 0.0f;
            OutBarycentrics.W = 0.0f;
            return TriV0;
        }
        else if (dist1Sq <= dist2Sq)
        {
            OutBarycentrics.U = 0.0f;
            OutBarycentrics.V = 1.0f;
            OutBarycentrics.W = 0.0f;
            return TriV1;
        }
        else
        {
            OutBarycentrics.U = 0.0f;
            OutBarycentrics.V = 0.0f;
            OutBarycentrics.W = 1.0f;
            return TriV2;
        }
    }
    
    // Check if point is inside triangle
    if (OutBarycentrics.IsInsideTriangle())
    {
        // Point projects inside triangle
        FVector v0 = TriV1 - TriV0;
        FVector v1 = TriV2 - TriV0;
        FVector normal = FVector::CrossProduct(v0, v1);
        float normalLenSq = normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z;
        
        if (normalLenSq > 1e-8f)
        {
            // Project onto triangle plane
            FVector v2 = Point - TriV0;
            float dist = (normal.X * v2.X + normal.Y * v2.Y + normal.Z * v2.Z) / normalLenSq;
            return Point - normal * dist;
        }
    }
    
    // Point projects outside triangle - find closest point on edges
    auto ClampToEdge = [](const FVector& P, const FVector& A, const FVector& B) -> FVector
    {
        FVector AB = B - A;
        FVector AP = P - A;
        float t = (AP.X * AB.X + AP.Y * AB.Y + AP.Z * AB.Z) /
                  (AB.X * AB.X + AB.Y * AB.Y + AB.Z * AB.Z);
        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        return A + AB * t;
    };
    
    FVector closestOnEdge0 = ClampToEdge(Point, TriV0, TriV1);
    FVector closestOnEdge1 = ClampToEdge(Point, TriV1, TriV2);
    FVector closestOnEdge2 = ClampToEdge(Point, TriV2, TriV0);
    
    float dist0Sq = (Point - closestOnEdge0).SizeSquared();
    float dist1Sq = (Point - closestOnEdge1).SizeSquared();
    float dist2Sq = (Point - closestOnEdge2).SizeSquared();
    
    FVector closestPoint;
    if (dist0Sq <= dist1Sq && dist0Sq <= dist2Sq)
    {
        closestPoint = closestOnEdge0;
    }
    else if (dist1Sq <= dist2Sq)
    {
        closestPoint = closestOnEdge1;
    }
    else
    {
        closestPoint = closestOnEdge2;
    }
    
    // Recalculate barycentric coordinates for clamped point
    OutBarycentrics = CalculateBarycentricCoordinates(closestPoint, TriV0, TriV1, TriV2);
    
    // Clamp barycentric coordinates to [0,1]
    OutBarycentrics.U = (OutBarycentrics.U < 0.0f) ? 0.0f : OutBarycentrics.U;
    OutBarycentrics.V = (OutBarycentrics.V < 0.0f) ? 0.0f : OutBarycentrics.V;
    OutBarycentrics.W = (OutBarycentrics.W < 0.0f) ? 0.0f : OutBarycentrics.W;
    
    // Renormalize
    float sum = OutBarycentrics.U + OutBarycentrics.V + OutBarycentrics.W;
    if (sum > 1e-8f)
    {
        OutBarycentrics.U /= sum;
        OutBarycentrics.V /= sum;
        OutBarycentrics.W /= sum;
    }
    
    return closestPoint;
}

/**
 * Calculate distance from point to triangle
 */
float FClothSkinningWeightGenerator::CalculatePointToTriangleDistance(
    const FVector& Point,
    const FVector& TriV0,
    const FVector& TriV1,
    const FVector& TriV2)
{
    FBarycentricCoordinates bary;
    FVector closestPoint = ProjectPointOntoTriangle(Point, TriV0, TriV1, TriV2, bary);
    
    FVector diff = Point - closestPoint;
    return sqrtf(diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z);
}
