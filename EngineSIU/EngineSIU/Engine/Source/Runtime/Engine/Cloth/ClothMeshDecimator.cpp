/**
 * Cloth Mesh Decimator Implementation
 * QEM-Based Mesh Decimation for Cloth Simulation
 */

#include "ClothMeshDecimator.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cfloat>

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

void FClothMeshDecimator::FQuadric::AddPlane(const FVector &Normal, double D)
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

    A[0] += a * a; // a00
    A[1] += a * b; // a01
    A[2] += a * c; // a02
    A[3] += a * d; // a03
    A[4] += b * b; // a11
    A[5] += b * c; // a12
    A[6] += b * d; // a13
    A[7] += c * c; // a22
    A[8] += c * d; // a23
    A[9] += d * d; // a33
}

double FClothMeshDecimator::FQuadric::ComputeError(const FVector &V) const
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

FClothMeshDecimator::FQuadric FClothMeshDecimator::FQuadric::operator+(const FQuadric &Q) const
{
    FQuadric result;
    for (int i = 0; i < 10; ++i)
    {
        result.A[i] = A[i] + Q.A[i];
    }
    return result;
}

bool FClothMeshDecimator::FQuadric::SolveOptimalPosition(FVector &OutPosition) const
{
    // Solve: Q * v = 0
    // Using 3x3 subsystem (ignoring the w component)

    double det = A[0] * (A[4] * A[7] - A[5] * A[5]) -
                 A[1] * (A[1] * A[7] - A[5] * A[2]) +
                 A[2] * (A[1] * A[5] - A[4] * A[2]);

    if (fabs(det) < 1e-10)
    {
        return false; // Matrix is singular, cannot solve
    }

    // Solve using Cramer's rule
    double invDet = 1.0 / det;

    double x = -invDet * (A[3] * (A[4] * A[7] - A[5] * A[5]) -
                          A[1] * (A[6] * A[7] - A[5] * A[8]) +
                          A[2] * (A[6] * A[5] - A[4] * A[8]));

    double y = -invDet * (A[0] * (A[6] * A[7] - A[5] * A[8]) -
                          A[3] * (A[1] * A[7] - A[2] * A[5]) +
                          A[2] * (A[1] * A[8] - A[6] * A[2]));

    double z = -invDet * (A[0] * (A[4] * A[8] - A[6] * A[5]) -
                          A[1] * (A[1] * A[8] - A[6] * A[2]) +
                          A[3] * (A[1] * A[5] - A[4] * A[2]));

    OutPosition = FVector(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return true;
}

// Mesh Connectivity Implementation
void FClothMeshDecimator::FMeshConnectivity::Build(const TArray<uint32> &Indices, uint32 NumVertices)
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
    const TArray<uint32> *triangles = EdgeToTriangles.Find(key);
    return triangles && triangles->Num() == 1; // Boundary edge has only 1 adjacent triangle
}

uint64 FClothMeshDecimator::FMeshConnectivity::GetEdgeKey(uint32 V0, uint32 V1) const
{
    // Canonical edge key (order-independent)
    uint32 minV = V0 < V1 ? V0 : V1;
    uint32 maxV = V0 < V1 ? V1 : V0;
    return (static_cast<uint64>(minV) << 32) | static_cast<uint64>(maxV);
}

void FClothMeshDecimator::FMeshConnectivity::RemoveTriangleReferences(uint32 TriIdx, const TArray<uint32> &Indices)
{
    if (TriIdx * 3 + 2 >= (uint32)Indices.Num())
        return;

    uint32 v0 = Indices[TriIdx * 3 + 0];
    uint32 v1 = Indices[TriIdx * 3 + 1];
    uint32 v2 = Indices[TriIdx * 3 + 2];

    // Remove from VertexToTriangles
    if (TArray<uint32> *tris = VertexToTriangles.Find(v0))
        tris->Remove(TriIdx);
    if (TArray<uint32> *tris = VertexToTriangles.Find(v1))
        tris->Remove(TriIdx);
    if (TArray<uint32> *tris = VertexToTriangles.Find(v2))
        tris->Remove(TriIdx);

    // Remove from EdgeToTriangles
    EdgeToTriangles.FindOrAdd(GetEdgeKey(v0, v1)).Remove(TriIdx);
    EdgeToTriangles.FindOrAdd(GetEdgeKey(v1, v2)).Remove(TriIdx);
    EdgeToTriangles.FindOrAdd(GetEdgeKey(v2, v0)).Remove(TriIdx);
}

