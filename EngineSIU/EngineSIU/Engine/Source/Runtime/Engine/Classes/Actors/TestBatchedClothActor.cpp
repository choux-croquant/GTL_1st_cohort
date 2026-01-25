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
        FString DriverName = FString::Printf(TEXT("AttachmentDriver_%d"), i);
        AttachmentDrivers[i] = nullptr;
        DriverInitialPositions[i] = FVector::ZeroVector;
    }

    // Set root component to first cloth mesh
    RootComponent = ClothMeshes[0];

    AnimationTime = 0.0f;
    bDriversSpawned = false;
    bClothInitialized = false; // CRITICAL: Prevent double initialization
}

void ATestBatchedClothActor::BeginPlay()
{
    Super::BeginPlay();
}

void ATestBatchedClothActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Spawn attachment drivers on first tick
    if (!bDriversSpawned)
    {
        bDriversSpawned = true;
        return;
    }

    // Accumulate animation time
    AnimationTime += DeltaTime;
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
    // Horizontal bend constraints (connect triangles across horizontal edges)
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 2; ++x)
        {
            // Two quads sharing a vertical edge
            int32 pA = y * GridSize + x;       // Left-top
            int32 pB = (y + 1) * GridSize + x; // Left-bottom (shared edge with pA)
            int32 pC = y * GridSize + (x + 1); // Middle-top (opposite in left quad)
            int32 pD = y * GridSize + (x + 2); // Right-top (opposite in right quad)

            // Calculate rest angle (initially flat = PI radians)
            float restAngle = PI;   // Flat cloth
            float stiffness = 1.0f; // Moderate bend resistance

            bendConstraints.Add(FClothBendConstraint(pA, pB, pC, pD, restAngle, stiffness));
        }
    }

    // Vertical bend constraints (connect triangles across vertical edges)
    for (int32 y = 0; y < GridSize - 2; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            // Two quads sharing a horizontal edge
            int32 pA = y * GridSize + x;       // Top-left (shared edge)
            int32 pB = y * GridSize + (x + 1); // Top-right (shared edge)
            int32 pC = (y + 1) * GridSize + x; // Middle-left (opposite in top quad)
            int32 pD = (y + 2) * GridSize + x; // Bottom-left (opposite in bottom quad)

            // Calculate rest angle (initially flat = PI radians)
            float restAngle = PI;   // Flat cloth
            float stiffness = 1.0f; // Moderate bend resistance

            bendConstraints.Add(FClothBendConstraint(pA, pB, pC, pD, restAngle, stiffness));
        }
    }

    // DATA-DRIVEN ATTACHMENT CONFIGURATION
    // Set up attachments for top row by configuring them directly on the cloth asset.
    // The simulation system will automatically resolve world positions each frame
    // based on these driver references - no manual updates needed!
    for (int32 x = 0; x < GridSize; ++x)
    {
        FClothAttachmentData attachment;

        // Specify which cloth vertex this attachment controls
        attachment.ClothVertexIndex = x; // Top row vertex index

        // Attachment type and driver reference
        attachment.Type = EClothAttachmentType::ActorTransform;
        attachment.DriverComponent = AttachmentDrivers[Index]->GetStaticMeshComponent();

        // Local offset from driver transform
        float Offset = (15.0f / float(GridSize - 1)) * x;
        attachment.LocalOffset = FTransform(FVector(0.0f, 0.0f, Offset));

        // Attachment properties
        attachment.Stiffness = 0.98f;
        attachment.bIsKinematic = true;

        // Add to asset - this is the single source of truth for attachments
        ClothAssets[Index]->AddAttachmentData(attachment);
    }

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

void ATestBatchedClothActor::PostSpawnInitialize()
{
    // CRITICAL FIX: Guard against double initialization
    // BeginPlay can be called multiple times in some engine scenarios
    if (bClothInitialized)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestBatchedClothActor: BeginPlay called again, skipping re-initialization"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: BeginPlay starting - Creating %d cloth instances"), NumClothInstances);

    // Create test cloths with different sizes and LOD levels
    // This demonstrates the batching system's ability to handle multiple instances

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

    // LOD 0 (High detail) - Batch Test
    for (int32 i = 0; i < NumClothInstances; i++)
    {
        CreateTestCloth(i, 20 - i * 2, EClothLODLevel::LOD_0);
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

        UE_LOG(ELogLevel::Display, TEXT("  Instance %d positioned at (%f, %f, %f)"),
               i, instanceLocation.X, instanceLocation.Y, instanceLocation.Z);
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

            if (ClothHandles[i])
            {
                UE_LOG(ELogLevel::Display, TEXT("  Instance %d: Handle created, MetadataIndex=%d"),
                       i, ClothHandles[i]->GetMetadataIndex());
            }
            else
            {
                UE_LOG(ELogLevel::Error, TEXT("  Instance %d: FAILED to create handle!"), i);
            }
        }
    }

    bClothInitialized = true; // Mark as initialized

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Created %d cloth instances across LOD levels"),
           NumClothInstances);
}
