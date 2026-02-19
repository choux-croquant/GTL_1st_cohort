/**
 * Cloth Mesh Decimator
 * QEM-Based Mesh Decimation for Cloth Simulation
 * Reference: Garland & Heckbert 1997 - "Surface Simplification Using Quadric Error Metrics"
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"
#include "Core/Container/Map.h"
#include <float.h>

/**
 * Cloth decimation method
 */
enum class EClothDecimationMethod : uint8
{
    QEM,        // Quadric Error Metrics (edge collapse)
    Voronoi,    // Voronoi clustering with Lloyd's algorithm
    Auto        // Automatically choose based on mesh characteristics
};

/**
 * Decimation parameters
 */
struct FClothDecimationParams
{
    // Method selection
    EClothDecimationMethod Method = EClothDecimationMethod::Voronoi; // Default to Voronoi for uniform distribution

    // Target reduction (choose one)
    float ReductionRatio = 0.1f;  // 0.0-1.0 (0.1 = 10% of original verts)
    uint32 TargetVertexCount = 0; // Alternative: specific vertex count (overrides ratio)

    // Quality preservation (QEM method)
    bool bPreserveBoundaryEdges = true; // Keep mesh boundaries intact (critical for cloth)
    bool bPreserveUVSeams = true;       // Maintain UV seam topology (for textures)
    bool bPreserveTopology = true;      // Prevent non-manifold results

    // QEM weights
    float BoundaryWeight = 1000.0f; // High penalty for boundary edge collapse
    float UVSeamWeight = 100.0f;    // Penalty for UV seam collapse
    float MaxEdgeLength = FLT_MAX;  // Limit collapse length (prevents long thin triangles)

    // Voronoi/Lloyd parameters
    int32 LloydIterations = 10;     // Number of Lloyd relaxation iterations (10 recommended, was 30)
    bool bUseFarthestPointSampling = true; // Use FPS for seed initialization (vs random)
    float ConvergenceThreshold = 0.001f; // Early termination threshold (relative to mesh size)

    // Validation
    float MinTriangleArea = 0.001f; // Discard degenerate triangles
    bool bValidateResult = true;    // Check manifoldness after decimation
};

/**
 * QEM decimation result
 */
struct FClothDecimationResult
{
    TArray<FVector> Positions;
    TArray<uint32> Indices;
    TArray<int32> VertexMapping; // Old vertex ID -> New vertex ID (-1 if removed)

    bool bSuccess;
    FString ErrorMessage;

    uint32 OriginalVertexCount;
    uint32 OutputVertexCount;
    uint32 OriginalTriangleCount;
    uint32 OutputTriangleCount;
};

/**
 * Cloth Mesh Decimator
 * Implements QEM-based mesh simplification optimized for cloth simulation
 */
class FClothMeshDecimator
{
public:
    /**
     * Decimate mesh using selected method (router function)
     * @param SourcePositions - Input high-detail vertices
     * @param SourceIndices - Input high-detail triangles
     * @param SourceUVs - Input UV coordinates (for seam detection)
     * @param Params - Decimation parameters
     * @param OutResult - Output decimation result
     * @return Success/failure
     */
    static bool DecimateMesh(
        const TArray<FVector> &SourcePositions,
        const TArray<uint32> &SourceIndices,
        const TArray<FVector2D> &SourceUVs,
        const FClothDecimationParams &Params,
        FClothDecimationResult &OutResult);

    /**
     * Decimate mesh using Quadric Error Metrics
     * @param SourcePositions - Input high-detail vertices
     * @param SourceIndices - Input high-detail triangles
     * @param SourceUVs - Input UV coordinates (for seam detection)
     * @param Params - Decimation parameters
     * @param OutResult - Output decimation result
     * @return Success/failure
     */
    static bool DecimateMeshQEM(
        const TArray<FVector> &SourcePositions,
        const TArray<uint32> &SourceIndices,
        const TArray<FVector2D> &SourceUVs,
        const FClothDecimationParams &Params,
        FClothDecimationResult &OutResult);

    /**
     * Decimate mesh using Voronoi Clustering with Lloyd's algorithm
     * @param SourcePositions - Input high-detail vertices
     * @param SourceIndices - Input high-detail triangles
     * @param SourceUVs - Input UV coordinates (unused for Voronoi)
     * @param Params - Decimation parameters
     * @param OutResult - Output decimation result
     * @return Success/failure
     */
    static bool DecimateMeshVoronoi(
        const TArray<FVector> &SourcePositions,
        const TArray<uint32> &SourceIndices,
        const TArray<FVector2D> &SourceUVs,
        const FClothDecimationParams &Params,
        FClothDecimationResult &OutResult);

public:
    // QEM quadric error matrix (4x4 symmetric, stored as 10 coefficients)
    struct FQuadric
    {
        double A[10]; // a00, a01, a02, a03, a11, a12, a13, a22, a23, a33

        FQuadric();
        void Clear();
        void AddPlane(const FVector &Normal, double D);
        double ComputeError(const FVector &V) const;
        FQuadric operator+(const FQuadric &Q) const;

        // Solve for optimal vertex position
        bool SolveOptimalPosition(FVector &OutPosition) const;
    };

    // Edge collapse candidate
    struct FEdgeCollapse
    {
        uint32 V0, V1;           // Vertices to collapse
        double Error;            // QEM error for this collapse
        FVector OptimalPosition; // Best position for merged vertex
        bool bIsBoundary;        // Boundary edge flag
        bool bIsUVSeam;          // UV seam flag
        uint32 Timestamp;        // For detecting stale heap entries

