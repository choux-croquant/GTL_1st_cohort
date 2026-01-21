#pragma once
#include "GameFramework/Actor.h"
#include "Cloth/ClothSimulationData.h"

class UClothMeshComponent;
class UClothAsset;
class AStaticMeshActor;

class ATestClothActor : public AActor
{
    DECLARE_CLASS(ATestClothActor, AActor)

public:
    ATestClothActor();
    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;

    UClothMeshComponent *ClothMesh;

    UClothAsset *ClothAsset;

    // Attachment driver - a StaticMesh that the cloth is attached to
    AStaticMeshActor *AttachmentDriver;

    // Initial driver position
    FVector DriverInitialPosition;

    // Create a simple test cloth (10x10 grid)
    void CreateTestCloth();

    // Update attachment positions each frame to follow the driver mesh
    void UpdateAttachments();

private:
    // Cached attachment data
    TArray<FClothAttachmentData> CachedAttachments;

    // Animation time for scripted motion
    float AnimationTime;


    // Flag to track if driver has been spawned
    bool bDriverSpawned;
};
