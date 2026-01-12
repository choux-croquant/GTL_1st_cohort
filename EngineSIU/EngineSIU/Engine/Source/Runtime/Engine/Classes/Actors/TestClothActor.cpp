#include "TestClothActor.h"
#include "Math/Vector.h"
#include "UObject/ObjectFactory.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"

ATestClothActor::ATestClothActor()
{
    // Create cloth mesh component
    ClothMesh = AddComponent<UClothMeshComponent>(TEXT("ClothMesh"));
    RootComponent = ClothMesh;

    // Create cloth asset
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
    const int32 GridSize = 9;
    const float Spacing = 10.0f;  // 10 cm between particles

    TArray<FVector> positions;
    TArray<uint32> indices;
    TArray<float> invMasses;

    // Generate grid vertices
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            FVector pos;
            pos.X = x * Spacing;
            pos.Y = y * Spacing;
            pos.Z = 0.0f;

            positions.Add(pos);

            // Top row is fixed (invMass = 0)
            float invMass = (y == 0) ? 0.0f : 1.0f;
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
                float restLength = (positions[neighborIdx] - positions[idx]).Size();
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 0.9f));
            }

            // Vertical constraint
            if (y < GridSize - 1)
            {
                int32 neighborIdx = (y + 1) * GridSize + x;
                float restLength = (positions[neighborIdx] - positions[idx]).Size();
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
    config.Damping = 0.05f;
    config.StretchStiffness = 0.9f;
    config.NumIterations = 5;
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;

    ClothAsset->SetConfig(config);
}
