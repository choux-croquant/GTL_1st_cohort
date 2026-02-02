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

    // for (int32 i = 0; i < NumClothInstances; ++i)
    //{
    //      if (AttachmentDrivers[i])
    //      {
    //          float phaseOffset = (float)i / (float)NumClothInstances * 2.0f * PI;
    //          const float Speed = 1.2f;
    //          const float MoveRadius = 200.0f;
    //          const float SwayAngleScale = 30.0f;

    //        float Time = AnimationTime * Speed + phaseOffset;

    //        // Different motion patterns based on instance index
    //        float OffsetY = FMath::Sin(Time) * MoveRadius;
    //        float OffsetX = FMath::Cos(Time * 2.0f) * (MoveRadius * 0.3f);
    //        float OffsetZ = FMath::Sin(Time * 1.5f) * (MoveRadius * 0.15f);

    //        FVector NewLocation = DriverInitialPositions[i] + FVector(OffsetX, OffsetY, OffsetZ);

    //        float RollAngle = -FMath::Cos(Time) * SwayAngleScale;
    //        float PitchAngle = FMath::Sin(Time * 1.5f) * (SwayAngleScale * 0.2f);

    //        FRotator NewRotation = FRotator(PitchAngle, 0.0f, RollAngle);

    //        AttachmentDrivers[i]->SetActorLocation(NewLocation);
    //        AttachmentDrivers[i]->SetActorRotation(NewRotation);
    //    }
    //}

    // Simple move
    //for (int32 i = 0; i < NumClothInstances; ++i)
    //{
    //    if (AttachmentDrivers[i])
    //    {
    //        // === MOTION PARAMETERS ===
    //        const float Frequency = 0.5f;        // Hz (0.5 = 2초 주기)
    //        const float Amplitude = 100.0f;      // 왕복 거리 (cm)
    //        const float Speed = 0.5f * PI * Frequency;

    //        // Phase offset per instance (optional, for multiple cloths)
    //        float phaseOffset = (float)i / (float)NumClothInstances * PI;

    //        // === SIMPLE SINUSOIDAL MOTION ===
    //        // Y-axis only: -Amplitude → 0 → +Amplitude → 0 → -Amplitude
    //        float Time = AnimationTime * Speed + phaseOffset;
    //        float OffsetY = FMath::Sin(Time) * Amplitude;

    //        // Position: oscillate along Y from initial position
    //        FVector NewLocation = DriverInitialPositions[i] + FVector(0.0f, OffsetY, 0.0f);

    //        // Rotation: Fixed 90° yaw (facing right), no roll/pitch
    //        FRotator NewRotation = FRotator(-89.9f, 0.0f, 0.0f);

    //        // Apply
    //        AttachmentDrivers[i]->SetActorLocation(NewLocation);
    //        AttachmentDrivers[i]->SetActorRotation(NewRotation);
    //    }
    //}
    // Accumulate animation time
    AnimationTime += DeltaTime;
}

