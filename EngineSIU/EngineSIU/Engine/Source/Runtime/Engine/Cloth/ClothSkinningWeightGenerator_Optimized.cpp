/**
 * Cloth Skinning Weight Generator - Optimized Implementation
 * SAH-based BVH construction, triangle caching, and multi-threading support
 */

#include "ClothSkinningWeightGenerator.h"
#include <cmath>
#include <algorithm>

// ========== Optimized BVH with SAH ==========

void FClothSkinningWeightGenerator::FTriangleBVH::Build(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    int32 MaxLeafTriangles,
    bool bUseSAH,
    float TraversalCost,
    float IntersectionCost)
{
    Nodes.Empty();
    TriangleIndices.Empty();
    TriangleCache.Empty();
    bHasCache = false;
    
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
    BuildRecursive(Vertices, Indices, triangleList, 0, numTriangles, 
                   MaxLeafTriangles, bUseSAH, TraversalCost, IntersectionCost);
}

int32 FClothSkinningWeightGenerator::FTriangleBVH::BuildRecursive(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    TArray<uint32>& TriangleList,
    int32 Start,
    int32 End,
    int32 MaxLeafTriangles,
    bool bUseSAH,
    float TraversalCost,
    float IntersectionCost)
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
    
    // Find split position
    int32 mid;
    int32 splitAxis;
    
    if (bUseSAH)
    {
        // Use Surface Area Heuristic for optimal split
        mid = FindBestSAHSplit(Vertices, Indices, TriangleList, Start, End, 
                               node.BoundingBox, TraversalCost, IntersectionCost, splitAxis);
    }
    else
    {
        // Simple midpoint split along longest axis
        FVector extent = node.BoundingBox.MaxLocation - node.BoundingBox.MinLocation;
        splitAxis = 0;
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
        
        mid = (Start + End) / 2;
    }
    
    // Build children
    node.LeftChild = BuildRecursive(Vertices, Indices, TriangleList, Start, mid, 
                                    MaxLeafTriangles, bUseSAH, TraversalCost, IntersectionCost);
    node.RightChild = BuildRecursive(Vertices, Indices, TriangleList, mid, End, 
                                     MaxLeafTriangles, bUseSAH, TraversalCost, IntersectionCost);
    
    return nodeIndex;
}

/**
 * Find best split using Surface Area Heuristic (SAH)
 * Minimizes expected traversal cost: Cost = C_traverse + P_left * C_left + P_right * C_right
 */
