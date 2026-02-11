/**
 * Cloth Skinning Weight Generator
 * Generates skinning weights to map high-res render mesh to low-res simulation mesh
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"
#include "Core/Math/Vector.h"

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
 * Triangle-based skinning weight with tangent-space offset
 * Binds render vertex to closest simulation triangle
 * Preserves local surface detail through tangent-space encoding
 */
struct FClothSkinningWeightTriangle
{
    uint32 SimTriangleIndices[3];      // 3 simulation vertex indices forming triangle
    float BarycentricCoords[3];        // Barycentric coordinates (u, v, w) where u+v+w=1
    FVector TangentSpaceOffset;        // Offset in triangle's local tangent frame (tangent, bitangent, normal)
    
    FClothSkinningWeightTriangle()
    {
        SimTriangleIndices[0] = 0;
        SimTriangleIndices[1] = 0;
        SimTriangleIndices[2] = 0;
        BarycentricCoords[0] = 1.0f;
        BarycentricCoords[1] = 0.0f;
        BarycentricCoords[2] = 0.0f;
        TangentSpaceOffset = FVector::ZeroVector;
    }
};

// Serialization operator for FClothSkinningWeightTriangle
inline FArchive& operator<<(FArchive& Ar, FClothSkinningWeightTriangle& W)
{
    for (int i = 0; i < 3; ++i)
        Ar << W.SimTriangleIndices[i];
    for (int i = 0; i < 3; ++i)
        Ar << W.BarycentricCoords[i];
    Ar << W.TangentSpaceOffset;
    return Ar;
}

/**
 * Skinning weight generation parameters
 */
struct FClothSkinningParams
{
    uint32 MaxInfluences = CLOTH_MAX_SKINNING_INFLUENCES;  // Number of sim vertices per render vertex
    float MaxDistance = 100.0f;                             // Max search distance for influences
    bool bNormalizeWeights = true;                          // Ensure weights sum to 1.0
    bool bUseInverseDistanceWeighting = true;               // Use 1/dist weighting (vs uniform)
    float WeightPower = 1.0f;                               // Exponent for distance weighting (higher = more local)
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
 * Cloth Skinning Weight Generator
 * Computes how to map high-res render vertices to low-res simulation vertices
 */
class FClothSkinningWeightGenerator
{
public:
    /**
     * Generate skinning weights for render mesh based on simulation mesh
     * Uses K-nearest neighbors with inverse distance weighting
     * 
     * @param RenderPositions - High-res render mesh vertices
     * @param SimPositions - Low-res simulation mesh vertices
     * @param Params - Generation parameters
     * @param OutResult - Output skinning weights
     * @return Success/failure
     */
    static bool GenerateSkinningWeights(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& SimPositions,
        const FClothSkinningParams& Params,
        FClothSkinningResult& OutResult
    );
    
    /**
     * Generate triangle-based skinning weights with tangent-space offsets
     * Fixes edge curling and UV distortion by preserving local surface detail
     *
     * @param RenderPositions - High-res render mesh vertices
     * @param SimPositions - Low-res simulation mesh vertices
     * @param SimIndices - Low-res simulation mesh triangle indices
     * @param Params - Generation parameters
     * @param OutWeights - Output triangle-based skinning weights
     * @param OutError - Error message if generation fails
     * @return Success/failure
     */
    static bool GenerateTriangleSkinningWeights(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& SimPositions,
        const TArray<uint32>& SimIndices,
        const FClothSkinningParams& Params,
        TArray<FClothSkinningWeightTriangle>& OutWeights,
        FString& OutError
    );
    
private:
    // Spatial acceleration structure for nearest neighbor search
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
    
    // Helper: Compute inverse distance weights
    static void ComputeWeightsFromDistances(
        const TArray<float>& Distances,
        const FClothSkinningParams& Params,
        TArray<float>& OutWeights
    );
    
    // Validation: Check all render vertices have valid influences
    static bool ValidateWeights(
        const TArray<FClothSkinningWeight>& Weights,
        FString& OutErrorMessage
    );
    
    // Helper: Compute barycentric coordinates of point P in triangle (A, B, C)
    static FVector ComputeBarycentricCoordinates(
        const FVector& P,
        const FVector& A,
        const FVector& B,
        const FVector& C
    );
    
    // Helper: Find closest triangle to a point (brute force)
    static int32 FindClosestTriangleBruteForce(
        const FVector& Point,
        const TArray<FVector>& Positions,
        const TArray<uint32>& Indices,
        FVector& OutClosestPoint,
        float& OutMinDistance
    );
    
    // Helper: Compute tangent frame for a triangle
    static void ComputeTangentFrame(
        const FVector& V0,
        const FVector& V1,
        const FVector& V2,
        FVector& OutTangent,
        FVector& OutBitangent,
        FVector& OutNormal
    );
};