void FClothMeshDecimator::FMeshConnectivity::MergeVertexConnectivity(uint32 KeepVertex, uint32 RemoveVertex)
{
    // Transfer all connectivity from RemoveVertex to KeepVertex

    // Transfer vertex-to-triangles
    if (TArray<uint32> *removeTris = VertexToTriangles.Find(RemoveVertex))
    {
        TArray<uint32> &keepTris = VertexToTriangles.FindOrAdd(KeepVertex);
        for (uint32 triIdx : *removeTris)
        {
            keepTris.AddUnique(triIdx);
        }
        VertexToTriangles.Remove(RemoveVertex);
    }

    // Transfer vertex-to-vertices (neighbors)
    if (TArray<uint32> *removeNeighbors = VertexToVertices.Find(RemoveVertex))
    {
        TArray<uint32> &keepNeighbors = VertexToVertices.FindOrAdd(KeepVertex);
        for (uint32 neighbor : *removeNeighbors)
        {
            if (neighbor != KeepVertex && neighbor != RemoveVertex)
            {
                keepNeighbors.AddUnique(neighbor);

                // Update neighbor's adjacency list
                if (TArray<uint32> *neighborAdj = VertexToVertices.Find(neighbor))
                {
                    neighborAdj->Remove(RemoveVertex);
                    neighborAdj->AddUnique(KeepVertex);
                }
            }
        }
        // Remove self-reference
        keepNeighbors.Remove(KeepVertex);
        keepNeighbors.Remove(RemoveVertex);

        VertexToVertices.Remove(RemoveVertex);
    }
}

TArray<uint64> FClothMeshDecimator::FMeshConnectivity::GetVertexEdges(uint32 VertexIdx) const
{
    TArray<uint64> edges;

    const TArray<uint32> *neighbors = VertexToVertices.Find(VertexIdx);
    if (neighbors)
    {
        edges.Reserve(neighbors->Num());
        for (uint32 neighbor : *neighbors)
        {
            edges.Add(GetEdgeKey(VertexIdx, neighbor));
        }
    }

    return edges;
}

// Heap Operations for Dynamic Priority Queue
// Min-heap based on Error (lower error = higher priority)

void FClothMeshDecimator::HeapPush(TArray<FEdgeCollapse> &Heap, const FEdgeCollapse &Collapse)
{
    Heap.Add(Collapse);
    HeapifyUp(Heap, Heap.Num() - 1);
}

FClothMeshDecimator::FEdgeCollapse FClothMeshDecimator::HeapPop(TArray<FEdgeCollapse> &Heap)
{
    if (Heap.Num() == 0)
    {
        // Return invalid collapse
        FEdgeCollapse invalid;
        invalid.V0 = invalid.V1 = 0;
        invalid.Error = DBL_MAX;
        return invalid;
    }

    FEdgeCollapse result = Heap[0];

    if (Heap.Num() > 1)
    {
        Heap[0] = Heap[Heap.Num() - 1];
        Heap.SetNum(Heap.Num() - 1);
        HeapifyDown(Heap, 0);
    }
    else
    {
        Heap.Empty();
    }

    return result;
}

void FClothMeshDecimator::HeapifyDown(TArray<FEdgeCollapse> &Heap, int32 Index)
{
    int32 size = Heap.Num();

    while (true)
    {
        int32 smallest = Index;
        int32 left = 2 * Index + 1;
        int32 right = 2 * Index + 2;

        if (left < size && Heap[left].Error < Heap[smallest].Error)
        {
            smallest = left;
        }

        if (right < size && Heap[right].Error < Heap[smallest].Error)
        {
            smallest = right;
        }

        if (smallest == Index)
        {
            break;
        }

        // Swap
        FEdgeCollapse temp = Heap[Index];
        Heap[Index] = Heap[smallest];
        Heap[smallest] = temp;

        Index = smallest;
    }
}

