/**
 * Cloth Skinning Weight Generator
 * Generates skinning weights to map high-res render mesh to low-res simulation mesh
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"
#include "Core/Math/Vector.h"
#include "Launch/Define.h"  // For FBoundingBox

// Maximum influences per render vertex (same as skeletal mesh)
#define CLOTH_MAX_SKINNING_INFLUENCES 4

/**
 * CPU-side skinning weight structure
 * Maps a single render vertex to multiple simulation vertices
 */
struct FClothSkinningWeight
{
    uint32 SimVertexIndices[CLOTH_MAX_SKINNING_INFLUENCES];
    float Weights[CLOTH_MAX_SKINNING_INFLUENCES];
    uint32 NumInfluences;
    
    FClothSkinningWeight()
        : NumInfluences(0)
    {
        for (int i = 0; i < CLOTH_MAX_SKINNING_INFLUENCES; ++i)
        {
            SimVertexIndices[i] = 0;
            Weights[i] = 0.0f;
        }
    }
};

// Serialization operator for FClothSkinningWeight
inline FArchive& operator<<(FArchive& Ar, FClothSkinningWeight& W)
{
    for (int i = 0; i < CLOTH_MAX_SKINNING_INFLUENCES; ++i)
    {
        Ar << W.SimVertexIndices[i];
    }
    for (int i = 0; i < CLOTH_MAX_SKINNING_INFLUENCES; ++i)
    {
        Ar << W.Weights[i];
    }
    Ar << W.NumInfluences;
    return Ar;
}

/**
 * Weighting method selection
 */
enum class EClothWeightingMethod
{
    InverseDistance,  // Legacy K-nearest neighbors with inverse distance weighting
    Barycentric       // Triangle-based projection with barycentric interpolation
};

/**
 * Skinning weight generation parameters
 */
struct FClothSkinningParams
{
    // Method selection
    EClothWeightingMethod WeightingMethod = EClothWeightingMethod::Barycentric;
    
    // Common parameters
    uint32 MaxInfluences = CLOTH_MAX_SKINNING_INFLUENCES;  // Number of sim vertices per render vertex
    bool bNormalizeWeights = true;                          // Ensure weights sum to 1.0
    
    // Inverse distance parameters (legacy)
    float MaxDistance = 100.0f;                             // Max search distance for influences
    bool bUseInverseDistanceWeighting = true;               // Use 1/dist weighting (vs uniform)
    float WeightPower = 1.0f;                               // Exponent for distance weighting (higher = more local)
    
    // Barycentric parameters
    float MaxSearchDistance = 100.0f;                       // Max distance for triangle search
    bool bUseClosestPointProjection = true;                 // Project to triangle plane
    bool bFallbackToInverseDistance = true;                 // Fallback if no triangle found
    int32 BVHMaxLeafTriangles = 8;                          // BVH construction parameter
    
    // Performance optimization parameters
    bool bUseSAH = false;                                   // Use Surface Area Heuristic for BVH (slower build, faster query)
    bool bEnableMultiThreading = false;                     // Enable parallel weight generation
    bool bCacheTriangleData = true;                         // Cache triangle computations
    float SAHTraversalCost = 1.0f;                          // Relative cost of BVH traversal vs triangle test
    float SAHIntersectionCost = 1.0f;                       // Relative cost of triangle intersection test
};

/**
 * Skinning weight generator result
 */
struct FClothSkinningResult
{
    TArray<FClothSkinningWeight> Weights;  // One entry per render vertex
    bool bSuccess;
    FString ErrorMessage;
    
    uint32 NumRenderVertices;
    uint32 NumSimVertices;
    uint32 VerticesWithoutInfluences;  // Error metric
};

/**
 * Barycentric coordinate result
 */
struct FBarycentricCoordinates
{
    float U, V, W;  // Barycentric coordinates (U+V+W=1)
    uint32 TriangleIndex;
    float DistanceToTriangle;
    bool bIsValid;
    
    FBarycentricCoordinates()
        : U(0.0f), V(0.0f), W(0.0f)
        , TriangleIndex(0)
        , DistanceToTriangle(FLT_MAX)
        , bIsValid(false)
    {}
    
    bool IsInsideTriangle() const { return bIsValid && U >= 0.0f && V >= 0.0f && W >= 0.0f; }
};

/**
 * Cloth Skinning Weight Generator
 * Computes how to map high-res render vertices to low-res simulation vertices
 */
class FClothSkinningWeightGenerator
{
public:
    /**
     * Generate skinning weights for render mesh based on simulation mesh
     * Supports both inverse distance and barycentric weighting methods
     *
     * @param RenderPositions - High-res render mesh vertices
     * @param SimPositions - Low-res simulation mesh vertices
     * @param SimIndices - Simulation mesh triangle indices (required for barycentric method)
     * @param Params - Generation parameters
     * @param OutResult - Output skinning weights
     * @return Success/failure
     */
    static bool GenerateSkinningWeights(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& SimPositions,
        const TArray<uint32>& SimIndices,
        const FClothSkinningParams& Params,
        FClothSkinningResult& OutResult
    );
    
    /**
     * Legacy overload for backward compatibility (uses inverse distance method)
     */
    static bool GenerateSkinningWeights(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& SimPositions,
        const FClothSkinningParams& Params,
        FClothSkinningResult& OutResult
    );
    
private:
    // ========== Legacy Inverse Distance Method ==========
    
    // Spatial acceleration structure for nearest neighbor search (legacy)
    struct FSimpleSpatialHash
    {
        TMap<uint64, TArray<uint32>> Grid;
        float CellSize;
        FVector MinBounds;
        FVector MaxBounds;
        
