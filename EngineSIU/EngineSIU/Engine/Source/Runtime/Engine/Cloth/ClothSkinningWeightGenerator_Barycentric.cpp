/**
 * Cloth Skinning Weight Generator - Barycentric Implementation
 * BVH and barycentric weight generation functions
 */

#include "ClothSkinningWeightGenerator.h"
#include <cmath>
#include <algorithm>

// ========== BVH Implementation ==========

void FClothSkinningWeightGenerator::FTriangleBVH::Build(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    int32 MaxLeafTriangles)
{
    Nodes.Empty();
    TriangleIndices.Empty();
    
    uint32 numTriangles = Indices.Num() / 3;
    if (numTriangles == 0)
        return;
    
    // Initialize triangle list
    TArray<uint32> triangleList;
    triangleList.Reserve(numTriangles);
    for (uint32 i = 0; i < numTriangles; ++i)
    {
        triangleList.Add(i);
    }
    
    // Build BVH recursively
    BuildRecursive(Vertices, Indices, triangleList, 0, numTriangles, MaxLeafTriangles);
}

int32 FClothSkinningWeightGenerator::FTriangleBVH::BuildRecursive(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    TArray<uint32>& TriangleList,
    int32 Start,
    int32 End,
    int32 MaxLeafTriangles)
{
    int32 nodeIndex = Nodes.Num();
    Nodes.AddDefaulted();
    FBVHNode& node = Nodes[nodeIndex];
    
    // Calculate bounding box for all triangles in range
    node.BoundingBox = CalculateTriangleBounds(Vertices, Indices, TriangleList[Start]);
    
    for (int32 i = Start + 1; i < End; ++i)
    {
        FBoundingBox triBounds = CalculateTriangleBounds(Vertices, Indices, TriangleList[i]);
        
        // Expand bounding box
        node.BoundingBox.MinLocation.X = (triBounds.MinLocation.X < node.BoundingBox.MinLocation.X) ? triBounds.MinLocation.X : node.BoundingBox.MinLocation.X;
        node.BoundingBox.MinLocation.Y = (triBounds.MinLocation.Y < node.BoundingBox.MinLocation.Y) ? triBounds.MinLocation.Y : node.BoundingBox.MinLocation.Y;
        node.BoundingBox.MinLocation.Z = (triBounds.MinLocation.Z < node.BoundingBox.MinLocation.Z) ? triBounds.MinLocation.Z : node.BoundingBox.MinLocation.Z;
        
        node.BoundingBox.MaxLocation.X = (triBounds.MaxLocation.X > node.BoundingBox.MaxLocation.X) ? triBounds.MaxLocation.X : node.BoundingBox.MaxLocation.X;
        node.BoundingBox.MaxLocation.Y = (triBounds.MaxLocation.Y > node.BoundingBox.MaxLocation.Y) ? triBounds.MaxLocation.Y : node.BoundingBox.MaxLocation.Y;
        node.BoundingBox.MaxLocation.Z = (triBounds.MaxLocation.Z > node.BoundingBox.MaxLocation.Z) ? triBounds.MaxLocation.Z : node.BoundingBox.MaxLocation.Z;
    }
    
    int32 count = End - Start;
    
    // Create leaf if small enough
    if (count <= MaxLeafTriangles)
    {
        node.TriangleStart = TriangleIndices.Num();
        node.TriangleCount = count;
        
        for (int32 i = Start; i < End; ++i)
        {
            TriangleIndices.Add(TriangleList[i]);
        }
        
        return nodeIndex;
    }
    
    // Find split axis (longest axis)
    FVector extent = node.BoundingBox.MaxLocation - node.BoundingBox.MinLocation;
    int32 splitAxis = 0;
    if (extent.Y > extent.X && extent.Y > extent.Z)
        splitAxis = 1;
    else if (extent.Z > extent.X)
        splitAxis = 2;
    
    // Sort triangles by centroid along split axis
    std::sort(TriangleList.GetData() + Start, TriangleList.GetData() + End,
        [&](uint32 a, uint32 b)
        {
            FVector centroidA = GetTriangleCentroid(Vertices, Indices, a);
            FVector centroidB = GetTriangleCentroid(Vertices, Indices, b);
            
            if (splitAxis == 0)
                return centroidA.X < centroidB.X;
            else if (splitAxis == 1)
                return centroidA.Y < centroidB.Y;
            else
                return centroidA.Z < centroidB.Z;
        });
    
    // Split in middle
    int32 mid = (Start + End) / 2;
    
    // Build children
    node.LeftChild = BuildRecursive(Vertices, Indices, TriangleList, Start, mid, MaxLeafTriangles);
    node.RightChild = BuildRecursive(Vertices, Indices, TriangleList, mid, End, MaxLeafTriangles);
    
    return nodeIndex;
}

