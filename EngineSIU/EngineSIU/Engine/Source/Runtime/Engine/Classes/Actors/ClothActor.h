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
    
public:
   

private:
    // Components
    UClothMeshComponent* ClothMeshComponent;
    
    // Runtime handles
    FClothInstanceHandle* ClothInstanceHandle;
};
