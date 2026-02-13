/**
 * Test Batched Cloth Actor Implementation
 * Performance testing with ~200 instances using ClothMeshComponent with asset generation
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
    // Reserve space for cloth meshes
    ClothMeshes.Reserve(NumClothInstances);

    // Initialize shared asset
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
    const float Spacing = 150.0f;  // Space between instances
    
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

    bClothInitialized = true;

    UE_LOG(ELogLevel::Display, TEXT("TestBatchedClothActor: Successfully created %d cloth instances"), ClothMeshes.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Layout: %dx%d grid with %.1f unit spacing"), GridCols, GridRows, Spacing);
    UE_LOG(ELogLevel::Display, TEXT("  Asset generation: 1 shared asset reused by all instances (significant performance improvement)"));
}