FBoundingBox FClothSkinningWeightGenerator::FTriangleBVH::CalculateTriangleBounds(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    uint32 TriangleIndex) const
{
    uint32 i0 = Indices[TriangleIndex * 3 + 0];
    uint32 i1 = Indices[TriangleIndex * 3 + 1];
    uint32 i2 = Indices[TriangleIndex * 3 + 2];
    
    const FVector& v0 = Vertices[i0];
    const FVector& v1 = Vertices[i1];
    const FVector& v2 = Vertices[i2];
    
    FVector minLoc, maxLoc;
    
    minLoc.X = (v0.X < v1.X) ? v0.X : v1.X;
    minLoc.X = (v2.X < minLoc.X) ? v2.X : minLoc.X;
    
    minLoc.Y = (v0.Y < v1.Y) ? v0.Y : v1.Y;
    minLoc.Y = (v2.Y < minLoc.Y) ? v2.Y : minLoc.Y;
    
    minLoc.Z = (v0.Z < v1.Z) ? v0.Z : v1.Z;
    minLoc.Z = (v2.Z < minLoc.Z) ? v2.Z : minLoc.Z;
    
    maxLoc.X = (v0.X > v1.X) ? v0.X : v1.X;
    maxLoc.X = (v2.X > maxLoc.X) ? v2.X : maxLoc.X;
    
    maxLoc.Y = (v0.Y > v1.Y) ? v0.Y : v1.Y;
    maxLoc.Y = (v2.Y > maxLoc.Y) ? v2.Y : maxLoc.Y;
    
    maxLoc.Z = (v0.Z > v1.Z) ? v0.Z : v1.Z;
    maxLoc.Z = (v2.Z > maxLoc.Z) ? v2.Z : maxLoc.Z;
    
    return FBoundingBox(minLoc, maxLoc);
}

FVector FClothSkinningWeightGenerator::FTriangleBVH::GetTriangleCentroid(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    uint32 TriangleIndex) const
{
    uint32 i0 = Indices[TriangleIndex * 3 + 0];
    uint32 i1 = Indices[TriangleIndex * 3 + 1];
    uint32 i2 = Indices[TriangleIndex * 3 + 2];
    
    const FVector& v0 = Vertices[i0];
    const FVector& v1 = Vertices[i1];
    const FVector& v2 = Vertices[i2];
    
    return FVector(
        (v0.X + v1.X + v2.X) / 3.0f,
        (v0.Y + v1.Y + v2.Y) / 3.0f,
        (v0.Z + v1.Z + v2.Z) / 3.0f
    );
}

bool FClothSkinningWeightGenerator::FTriangleBVH::FindNearestTriangle(
    const FVector& QueryPoint,
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    uint32& OutTriangleIndex,
    float& OutDistance) const
{
    if (Nodes.Num() == 0)
        return false;
    
    OutTriangleIndex = 0;
    OutDistance = FLT_MAX;
    
    FindNearestRecursive(0, QueryPoint, Vertices, Indices, OutTriangleIndex, OutDistance);
    
    return OutDistance < FLT_MAX;
}

