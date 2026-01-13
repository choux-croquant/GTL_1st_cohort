#include "TestClothActor.h"
#include "Math/Vector.h"
#include "UObject/ObjectFactory.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"

ATestClothActor::ATestClothActor()
{
    ClothMesh = AddComponent<UClothMeshComponent>(TEXT("ClothMesh"));
    RootComponent = ClothMesh;

    ClothAsset = FObjectFactory::ConstructObject<UClothAsset>(this, TEXT("TestClothAsset"));
}

void ATestClothActor::BeginPlay()
{
    Super::BeginPlay();

    // Generate simple test cloth
    CreateTestCloth();

    // Assign to component
    ClothMesh->SetClothAsset(ClothAsset);
    ClothMesh->StartSimulation();
}

void ATestClothActor::CreateTestCloth()
{
    const int32 GridSize = 10;
    const float Spacing = 2.0f;

    TArray<FVector> positions;
    TArray<uint32> indices;
    TArray<float> invMasses;

    // Generate grid vertices
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            FVector pos;

            pos.X = 0.0f;
            pos.Y = Spacing * (x - GridSize / 2);
            pos.Z = Spacing * (y - GridSize / 2);

            positions.Add(pos);

            // Top row`s left & right is fixed
            float invMass = 1.0f;
            //float invMass = (y == GridSize - 1) ? 0.0f : 1.0f;
            if (y == GridSize - 1) {
                if (x == 0 || x == GridSize - 1) {
                    invMass = 0.0f;
                }
            }
            invMasses.Add(invMass);
        }
    }

    // Generate triangle indices
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);

            // Triangle 1
            indices.Add(i0);
            indices.Add(i1);
            indices.Add(i2);

            // Triangle 2
            indices.Add(i1);
            indices.Add(i3);
            indices.Add(i2);
        }
    }

    // Generate distance constraints
    TArray<FClothConstraint> constraints;

    // Horizontal and vertical springs
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            int32 idx = y * GridSize + x;

            // Horizontal constraint
            if (x < GridSize - 1)
            {
                int32 neighborIdx = y * GridSize + (x + 1);
                float restLength = (positions[neighborIdx] - positions[idx]).Length() * 0.01f;
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 0.9f));
            }

            // Vertical constraint
            if (y < GridSize - 1)
            {
                int32 neighborIdx = (y + 1) * GridSize + x;
                float restLength = (positions[neighborIdx] - positions[idx]).Length() * 0.01f;
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 0.9f));
            }
        }
    }

    // Set cloth asset data
    ClothAsset->SetRestPositions(positions);
    ClothAsset->SetIndices(indices);
    ClothAsset->SetInvMasses(invMasses);

    for (const FClothConstraint& constraint : constraints)
    {
        ClothAsset->AddDistanceConstraint(constraint);
    }

    // Configure simulation parameters
    FClothConfig config;
    config.Mass = 1.0f;
    config.Damping = 0.1f;
    config.StretchStiffness = 0.9f;
    config.NumIterations = 2;
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;
    //config.bUseXPBD = true;

    ClothAsset->SetConfig(config);
}