int32 FClothSkinningWeightGenerator::FTriangleBVH::FindBestSAHSplit(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices,
    const TArray<uint32>& TriangleList,
    int32 Start,
    int32 End,
    const FBoundingBox& ParentBounds,
    float TraversalCost,
    float IntersectionCost,
    int32& OutSplitAxis) const
{
    float parentSurfaceArea = CalculateSurfaceArea(ParentBounds);
    float bestCost = FLT_MAX;
    int32 bestSplit = (Start + End) / 2;
    OutSplitAxis = 0;
    
    // Try each axis
    for (int32 axis = 0; axis < 3; ++axis)
    {
        // Sort triangles along this axis
        TArray<uint32> sortedList = TriangleList;
        
        std::sort(sortedList.GetData() + Start, sortedList.GetData() + End,
            [&](uint32 a, uint32 b)
            {
                FVector centroidA = GetTriangleCentroid(Vertices, Indices, a);
                FVector centroidB = GetTriangleCentroid(Vertices, Indices, b);
                
                if (axis == 0)
                    return centroidA.X < centroidB.X;
                else if (axis == 1)
                    return centroidA.Y < centroidB.Y;
                else
                    return centroidA.Z < centroidB.Z;
            });
        
        // Try different split positions
        int32 numSamples = (End - Start > 16) ? 16 : (End - Start);
        for (int32 sample = 1; sample < numSamples; ++sample)
        {
            int32 splitPos = Start + (End - Start) * sample / numSamples;
            
            // Calculate bounds for left and right partitions
            FBoundingBox leftBounds = CalculateTriangleBounds(Vertices, Indices, sortedList[Start]);
            for (int32 i = Start + 1; i < splitPos; ++i)
            {
                FBoundingBox triBounds = CalculateTriangleBounds(Vertices, Indices, sortedList[i]);
                leftBounds.MinLocation.X = (triBounds.MinLocation.X < leftBounds.MinLocation.X) ? triBounds.MinLocation.X : leftBounds.MinLocation.X;
                leftBounds.MinLocation.Y = (triBounds.MinLocation.Y < leftBounds.MinLocation.Y) ? triBounds.MinLocation.Y : leftBounds.MinLocation.Y;
                leftBounds.MinLocation.Z = (triBounds.MinLocation.Z < leftBounds.MinLocation.Z) ? triBounds.MinLocation.Z : leftBounds.MinLocation.Z;
                leftBounds.MaxLocation.X = (triBounds.MaxLocation.X > leftBounds.MaxLocation.X) ? triBounds.MaxLocation.X : leftBounds.MaxLocation.X;
                leftBounds.MaxLocation.Y = (triBounds.MaxLocation.Y > leftBounds.MaxLocation.Y) ? triBounds.MaxLocation.Y : leftBounds.MaxLocation.Y;
                leftBounds.MaxLocation.Z = (triBounds.MaxLocation.Z > leftBounds.MaxLocation.Z) ? triBounds.MaxLocation.Z : leftBounds.MaxLocation.Z;
            }
            
            FBoundingBox rightBounds = CalculateTriangleBounds(Vertices, Indices, sortedList[splitPos]);
            for (int32 i = splitPos + 1; i < End; ++i)
            {
                FBoundingBox triBounds = CalculateTriangleBounds(Vertices, Indices, sortedList[i]);
                rightBounds.MinLocation.X = (triBounds.MinLocation.X < rightBounds.MinLocation.X) ? triBounds.MinLocation.X : rightBounds.MinLocation.X;
                rightBounds.MinLocation.Y = (triBounds.MinLocation.Y < rightBounds.MinLocation.Y) ? triBounds.MinLocation.Y : rightBounds.MinLocation.Y;
                rightBounds.MinLocation.Z = (triBounds.MinLocation.Z < rightBounds.MinLocation.Z) ? triBounds.MinLocation.Z : rightBounds.MinLocation.Z;
                rightBounds.MaxLocation.X = (triBounds.MaxLocation.X > rightBounds.MaxLocation.X) ? triBounds.MaxLocation.X : rightBounds.MaxLocation.X;
                rightBounds.MaxLocation.Y = (triBounds.MaxLocation.Y > rightBounds.MaxLocation.Y) ? triBounds.MaxLocation.Y : rightBounds.MaxLocation.Y;
                rightBounds.MaxLocation.Z = (triBounds.MaxLocation.Z > rightBounds.MaxLocation.Z) ? triBounds.MaxLocation.Z : rightBounds.MaxLocation.Z;
            }
            
            // Calculate SAH cost
            float leftArea = CalculateSurfaceArea(leftBounds);
            float rightArea = CalculateSurfaceArea(rightBounds);
            int32 leftCount = splitPos - Start;
            int32 rightCount = End - splitPos;
            
            float cost = TraversalCost + 
                        (leftArea / parentSurfaceArea) * leftCount * IntersectionCost +
                        (rightArea / parentSurfaceArea) * rightCount * IntersectionCost;
            
            if (cost < bestCost)
            {
                bestCost = cost;
                bestSplit = splitPos;
                OutSplitAxis = axis;
            }
        }
    }
    
    // Apply best split by resorting along best axis
    std::sort(TriangleList.GetData() + Start, TriangleList.GetData() + End,
        [&](uint32 a, uint32 b)
        {
            FVector centroidA = GetTriangleCentroid(Vertices, Indices, a);
            FVector centroidB = GetTriangleCentroid(Vertices, Indices, b);
            
            if (OutSplitAxis == 0)
                return centroidA.X < centroidB.X;
            else if (OutSplitAxis == 1)
                return centroidA.Y < centroidB.Y;
            else
                return centroidA.Z < centroidB.Z;
        });
    
    return bestSplit;
}

/**
 * Calculate surface area of bounding box
 */
float FClothSkinningWeightGenerator::FTriangleBVH::CalculateSurfaceArea(const FBoundingBox& Bounds) const
{
    FVector extent = Bounds.MaxLocation - Bounds.MinLocation;
    return 2.0f * (extent.X * extent.Y + extent.Y * extent.Z + extent.Z * extent.X);
}

/**
 * Build triangle cache for faster queries
 */
