/**
 * Test Cloth Attachment Actor
 * Simple demonstration of cloth attached to a moving component
 * Ready to use in PIE mode without additional setup
 */

#pragma once

#include "GameFramework/Actor.h"

class UClothMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UClothAsset;

/**
 * Test actor that creates a cloth attached to a moving pole
 * The pole rotates and the cloth follows it
 */
class ATestClothAttachmentActor : public AActor
{
    DECLARE_CLASS(ATestClothAttachmentActor, AActor)

public:
    ATestClothAttachmentActor();
    virtual void PostSpawnInitialize() override;
    virtual void Tick(float DeltaTime) override;

private:
    // Components
    UStaticMeshComponent* PoleComponent;
    UClothMeshComponent* ClothComponent;

    // Animation
    float AnimationTime;
    
    // Initialization guard
    bool bInitialized;
};
