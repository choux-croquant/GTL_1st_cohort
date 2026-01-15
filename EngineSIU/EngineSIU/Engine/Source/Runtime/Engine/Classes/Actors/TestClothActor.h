#pragma once
#include "GameFramework/Actor.h"

class UClothMeshComponent;
class UClothAsset;

class ATestClothActor : public AActor
{
    DECLARE_CLASS(ATestClothActor, AActor)

public:
    ATestClothActor();
    virtual void BeginPlay() override;

    virtual void Tick(float DeltaTime) override;

    UClothMeshComponent* ClothMesh;

    UClothAsset* ClothAsset;

    // Create a simple test cloth (10x10 grid)
    void CreateTestCloth();
};