        void Build(const TArray<FVector>& Positions, float InCellSize);
        void FindNearestNeighbors(const FVector& QueryPos, uint32 K,
                                  const TArray<FVector>& Positions,
                                  TArray<uint32>& OutIndices,
                                  TArray<float>& OutDistances) const;
        
        uint64 GetCellKey(const FVector& Pos) const;
        void GetCellCoords(const FVector& Pos, int32& X, int32& Y, int32& Z) const;
    };
    
    // Generate weights using inverse distance method (legacy)
    static bool GenerateInverseDistanceWeights(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& SimPositions,
        const FClothSkinningParams& Params,
        FClothSkinningResult& OutResult
    );
    
    // Helper: Compute inverse distance weights
    static void ComputeWeightsFromDistances(
        const TArray<float>& Distances,
        const FClothSkinningParams& Params,
        TArray<float>& OutWeights
    );
    
    // ========== Barycentric Method ==========
    
    // BVH node for triangle acceleration structure
    struct FBVHNode
    {
        FBoundingBox BoundingBox;
        int32 LeftChild;      // -1 if leaf
        int32 RightChild;     // -1 if leaf
        int32 TriangleStart;  // For leaf nodes
        int32 TriangleCount;  // For leaf nodes
        
        FBVHNode()
            : LeftChild(-1), RightChild(-1)
            , TriangleStart(-1), TriangleCount(0)
        {}
        
        bool IsLeaf() const { return LeftChild == -1 && RightChild == -1; }
    };
    
    // Cached triangle data for optimization
    struct FCachedTriangleData
    {
        FVector V0, V1, V2;
        FVector Centroid;
        FBoundingBox Bounds;
        float SurfaceArea;
    };
    
    // Triangle BVH for fast nearest-triangle queries
    class FTriangleBVH
    {
    public:
        void Build(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            int32 MaxLeafTriangles,
            bool bUseSAH = false,
            float TraversalCost = 1.0f,
            float IntersectionCost = 1.0f
        );
        
        bool FindNearestTriangle(
            const FVector& QueryPoint,
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            uint32& OutTriangleIndex,
            float& OutDistance
        ) const;
        
        // Get cached triangle data if available
        const FCachedTriangleData* GetCachedTriangleData(uint32 TriangleIndex) const;
        
        // Build triangle cache for faster queries
        void BuildTriangleCache(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices
        );
        
    private:
        TArray<FBVHNode> Nodes;
        TArray<uint32> TriangleIndices;  // Reordered triangle indices
        TArray<FCachedTriangleData> TriangleCache;  // Optional triangle data cache
        bool bHasCache;
        
        int32 BuildRecursive(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            TArray<uint32>& TriangleList,
            int32 Start,
            int32 End,
            int32 MaxLeafTriangles,
            bool bUseSAH,
            float TraversalCost,
            float IntersectionCost
        );
        
        // SAH-based split finding
        int32 FindBestSAHSplit(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            const TArray<uint32>& TriangleList,
            int32 Start,
            int32 End,
            const FBoundingBox& ParentBounds,
            float TraversalCost,
            float IntersectionCost,
            int32& OutSplitAxis
        ) const;
        
        void FindNearestRecursive(
            int32 NodeIndex,
            const FVector& QueryPoint,
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            uint32& BestTriangle,
            float& BestDistance
        ) const;
        
        FBoundingBox CalculateTriangleBounds(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            uint32 TriangleIndex
        ) const;
        
        FVector GetTriangleCentroid(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices,
            uint32 TriangleIndex
        ) const;
        
        float CalculateSurfaceArea(const FBoundingBox& Bounds) const;
        
        void BuildTriangleCache(
            const TArray<FVector>& Vertices,
            const TArray<uint32>& Indices
        );
    };
    
    // Generate weights using barycentric method
    static bool GenerateBarycentricWeights(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& SimPositions,
        const TArray<uint32>& SimIndices,
        const FClothSkinningParams& Params,
        FClothSkinningResult& OutResult
    );
    
    /**
     * Calculate barycentric coordinates for a point relative to a triangle
     *
     * @param Point - Query point in 3D space
     * @param TriV0 - First triangle vertex
     * @param TriV1 - Second triangle vertex
     * @param TriV2 - Third triangle vertex
     * @return Barycentric coordinates and validity flag
     */
    static FBarycentricCoordinates CalculateBarycentricCoordinates(
        const FVector& Point,
        const FVector& TriV0,
        const FVector& TriV1,
        const FVector& TriV2
    );
    
    /**
     * Project point onto triangle and return closest point
     *
     * @param Point - Query point
     * @param TriV0 - First triangle vertex
     * @param TriV1 - Second triangle vertex
     * @param TriV2 - Third triangle vertex
     * @param OutBarycentrics - Output barycentric coordinates
     * @return Closest point on triangle
     */
    static FVector ProjectPointOntoTriangle(
        const FVector& Point,
        const FVector& TriV0,
        const FVector& TriV1,
        const FVector& TriV2,
        FBarycentricCoordinates& OutBarycentrics
    );
    
    /**
     * Calculate distance from point to triangle
     */
    static float CalculatePointToTriangleDistance(
        const FVector& Point,
        const FVector& TriV0,
        const FVector& TriV1,
        const FVector& TriV2
    );
    
    // ========== Common Helpers ==========
    
    // Validation: Check all render vertices have valid influences
    static bool ValidateWeights(
        const TArray<FClothSkinningWeight>& Weights,
        FString& OutErrorMessage
    );
};
