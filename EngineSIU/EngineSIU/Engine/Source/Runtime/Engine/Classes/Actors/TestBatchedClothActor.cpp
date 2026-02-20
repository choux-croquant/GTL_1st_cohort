/**
 * Test Batched Cloth Actor Implementation
 * Performance testing with ~200 instances using ClothMeshComponent with asset generation
 */

#include "TestBatchedClothActor.h"
#include "Math/Vector.h"
#include "UObject/ObjectFactory.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/ClothAsset.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "World/World.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "Classes/Engine/FObjLoader.h"

ATestBatchedClothActor::ATestBatchedClothActor()
{
    // Reserve space for cloth meshes
    ClothMeshes.Reserve(NumClothInstances);

    // Initialize shared asset
    SharedStaticMesh = nullptr;
    SharedClothAsset = nullptr;

    AnimationTime = 0.0f;
    bDriversSpawned = false;
    bClothInitialized = false;
}

void ATestBatchedClothActor::BeginPlay()
{
    Super::BeginPlay();
}

void ATestBatchedClothActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AnimationTime += DeltaTime;
}

void ATestBatchedClothActor::CreateSharedClothAsset()
{
    if (SharedClothAsset)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestBatchedClothActor: Shared cloth asset already exists"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Generating shared cloth asset..."));

    // Create a temporary ClothMeshComponent to generate the asset
    UClothMeshComponent* TempClothMesh = AddComponent<UClothMeshComponent>(TEXT("TempClothMesh"));
    
    if (!TempClothMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestBatchedClothActor: Failed to create temporary ClothMeshComponent"));
        return;
    }

    // Load source static mesh
    FString MeshName = "Contents/TestClothMesh/TestClothMesh.obj";
    UStaticMesh* StaticMesh = FObjManager::GetStaticMesh(MeshName.ToWideString());
    
    if (!StaticMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestBatchedClothActor: Failed to load mesh: %s"), *MeshName);
        return;
    }

    SharedStaticMesh = StaticMesh;
    // Set source mesh
    TempClothMesh->SourceStaticMesh = StaticMesh;
    
    // Configure decimation parameters
    // Original mesh has 2601 vertices
    // Target: ~400 particles per instance for 200 instances = 80,000 total
    // Reduction ratio: 400/2601 ≈ 0.15 (keep 15% of vertices)
    TempClothMesh->SimulationMeshReductionRatio = 0.15f;
    TempClothMesh->bPreserveBoundaryEdges = true;
    TempClothMesh->bPreserveUVSeams = true;
    TempClothMesh->DecimationMethod = EClothDecimationMethod::Voronoi;
    
    // Configure physics parameters
    TempClothMesh->bGenerateDistanceConstraints = true;
    TempClothMesh->bGenerateBendConstraints = true;
    TempClothMesh->bGenerateAreaConstraints = true;
    TempClothMesh->bGenerateEdgeCollisions = true;
    
    // Configure simulation parameters
    TempClothMesh->StretchStiffness = 0.9f;
    TempClothMesh->BendStiffness = 0.1f;
    TempClothMesh->AreaStiffness = 0.001f;
    TempClothMesh->TotalMass = 1.0f;
    
    // Generate cloth asset
    TempClothMesh->GenerateClothAsset();
    
    // Store the generated asset for reuse
    SharedClothAsset = TempClothMesh->GeneratedClothAsset;
    
    if (SharedClothAsset)
    {
        UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Shared cloth asset generated successfully"));
        UE_LOG(ELogLevel::Display, TEXT("  Simulation vertices: %d"), SharedClothAsset->RestPositions.Num());
        UE_LOG(ELogLevel::Display, TEXT("  Render vertices: %d"), SharedClothAsset->RenderRestPositions.Num());
    }
    else
    {
        UE_LOG(ELogLevel::Error, TEXT("TestBatchedClothActor: Failed to generate shared cloth asset"));
    }
}

