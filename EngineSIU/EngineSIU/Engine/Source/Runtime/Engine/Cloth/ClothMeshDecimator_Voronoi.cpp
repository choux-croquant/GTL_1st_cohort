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
// Spatial Hash Grid for Fast Nearest Neighbor Queries
// ============================================================================

/**
 * 3D spatial hash grid for fast nearest neighbor queries
 * Reduces O(N×K) brute force to O(N×27) grid cell checks
 */
struct FSpatialGrid
{
    TMap<int64, TArray<int32>> Grid;
    float CellSize;
    FVector MinBound;
    FVector MaxBound;
    
    void Build(const TArray<FVector> &Points, float cellSize)
    {
        CellSize = cellSize;
        Grid.Empty();
        
        if (Points.Num() == 0)
            return;
        
        // Compute bounds
        MinBound = Points[0];
        MaxBound = Points[0];
        for (const FVector &p : Points)
        {
            MinBound.X = FMath::Min(MinBound.X, p.X);
            MinBound.Y = FMath::Min(MinBound.Y, p.Y);
            MinBound.Z = FMath::Min(MinBound.Z, p.Z);
            MaxBound.X = FMath::Max(MaxBound.X, p.X);
            MaxBound.Y = FMath::Max(MaxBound.Y, p.Y);
            MaxBound.Z = FMath::Max(MaxBound.Z, p.Z);
        }
        
        // Insert points into grid
        for (int32 i = 0; i < Points.Num(); i++)
        {
            int64 key = GetCellKey(Points[i]);
            Grid.FindOrAdd(key).Add(i);
        }
    }
    
    int64 GetCellKey(const FVector &Pos) const
    {
        int32 x = static_cast<int32>((Pos.X - MinBound.X) / CellSize);
        int32 y = static_cast<int32>((Pos.Y - MinBound.Y) / CellSize);
        int32 z = static_cast<int32>((Pos.Z - MinBound.Z) / CellSize);
        
        // Pack into 64-bit key (21 bits per coordinate)
        return (static_cast<int64>(x & 0x1FFFFF) << 42) |
               (static_cast<int64>(y & 0x1FFFFF) << 21) |
                static_cast<int64>(z & 0x1FFFFF);
    }
    
    int32 FindNearest(const FVector &Pos, const TArray<FVector> &Points) const
    {
        float minDist = FLT_MAX;
        int32 nearestIdx = -1;
        
        // Get cell coordinates
        int32 cx = static_cast<int32>((Pos.X - MinBound.X) / CellSize);
        int32 cy = static_cast<int32>((Pos.Y - MinBound.Y) / CellSize);
        int32 cz = static_cast<int32>((Pos.Z - MinBound.Z) / CellSize);
        
        // Check 27 neighboring cells (3×3×3)
        for (int32 dx = -1; dx <= 1; dx++)
        {
            for (int32 dy = -1; dy <= 1; dy++)
            {
                for (int32 dz = -1; dz <= 1; dz++)
                {
                    int64 key = (static_cast<int64>((cx + dx) & 0x1FFFFF) << 42) |
                               (static_cast<int64>((cy + dy) & 0x1FFFFF) << 21) |
                                static_cast<int64>((cz + dz) & 0x1FFFFF);
                    
                    const TArray<int32> *cellPoints = Grid.Find(key);
                    if (cellPoints)
                    {
                        for (int32 idx : *cellPoints)
                        {
                            float dist = FVector::DistSquared(Pos, Points[idx]);
                            if (dist < minDist)
                            {
                                minDist = dist;
                                nearestIdx = idx;
                            }
                        }
                    }
                }
            }
        }
        
        // Fallback: if no points found in grid, do brute force
        if (nearestIdx < 0 && Points.Num() > 0)
        {
            nearestIdx = 0;
            minDist = FVector::DistSquared(Pos, Points[0]);
            for (int32 i = 1; i < Points.Num(); i++)
            {
                float dist = FVector::DistSquared(Pos, Points[i]);
                if (dist < minDist)
                {
                    minDist = dist;
                    nearestIdx = i;
                }
            }
        }
        
        return nearestIdx;
    }
};