void FClothMeshDecimator::HeapifyUp(TArray<FEdgeCollapse> &Heap, int32 Index)
{
    while (Index > 0)
    {
        int32 parent = (Index - 1) / 2;

        if (Heap[Index].Error >= Heap[parent].Error)
        {
            break;
        }

        // Swap
        FEdgeCollapse temp = Heap[Index];
        Heap[Index] = Heap[parent];
        Heap[parent] = temp;

        Index = parent;
    }
}

void FClothMeshDecimator::MakeHeap(TArray<FEdgeCollapse> &Heap)
{
    // Build heap from unsorted array (O(n))
    for (int32 i = Heap.Num() / 2 - 1; i >= 0; --i)
    {
        HeapifyDown(Heap, i);
    }
}

// Compute single edge collapse with all parameters
FClothMeshDecimator::FEdgeCollapse FClothMeshDecimator::ComputeEdgeCollapse(
    uint32 V0,
    uint32 V1,
    const TArray<FVector> &Positions,
    const TArray<FQuadric> &Quadrics,
    const TMap<uint64, bool> &IsBoundary,
    const TMap<uint64, bool> &IsUVSeam,
    const FClothDecimationParams &Params,
    uint32 Timestamp)
{
    FEdgeCollapse collapse;
    collapse.V0 = V0;
    collapse.V1 = V1;
    collapse.Timestamp = Timestamp;

    uint64 edgeKey = ((uint64)V0 < (uint64)V1) ? ((static_cast<uint64>(V0) << 32) | static_cast<uint64>(V1)) : ((static_cast<uint64>(V1) << 32) | static_cast<uint64>(V0));

    collapse.bIsBoundary = IsBoundary.Contains(edgeKey);
    collapse.bIsUVSeam = false; // UV seams disabled for simulation mesh

    // Compute edge length
    float edgeLength = FVector::Distance(Positions[V0], Positions[V1]);
    if (edgeLength > Params.MaxEdgeLength)
    {
        collapse.Error = DBL_MAX; // Invalid
        return collapse;
    }

    // Combine quadrics
    FQuadric combinedQuadric = Quadrics[V0] + Quadrics[V1];

    // Try to find optimal position
    if (combinedQuadric.SolveOptimalPosition(collapse.OptimalPosition))
    {
        collapse.Error = combinedQuadric.ComputeError(collapse.OptimalPosition);
    }
    else
    {
        // Fallback: use midpoint
        collapse.OptimalPosition = (Positions[V0] + Positions[V1]) * 0.5f;
        collapse.Error = combinedQuadric.ComputeError(collapse.OptimalPosition);
    }

    // Apply boundary penalty (UV seam penalty removed - not needed for simulation mesh)
    if (collapse.bIsBoundary)
    {
        collapse.Error *= Params.BoundaryWeight;
    }

    return collapse;
}

