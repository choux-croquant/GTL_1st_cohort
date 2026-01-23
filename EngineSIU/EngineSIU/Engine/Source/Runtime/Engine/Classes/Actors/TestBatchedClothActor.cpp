/**
 * Test Batched Cloth Actor Implementation
 * Demonstrates batched cloth simulation with multiple instances across LOD levels
 */

#include "TestBatchedClothActor.h"
#include "Math/Vector.h"
#include "UObject/ObjectFactory.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "World/World.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "Classes/Engine/FObjLoader.h"

ATestBatchedClothActor::ATestBatchedClothActor()
{
    // Initialize all cloth meshes and assets
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        FString ComponentName = FString::Printf(TEXT("ClothMesh_%d"), i);
        ClothMeshes[i] = AddComponent<UClothMeshComponent>(*ComponentName);

        FString AssetName = FString::Printf(TEXT("TestClothAsset_%d"), i);
        ClothAssets[i] = FObjectFactory::ConstructObject<UClothAsset>(this, *AssetName);

        ClothHandles[i] = nullptr;
        AttachmentDrivers[i] = nullptr;
        DriverInitialPositions[i] = FVector::ZeroVector;
    }

    // Set root component to first cloth mesh
    RootComponent = ClothMeshes[0];

    AnimationTime = 0.0f;
    bDriversSpawned = false;
}

void ATestBatchedClothActor::BeginPlay()
{
    Super::BeginPlay();

    // Create test cloths with different sizes and LOD levels
    // This demonstrates the batching system's ability to handle multiple instances

    // LOD 0 (High detail) - Batch Test Only use LOD1 at first
    for (int i = 0; i < 256; i++)
    {
        CreateTestCloth(i, 20, EClothLODLevel::LOD_0); // 12x12 grid
    }

    // Position instances in a grid layout
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        int32 row = i / 4;
        int32 col = i % 4;

        FVector offset(row * 150.0f, col * 150.0f, 0.0f);
        FVector instanceLocation = GetActorLocation() + offset;
        ClothMeshes[i]->SetWorldLocation(instanceLocation);

        // Store driver initial position
        DriverInitialPositions[i] = instanceLocation;
    }

    // Assign assets to components and start simulation
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        if (ClothMeshes[i] && ClothAssets[i])
        {
            ClothMeshes[i]->SetClothAsset(ClothAssets[i]);
            ClothMeshes[i]->StartSimulation();

            // Get the cloth instance handle (batched mode)
            ClothHandles[i] = ClothMeshes[i]->GetClothInstanceHandle();
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Created %d cloth instances across LOD levels"),
           NumClothInstances);
}

void ATestBatchedClothActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Spawn attachment drivers on first tick
    if (!bDriversSpawned)
    {
        bDriversSpawned = true;

        UWorld *world = GetWorld();
        if (world)
        {
            for (int32 i = 0; i < NumClothInstances; ++i)
            {
                AttachmentDrivers[i] = world->SpawnActor<AStaticMeshActor>();

                if (AttachmentDrivers[i])
                {
                    AttachmentDrivers[i]->SetActorLocation(DriverInitialPositions[i]);

                    // Set up mesh
                    UStaticMeshComponent *meshComp = AttachmentDrivers[i]->GetStaticMeshComponent();
                    if (meshComp)
                    {
                        FString MeshName = "Contents/pole/pole.obj";
                        UStaticMesh *StaticMesh = FObjManager::GetStaticMesh(MeshName.ToWideString());
                        meshComp->SetStaticMesh(StaticMesh);
                        meshComp->SetRelativeScale3D(FVector(1.5f, 1.5f, 3.0f));
                    }
                }
            }

            UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Spawned %d attachment drivers"),
                   NumClothInstances);
        }
    }

    // Accumulate animation time
    AnimationTime += DeltaTime;

    // Animate each attachment driver with different phase offsets
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        if (AttachmentDrivers[i])
        {
            float phaseOffset = (float)i / (float)NumClothInstances * 2.0f * PI;
            const float Speed = 0.8f;
            const float MoveRadius = 100.0f;
            const float SwayAngleScale = 30.0f;

            float Time = AnimationTime * Speed + phaseOffset;

            // Different motion patterns based on instance index
            float OffsetY = FMath::Sin(Time) * MoveRadius;
            float OffsetX = FMath::Cos(Time * 2.0f) * (MoveRadius * 0.3f);
            float OffsetZ = FMath::Sin(Time * 1.5f) * (MoveRadius * 0.15f);

            FVector NewLocation = DriverInitialPositions[i] + FVector(OffsetX, OffsetY, OffsetZ);

            float RollAngle = -FMath::Cos(Time) * SwayAngleScale;
            float PitchAngle = FMath::Sin(Time * 1.5f) * (SwayAngleScale * 0.2f);

            FRotator NewRotation = FRotator(PitchAngle, 0.0f, RollAngle);

            AttachmentDrivers[i]->SetActorLocation(NewLocation);
            AttachmentDrivers[i]->SetActorRotation(NewRotation);
        }
    }

    // Update attachments for all instances
    UpdateAllAttachments();
}

