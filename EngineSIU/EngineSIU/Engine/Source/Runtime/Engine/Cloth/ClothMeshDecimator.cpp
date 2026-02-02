/**
 * Cloth Mesh Decimator Implementation
 * QEM-Based Mesh Decimation for Cloth Simulation
 */

#include "ClothMeshDecimator.h"
#include <algorithm>
#include <cmath>

// FQuadric Implementation
FClothMeshDecimator::FQuadric::FQuadric()
{
    Clear();
}

void FClothMeshDecimator::FQuadric::Clear()
{
    for (int i = 0; i < 10; ++i)
    {
        A[i] = 0.0;
    }
}

void FClothMeshDecimator::FQuadric::AddPlane(const FVector& Normal, double D)
{
    // Quadric matrix from plane equation: ax + by + cz + d = 0
    // Q = [a b c d]^T * [a b c d]
    
    double a = static_cast<double>(Normal.X);
    double b = static_cast<double>(Normal.Y);
    double c = static_cast<double>(Normal.Z);
    double d = D;
    
    // Symmetric 4x4 matrix stored as 10 elements:
    // [a² ab ac ad]
    // [ab b² bc bd]
    // [ac bc c² cd]
    // [ad bd cd d²]
    
    A[0] += a * a;  // a00
    A[1] += a * b;  // a01
    A[2] += a * c;  // a02
    A[3] += a * d;  // a03
    A[4] += b * b;  // a11
    A[5] += b * c;  // a12
    A[6] += b * d;  // a13
    A[7] += c * c;  // a22
    A[8] += c * d;  // a23
    A[9] += d * d;  // a33
}

double FClothMeshDecimator::FQuadric::ComputeError(const FVector& V) const
{
    double x = static_cast<double>(V.X);
    double y = static_cast<double>(V.Y);
    double z = static_cast<double>(V.Z);
    
    // Quadric error: v^T * Q * v
    double error = 
        A[0] * x * x + 2.0 * A[1] * x * y + 2.0 * A[2] * x * z + 2.0 * A[3] * x +
        A[4] * y * y + 2.0 * A[5] * y * z + 2.0 * A[6] * y +
        A[7] * z * z + 2.0 * A[8] * z +
        A[9];
    
    return error;
}

FClothMeshDecimator::FQuadric FClothMeshDecimator::FQuadric::operator+(const FQuadric& Q) const
{
    FQuadric result;
    for (int i = 0; i < 10; ++i)
    {
        result.A[i] = A[i] + Q.A[i];
    }
    return result;
}

bool FClothMeshDecimator::FQuadric::SolveOptimalPosition(FVector& OutPosition) const
{
    // Solve: Q * v = 0
    // Using 3x3 subsystem (ignoring the w component)
    
    double det = A[0] * (A[4] * A[7] - A[5] * A[5]) -
                 A[1] * (A[1] * A[7] - A[5] * A[2]) +
                 A[2] * (A[1] * A[5] - A[4] * A[2]);
    
    if (fabs(det) < 1e-10)
    {
        return false;  // Matrix is singular, cannot solve
    }
    
    // Solve using Cramer's rule
    double invDet = 1.0 / det;
    
    double x = -invDet * (
        A[3] * (A[4] * A[7] - A[5] * A[5]) -
        A[1] * (A[6] * A[7] - A[5] * A[8]) +
        A[2] * (A[6] * A[5] - A[4] * A[8])
    );
    
    double y = -invDet * (
        A[0] * (A[6] * A[7] - A[5] * A[8]) -
        A[3] * (A[1] * A[7] - A[2] * A[5]) +
        A[2] * (A[1] * A[8] - A[6] * A[2])
    );
    
    double z = -invDet * (
        A[0] * (A[4] * A[8] - A[6] * A[5]) -
        A[1] * (A[1] * A[8] - A[6] * A[2]) +
        A[3] * (A[1] * A[5] - A[4] * A[2])
    );
    
    OutPosition = FVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return true;
}

