/**
 * Test Cloth Attachment Actor Implementation
 * Demonstrates cloth attachment to a moving component
 */

#include "TestClothAttachmentActor.h"
#include "Components/ClothMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/ClothAsset.h"
#include "Classes/Engine/FObjLoader.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

ATestClothAttachmentActor::ATestClothAttachmentActor()
    : PoleComponent(nullptr)
    , ClothComponent(nullptr)
    , AnimationTime(0.0f)
    , bInitialized(false)
{
}

void ATestClothAttachmentActor::PostSpawnInitialize()
{
    if (bInitialized)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestClothAttachmentActor: Already initialized"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Initializing cloth attachment test..."));

    // ===== STEP 1: Create the pole component =====
    PoleComponent = AddComponent<UStaticMeshComponent>(TEXT("PoleComponent"));
    if (!PoleComponent)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to create pole component"));
        return;
    }

    // Load pole mesh
    FString PoleMeshName = "Contents/pole/pole.obj";
    UStaticMesh* PoleMesh = FObjManager::GetStaticMesh(PoleMeshName.ToWideString());
    if (!PoleMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to load pole mesh: %s"), *PoleMeshName);
        return;
    }

    PoleComponent->SetStaticMesh(PoleMesh);
    PoleComponent->SetWorldLocation(GetActorLocation());
    PoleComponent->SetWorldRotation(FRotator(0.0f, 0.0f, 0.0f));
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Pole component created"));

    // ===== STEP 2: Create the cloth component =====
    ClothComponent = AddComponent<UClothMeshComponent>(TEXT("ClothComponent"));
    if (!ClothComponent)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to create cloth component"));
        return;
    }

    // Load cloth source mesh
    FString ClothMeshName = "Contents/TestClothMesh/TestClothMesh.obj";
    UStaticMesh* ClothSourceMesh = FObjManager::GetStaticMesh(ClothMeshName.ToWideString());
    if (!ClothSourceMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to load cloth mesh: %s"), *ClothMeshName);
        return;
    }

    // Configure cloth mesh component
    ClothComponent->SourceStaticMesh = ClothSourceMesh;
    ClothComponent->SimulationMeshReductionRatio = 0.15f;  // Reduce to ~400 vertices
    ClothComponent->bPreserveBoundaryEdges = true;
    ClothComponent->bPreserveUVSeams = true;
    ClothComponent->DecimationMethod = EClothDecimationMethod::Voronoi;
    
    // Physics parameters
    ClothComponent->bGenerateDistanceConstraints = true;
    ClothComponent->bGenerateBendConstraints = true;
    ClothComponent->bGenerateAreaConstraints = true;
    ClothComponent->bGenerateEdgeCollisions = true;
    
    // Simulation parameters
    ClothComponent->StretchStiffness = 0.9f;
    ClothComponent->BendStiffness = 0.1f;
    ClothComponent->AreaStiffness = 0.001f;
    ClothComponent->TotalMass = 1.0f;
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Cloth component created"));

    // ===== STEP 3: Generate cloth asset =====
    ClothComponent->GenerateClothAsset();
    
    if (!ClothComponent->GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to generate cloth asset"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Cloth asset generated"));
    UE_LOG(ELogLevel::Display, TEXT("  Simulation vertices: %d"), ClothComponent->GeneratedClothAsset->RestPositions.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Render vertices: %d"), ClothComponent->GeneratedClothAsset->RenderRestPositions.Num());

    // ===== STEP 4: Register with cloth world =====
    ClothComponent->RegisterWithClothWorld();
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Cloth registered with cloth world"));

    // ===== STEP 5: Attach top vertices of cloth to pole =====
    // Find the top vertices (highest Z values in rest pose)
    UClothAsset* ClothAsset = ClothComponent->GeneratedClothAsset;
    if (!ClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: No cloth asset available for attachment"));
        return;
    }

    // Find vertices to attach (top 5% of vertices by Z coordinate)
    TArray<uint32> TopVertices;
    float MaxY = -FLT_MAX;
    float MinY = FLT_MAX;
    
    // Find Z range
    for (const FVector& Pos : ClothAsset->RestPositions)
    {
        MaxY = FMath::Max(MaxY, Pos.Y);
        MinY = FMath::Min(MinY, Pos.Y);
    }
    
    float YRange = MaxY - MinY;
    float AttachThreshold = MaxY - (YRange * 0.05f);  // Top 5%
    
    // Collect top vertices
    for (int32 i = 0; i < ClothAsset->RestPositions.Num(); ++i)
    {
        if (ClothAsset->RestPositions[i].Y >= AttachThreshold)
        {
            TopVertices.Add(i);
        }
    }
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Found %d vertices to attach (Z >= %.2f)"), 
           TopVertices.Num(), AttachThreshold);

    // Attach each top vertex to the pole component
    for (uint32 VertexIndex : TopVertices)
    {
        // Calculate local offset from pole center
        FVector VertexWorldPos = ClothComponent->GetComponentLocation() + ClothAsset->RestPositions[VertexIndex];
        FVector PoleWorldPos = PoleComponent->GetComponentLocation();
        FVector LocalOffset = VertexWorldPos - PoleWorldPos;
        
        // Create attachment with local offset
        FTransform LocalTransform;
        LocalTransform.SetTranslation(LocalOffset);
        LocalTransform.SetRotation(FQuat::Identity);
        LocalTransform.SetScale3D(FVector::OneVector);
        
        // Bind attachment
        ClothComponent->BindAttachmentToComponent(
            VertexIndex,
            PoleComponent,
            LocalTransform,
            1.0f,   // Stiffness (1.0 = hard constraint)
            0.0f    // AttachDistance (0.0 = kinematic, no stretch)
        );
    }
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Attached %d vertices to pole component"), TopVertices.Num());
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Initialization complete!"));
    UE_LOG(ELogLevel::Display, TEXT("  - Pole will rotate continuously"));
    UE_LOG(ELogLevel::Display, TEXT("  - Cloth should follow the pole motion"));
    
    bInitialized = true;
}

void ATestClothAttachmentActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!bInitialized || !PoleComponent)
        return;
    
    AnimationTime += DeltaTime;
    
    // Rotate the pole continuously
    // This will cause the attached cloth to follow
    float RotationSpeed = 30.0f;  // degrees per second
    float CurrentAngle = AnimationTime * RotationSpeed;
    
    FRotator NewRotation(0.0f, CurrentAngle, 0.0f);  // Rotate around Z axis
    PoleComponent->SetWorldRotation(NewRotation);
    
    // Optional: Also move the pole up and down
    float BobSpeed = 1.0f;  // Hz
    float BobAmount = 1.0f;  // cm
    float BobOffset = FMath::Sin(AnimationTime * BobSpeed * 2.0f * PI) * BobAmount;
    
    FVector BaseLocation = GetActorLocation();
    FVector NewLocation = BaseLocation + FVector(0.0f, 0.0f, BobOffset);
    PoleComponent->SetWorldLocation(NewLocation);
}
