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
    // Target: ~200 instances with ~400 particles each = ~80,000 total particles
    static constexpr int32 NumClothInstances = 200;
    TArray<UClothMeshComponent*> ClothMeshes;

    // Shared cloth asset (generated once, reused by all instances)
    UClothAsset* SharedClothAsset;

    // Create shared cloth asset (called once)
    void CreateSharedClothAsset();
    
    // Create test cloth instance using shared asset
    void CreateTestClothMesh(int32 Index, const FVector& Position);

private:
    // Animation time
    float AnimationTime;

    // CRITICAL: Prevent double initialization
    bool bClothInitialized;
    bool bDriversSpawned;  // Kept for compatibility
};
