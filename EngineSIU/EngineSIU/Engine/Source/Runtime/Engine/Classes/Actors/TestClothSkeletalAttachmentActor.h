/**
 * Test Cloth Skeletal Attachment Actor
 * Demonstrates cloth attached to animated skeletal mesh bones
 * 
 * Usage:
 *   SpawnActor ATestClothSkeletalAttachmentActor 0 0 100
 */

#pragma once

#include "GameFramework/Actor.h"

class USkeletalMeshComponent;
class UClothMeshComponent;
class UStaticMesh;
class UClothAsset;

/**
 * Test actor for skeletal mesh cloth attachments
 * Creates an animated character with a cape attached to bones
 */
class ATestClothSkeletalAttachmentActor : public AActor
{
    DECLARE_CLASS(ATestClothSkeletalAttachmentActor, AActor)

public:
    ATestClothSkeletalAttachmentActor();
    virtual void PostSpawnInitialize() override;
    virtual void Tick(float DeltaTime) override;

private:
    // Components
    USkeletalMeshComponent* CharacterMesh;
    UClothMeshComponent* CapeCloth;
    
    // Animation state
    float AnimationTime;
    FVector InitialCharacterLocation;
    
    // Initialization guard
    bool bInitialized;
};