void ATestBatchedClothActor::CreateTestCloth(int32 Index, int32 GridSize, int32 Spacing, EClothLODLevel LOD)
{
    if (Index < 0 || Index >= NumClothInstances)
        return;

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

    // Generate bend constraints - PURE XPBD WITH CPU-GPU CONSISTENCY
    // CRITICAL: Must match GPU shader convention EXACTLY
    //
    // CONVENTION (matches ClothBendConstraintSolver.hlsl):
    // - A and B form the shared edge
    // - Triangle 1: (A, B, C) with normal n1 = cross(B-A, C-A)
    // - Triangle 2: (A, B, D) with normal n2 = cross(B-A, D-A)
    // - RestAngle computed using same signed dihedral angle as GPU

    TArray<FClothBendConstraint> bendConstraints;

    // Helper lambda to compute signed dihedral angle (matches GPU)
    auto ComputeDihedralAngle = [](const FVector &pA, const FVector &pB,
                                   const FVector &pC, const FVector &pD) -> float
    {
        // Shared edge
        FVector e = pB - pA;
        float eLen = e.Length();
        if (eLen < 1e-6f)
            return 0.0f;
        FVector eNorm = e / eLen;

        // Triangle normals (same as GPU)
        FVector n1 = FVector::CrossProduct(pB - pA, pC - pA);
        FVector n2 = FVector::CrossProduct(pB - pA, pD - pA);

        float n1Len = n1.Length();
        float n2Len = n2.Length();
        if (n1Len < 1e-6f || n2Len < 1e-6f)
            return 0.0f;

        FVector n1Norm = n1 / n1Len;
        FVector n2Norm = n2 / n2Len;

        // Compute angle
        float cosAngle = FMath::Clamp(FVector::DotProduct(n1Norm, n2Norm), -1.0f, 1.0f);
        float phi = FMath::Acos(cosAngle);

        // Apply sign (same as GPU)
        FVector crossNormals = FVector::CrossProduct(n1Norm, n2Norm);
        float angleSign = FVector::DotProduct(crossNormals, eNorm);
        if (angleSign < 0.0f)
            phi = -phi;

        return phi;
    };

    // Generate bend constraints for grid edges
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);

            // --- HORIZONTAL EDGE: i0-i1 ---
            // Shared edge: i0 (A) to i1 (B)
            // Triangle 1: (i0, i1, i2) → C = i2
            // Triangle 2: (i0, i1, i3) → D = i3
            if (x < GridSize - 1)
            {
                FVector pA = positions[i0];
                FVector pB = positions[i1];
                FVector pC = positions[i2]; // Below the edge
                FVector pD = positions[i3]; // Diagonal opposite

                // Compute rest angle from current (flat) configuration
                float restAngle = ComputeDihedralAngle(pA, pB, pC, pD);

                // Pure XPBD: control via compliance only
                // Lower compliance = stiffer (0.0 = rigid)
                // Higher compliance = softer
                float bendCompliance = 1e-4;  // Moderate flexibility
                float unusedStiffness = 1.0f; // Kept for data compatibility, not used in solve

                bendConstraints.Add(FClothBendConstraint(i0, i1, i2, i3, restAngle, unusedStiffness, bendCompliance));
            }

            // --- VERTICAL EDGE: i0-i2 ---
            // Shared edge: i0 (A) to i2 (B)
            // Triangle 1: (i0, i2, i1) → C = i1
            // Triangle 2: (i0, i2, i3) → D = i3
            if (y < GridSize - 1)
            {
                FVector pA = positions[i0];
                FVector pB = positions[i2]; // Note: i2 is now B (vertical edge)
                FVector pC = positions[i1]; // Right of edge
                FVector pD = positions[i3]; // Diagonal opposite

                // Compute rest angle
                float restAngle = ComputeDihedralAngle(pA, pB, pC, pD);

                float bendCompliance = 1e-4;
                float unusedStiffness = 1.0f;

                bendConstraints.Add(FClothBendConstraint(i0, i2, i1, i3, restAngle, unusedStiffness, bendCompliance));
            }
        }
    }

    // NEW: Generate area constraints (one per triangle)
    // Area constraints preserve triangle area to resist in-plane stretching/compression
    // This complements distance constraints by preventing triangle collapse or excessive expansion
    TArray<FClothAreaConstraint> areaConstraints;

    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);

            // Triangle 1: (i0, i1, i2)
            {
                FVector e1 = positions[i1] - positions[i0];
                FVector e2 = positions[i2] - positions[i0];
                FVector crossProd = FVector::CrossProduct(e1, e2);
                float restArea = 0.5f * crossProd.Length();
                FVector restNormal = crossProd;
                restNormal.Normalize();

                // XPBD compliance: lower = stiffer area preservation
                // 1e-5 is quite stiff, preventing significant area change
                float compliance = 1e-5f;

                areaConstraints.Add(FClothAreaConstraint(i0, i1, i2, restArea, restNormal, compliance));
            }

            // Triangle 2: (i1, i3, i2)
            {
                FVector e1 = positions[i3] - positions[i1];
                FVector e2 = positions[i2] - positions[i1];
                FVector crossProd = FVector::CrossProduct(e1, e2);
                float restArea = 0.5f * crossProd.Length();
                FVector restNormal = crossProd;
                restNormal.Normalize();

                float compliance = 1e-5f;

                areaConstraints.Add(FClothAreaConstraint(i1, i3, i2, restArea, restNormal, compliance));
            }
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Generated %d area constraints for cloth %d"),
           areaConstraints.Num(), Index);

    // NEW: Generate edge collision constraints
    // Extract unique edges from triangles to prevent edge penetration in low-resolution meshes
    TArray<FClothEdgeCollisionConstraint> edgeCollisions;
    
    // Use a map to track unique edges (order-independent: {A,B} == {B,A})
    // Map key is (minIdx << 16 | maxIdx) to create unique identifier
    TMap<uint64, bool> uniqueEdges;
    
    // Extract edges from all triangles
    for (int32 triIdx = 0; triIdx < indices.Num(); triIdx += 3)
    {
        uint32 i0 = indices[triIdx + 0];
        uint32 i1 = indices[triIdx + 1];
        uint32 i2 = indices[triIdx + 2];
        
        // Lambda to add edge ensuring A < B for uniqueness
        auto AddEdge = [&uniqueEdges](uint32 a, uint32 b)
        {
            uint32 minIdx = FMath::Min(a, b);
            uint32 maxIdx = FMath::Max(a, b);
            uint64 key = (static_cast<uint64>(minIdx) << 32) | static_cast<uint64>(maxIdx);
            uniqueEdges.Add(key, true);
        };
        
        AddEdge(i0, i1);
        AddEdge(i1, i2);
        AddEdge(i2, i0);
    }
    
    // Build edge collision constraints from unique edges
    for (const auto& edgePair : uniqueEdges)
    {
        uint64 key = edgePair.Key;
        uint32 idxA = static_cast<uint32>(key >> 32);
        uint32 idxB = static_cast<uint32>(key & 0xFFFFFFFF);
        
        // Calculate rest length
        float restLength = (positions[idxB] - positions[idxA]).Length();
        
        edgeCollisions.Add(FClothEdgeCollisionConstraint(idxA, idxB, restLength));
    }
    
    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Generated %d edge collision constraints for cloth %d"),
           edgeCollisions.Num(), Index);

    // DATA-DRIVEN ATTACHMENT CONFIGURATION
    // Set up attachments for top row by configuring them directly on the cloth asset.
    // The simulation system will automatically resolve world positions each frame
    // based on these driver references - no manual updates needed!

    // OPTION 1: Hard Kinematic Attachments (Top Row Only)
    // These are the primary attachment points that the cloth is pinned to
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

        // Hard kinematic attachment (no distance tolerance)
        attachment.Stiffness = 1.0f;
        attachment.bIsKinematic = true;
        attachment.AttachDistance = 0.0f; // NEW: 0 = hard kinematic (no LRA)

        // Add to asset - this is the single source of truth for attachments
        ClothAssets[Index]->AddAttachmentData(attachment);
    }

    // OPTION 2: Long Range Attachments (LRA) - OPTIONAL
    // Create LRA constraints for all free particles to prevent global stretching
    // Uncomment to enable full LRA behavior (Velvet pattern)
    /*
    FVector attachmentCenter = FVector::ZeroVector;

    // Calculate center of attachment points (average of top row)
    for (int32 x = 0; x < GridSize; ++x)
    {
        attachmentCenter += positions[x];  // Top row positions
    }
    attachmentCenter /= static_cast<float>(GridSize);

    // Create LRA for all non-pinned particles
    for (int32 y = 1; y < GridSize; ++y)  // Skip top row (already pinned)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            int32 particleIdx = y * GridSize + x;
            FVector particlePos = positions[particleIdx];

            // Compute rest distance from this particle to attachment center
            float restDistance = FVector::Distance(particlePos, attachmentCenter);

            FClothAttachmentData lraAttachment;
            lraAttachment.ClothVertexIndex = particleIdx;
            lraAttachment.Type = EClothAttachmentType::ActorTransform;
            lraAttachment.DriverComponent = AttachmentDrivers[Index]->GetStaticMeshComponent();
            lraAttachment.LocalOffset = FTransform(FVector::ZeroVector);  // Use center
            lraAttachment.Stiffness = 1.0f;
            lraAttachment.bIsKinematic = false;  // Not kinematic, just distance limited
            lraAttachment.AttachDistance = restDistance;  // LRA: max distance from attachment

            ClothAssets[Index]->AddAttachmentData(lraAttachment);
        }
    }
    */

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

    // NEW: Add area constraints to cloth asset
    for (const FClothAreaConstraint &constraint : areaConstraints)
    {
        ClothAssets[Index]->AddAreaConstraint(constraint);
    }

    // NEW: Add edge collision constraints to cloth asset
    for (const FClothEdgeCollisionConstraint &edgeCollision : edgeCollisions)
    {
        ClothAssets[Index]->AddEdgeCollision(edgeCollision);
    }

    // Configure simulation parameters with variations per instance
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
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        int32 row = i / 4;
        int32 col = i % 4;

        FVector offset(row * 150.0f, col * 150.0f, 0.0f);
        FVector instanceLocation = GetActorLocation() + offset;

        // Calculate and store driver positions BEFORE spawning
        DriverInitialPositions[i] = instanceLocation;

        UE_LOG(ELogLevel::Display, TEXT("  Instance %d will be positioned at (%f, %f, %f)"),
               i, instanceLocation.X, instanceLocation.Y, instanceLocation.Z);
    }

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
                    //FString MeshName = "Contents/TestClothMesh/TestClothMesh.obj";
                    
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
        // CreateTestCloth(i, 20 - i * 2, 5.0f, EClothLODLevel::LOD_0);
        CreateTestCloth(i, 10, 5.0f, EClothLODLevel::LOD_0);
        //CreateTestCloth(i, 20, 2.5f, EClothLODLevel::LOD_0);
        // CreateTestCloth(i, 100, 0.5f, EClothLODLevel::LOD_0);
    }

    // Position instances in a grid layout
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        int32 row = i / 10;
        int32 col = i % 10;

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