void ATestBatchedClothActor::CreateTestClothMesh(int32 Index, const FVector& Position)
{
    if (Index < 0 || Index >= NumClothInstances)
        return;

    if (!SharedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestBatchedClothActor: Shared cloth asset not available for instance %d"), Index);
        return;
    }

    // Create ClothMeshComponent
    FString ComponentName = FString::Printf(TEXT("ClothMesh_%d"), Index);
    UClothMeshComponent* ClothMesh = AddComponent<UClothMeshComponent>(*ComponentName);
    
    if (!ClothMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestBatchedClothActor: Failed to create ClothMeshComponent %d"), Index);
        return;
    }

    // Set position
    ClothMesh->SetWorldLocation(Position);
    
    // Reuse the shared cloth asset (no need to regenerate)
    ClothMesh->SourceStaticMesh = SharedStaticMesh;
    ClothMesh->GeneratedClothAsset = SharedClothAsset;
    ClothMesh->bAssetGenerated = true;
    
    // Register with cloth world
    ClothMesh->RegisterWithClothWorld();
    
    // Add to array
    ClothMeshes.Add(ClothMesh);
    
    if (Index % 20 == 0)  // Log every 20th instance to reduce spam
    {
        UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Created ClothMesh %d at (%f, %f, %f) using shared asset"),
               Index, Position.X, Position.Y, Position.Z);
    }
}

void ATestBatchedClothActor::PostSpawnInitialize()
{
    // Guard against double initialization
    if (bClothInitialized)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestBatchedClothActor: Already initialized, skipping"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Initializing %d cloth instances for performance testing"), NumClothInstances);
    UE_LOG(ELogLevel::Display, TEXT("  Target: ~400 particles per instance = ~%d total particles"), NumClothInstances * 400);

    // STEP 1: Generate shared cloth asset (only once)
    CreateSharedClothAsset();
    
    if (!SharedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestBatchedClothActor: Failed to create shared cloth asset, aborting"));
        return;
    }

    // STEP 2: Create cloth instances in a grid layout, all reusing the shared asset
    // Grid: 20x10 = 200 instances
    const int32 GridCols = 20;
    const int32 GridRows = 10;
    const float Spacing = 0.0f;  // Space between instances
    
    FVector BasePosition = GetActorLocation();
    
    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Creating %d cloth instances (reusing shared asset)..."), NumClothInstances);
    
    for (int32 i = 0; i < NumClothInstances; ++i)
    {
        int32 row = i / GridCols;
        int32 col = i % GridCols;
        
        FVector offset(row * Spacing, col * Spacing, 0.0f);
        FVector instancePosition = BasePosition + offset;
        
        CreateTestClothMesh(i, instancePosition);
    }

    TestAttachmentIndependence();

    bClothInitialized = true;

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Successfully created %d cloth instances"), ClothMeshes.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Layout: %dx%d grid with %.1f unit spacing"), GridCols, GridRows, Spacing);
    UE_LOG(ELogLevel::Display, TEXT("  Asset generation: 1 shared asset reused by all instances (significant performance improvement)"));
}

// ===== NEW: Attachment System Testing Methods =====

void ATestBatchedClothActor::TestAttachmentIndependence()
{
    if (ClothMeshes.Num() < 3)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestAttachmentIndependence: Need at least 3 cloth instances"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("=== Testing Attachment Independence ==="));
    
    // Get first 3 instances
    UClothMeshComponent* instance1 = ClothMeshes[0];
    UClothMeshComponent* instance2 = ClothMeshes[1];
    UClothMeshComponent* instance3 = ClothMeshes[2];
    
    // Attach different vertices on each instance
    FVector pos1 = instance1->GetComponentLocation() + FVector(0, 0, 0);
    FVector pos2 = instance2->GetComponentLocation() + FVector(10, 0, 0);
    FVector pos3 = instance3->GetComponentLocation() + FVector(0, 10, 5);
    
    instance1->BindAttachmentToWorldPosition(0, pos1);
    instance2->BindAttachmentToWorldPosition(5, pos2);
    instance3->BindAttachmentToWorldPosition(10, pos3);
    
    // Verify independence
    UE_LOG(ELogLevel::Display, TEXT("Instance 1: Vertex 0 attached, InvMass[0]=%.4f (should be 0.0)"),
           instance1->GetRuntimeInvMasses()[0]);
    UE_LOG(ELogLevel::Display, TEXT("Instance 1: Vertex 5 free, InvMass[5]=%.4f (should be >0.0)"),
           instance1->GetRuntimeInvMasses()[5]);
    
    UE_LOG(ELogLevel::Display, TEXT("Instance 2: Vertex 0 free, InvMass[0]=%.4f (should be >0.0)"),
           instance2->GetRuntimeInvMasses()[0]);
    UE_LOG(ELogLevel::Display, TEXT("Instance 2: Vertex 5 attached, InvMass[5]=%.4f (should be 0.0)"),
           instance2->GetRuntimeInvMasses()[5]);
    
    UE_LOG(ELogLevel::Display, TEXT("Instance 3: Vertex 10 attached, InvMass[10]=%.4f (should be 0.0)"),
           instance3->GetRuntimeInvMasses()[10]);
    
    // Verify asset unchanged
    const TArray<float>& baseInvMasses = SharedClothAsset->GetBaseInvMasses();
    UE_LOG(ELogLevel::Display, TEXT("Asset BaseInvMass[0]=%.4f (should be >0.0, unchanged)"),
           baseInvMasses[0]);
    UE_LOG(ELogLevel::Display, TEXT("Asset BaseInvMass[5]=%.4f (should be >0.0, unchanged)"),
           baseInvMasses[5]);
    
    UE_LOG(ELogLevel::Display, TEXT("=== Attachment Independence Test Complete ==="));
}