/**
 * Triangle spatial hash for fast projection queries
 */
struct FTriangleSpatialHash
{
    TMap<int64, TArray<int32>> Grid;
    float CellSize;
    FVector MinBound;
    
    void Build(const TArray<FVector> &Positions, const TArray<uint32> &Indices, float cellSize)
    {
        CellSize = cellSize;
        Grid.Empty();
        
        if (Positions.Num() == 0 || Indices.Num() == 0)
            return;
        
        // Compute bounds
        MinBound = Positions[0];
        for (const FVector &p : Positions)
        {
            MinBound.X = FMath::Min(MinBound.X, p.X);
            MinBound.Y = FMath::Min(MinBound.Y, p.Y);
            MinBound.Z = FMath::Min(MinBound.Z, p.Z);
        }
        
        // Insert triangles by their centroid
        for (uint32 i = 0; i < Indices.Num(); i += 3)
        {
            uint32 i0 = Indices[i];
            uint32 i1 = Indices[i + 1];
            uint32 i2 = Indices[i + 2];
            
            if (i0 >= (uint32)Positions.Num() || i1 >= (uint32)Positions.Num() || i2 >= (uint32)Positions.Num())
                continue;
            
            FVector centroid = (Positions[i0] + Positions[i1] + Positions[i2]) / 3.0f;
            
            int64 key = GetCellKey(centroid);
            Grid.FindOrAdd(key).Add(i / 3);
        }
    }
    
    int64 GetCellKey(const FVector &Pos) const
    {
        int32 x = static_cast<int32>((Pos.X - MinBound.X) / CellSize);
        int32 y = static_cast<int32>((Pos.Y - MinBound.Y) / CellSize);
        int32 z = static_cast<int32>((Pos.Z - MinBound.Z) / CellSize);
        
        return (static_cast<int64>(x & 0x1FFFFF) << 42) |
               (static_cast<int64>(y & 0x1FFFFF) << 21) |
                static_cast<int64>(z & 0x1FFFFF);
    }
    
    void ProjectPointFast(
        const FVector &Point,
        const TArray<FVector> &Positions,
        const TArray<uint32> &Indices,
        FVector &OutProjectedPoint) const
    {
        float minDist = FLT_MAX;
        FVector closestPoint = Point;
        
        int32 cx = static_cast<int32>((Point.X - MinBound.X) / CellSize);
        int32 cy = static_cast<int32>((Point.Y - MinBound.Y) / CellSize);
        int32 cz = static_cast<int32>((Point.Z - MinBound.Z) / CellSize);
        
        // Check 27 neighboring cells
        for (int32 dx = -1; dx <= 1; dx++)
        {
            for (int32 dy = -1; dy <= 1; dy++)
            {
                for (int32 dz = -1; dz <= 1; dz++)
                {
                    int64 key = (static_cast<int64>((cx + dx) & 0x1FFFFF) << 42) |
                               (static_cast<int64>((cy + dy) & 0x1FFFFF) << 21) |
                                static_cast<int64>((cz + dz) & 0x1FFFFF);
                    
                    const TArray<int32> *cellTris = Grid.Find(key);
                    if (cellTris)
                    {
                        for (int32 triIdx : *cellTris)
                        {
                            uint32 i0 = Indices[triIdx * 3 + 0];
                            uint32 i1 = Indices[triIdx * 3 + 1];
                            uint32 i2 = Indices[triIdx * 3 + 2];
                            
                            if (i0 >= (uint32)Positions.Num() || i1 >= (uint32)Positions.Num() || i2 >= (uint32)Positions.Num())
                                continue;
                            
                            FVector proj = FClothMeshDecimator::ClosestPointOnTriangle(
                                Point, Positions[i0], Positions[i1], Positions[i2]);
                            
                            float dist = FVector::DistSquared(Point, proj);
                            if (dist < minDist)
                            {
                                minDist = dist;
                                closestPoint = proj;
                            }
                        }
                    }
                }
            }
        }
        
        OutProjectedPoint = closestPoint;
    }
};