// Mesh Connectivity Implementation
void FClothMeshDecimator::FMeshConnectivity::Build(const TArray<uint32>& Indices, uint32 NumVertices)
{
    EdgeToTriangles.Empty();
    VertexToTriangles.Empty();
    VertexToVertices.Empty();
    
    uint32 numTriangles = Indices.Num() / 3;
    
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = Indices[triIdx * 3 + 0];
        uint32 i1 = Indices[triIdx * 3 + 1];
        uint32 i2 = Indices[triIdx * 3 + 2];
        
        // Add triangle to vertex connectivity
        VertexToTriangles.FindOrAdd(i0).Add(triIdx);
        VertexToTriangles.FindOrAdd(i1).Add(triIdx);
        VertexToTriangles.FindOrAdd(i2).Add(triIdx);
        
        // Add edges
        EdgeToTriangles.FindOrAdd(GetEdgeKey(i0, i1)).Add(triIdx);
        EdgeToTriangles.FindOrAdd(GetEdgeKey(i1, i2)).Add(triIdx);
        EdgeToTriangles.FindOrAdd(GetEdgeKey(i2, i0)).Add(triIdx);
        
        // Add vertex adjacencies
        VertexToVertices.FindOrAdd(i0).AddUnique(i1);
        VertexToVertices.FindOrAdd(i0).AddUnique(i2);
        VertexToVertices.FindOrAdd(i1).AddUnique(i0);
        VertexToVertices.FindOrAdd(i1).AddUnique(i2);
        VertexToVertices.FindOrAdd(i2).AddUnique(i0);
        VertexToVertices.FindOrAdd(i2).AddUnique(i1);
    }
}

bool FClothMeshDecimator::FMeshConnectivity::IsBoundaryEdge(uint32 V0, uint32 V1) const
{
    uint64 key = GetEdgeKey(V0, V1);
    const TArray<uint32>* triangles = EdgeToTriangles.Find(key);
    return triangles && triangles->Num() == 1;  // Boundary edge has only 1 adjacent triangle
}

uint64 FClothMeshDecimator::FMeshConnectivity::GetEdgeKey(uint32 V0, uint32 V1) const
{
    // Canonical edge key (order-independent)
    uint32 minV = V0 < V1 ? V0 : V1;
    uint32 maxV = V0 < V1 ? V1 : V0;
    return (static_cast<uint64>(minV) << 32) | static_cast<uint64>(maxV);
}

// Main Decimation Implementation
bool FClothMeshDecimator::DecimateMeshQEM(
    const TArray<FVector>& SourcePositions,
    const TArray<uint32>& SourceIndices,
    const TArray<FVector2D>& SourceUVs,
    const FClothDecimationParams& Params,
    FClothDecimationResult& OutResult)
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
    
    // Working data
    TArray<FVector> positions = SourcePositions;
    TArray<uint32> indices = SourceIndices;
    TArray<uint8> validVertices;
    validVertices.SetNum(positions.Num());
    for (int32 i = 0; i < validVertices.Num(); ++i)
    {
        validVertices[i] = true;
    }
    
    // Build mesh connectivity
    FMeshConnectivity connectivity;
    connectivity.Build(indices, positions.Num());
    
    // Compute quadrics for each vertex
    TArray<FQuadric> quadrics;
    ComputeInitialQuadrics(positions, indices, quadrics);
    
    // Detect boundaries and UV seams
    TMap<uint64, bool> isBoundary;
    TMap<uint64, bool> isUVSeam;
    DetectBoundariesAndSeams(indices, SourceUVs, positions.Num(), connectivity, isBoundary, isUVSeam);
    
    // Build edge collapse queue
    TArray<FEdgeCollapse> collapseQueue;
    BuildEdgeCollapseQueue(positions, connectivity, quadrics, isBoundary, isUVSeam, Params, collapseQueue);
    
    // Sort queue by error (lowest first)
    collapseQueue.Sort([](const FEdgeCollapse& A, const FEdgeCollapse& B) {
        return A.Error < B.Error;
    });
    
    // Perform edge collapses
    uint32 currentVertexCount = positions.Num();
    int32 queueIndex = 0;
    int32 maxIterations = collapseQueue.Num() * 2;  // Safety limit
    int32 iterationCount = 0;
    
    while (currentVertexCount > targetVertexCount && queueIndex < collapseQueue.Num() && iterationCount < maxIterations)
    {
        iterationCount++;
        
        const FEdgeCollapse& collapse = collapseQueue[queueIndex++];
        
        // Check if vertices are still valid
        if (!validVertices[collapse.V0] || !validVertices[collapse.V1])
        {
            continue;
        }
        
        // Check if this would violate boundary/seam preservation
        if (Params.bPreserveBoundaryEdges && collapse.bIsBoundary)
        {
            continue;
        }
        
        if (Params.bPreserveUVSeams && collapse.bIsUVSeam)
        {
            continue;
        }
        
        // Execute collapse
        if (ExecuteEdgeCollapse(collapse, positions, indices, quadrics, validVertices, connectivity))
        {
            currentVertexCount--;
        }
    }
    
    // Compact mesh (remove deleted vertices and triangles)
    CompactMesh(positions, indices, validVertices, OutResult.Positions, OutResult.Indices, OutResult.VertexMapping);
    
    OutResult.OutputVertexCount = OutResult.Positions.Num();
    OutResult.OutputTriangleCount = OutResult.Indices.Num() / 3;
    
    // Validation
    if (Params.bValidateResult)
    {
        if (HasDegenerateTriangles(OutResult.Positions, OutResult.Indices, Params.MinTriangleArea))
        {
            OutResult.ErrorMessage = "Result contains degenerate triangles";
            return false;
        }
        
        if (Params.bPreserveTopology && !ValidateManifold(OutResult.Indices, OutResult.Positions.Num()))
        {
            OutResult.ErrorMessage = "Result is not a manifold mesh";
            return false;
        }
    }
    
    OutResult.bSuccess = true;
    return true;
}

