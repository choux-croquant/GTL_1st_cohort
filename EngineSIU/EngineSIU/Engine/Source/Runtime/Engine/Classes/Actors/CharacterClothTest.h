#pragma once
#include "GameFramework/Actor.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;
class UAnimSequence;


class ACharacterClothTest : public AActor
{
    DECLARE_CLASS(ACharacterClothTest, AActor)
public:
    ACharacterClothTest() = default;

    virtual void PostSpawnInitialize() override;

    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;

    UObject* Duplicate(UObject* InOuter);
  
public:
    FTransform InitialTransform;
};
