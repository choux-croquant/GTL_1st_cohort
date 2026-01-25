/**
 * Test Batched Cloth Actor
 * Demonstrates the new batched cloth simulation system with multiple LOD levels
 */

#pragma once

#include "GameFramework/Actor.h"
#include "Cloth/ClothSimulationData.h"
#include "Cloth/ClothBatchTypes.h"

class UClothMeshComponent;
class UClothAsset;
class AStaticMeshActor;
class FClothInstanceHandle;

/**
 * Test actor for batched cloth simulation
 * Creates multiple cloth instances to demonstrate batching and LOD system
 */
class ATestBatchedClothActor : public AActor
{
    DECLARE_CLASS(ATestBatchedClothActor, AActor)

public:
    ATestBatchedClothActor();
    virtual void BeginPlay() override;
    virtual void PostSpawnInitialize() override;
    virtual void Tick(float DeltaTime) override;

    // Multiple cloth instances for batching test
    static constexpr int32 NumClothInstances = 5;
    UClothMeshComponent *ClothMeshes[NumClothInstances];
    UClothAsset *ClothAssets[NumClothInstances];
    FClothInstanceHandle *ClothHandles[NumClothInstances];

    // Attachment drivers
    AStaticMeshActor *AttachmentDrivers[NumClothInstances];
    FVector DriverInitialPositions[NumClothInstances];

    // Create test cloth grid
    void CreateTestCloth(int32 Index, int32 GridSize, EClothLODLevel LOD);

private:
    // Animation time
    float AnimationTime;

    // Driver spawn tracking
    bool bDriversSpawned;

    // CRITICAL: Prevent double initialization
    bool bClothInitialized;
};
