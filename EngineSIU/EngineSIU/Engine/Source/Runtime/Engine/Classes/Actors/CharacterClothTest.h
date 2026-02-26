#pragma once
#include "GameFramework/Actor.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;
class UAnimSequence;
class UClothMeshComponent;


class ACharacterClothTest : public AActor
{
    DECLARE_CLASS(ACharacterClothTest, AActor)
public:
    ACharacterClothTest();

    virtual void PostSpawnInitialize() override;

    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;

    UObject* Duplicate(UObject* InOuter);

private:
    // Helper function to apply attachment paint data from asset
    void ApplyAttachmentPaintData(
        UClothMeshComponent* ClothComp,
        USkeletalMeshComponent* SkelMeshComp
    );
  
public:
    FTransform InitialTransform;

private:
    bool bInitialized;
};
