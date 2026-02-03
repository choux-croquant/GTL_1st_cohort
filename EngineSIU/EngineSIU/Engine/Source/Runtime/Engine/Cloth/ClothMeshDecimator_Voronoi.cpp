/**
 * Cloth Mesh Decimator - Voronoi/Lloyd Implementation
 * Voronoi Clustering with Lloyd's Algorithm for Cloth Simulation
 */

#include "ClothMeshDecimator.h"
#include "Core/Container/Set.h"
#include <algorithm>
#include <cmath>
#include <random>

// ============================================================================
// Voronoi/Lloyd Decimation Implementation
// ============================================================================

/**
 * Initialize seeds using Farthest Point Sampling (FPS)
 * Provides better initial distribution than random sampling
 */
void FClothMeshDecimator::InitializeSeedsWithFPS(
    const TArray<FVector> &Positions,
    int32 NumSeeds,
    TArray<FVector> &OutSeeds)
{
    OutSeeds.Empty();
    
    if (Positions.Num() == 0 || NumSeeds <= 0)
        return;
    
    // Ensure we don't request more seeds than vertices
    NumSeeds = FMath::Min(NumSeeds, Positions.Num());
    
    TArray<uint8> selected;
    selected.SetNum(Positions.Num());
    for (int32 i = 0; i < selected.Num(); i++)
    {
        selected[i] = false;
    }
    
    // Start with first vertex (could also use random or centroid)
    int32 firstIdx = 0;
    OutSeeds.Add(Positions[firstIdx]);
    selected[firstIdx] = true;
    
    // Iteratively add farthest point from existing seeds
    for (int32 i = 1; i < NumSeeds; i++)
    {
        float maxDist = -1.0f;
        int32 farthestIdx = -1;
        
        // For each unselected vertex
        for (int32 j = 0; j < Positions.Num(); j++)
        {
            if (selected[j]) continue;
            
            // Find minimum distance to any existing seed
            float minDistToSeeds = FLT_MAX;
            for (const FVector &seed : OutSeeds)
            {
                float dist = FVector::DistSquared(Positions[j], seed);
                minDistToSeeds = FMath::Min(minDistToSeeds, dist);
            }
            
            // Track the vertex that is farthest from all seeds
            if (minDistToSeeds > maxDist)
            {
                maxDist = minDistToSeeds;
                farthestIdx = j;
            }
        }
        
        if (farthestIdx >= 0)
        {
            OutSeeds.Add(Positions[farthestIdx]);
            selected[farthestIdx] = true;
        }
    }
}

/**
 * Initialize seeds randomly
 * Faster but less uniform than FPS
 */
void FClothMeshDecimator::InitializeSeedsRandom(
    const TArray<FVector> &Positions,
    int32 NumSeeds,
    TArray<FVector> &OutSeeds)
{
    OutSeeds.Empty();
    
    if (Positions.Num() == 0 || NumSeeds <= 0)
        return;
    
    NumSeeds = FMath::Min(NumSeeds, Positions.Num());
    
    TArray<int32> indices;
    for (int32 i = 0; i < Positions.Num(); i++)
    {
        indices.Add(i);
    }
    
    // Shuffle and take first NumSeeds (using simple rand)
    std::random_device rd;
    std::mt19937 gen(rd());
    
    for (int32 i = 0; i < NumSeeds; i++)
    {
        std::uniform_int_distribution<> dis(i, Positions.Num() - 1);
        int32 swapIdx = dis(gen);
        int32 temp = indices[i];
        indices[i] = indices[swapIdx];
        indices[swapIdx] = temp;
        
        OutSeeds.Add(Positions[indices[i]]);
    }
}

/**
 * Find nearest seed to a given position
 */
int32 FClothMeshDecimator::FindNearestSeed(
    const FVector &Position,
    const TArray<FVector> &Seeds)
{
    if (Seeds.Num() == 0)
        return -1;
    
    float minDist = FLT_MAX;
    int32 nearestIdx = 0;
    
    for (int32 i = 0; i < Seeds.Num(); i++)
    {
        float dist = FVector::DistSquared(Position, Seeds[i]);
        if (dist < minDist)
        {
            minDist = dist;
            nearestIdx = i;
        }
    }
    
    return nearestIdx;
}