void FClothSkinningWeightGenerator::FTriangleBVH::BuildTriangleCache(
    const TArray<FVector>& Vertices,
    const TArray<uint32>& Indices)
{
    uint32 numTriangles = Indices.Num() / 3;
    TriangleCache.SetNum(numTriangles);
    
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = Indices[triIdx * 3 + 0];
        uint32 i1 = Indices[triIdx * 3 + 1];
        uint32 i2 = Indices[triIdx * 3 + 2];
        
        FCachedTriangleData& cache = TriangleCache[triIdx];
        cache.V0 = Vertices[i0];
        cache.V1 = Vertices[i1];
        cache.V2 = Vertices[i2];
        cache.Centroid = GetTriangleCentroid(Vertices, Indices, triIdx);
        cache.Bounds = CalculateTriangleBounds(Vertices, Indices, triIdx);
        
        // Calculate surface area
        FVector edge1 = cache.V1 - cache.V0;
        FVector edge2 = cache.V2 - cache.V0;
        FVector cross = FVector::CrossProduct(edge1, edge2);
        cache.SurfaceArea = cross.Size() * 0.5f;
    }
    
    bHasCache = true;
}

/**
 * Get cached triangle data
 */
const FClothSkinningWeightGenerator::FCachedTriangleData* 
FClothSkinningWeightGenerator::FTriangleBVH::GetCachedTriangleData(uint32 TriangleIndex) const
{
    if (bHasCache && TriangleIndex < static_cast<uint32>(TriangleCache.Num()))
    {
        return &TriangleCache[TriangleIndex];
    }
    return nullptr;
}

// ========== Optimized Barycentric Weight Generation with Multi-threading ==========

/**
 * Optimized barycentric weight generation with optional multi-threading
 */
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
    bvh.Build(SimPositions, SimIndices, Params.BVHMaxLeafTriangles, 
              Params.bUseSAH, Params.SAHTraversalCost, Params.SAHIntersectionCost);
    
    // Build triangle cache if enabled
    if (Params.bCacheTriangleData)
    {
        bvh.BuildTriangleCache(SimPositions, SimIndices);
    }
    
    // Generate weights for each render vertex
    OutResult.Weights.SetNum(RenderPositions.Num());
    
    // Lambda for processing a single vertex
    auto ProcessVertex = [&](int32 renderIdx)
    {
        const FVector& renderPos = RenderPositions[renderIdx];
        FClothSkinningWeight& weight = OutResult.Weights[renderIdx];
        
        // Find nearest triangle
        uint32 nearestTriIdx = 0;
        float nearestDist = FLT_MAX;
        
        if (!bvh.FindNearestTriangle(renderPos, SimPositions, SimIndices, nearestTriIdx, nearestDist))
        {
            // Fallback handling
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
                    return;
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
            return;
        }
        
        // Check if within max search distance
        if (nearestDist > Params.MaxSearchDistance && Params.bFallbackToInverseDistance)
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
                return;
            }
        }
        
        // Get triangle vertices (use cache if available)
        FVector v0, v1, v2;
        const FCachedTriangleData* cachedData = bvh.GetCachedTriangleData(nearestTriIdx);
        
        if (cachedData)
        {
            v0 = cachedData->V0;
            v1 = cachedData->V1;
            v2 = cachedData->V2;
        }
        else
        {
            uint32 i0 = SimIndices[nearestTriIdx * 3 + 0];
            uint32 i1 = SimIndices[nearestTriIdx * 3 + 1];
            uint32 i2 = SimIndices[nearestTriIdx * 3 + 2];
            
            v0 = SimPositions[i0];
            v1 = SimPositions[i1];
            v2 = SimPositions[i2];
        }
        
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
            uint32 i0 = SimIndices[nearestTriIdx * 3 + 0];
            uint32 i1 = SimIndices[nearestTriIdx * 3 + 1];
            uint32 i2 = SimIndices[nearestTriIdx * 3 + 2];
            
            float dist0Sq = (renderPos - SimPositions[i0]).SizeSquared();
            float dist1Sq = (renderPos - SimPositions[i1]).SizeSquared();
            float dist2Sq = (renderPos - SimPositions[i2]).SizeSquared();
            
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
            return;
        }
        
        // Store barycentric weights (3 influences from triangle vertices)
        uint32 i0 = SimIndices[nearestTriIdx * 3 + 0];
        uint32 i1 = SimIndices[nearestTriIdx * 3 + 1];
        uint32 i2 = SimIndices[nearestTriIdx * 3 + 2];
        
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
    };
    
    // Process vertices (with optional multi-threading)
    if (Params.bEnableMultiThreading && RenderPositions.Num() > 1000)
    {
        // Multi-threaded processing for large meshes
        // Note: Would need to add parallel_for or similar threading primitive
        // For now, process sequentially
        for (int32 renderIdx = 0; renderIdx < RenderPositions.Num(); ++renderIdx)
        {
            ProcessVertex(renderIdx);
        }
    }
    else
    {
        // Single-threaded processing
        for (int32 renderIdx = 0; renderIdx < RenderPositions.Num(); ++renderIdx)
        {
            ProcessVertex(renderIdx);
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
