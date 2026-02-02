/**
 * Cloth Actor Implementation
 * Automated cloth asset generation from Static Mesh with editor integration
 */

#include "ClothActor.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/Asset/StaticMeshAsset.h"
#include "World/World.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "Cloth/ClothAssetGenerator.h"
#include "UObject/ObjectFactory.h"

AClothActor::AClothActor()
{
    // Create cloth mesh component
    ClothMeshComponent = AddComponent<UClothMeshComponent>(TEXT("ClothMeshComponent"));
    RootComponent = ClothMeshComponent;
}

AClothActor::~AClothActor()
{
}

void AClothActor::BeginPlay()
{
    Super::BeginPlay();
}

void AClothActor::PostSpawnInitialize()
{
    // Called after actor spawned in world
    // Can be used for additional setup
}

void AClothActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    // Cloth simulation runs in ClothWorld::Update()
    // This actor just needs to exist - no per-frame logic needed
}