/**
 * Compute closest point on triangle to a given point
 * Based on Real-Time Collision Detection by Christer Ericson
 */
FVector FClothMeshDecimator::ClosestPointOnTriangle(
    const FVector &P,
    const FVector &A,
    const FVector &B,
    const FVector &C)
{
    // Check if P in vertex region outside A
    FVector ab = B - A;
    FVector ac = C - A;
    FVector ap = P - A;
    
    float d1 = FVector::DotProduct(ab, ap);
    float d2 = FVector::DotProduct(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
        return A; // Barycentric coordinates (1,0,0)
    
    // Check if P in vertex region outside B
    FVector bp = P - B;
    float d3 = FVector::DotProduct(ab, bp);
    float d4 = FVector::DotProduct(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
        return B; // Barycentric coordinates (0,1,0)
    
    // Check if P in edge region of AB
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        float v = d1 / (d1 - d3);
        return A + ab * v; // Barycentric coordinates (1-v,v,0)
    }
    
    // Check if P in vertex region outside C
    FVector cp = P - C;
    float d5 = FVector::DotProduct(ab, cp);
    float d6 = FVector::DotProduct(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
        return C; // Barycentric coordinates (0,0,1)
    
    // Check if P in edge region of AC
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        float w = d2 / (d2 - d6);
        return A + ac * w; // Barycentric coordinates (1-w,0,w)
    }
    
    // Check if P in edge region of BC
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return B + (C - B) * w; // Barycentric coordinates (0,1-w,w)
    }
    
    // P inside face region. Compute Q through its barycentric coordinates (u,v,w)
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return A + ab * v + ac * w; // = u*A + v*B + w*C, u = va * denom = 1.0f - v - w
}

/**
 * Project point to closest point on mesh surface
 */
void FClothMeshDecimator::ProjectPointToMesh(
    const FVector &Point,
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    FVector &OutProjectedPoint)
{
    float minDist = FLT_MAX;
    FVector closestPoint = Point;
    
    // Check all triangles
    uint32 numTriangles = Indices.Num() / 3;
    for (uint32 i = 0; i < numTriangles; i++)
    {
        uint32 i0 = Indices[i * 3 + 0];
        uint32 i1 = Indices[i * 3 + 1];
        uint32 i2 = Indices[i * 3 + 2];
        
        if (i0 >= (uint32)Positions.Num() || i1 >= (uint32)Positions.Num() || i2 >= (uint32)Positions.Num())
            continue;
        
        FVector v0 = Positions[i0];
        FVector v1 = Positions[i1];
        FVector v2 = Positions[i2];
        
        // Find closest point on this triangle
        FVector projected = ClosestPointOnTriangle(Point, v0, v1, v2);
        
        float dist = FVector::DistSquared(Point, projected);
        if (dist < minDist)
        {
            minDist = dist;
            closestPoint = projected;
        }
    }
    
    OutProjectedPoint = closestPoint;
}

/**
 * Perform one iteration of Lloyd's algorithm
 * Assigns vertices to Voronoi regions, computes centroids, and moves seeds
 */
void FClothMeshDecimator::PerformLloydIteration(
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    TArray<FVector> &InOutSeeds)
{
    int32 numSeeds = InOutSeeds.Num();
    if (numSeeds == 0)
        return;
    
    TArray<FVector> newSeeds;
    newSeeds.SetNum(numSeeds);
    
    TArray<TArray<int32>> regions;
    regions.SetNum(numSeeds);
    
    // Step 1: Assign each vertex to nearest seed (Voronoi assignment)
    for (int32 i = 0; i < Positions.Num(); i++)
    {
        int32 nearestSeed = FindNearestSeed(Positions[i], InOutSeeds);
        if (nearestSeed >= 0)
        {
            regions[nearestSeed].Add(i);
        }
    }
    
    // Step 2: Compute centroid of each region and project to surface
    for (int32 s = 0; s < numSeeds; s++)
    {
        if (regions[s].Num() == 0)
        {
            // No vertices assigned - keep seed where it is
            newSeeds[s] = InOutSeeds[s];
            continue;
        }
        
        // Compute centroid
        FVector centroid = FVector::ZeroVector;
        for (int32 idx : regions[s])
        {
            centroid += Positions[idx];
        }
        centroid /= static_cast<float>(regions[s].Num());
        
        // Project centroid back to mesh surface
        FVector projected;
        ProjectPointToMesh(centroid, Positions, Indices, projected);
        newSeeds[s] = projected;
    }
    
    InOutSeeds = newSeeds;
}

