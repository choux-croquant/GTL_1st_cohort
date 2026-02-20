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
    UStaticMeshComponent* PoleComponent2;  // Second pole for second cloth
    UClothMeshComponent* ClothComponent2;  // Second cloth with 90 degree rotation
    UStaticMeshComponent* SphereComponent;  // Sphere for third cloth
    UClothMeshComponent* ClothComponent3;  // Third cloth with single vertex attachment
    UStaticMeshComponent* PoleComponent3;
    UClothMeshComponent* ClothComponent4;

    // Animation
    float AnimationTime;
    FVector InitialPoleLocation;    // Store initial location for first pole oscillation
    FVector InitialPole2Location;   // Store initial location for second pole oscillation
    FVector InitialPole3Location;   // Store initial location for second pole oscillation
    FVector InitialSphereLocation;  // Store initial location for sphere circular motion
    
    // Initialization guard
    bool bInitialized;
};