void FClothSkinningWeightGenerator::FTriangleBVH::FindNearestRecursive(
    int32 NodeIndex,
    const FVector& QueryPoint,
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    uint32& BestTriangle,
    float& BestDistance) const
{
    if (NodeIndex < 0 || NodeIndex >= Nodes.Num())
        return;
    
    const FBVHNode& node = Nodes[NodeIndex];
    
    // Calculate distance to bounding box
    float boxDist = 0.0f;
    
    // X axis
    if (QueryPoint.X < node.BoundingBox.MinLocation.X)
        boxDist += (node.BoundingBox.MinLocation.X - QueryPoint.X) * (node.BoundingBox.MinLocation.X - QueryPoint.X);
    else if (QueryPoint.X > node.BoundingBox.MaxLocation.X)
        boxDist += (QueryPoint.X - node.BoundingBox.MaxLocation.X) * (QueryPoint.X - node.BoundingBox.MaxLocation.X);
    
    // Y axis
    if (QueryPoint.Y < node.BoundingBox.MinLocation.Y)
        boxDist += (node.BoundingBox.MinLocation.Y - QueryPoint.Y) * (node.BoundingBox.MinLocation.Y - QueryPoint.Y);
    else if (QueryPoint.Y > node.BoundingBox.MaxLocation.Y)
        boxDist += (QueryPoint.Y - node.BoundingBox.MaxLocation.Y) * (QueryPoint.Y - node.BoundingBox.MaxLocation.Y);
    
    // Z axis
    if (QueryPoint.Z < node.BoundingBox.MinLocation.Z)
        boxDist += (node.BoundingBox.MinLocation.Z - QueryPoint.Z) * (node.BoundingBox.MinLocation.Z - QueryPoint.Z);
    else if (QueryPoint.Z > node.BoundingBox.MaxLocation.Z)
        boxDist += (QueryPoint.Z - node.BoundingBox.MaxLocation.Z) * (QueryPoint.Z - node.BoundingBox.MaxLocation.Z);
    
    boxDist = sqrtf(boxDist);
    
    // Early exit if this node can't contain closer triangle
    if (boxDist >= BestDistance)
        return;
    
    if (node.IsLeaf())
    {
        // Test all triangles in leaf
        for (int32 i = 0; i < node.TriangleCount; ++i)
        {
            uint32 triIdx = TriangleIndices[node.TriangleStart + i];
            
            uint32 i0 = Indices[triIdx * 3 + 0];
            uint32 i1 = Indices[triIdx * 3 + 1];
            uint32 i2 = Indices[triIdx * 3 + 2];
            
            float dist = CalculatePointToTriangleDistance(QueryPoint, Vertices[i0], Vertices[i1], Vertices[i2]);
            
            if (dist < BestDistance)
            {
                BestDistance = dist;
                BestTriangle = triIdx;
            }
        }
    }
    else
    {
        // Recurse into children
        FindNearestRecursive(node.LeftChild, QueryPoint, Vertices, Indices, BestTriangle, BestDistance);
        FindNearestRecursive(node.RightChild, QueryPoint, Vertices, Indices, BestTriangle, BestDistance);
    }
}

// ========== Barycentric Weight Generation ==========