void FClothMeshDecimator::ComputeInitialQuadrics(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    TArray<FQuadric>& OutQuadrics)
{
    OutQuadrics.SetNum(Positions.Num());
    
    // Initialize all quadrics to zero
    for (FQuadric& q : OutQuadrics)
    {
        q.Clear();
    }
    
    // Accumulate quadrics from adjacent triangles
    uint32 numTriangles = Indices.Num() / 3;
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = Indices[triIdx * 3 + 0];
        uint32 i1 = Indices[triIdx * 3 + 1];
        uint32 i2 = Indices[triIdx * 3 + 2];
        
        FVector v0 = Positions[i0];
        FVector v1 = Positions[i1];
        FVector v2 = Positions[i2];
        
        // Compute triangle normal
        FVector normal = ComputeTriangleNormal(v0, v1, v2);
        
        // Compute plane equation: n·p + d = 0
        double d = -static_cast<double>(FVector::DotProduct(normal, v0));
        
        // Add plane to vertex quadrics
        OutQuadrics[i0].AddPlane(normal, d);
        OutQuadrics[i1].AddPlane(normal, d);
        OutQuadrics[i2].AddPlane(normal, d);
    }
}

void FClothMeshDecimator::DetectBoundariesAndSeams(
    const TArray<uint32>& Indices,
    const TArray<FVector2D>& UVs,
    uint32 NumVertices,
    FMeshConnectivity& Connectivity,
    TMap<uint64, bool>& OutIsBoundary,
    TMap<uint64, bool>& OutIsUVSeam)
{
    OutIsBoundary.Empty();
    OutIsUVSeam.Empty();
    
    // Detect boundary edges
    for (const auto& pair : Connectivity.EdgeToTriangles)
    {
        uint64 edgeKey = pair.Key;
        const TArray<uint32>& triangles = pair.Value;
        
        if (triangles.Num() == 1)
        {
            OutIsBoundary.Add(edgeKey, true);
        }
    }
    
    // Detect UV seams (edges where UVs are discontinuous)
    if (UVs.Num() == Indices.Num())  // Per-index UVs
    {
        uint32 numTriangles = Indices.Num() / 3;
        for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
        {
            uint32 idx0 = triIdx * 3 + 0;
            uint32 idx1 = triIdx * 3 + 1;
            uint32 idx2 = triIdx * 3 + 2;
            
            uint32 v0 = Indices[idx0];
            uint32 v1 = Indices[idx1];
            uint32 v2 = Indices[idx2];
            
            FVector2D uv0 = UVs[idx0];
            FVector2D uv1 = UVs[idx1];
            FVector2D uv2 = UVs[idx2];
            
            // Check each edge for UV discontinuity
            auto checkEdge = [&](uint32 vA, uint32 vB, const FVector2D& uvA, const FVector2D& uvB)
            {
                uint64 edgeKey = Connectivity.GetEdgeKey(vA, vB);
                const TArray<uint32>* adjacentTris = Connectivity.EdgeToTriangles.Find(edgeKey);
                
                if (adjacentTris && adjacentTris->Num() > 1)
                {
                    // Find UVs from the other triangle
                    for (uint32 otherTriIdx : *adjacentTris)
                    {
                        if (otherTriIdx == triIdx) continue;
                        
                        // Check if UVs match
                        // Simplified check - in production, would need proper UV comparison
                        float uvDist = (uvA - uvB).Length();
                        if (uvDist > 0.01f)  // UV seam threshold
                        {
                            OutIsUVSeam.Add(edgeKey, true);
                        }
                    }
                }
            };
            
            checkEdge(v0, v1, uv0, uv1);
            checkEdge(v1, v2, uv1, uv2);
            checkEdge(v2, v0, uv2, uv0);
        }
    }
}