void ATestBatchedClothActor::CreateTestCloth(int32 Index, int32 GridSize, EClothLODLevel LOD)
{
    if (Index < 0 || Index >= NumClothInstances)
        return;

    const float Spacing = 5.0f; // 5cm spacing between particles

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
            pos.Z = Spacing * (GridSize / 2.0f - y);

            positions.Add(pos);

            // Top row is fixed (pinned)
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

            indices.Add(i0);
            indices.Add(i1);
            indices.Add(i2);

            indices.Add(i1);
            indices.Add(i3);
            indices.Add(i2);
        }
    }

    // Generate distance constraints
    TArray<FClothDistanceConstraint> constraints;

    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            int32 idx = y * GridSize + x;

            // Horizontal constraint
            if (x < GridSize - 1)
            {
                int32 neighborIdx = y * GridSize + (x + 1);
                float restLength = (positions[neighborIdx] - positions[idx]).Length();
                constraints.Add(FClothDistanceConstraint(idx, neighborIdx, restLength, 1.0f));
            }

            // Vertical constraint
            if (y < GridSize - 1)
            {
                int32 neighborIdx = (y + 1) * GridSize + x;
                float restLength = (positions[neighborIdx] - positions[idx]).Length();
                constraints.Add(FClothDistanceConstraint(idx, neighborIdx, restLength, 1.0f));
            }
        }
    }

    // Shear springs (diagonals)
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
            constraints.Add(FClothDistanceConstraint(i0, i3, restLength03, 1.0f));
            constraints.Add(FClothDistanceConstraint(i1, i2, restLength12, 1.0f));
        }
    }

    // Generate bend constraints (simplified for test)
    TArray<FClothBendConstraint> bendConstraints;
    // TODO: Add bend constraint generation if needed

    // Set up attachments for top row
    TArray<FClothAttachmentData> attachments;
    for (int32 x = 0; x < GridSize; ++x)
    {
        FClothAttachmentData attachment;
        attachment.Type = EClothAttachmentType::ActorTransform;
        attachment.ClothVertexIndex = x; // Top row

        float Offset = (15.0f / float(GridSize - 1)) * x;
        attachment.LocalOffset = FTransform(FVector(0.0f, 0.0f, Offset));
        attachment.Stiffness = 0.98f;
        attachment.bIsKinematic = true;

        attachments.Add(attachment);
    }

    // Store attachments for runtime updates
    CachedAttachments[Index] = attachments;

    // Set cloth asset data
    ClothAssets[Index]->SetRestPositions(positions);
    ClothAssets[Index]->SetIndices(indices);
    ClothAssets[Index]->SetInvMasses(invMasses);

    for (const FClothDistanceConstraint &constraint : constraints)
    {
        ClothAssets[Index]->AddDistanceConstraint(constraint);
    }

    for (const FClothBendConstraint &constraint : bendConstraints)
    {
        ClothAssets[Index]->AddBendConstraint(constraint);
    }

    for (const FClothAttachmentData &data : attachments)
    {
        ClothAssets[Index]->AddAttachmentData(data);
    }

    // Configure simulation parameters with variations per instance
    FClothConfig config;
    config.Mass = 1.0f;
    config.Damping = 0.6f + (Index * 0.05f); // Vary damping slightly
    config.StretchStiffness = 0.9f + (Index * 0.01f);
    config.BendStiffness = 0.2f;
    config.NumIterations = 5;
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;
    config.AirDrag = 0.5f + (Index * 0.1f); // Vary air drag

    ClothAssets[Index]->SetConfig(config);

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Created cloth %d - Grid: %dx%d, LOD: %d, Particles: %d"),
           Index, GridSize, GridSize, static_cast<int32>(LOD), positions.Num());
}

void ATestBatchedClothActor::UpdateAllAttachments()
{
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        UpdateAttachmentsForInstance(i);
    }
}

void ATestBatchedClothActor::UpdateAttachmentsForInstance(int32 Index)
{
    if (Index < 0 || Index >= NumClothInstances)
        return;

    if (!AttachmentDrivers[Index] || !ClothMeshes[Index] || CachedAttachments[Index].Num() == 0)
        return;

    // Get driver transform
    FVector driverPosition = AttachmentDrivers[Index]->GetActorLocation();

    // Update all attachment world positions
    for (int32 i = 0; i < CachedAttachments[Index].Num(); ++i)
    {
        FClothAttachmentData &attachment = CachedAttachments[Index][i];

        if (attachment.Type == EClothAttachmentType::ActorTransform)
        {
            const FTransform AttachmentWorldTransform =
                FTransform(AttachmentDrivers[Index]->GetStaticMeshComponent()->GetWorldMatrix()) *
                attachment.LocalOffset;

            attachment.WorldPosition = AttachmentWorldTransform.GetTranslation();
        }
        else if (attachment.Type == EClothAttachmentType::WorldPosition)
        {
            attachment.WorldPosition = driverPosition;
        }
    }

    // Send updated attachments to cloth component
    // For batched mode, this will update via the instance handle
    FClothInstanceHandle *handle = ClothMeshes[Index]->GetClothInstanceHandle();
    if (handle)
    {
        handle->UpdateKinematicTargets(CachedAttachments[Index]);
    }
    else
    {
        // Fallback to legacy mode
        FClothInstance *instance = ClothMeshes[Index]->GetClothInstance();
        if (instance)
        {
            instance->UpdateAttachments(CachedAttachments[Index]);
        }
    }
}