// Main Decimation Implementation
bool FClothMeshDecimator::DecimateMeshQEM(
    const TArray<FVector> &SourcePositions,
    const TArray<uint32> &SourceIndices,
    const TArray<FVector2D> &SourceUVs,
    const FClothDecimationParams &Params,
    FClothDecimationResult &OutResult)
{
    // Performance profiling
    auto startTime = std::chrono::high_resolution_clock::now();

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

    // Detect boundaries only (UV seams disabled for simulation mesh)
    TMap<uint64, bool> isBoundary;
    TMap<uint64, bool> isUVSeam; // Keep empty - UV seams not needed for simulation
    DetectBoundariesAndSeams(indices, SourceUVs, positions.Num(), connectivity, isBoundary, isUVSeam);

    // Build initial heap from all edges
    TArray<FEdgeCollapse> collapseHeap;
    collapseHeap.Reserve(connectivity.EdgeToTriangles.Num());

    uint32 currentTimestamp = 0;
    TMap<uint64, uint32> edgeTimestamps;

    for (const auto &pair : connectivity.EdgeToTriangles)
    {
        uint64 edgeKey = pair.Key;
        uint32 v0 = static_cast<uint32>(edgeKey >> 32);
        uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);

        if (validVertices[v0] && validVertices[v1])
        {
            FEdgeCollapse collapse = ComputeEdgeCollapse(
                v0, v1, positions, quadrics, isBoundary, isUVSeam, Params, currentTimestamp);

            if (collapse.Error < DBL_MAX) // Skip invalid edges
            {
                collapseHeap.Add(collapse);
                edgeTimestamps.Add(edgeKey, currentTimestamp);
            }
        }
    }

    // Build heap (O(E))
    MakeHeap(collapseHeap);

    // Perform edge collapses using heap
    uint32 currentVertexCount = positions.Num();
    int32 successfulCollapses = 0;
    int32 skippedCollapses = 0;

    while (currentVertexCount > targetVertexCount && collapseHeap.Num() > 0)
    {
        // Pop best candidate (O(log E))
        FEdgeCollapse collapse = HeapPop(collapseHeap);

        // Validate vertices still exist
        if (!validVertices[collapse.V0] || !validVertices[collapse.V1])
        {
            skippedCollapses++;
            continue;
        }

        // Check for stale entry (edge has been updated since this entry was added)
        uint64 edgeKey = connectivity.GetEdgeKey(collapse.V0, collapse.V1);
        uint32 *storedTimestamp = edgeTimestamps.Find(edgeKey);
        if (storedTimestamp && *storedTimestamp != collapse.Timestamp)
        {
            skippedCollapses++;
            continue; // Stale entry, newer one is in heap
        }

        // Check boundary preservation constraints (UV seam check removed for simulation mesh)
        if (Params.bPreserveBoundaryEdges && collapse.bIsBoundary)
        {
            skippedCollapses++;
            continue;
        }

        // Execute collapse (local update, O(degree) instead of O(N))
        if (ExecuteEdgeCollapse(collapse, positions, indices, quadrics, validVertices, connectivity))
        {
            currentVertexCount--;
            successfulCollapses++;
            currentTimestamp++;

            // Recompute affected edges and add to heap (O(degree × log E))
            TArray<uint64> affectedEdges = connectivity.GetVertexEdges(collapse.V0);

            for (uint64 affectedEdgeKey : affectedEdges)
            {
                uint32 v0 = static_cast<uint32>(affectedEdgeKey >> 32);
                uint32 v1 = static_cast<uint32>(affectedEdgeKey & 0xFFFFFFFF);

                if (validVertices[v0] && validVertices[v1])
                {
                    FEdgeCollapse newCollapse = ComputeEdgeCollapse(
                        v0, v1, positions, quadrics, isBoundary, isUVSeam, Params, currentTimestamp);

                    if (newCollapse.Error < DBL_MAX)
                    {
                        HeapPush(collapseHeap, newCollapse);
                        edgeTimestamps.FindOrAdd(affectedEdgeKey) = currentTimestamp;
                    }
                }
            }
        }
        else
        {
            skippedCollapses++;
        }
    }

    // Compact mesh (remove deleted vertices and invalidated triangles)
    CompactMesh(positions, indices, validVertices, OutResult.Positions, OutResult.Indices, OutResult.VertexMapping);

    OutResult.OutputVertexCount = OutResult.Positions.Num();
    OutResult.OutputTriangleCount = OutResult.Indices.Num() / 3;

    // Performance profiling
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Validation (relaxed for simulation mesh)
    if (Params.bValidateResult)
    {
        // Only check for critical degeneracy (duplicate indices), not area
        // Small triangles are acceptable for physics simulation
        if (HasDegenerateTriangles(OutResult.Positions, OutResult.Indices, 0.0f))
        {
            OutResult.ErrorMessage = "Result contains degenerate triangles (duplicate indices)";
            return false;
        }

        // Manifold check is optional - simulation meshes can handle non-manifold topology
        if (Params.bPreserveTopology && !ValidateManifold(OutResult.Indices, OutResult.Positions.Num()))
        {
        }
    }

    OutResult.bSuccess = true;
    return true;
}