void FClothMeshDecimator::BuildEdgeCollapseQueue(
    const TArray<FVector>& Positions,
    const FMeshConnectivity& Connectivity,
    const TArray<FQuadric>& Quadrics,
    const TMap<uint64, bool>& IsBoundary,
    const TMap<uint64, bool>& IsUVSeam,
    const FClothDecimationParams& Params,
    TArray<FEdgeCollapse>& OutQueue)
{
    OutQueue.Empty();
    
    // Build collapse candidate for each edge
    for (const auto& pair : Connectivity.EdgeToTriangles)
    {
        uint64 edgeKey = pair.Key;
        uint32 v0 = static_cast<uint32>(edgeKey >> 32);
        uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);
        
        FEdgeCollapse collapse;
        collapse.V0 = v0;
        collapse.V1 = v1;
        collapse.bIsBoundary = IsBoundary.Contains(edgeKey);
        collapse.bIsUVSeam = IsUVSeam.Contains(edgeKey);
        
        // Compute edge length
        float edgeLength = FVector::Distance(Positions[v0], Positions[v1]);
        if (edgeLength > Params.MaxEdgeLength)
        {
            continue;  // Skip long edges
        }
        
        // Combine quadrics
        FQuadric combinedQuadric = Quadrics[v0] + Quadrics[v1];
        
        // Try to find optimal position
        if (combinedQuadric.SolveOptimalPosition(collapse.OptimalPosition))
        {
            collapse.Error = combinedQuadric.ComputeError(collapse.OptimalPosition);
        }
        else
        {
            // Fallback: use midpoint
            collapse.OptimalPosition = (Positions[v0] + Positions[v1]) * 0.5f;
            collapse.Error = combinedQuadric.ComputeError(collapse.OptimalPosition);
        }
        
        // Apply penalties
        if (collapse.bIsBoundary)
        {
            collapse.Error *= Params.BoundaryWeight;
        }
        if (collapse.bIsUVSeam)
        {
            collapse.Error *= Params.UVSeamWeight;
        }
        
        OutQueue.Add(collapse);
    }
}

bool FClothMeshDecimator::ExecuteEdgeCollapse(
    const FEdgeCollapse& Collapse,
    TArray<FVector>& Positions,
    TArray<uint32>& Indices,
    TArray<FQuadric>& Quadrics,
    TArray<uint8>& ValidVertices,
    FMeshConnectivity& Connectivity)
{
    uint32 v0 = Collapse.V0;
    uint32 v1 = Collapse.V1;
    
    // Move V0 to optimal position
    Positions[v0] = Collapse.OptimalPosition;
    
    // Merge quadrics
    Quadrics[v0] = Quadrics[v0] + Quadrics[v1];
    
    // Mark V1 as deleted
    ValidVertices[v1] = false;
    
    // Update all triangles that reference V1 to reference V0 instead
    for (int32 i = 0; i < Indices.Num(); ++i)
    {
        if (Indices[i] == v1)
        {
            Indices[i] = v0;
        }
    }
    
    // Remove degenerate triangles (triangles where all vertices are the same)
    TArray<uint32> newIndices;
    newIndices.Reserve(Indices.Num());
    
    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        uint32 i0 = Indices[i + 0];
        uint32 i1 = Indices[i + 1];
        uint32 i2 = Indices[i + 2];
        
        // Skip degenerate triangles
        if (i0 != i1 && i1 != i2 && i2 != i0)
        {
            newIndices.Add(i0);
            newIndices.Add(i1);
            newIndices.Add(i2);
        }
    }
    
    Indices = newIndices;
    
    // Rebuild connectivity (simplified - in production, would do incremental update)
    Connectivity.Build(Indices, Positions.Num());
    
    return true;
}