// ============================================================================
// Voronoi/Lloyd Decimation Implementation
// ============================================================================

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
    
    TArray<float> minDistances;
    minDistances.SetNum(Positions.Num());
    for (int32 i = 0; i < Positions.Num(); i++)
    {
        minDistances[i] = FLT_MAX;
    }
    
    // Initialize first seed and distances
    int32 firstIdx = 0;
    OutSeeds.Add(Positions[firstIdx]);
    minDistances[firstIdx] = 0.0f;
    
    // Compute initial distances to first seed
    for (int32 j = 0; j < Positions.Num(); j++)
    {
        if (j == firstIdx)
            continue;
        minDistances[j] = FVector::DistSquared(Positions[j], Positions[firstIdx]);
    }
    
    // Each iteration: find farthest vertex, add as seed, UPDATE cache
    for (int32 i = 1; i < NumSeeds; i++)
    {
        // Find farthest vertex using cached distances (O(N) instead of O(N×K))
        float maxDist = -1.0f;
        int32 farthestIdx = -1;
        
        for (int32 j = 0; j < Positions.Num(); j++)
        {
            if (minDistances[j] == 0.0f) // Already selected
                continue;
            
            if (minDistances[j] > maxDist)
            {
                maxDist = minDistances[j];
                farthestIdx = j;
            }
        }
        
        if (farthestIdx < 0)
            break;
        
        // Add new seed
        OutSeeds.Add(Positions[farthestIdx]);
        minDistances[farthestIdx] = 0.0f;
        
        // Update cache: only compare to this NEW seed (not all seeds!)
        for (int32 j = 0; j < Positions.Num(); j++)
        {
            if (minDistances[j] == 0.0f)
                continue;
            
            float dist = FVector::DistSquared(Positions[j], Positions[farthestIdx]);
            minDistances[j] = FMath::Min(minDistances[j], dist);
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
    indices.Reserve(Positions.Num());
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
 * Find nearest seed to a given position (brute force fallback)
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

void FClothMeshDecimator::ProjectPointToMesh(
    const FVector &Point,
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    FVector &OutProjectedPoint)
{
    // Note: This function now expects a spatial hash to be used
    // For now, fall back to brute force but will be replaced by hash in caller
    
    float minDist = FLT_MAX;
    FVector closestPoint = Point;
    
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

void FClothMeshDecimator::PerformLloydIteration(
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    TArray<FVector> &InOutSeeds)
{
    int32 numSeeds = InOutSeeds.Num();
    if (numSeeds == 0)
        return;
    
    // Compute average edge length for spatial hash cell size
    float avgEdgeLength = 0.0f;
    int32 edgeCount = 0;
    uint32 sampleCount = FMath::Min<uint32>(1000, Indices.Num() / 3); // Sample first 1000 triangles
    for (uint32 i = 0; i < sampleCount * 3; i += 3)
    {
        if (i + 2 >= Indices.Num())
            break;
        
        uint32 i0 = Indices[i];
        uint32 i1 = Indices[i + 1];
        uint32 i2 = Indices[i + 2];
        
        if (i0 >= (uint32)Positions.Num() || i1 >= (uint32)Positions.Num() || i2 >= (uint32)Positions.Num())
            continue;
        
        FVector v0 = Positions[i0];
        FVector v1 = Positions[i1];
        FVector v2 = Positions[i2];
        
        avgEdgeLength += FVector::Dist(v0, v1);
        avgEdgeLength += FVector::Dist(v1, v2);
        avgEdgeLength += FVector::Dist(v2, v0);
        edgeCount += 3;
    }
    avgEdgeLength = (edgeCount > 0) ? (avgEdgeLength / edgeCount) : 1.0f;
    
    // Build spatial hash for seeds (for fast nearest neighbor)
    FSpatialGrid seedGrid;
    seedGrid.Build(InOutSeeds, avgEdgeLength * 2.0f);
    
    // Build triangle spatial hash (for fast projection)
    FTriangleSpatialHash triHash;
    triHash.Build(Positions, Indices, avgEdgeLength * 3.0f);
    
    TArray<TArray<int32>> regions;
    regions.SetNum(numSeeds);
    
    for (int32 i = 0; i < Positions.Num(); i++)
    {
        int32 nearestSeed = seedGrid.FindNearest(Positions[i], InOutSeeds);
        if (nearestSeed >= 0)
        {
            regions[nearestSeed].Add(i);
        }
    }
    
    TArray<FVector> newSeeds;
    newSeeds.SetNum(numSeeds);
    
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
        
        FVector projected;
        triHash.ProjectPointFast(centroid, Positions, Indices, projected);
        newSeeds[s] = projected;
    }
    
    InOutSeeds = newSeeds;
}

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
    addedTriangles.Reserve(OriginalIndices.Num() / 3);
    
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
        
        int32 minIdx = FMath::Min3(s0, s1, s2);
        int32 maxIdx = FMath::Max3(s0, s1, s2);
        int32 midIdx = s0 + s1 + s2 - minIdx - maxIdx;
        
        uint64 triKey = (static_cast<uint64>(minIdx) << 42) |
                       (static_cast<uint64>(midIdx) << 21) |
                        static_cast<uint64>(maxIdx);
        
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
    
    // Compute mesh bounding box for convergence test
    FVector minBound = SourcePositions[0];
    FVector maxBound = SourcePositions[0];
    for (const FVector &p : SourcePositions)
    {
        minBound.X = FMath::Min(minBound.X, p.X);
        minBound.Y = FMath::Min(minBound.Y, p.Y);
        minBound.Z = FMath::Min(minBound.Z, p.Z);
        maxBound.X = FMath::Max(maxBound.X, p.X);
        maxBound.Y = FMath::Max(maxBound.Y, p.Y);
        maxBound.Z = FMath::Max(maxBound.Z, p.Z);
    }
    float meshSize = FVector::Dist(minBound, maxBound);
    float convergenceThreshold = meshSize * Params.ConvergenceThreshold;
    
    for (int32 iter = 0; iter < Params.LloydIterations; iter++)
    {
        TArray<FVector> oldSeeds = seeds;
        
        PerformLloydIteration(SourcePositions, SourceIndices, seeds);
        
        // Check convergence - early termination
        float maxMovement = 0.0f;
        for (int32 i = 0; i < seeds.Num(); i++)
        {
            float movement = FVector::Dist(seeds[i], oldSeeds[i]);
            maxMovement = FMath::Max(maxMovement, movement);
        }
        
        if (maxMovement < convergenceThreshold)
        {
            // Converged early - stop iterations
            break;
        }
    }
    
    TArray<int32> vertexMapping;
    TriangulateSeeds(seeds, SourcePositions, SourceIndices, OutResult.Indices, vertexMapping);
    
    OutResult.Positions = seeds;
    OutResult.VertexMapping = vertexMapping;
    
    OutResult.OutputVertexCount = OutResult.Positions.Num();
    OutResult.OutputTriangleCount = OutResult.Indices.Num() / 3;
    
    // Validation
    if (Params.bValidateResult)
    {
        if (FClothMeshDecimator::HasDegenerateTriangles(OutResult.Positions, OutResult.Indices, 0.0f))
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
            return FClothMeshDecimator::DecimateMeshQEM(SourcePositions, SourceIndices, SourceUVs, Params, OutResult);
        
        case EClothDecimationMethod::Voronoi:
            return FClothMeshDecimator::DecimateMeshVoronoi(SourcePositions, SourceIndices, SourceUVs, Params, OutResult);
        
        default:
            OutResult.ErrorMessage = "Unknown decimation method";
            OutResult.bSuccess = false;
            return false;
    }
}