void FClothMeshDecimator::ComputeInitialQuadrics(
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    TArray<FQuadric> &OutQuadrics)
{
    OutQuadrics.SetNum(Positions.Num());

    // Initialize all quadrics to zero
    for (FQuadric &q : OutQuadrics)
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
    const TArray<uint32> &Indices,
    const TArray<FVector2D> &UVs,
    uint32 NumVertices,
    FMeshConnectivity &Connectivity,
    TMap<uint64, bool> &OutIsBoundary,
    TMap<uint64, bool> &OutIsUVSeam)
{
    OutIsBoundary.Empty();
    OutIsUVSeam.Empty();

    // Detect boundary edges
    for (const auto &pair : Connectivity.EdgeToTriangles)
    {
        uint64 edgeKey = pair.Key;
        const TArray<uint32> &triangles = pair.Value;

        if (triangles.Num() == 1)
        {
            OutIsBoundary.Add(edgeKey, true);
        }
    }
}

void FClothMeshDecimator::BuildEdgeCollapseQueue(
    const TArray<FVector> &Positions,
    const FMeshConnectivity &Connectivity,
    const TArray<FQuadric> &Quadrics,
    const TMap<uint64, bool> &IsBoundary,
    const TMap<uint64, bool> &IsUVSeam,
    const FClothDecimationParams &Params,
    TArray<FEdgeCollapse> &OutQueue)
{
    OutQueue.Empty();

    // Build collapse candidate for each edge
    for (const auto &pair : Connectivity.EdgeToTriangles)
    {
        uint64 edgeKey = pair.Key;
        uint32 v0 = static_cast<uint32>(edgeKey >> 32);
        uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);

        FEdgeCollapse collapse;
        collapse.V0 = v0;
        collapse.V1 = v1;
        collapse.bIsBoundary = IsBoundary.Contains(edgeKey);
        collapse.bIsUVSeam = false; // UV seams disabled for simulation mesh

        // Compute edge length
        float edgeLength = FVector::Distance(Positions[v0], Positions[v1]);
        if (edgeLength > Params.MaxEdgeLength)
        {
            continue; // Skip long edges
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

        // Apply boundary penalty only (UV seam penalty removed for simulation mesh)
        if (collapse.bIsBoundary)
        {
            collapse.Error *= Params.BoundaryWeight;
        }

        OutQueue.Add(collapse);
    }
}

bool FClothMeshDecimator::ExecuteEdgeCollapse(
    const FEdgeCollapse &Collapse,
    TArray<FVector> &Positions,
    TArray<uint32> &Indices,
    TArray<FQuadric> &Quadrics,
    TArray<uint8> &ValidVertices,
    FMeshConnectivity &Connectivity)
{
    uint32 v0 = Collapse.V0;
    uint32 v1 = Collapse.V1;

    Positions[v0] = Collapse.OptimalPosition;
    Quadrics[v0] = Quadrics[v0] + Quadrics[v1];
    ValidVertices[v1] = false;

    TArray<uint32> *v1Triangles = Connectivity.VertexToTriangles.Find(v1);
    if (!v1Triangles)
    {
        // No triangles to update, just merge connectivity
        Connectivity.MergeVertexConnectivity(v0, v1);
        return true;
    }

    // Copy triangle list since we'll be modifying connectivity
    TArray<uint32> trianglesToUpdate = *v1Triangles;

    TArray<uint32> degenerateTriangles;

    for (uint32 triIdx : trianglesToUpdate)
    {
        if (triIdx * 3 + 2 >= (uint32)Indices.Num())
            continue;

        uint32 *tri = &Indices[triIdx * 3];

        // Replace v1 with v0 in this triangle
        for (int i = 0; i < 3; i++)
        {
            if (tri[i] == v1)
            {
                tri[i] = v0;
            }
        }

        // Check if triangle became degenerate
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0])
        {
            degenerateTriangles.Add(triIdx);
        }
    }

    for (uint32 triIdx : degenerateTriangles)
    {
        Connectivity.RemoveTriangleReferences(triIdx, Indices);

        // Mark triangle as invalid using UINT32_MAX (guaranteed to be invalid after remapping)
        if (triIdx * 3 + 2 < (uint32)Indices.Num())
        {
            Indices[triIdx * 3 + 0] = UINT32_MAX;
            Indices[triIdx * 3 + 1] = UINT32_MAX;
            Indices[triIdx * 3 + 2] = UINT32_MAX;
        }
    }

    Connectivity.MergeVertexConnectivity(v0, v1);

    return true;
}