//void ATestBatchedClothActor::TestRuntimeAttachmentChanges()
//{
//    if (ClothMeshes.Num() < 1)
//    {
//        UE_LOG(ELogLevel::Error, TEXT("TestRuntimeAttachmentChanges: Need at least 1 cloth instance"));
//        return;
//    }
//
//    UE_LOG(ELogLevel::Display, TEXT("=== Testing Runtime Attachment Changes ==="));
//    
//    UClothMeshComponent* cloth = ClothMeshes[0];
//    FVector attachPos = cloth->GetComponentLocation() + FVector(0, 0, 100);
//    
//    // Test 1: Bind attachment
//    UE_LOG(ELogLevel::Display, TEXT("Test 1: Binding attachment to vertex 0"));
//    float originalInvMass = cloth->GetRuntimeInvMasses()[0];
//    UE_LOG(ELogLevel::Display, TEXT("  Original InvMass[0]=%.4f"), originalInvMass);
//    
//    cloth->BindAttachmentToWorldPosition(0, attachPos);
//    UE_LOG(ELogLevel::Display, TEXT("  After bind: InvMass[0]=%.4f (should be 0.0)"),
//           cloth->GetRuntimeInvMasses()[0]);
//    UE_LOG(ELogLevel::Display, TEXT("  IsAttached=%d (should be 1)"),
//           cloth->IsVertexAttached(0) ? 1 : 0);
//    
//    // Test 2: Unbind attachment
//    UE_LOG(ELogLevel::Display, TEXT("Test 2: Unbinding attachment from vertex 0"));
//    cloth->UnbindAttachment(0);
//    UE_LOG(ELogLevel::Display, TEXT("  After unbind: InvMass[0]=%.4f (should be %.4f)"),
//           cloth->GetRuntimeInvMasses()[0], originalInvMass);
//    UE_LOG(ELogLevel::Display, TEXT("  IsAttached=%d (should be 0)"),
//           cloth->IsVertexAttached(0) ? 1 : 0);
//    
//    // Test 3: Multiple bind/unbind cycles
//    UE_LOG(ELogLevel::Display, TEXT("Test 3: Multiple bind/unbind cycles (10x)"));
//    for (int32 i = 0; i < 10; ++i)
//    {
//        cloth->BindAttachmentToWorldPosition(0, attachPos);
//        cloth->UnbindAttachment(0);
//    }
//    UE_LOG(ELogLevel::Display, TEXT("  After 10 cycles: InvMass[0]=%.4f (should be %.4f)"),
//           cloth->GetRuntimeInvMasses()[0], originalInvMass);
//    
//    // Test 4: Update target
//    UE_LOG(ELogLevel::Display, TEXT("Test 4: Updating attachment target"));
//    cloth->BindAttachmentToWorldPosition(0, attachPos);
//    
//    FClothAttachmentTarget newTarget;
//    newTarget.Type = EClothAttachmentType::WorldPosition;
//    newTarget.WorldPosition = attachPos + FVector(50, 50, 50);
//    cloth->UpdateAttachmentTarget(0, newTarget);
//    
//    UE_LOG(ELogLevel::Display, TEXT("  After target update: InvMass[0]=%.4f (should still be 0.0)"),
//           cloth->GetRuntimeInvMasses()[0]);
//    UE_LOG(ELogLevel::Display, TEXT("  IsAttached=%d (should still be 1)"),
//           cloth->IsVertexAttached(0) ? 1 : 0);
//    
//    // Cleanup
//    cloth->ClearAllAttachments();
//    
//    UE_LOG(ELogLevel::Display, TEXT("=== Runtime Attachment Changes Test Complete ==="));
//}

