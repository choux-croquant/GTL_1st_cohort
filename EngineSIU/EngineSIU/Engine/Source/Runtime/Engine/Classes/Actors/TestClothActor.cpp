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

void ATestClothActor::Tick(float DeltaTime)
{
}

void ATestClothActor::CreateTestCloth()
{
    const int32 GridSize = 24;
    const float Spacing = 5.0f; // 10 cm spacing between particles

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
            pos.Y = Spacing * (x - GridSize / 2.0f);
            pos.Z = Spacing * (GridSize / 2.0f - y); // Top row at top, bottom row at bottom

            positions.Add(pos);

            // Top row is fixed (pinned)
            float invMass = (y == 0) ? 0.0f : 1.0f;
            invMasses.Add(invMass);
            /*bool bIsTopRow = (y == 0);
            bool bIsLeftCorner = (x == 0);
            bool bIsRightCorner = (x == GridSize - 1);

            float invMass = (bIsTopRow && (bIsLeftCorner || bIsRightCorner)) ? 0.0f : 1.0f;
            invMasses.Add(invMass);*/
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

    // Structural springs (horizontal and vertical)
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            int32 idx = y * GridSize + x;

            // Horizontal constraint (structural)
            if (x < GridSize - 1)
            {
                int32 neighborIdx = y * GridSize + (x + 1);
                float restLength = (positions[neighborIdx] - positions[idx]).Length();
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 1.0f));
            }

            // Vertical constraint (structural)
            if (y < GridSize - 1)
            {
                int32 neighborIdx = (y + 1) * GridSize + x;
                float restLength = (positions[neighborIdx] - positions[idx]).Length();
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 1.0f));
            }
        }
    }

    // Shear springs (diagonals for triangle stability)
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);

            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;

            float restLength03 = (positions[i3] - positions[i0]).Length();
            float restLength12 = (positions[i2] - positions[i1]).Length();
            constraints.Add(FClothConstraint(i0, i3, restLength03, 1.0f));
            constraints.Add(FClothConstraint(i1, i2, restLength12, 1.0f));
        }
    }

    // Set cloth asset data
    ClothAsset->SetRestPositions(positions);
    ClothAsset->SetIndices(indices);
    ClothAsset->SetInvMasses(invMasses);

    for (const FClothConstraint &constraint : constraints)
    {
        ClothAsset->AddDistanceConstraint(constraint);
    }

    // Configure simulation parameters
    FClothConfig config;
    config.Mass = 1.0f;
    config.Damping = 0.5f;          // Lower damping for more dynamic motion
    config.StretchStiffness = 0.9f; // High stiffness for structural integrity
    config.NumIterations = 5;        // Increase iterations for better convergence
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;

    ClothAsset->SetConfig(config);
}
