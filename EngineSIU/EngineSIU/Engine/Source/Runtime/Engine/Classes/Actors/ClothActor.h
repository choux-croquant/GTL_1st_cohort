/**
 * Cloth Actor
 * Automated cloth asset generation from Static Mesh with editor UI integration
 * Supports render/simulation mesh separation with QEM decimation
 */

#pragma once

#include "GameFramework/Actor.h"
#include "Cloth/ClothSimulationData.h"
#include "Cloth/ClothBatchTypes.h"
#include "Cloth/ClothAssetGenerator.h"
#include "UObject/ObjectMacros.h"

class UClothMeshComponent;
class UClothAsset;
class UStaticMesh;
class FClothInstanceHandle;

/**
 * Cloth Actor - Artist-friendly cloth simulation actor
 * Workflow:
 *   1. Assign Source Static Mesh in Detail Panel
 *   2. Configure generation parameters
 *   3. Click "Generate Cloth Asset" button
 *   4. Enter PIE - cloth simulates with batched system
 */
class AClothActor : public AActor
{
    DECLARE_CLASS(AClothActor, AActor)

public:
    AClothActor();
    virtual ~AClothActor();
    
    // Actor lifecycle
    virtual void BeginPlay() override;
    virtual void PostSpawnInitialize() override;
    virtual void Tick(float DeltaTime) override;
    
    // Asset generation API (called from Detail Panel button or code)
    void GenerateClothAsset();
    void ClearClothAsset();
    bool ValidateSetup(FString& OutErrorMessage);
    
    // PIE mode initialization
    void RegisterWithClothWorld();
    void UnregisterFromClothWorld();

public:
    // ===== EDITOR PROPERTIES (exposed in Detail Panel) =====
    
    // Source mesh (assigned by artist)
    //UPROPERTY(EditAnywhere, UStaticMesh*, SourceStaticMesh, = nullptr)
    UStaticMesh* GetStaticMesh() const { return SourceStaticMesh; }
    void SetStaticMesh(UStaticMesh* Value) { SourceStaticMesh = Value; }

    UStaticMesh* SourceStaticMesh = nullptr;

    // Generated cloth asset (visible but not editable)
    UPROPERTY(VisibleAnywhere, UClothAsset*, GeneratedClothAsset, = nullptr)
    
    // === Generation Parameters ===

    UPROPERTY(EditAnywhere, float, SimulationMeshReductionRatio, = 0.1f)  // 0.1 = 10% of original vertices

    UPROPERTY(EditAnywhere, bool, bPreserveBoundaryEdges, = true)

    UPROPERTY(EditAnywhere, bool, bPreserveUVSeams, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateDistanceConstraints, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateBendConstraints, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateAreaConstraints, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateEdgeCollisions, = true)

    // === Simulation Parameters ===

    UPROPERTY(EditAnywhere, float, StretchStiffness, = 0.9f)

    UPROPERTY(EditAnywhere, float, BendStiffness, = 0.1f)

    UPROPERTY(EditAnywhere, float, AreaStiffness, = 0.001f)

    UPROPERTY(EditAnywhere, float, Damping, = 0.01f)

    UPROPERTY(EditAnywhere, FVector, Gravity, = FVector(0, 0, -980.0f))

    UPROPERTY(EditAnywhere, float, TotalMass, = 1.0f)

    UPROPERTY(EditAnywhere, EClothLODLevel, LODLevel, = EClothLODLevel::LOD_0)

    // === Status Display (read-only) ===

    UPROPERTY(VisibleAnywhere, bool, bAssetGenerated, = false)

    UPROPERTY(VisibleAnywhere, FString, LastErrorMessage, = "")

private:
    // Components
    UClothMeshComponent* ClothMeshComponent;
    
    // Runtime handles
    FClothInstanceHandle* ClothInstanceHandle;
    
    // Initialization guards
    bool bClothInitialized;
    bool bRegisteredWithWorld;
    
    // Helper methods
    void UpdateStatusDisplay();
    FClothAssetGenerationParams BuildGenerationParams() const;
};