void ATestBatchedClothActor::TestInvMassIsolation()
{
    if (ClothMeshes.Num() < 5)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestInvMassIsolation: Need at least 5 cloth instances"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("=== Testing InvMass Isolation ==="));
    
    // Attach same vertex index on different instances
    for (int32 i = 0; i < 5; ++i)
    {
        UClothMeshComponent* cloth = ClothMeshes[i];
        FVector attachPos = cloth->GetComponentLocation() + FVector(0, 0, 100);
        
        // Attach vertex 0 on all 5 instances
        cloth->BindAttachmentToWorldPosition(0, attachPos);
    }
    
    // Verify all have InvMass[0] = 0
    UE_LOG(ELogLevel::Display, TEXT("After attaching vertex 0 on 5 instances:"));
    for (int32 i = 0; i < 5; ++i)
    {
        float invMass = ClothMeshes[i]->GetRuntimeInvMasses()[0];
        UE_LOG(ELogLevel::Display, TEXT("  Instance %d: InvMass[0]=%.4f"), i, invMass);
    }
    
    // Unbind from instance 2 only
    UE_LOG(ELogLevel::Display, TEXT("Unbinding vertex 0 from instance 2 only..."));
    ClothMeshes[2]->UnbindAttachment(0);
    
    // Verify only instance 2 changed
    UE_LOG(ELogLevel::Display, TEXT("After unbinding instance 2:"));
    for (int32 i = 0; i < 5; ++i)
    {
        float invMass = ClothMeshes[i]->GetRuntimeInvMasses()[0];
        bool isAttached = ClothMeshes[i]->IsVertexAttached(0);
        UE_LOG(ELogLevel::Display, TEXT("  Instance %d: InvMass[0]=%.4f, Attached=%d"),
               i, invMass, isAttached ? 1 : 0);
    }
    
    // Verify asset unchanged
    const TArray<float>& baseInvMasses = SharedClothAsset->GetBaseInvMasses();
    UE_LOG(ELogLevel::Display, TEXT("Asset BaseInvMass[0]=%.4f (should be >0.0, unchanged)"),
           baseInvMasses[0]);
    
    // Cleanup
    for (int32 i = 0; i < 5; ++i)
    {
        ClothMeshes[i]->ClearAllAttachments();
    }
    
    UE_LOG(ELogLevel::Display, TEXT("=== InvMass Isolation Test Complete ==="));
}

void ATestBatchedClothActor::TestAttachmentPatterns()
{
    if (ClothMeshes.Num() < 10)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestAttachmentPatterns: Need at least 10 cloth instances"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("=== Testing Various Attachment Patterns ==="));
    
    // Pattern 1: Single corner attachment (instances 0-2)
    UE_LOG(ELogLevel::Display, TEXT("Pattern 1: Single corner attachment (3 instances)"));
    for (int32 i = 0; i < 3; ++i)
    {
        UClothMeshComponent* cloth = ClothMeshes[i];
        FVector attachPos = cloth->GetComponentLocation() + FVector(0, 0, 100);
        cloth->BindAttachmentToWorldPosition(0, attachPos);
        UE_LOG(ELogLevel::Display, TEXT("  Instance %d: Attached vertex 0, count=%d"),
               i, cloth->GetAttachmentCount());
    }
    
    // Pattern 2: Two corner attachment (instances 3-5)
    UE_LOG(ELogLevel::Display, TEXT("Pattern 2: Two corner attachment (3 instances)"));
    for (int32 i = 3; i < 6; ++i)
    {
        UClothMeshComponent* cloth = ClothMeshes[i];
        FVector pos1 = cloth->GetComponentLocation() + FVector(0, 0, 100);
        FVector pos2 = cloth->GetComponentLocation() + FVector(50, 0, 100);
        
        cloth->BindAttachmentToWorldPosition(0, pos1);
        cloth->BindAttachmentToWorldPosition(10, pos2);
        UE_LOG(ELogLevel::Display, TEXT("  Instance %d: Attached vertices 0,10, count=%d"),
               i, cloth->GetAttachmentCount());
    }
    
    // Pattern 3: Four corner attachment (instances 6-8)
    UE_LOG(ELogLevel::Display, TEXT("Pattern 3: Four corner attachment (3 instances)"));
    for (int32 i = 6; i < 9; ++i)
    {
        UClothMeshComponent* cloth = ClothMeshes[i];
        FVector base = cloth->GetComponentLocation();
        
        cloth->BindAttachmentToWorldPosition(0, base + FVector(0, 0, 100));
        cloth->BindAttachmentToWorldPosition(5, base + FVector(50, 0, 100));
        cloth->BindAttachmentToWorldPosition(10, base + FVector(0, 50, 100));
        cloth->BindAttachmentToWorldPosition(15, base + FVector(50, 50, 100));
        UE_LOG(ELogLevel::Display, TEXT("  Instance %d: Attached vertices 0,5,10,15, count=%d"),
               i, cloth->GetAttachmentCount());
    }
    
    // Pattern 4: Soft attachment with LRA (instance 9)
    UE_LOG(ELogLevel::Display, TEXT("Pattern 4: Soft attachment with LRA (1 instance)"));
    UClothMeshComponent* cloth9 = ClothMeshes[9];
    FVector softPos = cloth9->GetComponentLocation() + FVector(0, 0, 100);
    cloth9->BindAttachmentToWorldPosition(
        0,          // Vertex
        softPos,    // Target
        0.8f,       // Stiffness (slightly soft)
        10.0f       // Max distance (10cm slack)
    );
    UE_LOG(ELogLevel::Display, TEXT("  Instance 9: Soft attachment (stiffness=0.8, distance=10.0)"));
    
    // Verify all patterns are independent
    UE_LOG(ELogLevel::Display, TEXT("Verifying pattern independence:"));
    UE_LOG(ELogLevel::Display, TEXT("  Instance 0: %d attachments"), ClothMeshes[0]->GetAttachmentCount());
    UE_LOG(ELogLevel::Display, TEXT("  Instance 3: %d attachments"), ClothMeshes[3]->GetAttachmentCount());
    UE_LOG(ELogLevel::Display, TEXT("  Instance 6: %d attachments"), ClothMeshes[6]->GetAttachmentCount());
    UE_LOG(ELogLevel::Display, TEXT("  Instance 9: %d attachments"), ClothMeshes[9]->GetAttachmentCount());
    
    // Verify asset unchanged
    const TArray<float>& baseInvMasses = SharedClothAsset->GetBaseInvMasses();
    bool assetUnchanged = true;
    for (int32 i = 0; i < FMath::Min(20, baseInvMasses.Num()); ++i)
    {
        if (baseInvMasses[i] <= 0.0f)
        {
            assetUnchanged = false;
            break;
        }
    }
    UE_LOG(ELogLevel::Display, TEXT("Asset BaseInvMasses unchanged: %s"),
           assetUnchanged ? TEXT("YES") : TEXT("NO"));
    
    UE_LOG(ELogLevel::Display, TEXT("=== Attachment Patterns Test Complete ==="));
    UE_LOG(ELogLevel::Display, TEXT("NOTE: Attachments remain active - call ClearAllAttachments() to reset"));
}