/**
 * Triangulate seeds to create decimated mesh
 * Uses simple approach: map original triangles to seed triangles
 */
void FClothMeshDecimator::TriangulateSeeds(
    const TArray<FVector> &Seeds,
    const TArray<FVector> &OriginalPositions,
    const TArray<uint32> &OriginalIndices,
    TArray<uint32> &OutIndices,
    TArray<int32> &OutVertexMapping)
{
    OutIndices.Empty();
    OutVertexMapping.SetNum(OriginalPositions.Num());
    
    // Create mapping: original vertex -> nearest seed
    for (int32 i = 0; i < OriginalPositions.Num(); i++)
    {
        OutVertexMapping[i] = FindNearestSeed(OriginalPositions[i], Seeds);
    }
    
    // Process original triangles, replacing vertices with their nearest seeds
    TMap<uint64, bool> addedTriangles; // Avoid duplicate triangles
    
    uint32 numTriangles = OriginalIndices.Num() / 3;
    for (uint32 triIdx = 0; triIdx < numTriangles; triIdx++)
    {
        uint32 v0 = OriginalIndices[triIdx * 3 + 0];
        uint32 v1 = OriginalIndices[triIdx * 3 + 1];
        uint32 v2 = OriginalIndices[triIdx * 3 + 2];
        
        if (v0 >= (uint32)OutVertexMapping.Num() ||
            v1 >= (uint32)OutVertexMapping.Num() ||
            v2 >= (uint32)OutVertexMapping.Num())
            continue;
        
        // Map to nearest seeds
        int32 s0 = OutVertexMapping[v0];
        int32 s1 = OutVertexMapping[v1];
        int32 s2 = OutVertexMapping[v2];
        
        // Skip degenerate triangles (all vertices map to same seed)
        if (s0 == s1 || s1 == s2 || s2 == s0)
            continue;
        
        if (s0 < 0 || s1 < 0 || s2 < 0)
            continue;
        
        // Create canonical triangle key (sorted indices)
        uint32 indices[3] = { static_cast<uint32>(s0), static_cast<uint32>(s1), static_cast<uint32>(s2) };
        if (indices[0] > indices[1]) { uint32 t = indices[0]; indices[0] = indices[1]; indices[1] = t; }
        if (indices[1] > indices[2]) { uint32 t = indices[1]; indices[1] = indices[2]; indices[2] = t; }
        if (indices[0] > indices[1]) { uint32 t = indices[0]; indices[0] = indices[1]; indices[1] = t; }
        
        uint64 triKey = (static_cast<uint64>(indices[0]) << 32) | 
                       (static_cast<uint64>(indices[1]) << 16) | 
                        static_cast<uint64>(indices[2]);
        
        // Only add unique triangles
        if (!addedTriangles.Contains(triKey))
        {
            OutIndices.Add(static_cast<uint32>(s0));
            OutIndices.Add(static_cast<uint32>(s1));
            OutIndices.Add(static_cast<uint32>(s2));
            addedTriangles.Add(triKey, true);
        }
    }
}

/**
 * Main Voronoi decimation function using Lloyd's algorithm
 */
