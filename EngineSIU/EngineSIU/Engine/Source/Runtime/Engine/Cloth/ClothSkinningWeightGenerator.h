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
};