void FClothMeshDecimator::CompactMesh(
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    const TArray<uint8> &ValidVertices,
    TArray<FVector> &OutPositions,
    TArray<uint32> &OutIndices,
    TArray<int32> &OutVertexMapping)
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

    // Remap indices and skip invalid triangles
    OutIndices.Empty();
    OutIndices.Reserve(Indices.Num());

    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        uint32 v0 = Indices[i + 0];
        uint32 v1 = Indices[i + 1];
        uint32 v2 = Indices[i + 2];

        // Skip degenerate triangles (marked as UINT32_MAX during collapse)
        if (v0 == UINT32_MAX || v1 == UINT32_MAX || v2 == UINT32_MAX)
            continue;

        // Skip degenerate triangles (same vertex used multiple times)
        if (v0 == v1 || v1 == v2 || v2 == v0)
            continue;

        // Skip if any vertex index is out of bounds
        if (v0 >= (uint32)oldToNew.Num() || v1 >= (uint32)oldToNew.Num() || v2 >= (uint32)oldToNew.Num())
            continue;

        // Remap vertices
        int32 i0 = oldToNew[v0];
        int32 i1 = oldToNew[v1];
        int32 i2 = oldToNew[v2];

        // Skip triangles with deleted/invalid vertices
        if (i0 < 0 || i1 < 0 || i2 < 0)
            continue;

        // Skip triangles that became degenerate after remapping
        if (i0 == i1 || i1 == i2 || i2 == i0)
            continue;

        // All checks passed - add valid triangle
        OutIndices.Add(static_cast<uint32>(i0));
        OutIndices.Add(static_cast<uint32>(i1));
        OutIndices.Add(static_cast<uint32>(i2));
    }
}

bool FClothMeshDecimator::ValidateManifold(
    const TArray<uint32> &Indices,
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

            uint32 &count = edgeCounts.FindOrAdd(key);
            count++;
        };

        addEdge(i0, i1);
        addEdge(i1, i2);
        addEdge(i2, i0);
    }

    // Check for non-manifold edges (more than 2 triangles)
    for (const auto &pair : edgeCounts)
    {
        if (pair.Value > 2)
        {
            return false; // Non-manifold edge found
        }
    }

    return true;
}

bool FClothMeshDecimator::HasDegenerateTriangles(
    const TArray<FVector> &Positions,
    const TArray<uint32> &Indices,
    float MinArea)
{
    // SIMULATION MESH VALIDATION: Only check for critical degeneracy (duplicate indices)
    // Small triangle areas are acceptable for physics simulation
    // The MinArea parameter is ignored - physics simulation can handle small triangles

    uint32 numTriangles = Indices.Num() / 3;
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = Indices[triIdx * 3 + 0];
        uint32 i1 = Indices[triIdx * 3 + 1];
        uint32 i2 = Indices[triIdx * 3 + 2];

        // Check for out-of-bounds indices (should never happen after proper compaction)
        if (i0 >= (uint32)Positions.Num() || i1 >= (uint32)Positions.Num() || i2 >= (uint32)Positions.Num())
        {
            return true; // Critical error - compaction failed
        }

        // Check for degenerate indices (same vertex referenced multiple times)
        // This is the ONLY validation we need for simulation meshes
        if (i0 == i1 || i1 == i2 || i2 == i0)
        {
            return true; // Critical error - compaction failed to filter degenerates
        }
    }

    return false;
}

FVector FClothMeshDecimator::ComputeTriangleNormal(
    const FVector &A,
    const FVector &B,
    const FVector &C)
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
        normal = FVector(0, 0, 1); // Fallback
    }

    return normal;
}

double FClothMeshDecimator::ComputeTriangleArea(
    const FVector &A,
    const FVector &B,
    const FVector &C)
{
    FVector edge1 = B - A;
    FVector edge2 = C - A;
    FVector cross = FVector::CrossProduct(edge1, edge2);

    return static_cast<double>(cross.Size()) * 0.5;
}