bool FClothMeshDecimator::DecimateMeshVoronoi(
    const TArray<FVector> &SourcePositions,
    const TArray<uint32> &SourceIndices,
    const TArray<FVector2D> &SourceUVs,
    const FClothDecimationParams &Params,
    FClothDecimationResult &OutResult)
{
    OutResult.bSuccess = false;
    OutResult.OriginalVertexCount = SourcePositions.Num();
    OutResult.OriginalTriangleCount = SourceIndices.Num() / 3;
    
    if (SourcePositions.Num() == 0 || SourceIndices.Num() < 3)
    {
        OutResult.ErrorMessage = "Invalid input mesh";
        return false;
    }
    
    // Calculate target vertex count
    uint32 targetVertexCount = Params.TargetVertexCount > 0
                                   ? Params.TargetVertexCount
                                   : static_cast<uint32>(SourcePositions.Num() * Params.ReductionRatio);
    
    if (targetVertexCount >= SourcePositions.Num())
    {
        // No decimation needed
        OutResult.Positions = SourcePositions;
        OutResult.Indices = SourceIndices;
        OutResult.VertexMapping.SetNum(SourcePositions.Num());
        for (int32 i = 0; i < OutResult.VertexMapping.Num(); ++i)
        {
            OutResult.VertexMapping[i] = i;
        }
        OutResult.OutputVertexCount = SourcePositions.Num();
        OutResult.OutputTriangleCount = SourceIndices.Num() / 3;
        OutResult.bSuccess = true;
        return true;
    }
    
    // Step 1: Initialize seeds
    TArray<FVector> seeds;
    if (Params.bUseFarthestPointSampling)
    {
        InitializeSeedsWithFPS(SourcePositions, targetVertexCount, seeds);
    }
    else
    {
        InitializeSeedsRandom(SourcePositions, targetVertexCount, seeds);
    }
    
    if (seeds.Num() == 0)
    {
        OutResult.ErrorMessage = "Failed to initialize seeds";
        return false;
    }
    
    // Step 2: Lloyd iterations for uniform distribution
    for (int32 iter = 0; iter < Params.LloydIterations; iter++)
    {
        PerformLloydIteration(SourcePositions, SourceIndices, seeds);
    }
    
    // Step 3: Triangulate seeds
    TArray<int32> vertexMapping;
    TriangulateSeeds(seeds, SourcePositions, SourceIndices, OutResult.Indices, vertexMapping);
    
    // Step 4: Set output positions to seed positions
    OutResult.Positions = seeds;
    OutResult.VertexMapping = vertexMapping;
    
    OutResult.OutputVertexCount = OutResult.Positions.Num();
    OutResult.OutputTriangleCount = OutResult.Indices.Num() / 3;
    
    // Validation
    if (Params.bValidateResult)
    {
        if (HasDegenerateTriangles(OutResult.Positions, OutResult.Indices, 0.0f))
        {
            OutResult.ErrorMessage = "Result contains degenerate triangles";
            return false;
        }
    }
    
    OutResult.bSuccess = true;
    return true;
}

/**
 * Router function - chooses decimation method based on parameters
 */
bool FClothMeshDecimator::DecimateMesh(
    const TArray<FVector> &SourcePositions,
    const TArray<uint32> &SourceIndices,
    const TArray<FVector2D> &SourceUVs,
    const FClothDecimationParams &Params,
    FClothDecimationResult &OutResult)
{
    EClothDecimationMethod method = Params.Method;
    
    // Auto-select method based on mesh characteristics
    if (method == EClothDecimationMethod::Auto)
    {
        // Use Voronoi for most cloth meshes (better uniform distribution)
        // Could add heuristics here to choose QEM for certain cases
        method = EClothDecimationMethod::Voronoi;
    }
    
    // Call appropriate decimation method
    switch (method)
    {
        case EClothDecimationMethod::QEM:
            return DecimateMeshQEM(SourcePositions, SourceIndices, SourceUVs, Params, OutResult);
        
        case EClothDecimationMethod::Voronoi:
            return DecimateMeshVoronoi(SourcePositions, SourceIndices, SourceUVs, Params, OutResult);
        
        default:
            OutResult.ErrorMessage = "Unknown decimation method";
            OutResult.bSuccess = false;
            return false;
    }
}