        bool operator<(const FEdgeCollapse &Other) const { return Error < Other.Error; }
        bool operator>(const FEdgeCollapse &Other) const { return Error > Other.Error; }
    };

    // Mesh connectivity helper
    struct FMeshConnectivity
    {
        TMap<uint64, TArray<uint32>> EdgeToTriangles;   // Edge -> triangle IDs
        TMap<uint32, TArray<uint32>> VertexToTriangles; // Vertex -> triangle IDs
        TMap<uint32, TArray<uint32>> VertexToVertices;  // Vertex -> adjacent vertices

        void Build(const TArray<uint32> &Indices, uint32 NumVertices);
        bool IsBoundaryEdge(uint32 V0, uint32 V1) const;
        uint64 GetEdgeKey(uint32 V0, uint32 V1) const;

        // Incremental update methods for O(E log E) optimization
        void RemoveTriangleReferences(uint32 TriIdx, const TArray<uint32> &Indices);
        void MergeVertexConnectivity(uint32 KeepVertex, uint32 RemoveVertex);
        TArray<uint64> GetVertexEdges(uint32 VertexIdx) const;
    };

    // Core QEM algorithm
    static void ComputeInitialQuadrics(
        const TArray<FVector> &Positions,
        const TArray<uint32> &Indices,
        TArray<FQuadric> &OutQuadrics);

    static void DetectBoundariesAndSeams(
        const TArray<uint32> &Indices,
        const TArray<FVector2D> &UVs,
        uint32 NumVertices,
        FMeshConnectivity &Connectivity,
        TMap<uint64, bool> &OutIsBoundary,
        TMap<uint64, bool> &OutIsUVSeam);

    static void BuildEdgeCollapseQueue(
        const TArray<FVector> &Positions,
        const FMeshConnectivity &Connectivity,
        const TArray<FQuadric> &Quadrics,
        const TMap<uint64, bool> &IsBoundary,
        const TMap<uint64, bool> &IsUVSeam,
        const FClothDecimationParams &Params,
        TArray<FEdgeCollapse> &OutQueue);

    // Heap operations for dynamic priority queue
    static void HeapPush(TArray<FEdgeCollapse> &Heap, const FEdgeCollapse &Collapse);
    static FEdgeCollapse HeapPop(TArray<FEdgeCollapse> &Heap);
    static void HeapifyDown(TArray<FEdgeCollapse> &Heap, int32 Index);
    static void HeapifyUp(TArray<FEdgeCollapse> &Heap, int32 Index);
    static void MakeHeap(TArray<FEdgeCollapse> &Heap);

    // Compute single edge collapse
    static FEdgeCollapse ComputeEdgeCollapse(
        uint32 V0,
        uint32 V1,
        const TArray<FVector> &Positions,
        const TArray<FQuadric> &Quadrics,
        const TMap<uint64, bool> &IsBoundary,
        const TMap<uint64, bool> &IsUVSeam,
        const FClothDecimationParams &Params,
        uint32 Timestamp);

    static bool ExecuteEdgeCollapse(
        const FEdgeCollapse &Collapse,
        TArray<FVector> &Positions,
        TArray<uint32> &Indices,
        TArray<FQuadric> &Quadrics,
        TArray<uint8> &ValidVertices,
        FMeshConnectivity &Connectivity);

    static void CompactMesh(
        const TArray<FVector> &Positions,
        const TArray<uint32> &Indices,
        const TArray<uint8> &ValidVertices,
        TArray<FVector> &OutPositions,
        TArray<uint32> &OutIndices,
        TArray<int32> &OutVertexMapping);

    // Validation
    static bool ValidateManifold(
        const TArray<uint32> &Indices,
        uint32 NumVertices);

    static bool HasDegenerateTriangles(
        const TArray<FVector> &Positions,
        const TArray<uint32> &Indices,
        float MinArea);

    // Voronoi/Lloyd helpers
    static void InitializeSeedsWithFPS(
        const TArray<FVector> &Positions,
        int32 NumSeeds,
        TArray<FVector> &OutSeeds);

    static void InitializeSeedsRandom(
        const TArray<FVector> &Positions,
        int32 NumSeeds,
        TArray<FVector> &OutSeeds);

    static void PerformLloydIteration(
        const TArray<FVector> &Positions,
        const TArray<uint32> &Indices,
        TArray<FVector> &InOutSeeds);

    static void ProjectPointToMesh(
        const FVector &Point,
        const TArray<FVector> &Positions,
        const TArray<uint32> &Indices,
        FVector &OutProjectedPoint);

    static FVector ClosestPointOnTriangle(
        const FVector &P,
        const FVector &A,
        const FVector &B,
        const FVector &C);

    static void TriangulateSeeds(
        const TArray<FVector> &Seeds,
        const TArray<FVector> &OriginalPositions,
        const TArray<uint32> &OriginalIndices,
        TArray<uint32> &OutIndices,
        TArray<int32> &OutVertexMapping);

    static int32 FindNearestSeed(
        const FVector &Position,
        const TArray<FVector> &Seeds);

    // Utility
    static FVector ComputeTriangleNormal(
        const FVector &A,
        const FVector &B,
        const FVector &C);

    static double ComputeTriangleArea(
        const FVector &A,
        const FVector &B,
        const FVector &C);
};