bool FClothSkinningWeightGenerator::GenerateBarycentricWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const TArray<uint32>& SimIndices,
    const FClothSkinningParams& Params,
    FClothSkinningResult& OutResult)
{
    OutResult.bSuccess = false;
    OutResult.NumRenderVertices = RenderPositions.Num();
    OutResult.NumSimVertices = SimPositions.Num();
    OutResult.VerticesWithoutInfluences = 0;
    
    if (RenderPositions.Num() == 0 || SimPositions.Num() == 0 || SimIndices.Num() < 3)
    {
        OutResult.ErrorMessage = "Invalid input: empty mesh or no triangles";
        return false;
    }
    
    // Build BVH for fast triangle queries
    FTriangleBVH bvh;
    bvh.Build(SimPositions, SimIndices, Params.BVHMaxLeafTriangles);
    
    // Generate weights for each render vertex
    OutResult.Weights.SetNum(RenderPositions.Num());
    
    for (int32 renderIdx = 0; renderIdx < RenderPositions.Num(); ++renderIdx)
    {
        const FVector& renderPos = RenderPositions[renderIdx];
        FClothSkinningWeight& weight = OutResult.Weights[renderIdx];
        
        // Find nearest triangle
        uint32 nearestTriIdx = 0;
        float nearestDist = FLT_MAX;
        
        if (!bvh.FindNearestTriangle(renderPos, SimPositions, SimIndices, nearestTriIdx, nearestDist))
        {
            OutResult.VerticesWithoutInfluences++;
            
            // Fallback to inverse distance if enabled
            if (Params.bFallbackToInverseDistance)
            {
                // Use inverse distance method for this vertex
                FClothSkinningParams fallbackParams = Params;
                fallbackParams.WeightingMethod = EClothWeightingMethod::InverseDistance;
                
                TArray<FVector> singleVertex;
                singleVertex.Add(renderPos);
                
                FClothSkinningResult fallbackResult;
                if (GenerateInverseDistanceWeights(singleVertex, SimPositions, fallbackParams, fallbackResult))
                {
                    weight = fallbackResult.Weights[0];
                    continue;
                }
            }
            
            // Last resort: use nearest vertex
            float minDistSq = FLT_MAX;
            int32 nearestVertIdx = 0;
            
            for (int32 simIdx = 0; simIdx < SimPositions.Num(); ++simIdx)
            {
                FVector diff = SimPositions[simIdx] - renderPos;
                float distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
                
                if (distSq < minDistSq)
                {
                    minDistSq = distSq;
                    nearestVertIdx = simIdx;
                }
            }
            
            weight.NumInfluences = 1;
            weight.SimVertexIndices[0] = nearestVertIdx;
            weight.Weights[0] = 1.0f;
            continue;
        }
        
        // Check if within max search distance
        if (nearestDist > Params.MaxSearchDistance)
        {
            OutResult.VerticesWithoutInfluences++;
            
            if (Params.bFallbackToInverseDistance)
            {
                // Fallback to inverse distance
                FClothSkinningParams fallbackParams = Params;
                fallbackParams.WeightingMethod = EClothWeightingMethod::InverseDistance;
                
                TArray<FVector> singleVertex;
                singleVertex.Add(renderPos);
                
                FClothSkinningResult fallbackResult;
                if (GenerateInverseDistanceWeights(singleVertex, SimPositions, fallbackParams, fallbackResult))
                {
                    weight = fallbackResult.Weights[0];
                    continue;
                }
            }
        }
        
        // Get triangle vertices
        uint32 i0 = SimIndices[nearestTriIdx * 3 + 0];
        uint32 i1 = SimIndices[nearestTriIdx * 3 + 1];
        uint32 i2 = SimIndices[nearestTriIdx * 3 + 2];
        
        const FVector& v0 = SimPositions[i0];
        const FVector& v1 = SimPositions[i1];
        const FVector& v2 = SimPositions[i2];
        
        // Calculate barycentric coordinates
        FBarycentricCoordinates bary;
        if (Params.bUseClosestPointProjection)
        {
            ProjectPointOntoTriangle(renderPos, v0, v1, v2, bary);
        }
        else
        {
            bary = CalculateBarycentricCoordinates(renderPos, v0, v1, v2);
        }
        
        if (!bary.bIsValid)
        {
            // Degenerate triangle - use nearest vertex
            float dist0Sq = (renderPos - v0).SizeSquared();
            float dist1Sq = (renderPos - v1).SizeSquared();
            float dist2Sq = (renderPos - v2).SizeSquared();
            
            weight.NumInfluences = 1;
            if (dist0Sq <= dist1Sq && dist0Sq <= dist2Sq)
            {
                weight.SimVertexIndices[0] = i0;
            }
            else if (dist1Sq <= dist2Sq)
            {
                weight.SimVertexIndices[0] = i1;
            }
            else
            {
                weight.SimVertexIndices[0] = i2;
            }
            weight.Weights[0] = 1.0f;
            continue;
        }
        
        // Store barycentric weights (3 influences from triangle vertices)
        weight.NumInfluences = 3;
        weight.SimVertexIndices[0] = i0;
        weight.SimVertexIndices[1] = i1;
        weight.SimVertexIndices[2] = i2;
        weight.Weights[0] = bary.U;
        weight.Weights[1] = bary.V;
        weight.Weights[2] = bary.W;
        
        // Ensure weights are normalized
        float sum = weight.Weights[0] + weight.Weights[1] + weight.Weights[2];
        if (fabs(sum - 1.0f) > 1e-6f && sum > 1e-8f)
        {
            weight.Weights[0] /= sum;
            weight.Weights[1] /= sum;
            weight.Weights[2] /= sum;
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