void FClothMeshDecimator::CompactMesh(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    const TArray<uint8>& ValidVertices,
    TArray<FVector>& OutPositions,
    TArray<uint32>& OutIndices,
    TArray<int32>& OutVertexMapping)
{
    // Build vertex remapping
    TArray<int32> oldToNew;
    oldToNew.SetNum(Positions.Num());
    
    OutPositions.Empty();
    OutVertexMapping.SetNum(Positions.Num());
    
    for (int32 i = 0; i < Positions.Num(); ++i)
    {
        if (ValidVertices[i])
        {
            oldToNew[i] = OutPositions.Num();
            OutVertexMapping[i] = OutPositions.Num();
            OutPositions.Add(Positions[i]);
        }
        else
        {
            oldToNew[i] = -1;
            OutVertexMapping[i] = -1;
        }
    }
    
    // Remap indices
    OutIndices.Empty();
    OutIndices.Reserve(Indices.Num());
    
    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        int32 i0 = oldToNew[Indices[i + 0]];
        int32 i1 = oldToNew[Indices[i + 1]];
        int32 i2 = oldToNew[Indices[i + 2]];
        
        // Skip triangles with invalid vertices
        if (i0 >= 0 && i1 >= 0 && i2 >= 0)
        {
            OutIndices.Add(static_cast<uint32>(i0));
            OutIndices.Add(static_cast<uint32>(i1));
            OutIndices.Add(static_cast<uint32>(i2));
        }
    }
}

bool FClothMeshDecimator::ValidateManifold(
    const TArray<uint32>& Indices,
    uint32 NumVertices)
{
    // Simple manifold check: each edge should have at most 2 adjacent triangles
    TMap<uint64, uint32> edgeCounts;
    
    uint32 numTriangles = Indices.Num() / 3;
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = Indices[triIdx * 3 + 0];
        uint32 i1 = Indices[triIdx * 3 + 1];
        uint32 i2 = Indices[triIdx * 3 + 2];
        
        auto addEdge = [&](uint32 a, uint32 b)
        {
            uint32 minV = a < b ? a : b;
            uint32 maxV = a < b ? b : a;
            uint64 key = (static_cast<uint64>(minV) << 32) | static_cast<uint64>(maxV);
            
            uint32& count = edgeCounts.FindOrAdd(key);
            count++;
        };
        
        addEdge(i0, i1);
        addEdge(i1, i2);
        addEdge(i2, i0);
    }
    
    // Check for non-manifold edges (more than 2 triangles)
    for (const auto& pair : edgeCounts)
    {
        if (pair.Value > 2)
        {
            return false;  // Non-manifold edge found
        }
    }
    
    return true;
}

bool FClothMeshDecimator::HasDegenerateTriangles(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    float MinArea)
{
    uint32 numTriangles = Indices.Num() / 3;
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        FVector v0 = Positions[Indices[triIdx * 3 + 0]];
        FVector v1 = Positions[Indices[triIdx * 3 + 1]];
        FVector v2 = Positions[Indices[triIdx * 3 + 2]];
        
        double area = ComputeTriangleArea(v0, v1, v2);
        if (area < static_cast<double>(MinArea))
        {
            return true;  // Degenerate triangle found
        }
    }
    
    return false;
}

FVector FClothMeshDecimator::ComputeTriangleNormal(
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    FVector edge1 = B - A;
    FVector edge2 = C - A;
    FVector normal = FVector::CrossProduct(edge1, edge2);
    
    float len = normal.Size();
    if (len > 1e-6f)
    {
        normal = normal / len;
    }
    else
    {
        normal = FVector(0, 0, 1);  // Fallback
    }
    
    return normal;
}

double FClothMeshDecimator::ComputeTriangleArea(
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    FVector edge1 = B - A;
    FVector edge2 = C - A;
    FVector cross = FVector::CrossProduct(edge1, edge2);
    
    return static_cast<double>(cross.Size()) * 0.5;
}