void ATestBatchedClothActor::TestRuntimeAttachmentChanges()
{
    if (ClothMeshes.Num() < 1)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestRuntimeAttachmentChanges: Need at least 1 cloth instance"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("=== Testing Runtime Attachment Changes ==="));
    
    UClothMeshComponent* cloth = ClothMeshes[0];
    FVector basePos = cloth->GetComponentLocation();
    
    // Test enable/disable without unbinding
    UE_LOG(ELogLevel::Display, TEXT("Test: Enable/Disable attachment"));
    cloth->BindAttachmentToWorldPosition(0, basePos + FVector(0, 0, 100));
    UE_LOG(ELogLevel::Display, TEXT("  Bound: InvMass[0]=%.4f"), cloth->GetRuntimeInvMasses()[0]);
    
    cloth->SetAttachmentEnabled(0, false);
    UE_LOG(ELogLevel::Display, TEXT("  Disabled: InvMass[0]=%.4f (should be >0.0)"),
           cloth->GetRuntimeInvMasses()[0]);
    
    cloth->SetAttachmentEnabled(0, true);
    UE_LOG(ELogLevel::Display, TEXT("  Re-enabled: InvMass[0]=%.4f (should be 0.0)"),
           cloth->GetRuntimeInvMasses()[0]);
    
    // Test clear all
    UE_LOG(ELogLevel::Display, TEXT("Test: Clear all attachments"));
    cloth->BindAttachmentToWorldPosition(5, basePos + FVector(50, 0, 100));
    cloth->BindAttachmentToWorldPosition(10, basePos + FVector(0, 50, 100));
    
    int32 countBefore = cloth->GetAttachmentCount();
    UE_LOG(ELogLevel::Display, TEXT("  Before clear: %d attachments"), countBefore);
    
    cloth->ClearAllAttachments();
    UE_LOG(ELogLevel::Display, TEXT("  After clear: %d attachments"), cloth->GetAttachmentCount());
    UE_LOG(ELogLevel::Display, TEXT("  InvMass[0]=%.4f (should be restored)"),
           cloth->GetRuntimeInvMasses()[0]);
    
    UE_LOG(ELogLevel::Display, TEXT("=== Runtime Attachment Changes Test Complete ==="));
}
