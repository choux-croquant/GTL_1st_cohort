/**
 * Cloth Asset Generator
 * High-level cloth asset generation orchestrator
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"
#include "ClothMeshDecimator.h"
#include "ClothSkinningWeightGenerator.h"
#include "Cloth/ClothSimulationData.h"

class UStaticMesh;
class UClothAsset;
class UClothMaterial;

/**
 * Render mesh data for cloth asset
 */
struct FClothRenderMeshData
{
    TArray<FVector> Positions;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<uint32> Indices;
    
    uint32 GetVertexCount() const { return Positions.Num(); }
    uint32 GetTriangleCount() const { return Indices.Num() / 3; }
};

/**
 * Simulation mesh data for cloth asset
 */
struct FClothSimulationMeshData
{
    TArray<FVector> Positions;
    TArray<uint32> Indices;
    TArray<float> InvMasses;
    
    uint32 GetVertexCount() const { return Positions.Num(); }
    uint32 GetTriangleCount() const { return Indices.Num() / 3; }
};

/**
 * Cloth skinning data
 */
struct FClothSkinningData
{
    TArray<FClothSkinningWeight> Weights;  // One per render vertex (K-nearest neighbor, legacy)
    TArray<FClothSkinningWeightTriangle> TriangleWeights;  // One per render vertex (triangle-based, recommended)
};

/**
 * Asset generation parameters
 */
struct FClothAssetGenerationParams
{
    // Decimation parameters
    FClothDecimationParams DecimationParams;
    
    // Skinning parameters
    FClothSkinningParams SkinningParams;
    
    // Constraint generation flags
    bool bGenerateDistanceConstraints = true;
    bool bGenerateBendConstraints = true;
    bool bGenerateAreaConstraints = true;
    bool bGenerateEdgeCollisions = true;
    
    // Mass distribution
    float UniformMass = 1.0f;  // Total mass of cloth
    bool bUseUniformMass = true;
    
    UClothMaterial* ClothMaterial = nullptr;  // Optional material override
    
    // Helper: Build from ClothMaterial
    static FClothAssetGenerationParams FromClothMaterial(
        UClothMaterial* Material,
        const FClothDecimationParams& DecimationParams,
        const FClothSkinningParams& SkinningParams
    );
};

/**
 * Asset generation result
 */
struct FClothAssetGenerationResult
{
    UClothAsset* Asset;
    
    bool bSuccess;
    FString ErrorMessage;
    
    // Statistics
    uint32 RenderVertexCount;
    uint32 RenderTriangleCount;
    uint32 SimVertexCount;
    uint32 SimTriangleCount;
    uint32 ConstraintCount;
    uint32 BendConstraintCount;
    uint32 AreaConstraintCount;
    uint32 EdgeCollisionCount;
    
    float GenerationTimeSeconds;
};

/**
 * Cloth Asset Generator
 * Orchestrates the complete asset generation pipeline
 */
class FClothAssetGenerator
{
public:
    /**
     * Generate complete cloth asset from Static Mesh
     * Pipeline: Extract render mesh → QEM decimate → Generate constraints → Calculate skinning
     * 
     * @param SourceMesh - Input static mesh
     * @param Params - Generation parameters
     * @param OutResult - Output generation result
     * @return Success/failure
     */
    static bool GenerateClothAssetFromStaticMesh(
        UStaticMesh* SourceMesh,
        const FClothAssetGenerationParams& Params,
        FClothAssetGenerationResult& OutResult
    );
    
private:
    /**
     * Extract render mesh data from Static Mesh
     */
    static bool ExtractRenderMeshData(
        UStaticMesh* SourceMesh,
        FClothRenderMeshData& OutRenderMesh,
        FString& OutError
    );

    /**
     * Generate simulation mesh from render mesh using QEM decimation
     */
    static bool GenerateSimulationMesh(
        const FClothRenderMeshData& RenderMesh,
        const FClothDecimationParams& Params,
        FClothSimulationMeshData& OutSimMesh,
        FString& OutError
    );
    
    /**
     * Generate physics constraints for simulation mesh
     */
    static bool GeneratePhysicsConstraints(
        const FClothSimulationMeshData& SimMesh,
        const FClothAssetGenerationParams& Params,
        TArray<FClothDistanceConstraint>& OutDistanceConstraints,
        TArray<FClothBendConstraint>& OutBendConstraints,
        TArray<FClothAreaConstraint>& OutAreaConstraints,
        TArray<FClothEdgeCollisionConstraint>& OutEdgeCollisions,
        FString& OutError
    );
    
    /**
     * Calculate skinning weights (render → sim mapping)
     */
    static bool CalculateSkinningWeights(
        const FClothRenderMeshData& RenderMesh,
        const FClothSimulationMeshData& SimMesh,
        const FClothSkinningParams& Params,
        FClothSkinningData& OutSkinningData,
        FString& OutError
    );
    
    /**
     * Package all data into UClothAsset
     */
    static UClothAsset* PackageIntoAsset(
        const FClothRenderMeshData& RenderMesh,
        const FClothSimulationMeshData& SimMesh,
        const FClothSkinningData& SkinningData,
        const TArray<FClothDistanceConstraint>& DistanceConstraints,
        const TArray<FClothBendConstraint>& BendConstraints,
        const TArray<FClothAreaConstraint>& AreaConstraints,
        const TArray<FClothEdgeCollisionConstraint>& EdgeCollisions,
        UStaticMesh* SourceMesh
    );
    
    /**
     * Helper: Calculate inverse masses
     */
    static void CalculateInverseMasses(
        const TArray<FVector>& Positions,
        const TArray<uint32>& Indices,
        float TotalMass,
        bool bUseUniformMass,
        TArray<float>& OutInvMasses
    );
};
