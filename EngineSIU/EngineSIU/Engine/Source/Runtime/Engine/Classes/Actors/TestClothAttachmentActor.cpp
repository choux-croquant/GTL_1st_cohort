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
    , PoleComponent2(nullptr)
    , ClothComponent2(nullptr)
    , SphereComponent(nullptr)
    , ClothComponent3(nullptr)
    , AnimationTime(0.0f)
    , InitialPoleLocation(FVector::ZeroVector)
    , InitialPole2Location(FVector::ZeroVector)
    , InitialSphereLocation(FVector::ZeroVector)
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
    ClothComponent->SimulationMeshReductionRatio = 0.20f;  // Reduce to ~400 vertices
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

    // Find vertices to attach (top 5% of vertices by Y coordinate)
    TArray<uint32> TopVertices;
    float MaxY = -FLT_MAX;
    float MinY = FLT_MAX;

    // Find Y range
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

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Found %d vertices to attach (Y >= %.2f)"),
        TopVertices.Num(), AttachThreshold);

    // Sort vertices by X coordinate (ascending order)
    // Use descending order by reversing the comparison: return PosB.X < PosA.X;
    TopVertices.Sort([&ClothAsset](const uint32& A, const uint32& B) {
        const FVector& PosA = ClothAsset->RestPositions[A];
        const FVector& PosB = ClothAsset->RestPositions[B];
        return PosA.X < PosB.X;  // Ascending order by X
        });

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Sorted %d vertices by X coordinate (ascending)"),
        TopVertices.Num());

    // Attach each top vertex to the pole component
    float i = 0.0f;
    float n = float(TopVertices.Num());
    for (uint32 VertexIndex : TopVertices)
    {
        // Calculate local offset along Z axis based on sorted order
        FVector LocalOffset = FVector(0.0f, 0.0f, -10.0f + (i / n) * 25.0f);
        i += 1.0f;

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

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Successfully attached %d vertices to pole component"),
        TopVertices.Num());

    // Store initial pole location for oscillation
    InitialPoleLocation = PoleComponent->GetComponentLocation();

    // ===== STEP 6: Create second pole component =====
    PoleComponent2 = AddComponent<UStaticMeshComponent>(TEXT("PoleComponent2"));
    if (!PoleComponent2)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to create second pole component"));
        return;
    }

    // Load pole mesh for second pole (reuse same mesh)
    PoleComponent2->SetStaticMesh(PoleMesh);
    
    // Position second pole offset from the first pole
    FVector Pole2Location = GetActorLocation() + FVector(0.0f, 100.0f, 0.0f);  // 100 units on Y axis
    PoleComponent2->SetWorldLocation(Pole2Location);
    PoleComponent2->SetWorldRotation(FRotator(0.0f, 90.0f, 0.0f));  // Rotate 90 degrees on Y axis
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Second pole component created"));

    // ===== STEP 7: Create second cloth component =====
    ClothComponent2 = AddComponent<UClothMeshComponent>(TEXT("ClothComponent2"));
    if (!ClothComponent2)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to create second cloth component"));
        return;
    }

    // Load second cloth source mesh
    FString ClothMeshName2 = "Contents/TestClothMesh2/TestClothMesh2.obj";
    UStaticMesh* ClothSourceMesh2 = FObjManager::GetStaticMesh(ClothMeshName2.ToWideString());
    if (!ClothSourceMesh2)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to load second cloth mesh: %s"), *ClothMeshName2);
        return;
    }

    // Configure second cloth mesh component
    ClothComponent2->SourceStaticMesh = ClothSourceMesh2;
    ClothComponent2->SimulationMeshReductionRatio = 0.20f;
    ClothComponent2->bPreserveBoundaryEdges = true;
    ClothComponent2->bPreserveUVSeams = true;
    ClothComponent2->DecimationMethod = EClothDecimationMethod::Voronoi;
    
    // Physics parameters
    ClothComponent2->bGenerateDistanceConstraints = true;
    ClothComponent2->bGenerateBendConstraints = true;
    ClothComponent2->bGenerateAreaConstraints = true;
    ClothComponent2->bGenerateEdgeCollisions = true;
    
    // Simulation parameters
    ClothComponent2->StretchStiffness = 0.9f;
    ClothComponent2->BendStiffness = 0.1f;
    ClothComponent2->AreaStiffness = 0.001f;
    ClothComponent2->TotalMass = 1.0f;
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Second cloth component created"));

    // Generate cloth asset for second cloth
    ClothComponent2->GenerateClothAsset();
    
    if (!ClothComponent2->GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to generate second cloth asset"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Second cloth asset generated"));
    UE_LOG(ELogLevel::Display, TEXT("  Simulation vertices: %d"), ClothComponent2->GeneratedClothAsset->RestPositions.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Render vertices: %d"), ClothComponent2->GeneratedClothAsset->RenderRestPositions.Num());

    // Register second cloth with cloth world
    ClothComponent2->RegisterWithClothWorld();
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Second cloth registered with cloth world"));

    // ===== STEP 8: Attach second cloth to second pole =====
    UClothAsset* ClothAsset2 = ClothComponent2->GeneratedClothAsset;
    if (!ClothAsset2)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: No second cloth asset available for attachment"));
        return;
    }

    // Find vertices to attach for second cloth (top 5% of vertices by Y coordinate)
    TArray<uint32> TopVertices2;
    float MaxY2 = -FLT_MAX;
    float MinY2 = FLT_MAX;

    // Find Y range
    for (const FVector& Pos : ClothAsset2->RestPositions)
    {
        MaxY2 = FMath::Max(MaxY2, Pos.Y);
        MinY2 = FMath::Min(MinY2, Pos.Y);
    }

    float YRange2 = MaxY2 - MinY2;
    float AttachThreshold2 = MaxY2 - (YRange2 * 0.05f);  // Top 5%

    // Collect top vertices
    for (int32 i = 0; i < ClothAsset2->RestPositions.Num(); ++i)
    {
        if (ClothAsset2->RestPositions[i].Y >= AttachThreshold2)
        {
            TopVertices2.Add(i);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Found %d vertices to attach for second cloth (Y >= %.2f)"),
        TopVertices2.Num(), AttachThreshold2);

    // Sort vertices by X coordinate (ascending order)
    TopVertices2.Sort([&ClothAsset2](const uint32& A, const uint32& B) {
        const FVector& PosA = ClothAsset2->RestPositions[A];
        const FVector& PosB = ClothAsset2->RestPositions[B];
        return PosA.X < PosB.X;
    });

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Sorted %d vertices by X coordinate for second cloth"),
        TopVertices2.Num());

    // Attach each top vertex to the SECOND pole component
    float i2 = 0.0f;
    float n2 = float(TopVertices2.Num());
    for (uint32 VertexIndex : TopVertices2)
    {
        // Calculate local offset along Z axis based on sorted order
        FVector LocalOffset = FVector(0.0f, 0.0f, -10.0f + (i2 / n2) * 25.0f);
        i2 += 1.0f;

        // Create attachment with local offset
        FTransform LocalTransform;
        LocalTransform.SetTranslation(LocalOffset);
        LocalTransform.SetRotation(FQuat::Identity);
        LocalTransform.SetScale3D(FVector::OneVector);

        // Bind attachment to SECOND pole
        ClothComponent2->BindAttachmentToComponent(
            VertexIndex,
            PoleComponent2,  // Attach to second pole
            LocalTransform,
            1.0f,   // Stiffness (1.0 = hard constraint)
            0.0f    // AttachDistance (0.0 = kinematic, no stretch)
        );
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Successfully attached %d vertices of second cloth to second pole"),
        TopVertices2.Num());

    // Store initial second pole location for oscillation
    InitialPole2Location = PoleComponent2->GetComponentLocation();

    // ===== STEP 9: Create sphere component =====
    SphereComponent = AddComponent<UStaticMeshComponent>(TEXT("SphereComponent"));
    if (!SphereComponent)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to create sphere component"));
        return;
    }

    // Load sphere mesh
    FString SphereMeshName = "Contents/sphereblender/sphereblender.obj";
    UStaticMesh* SphereMesh = FObjManager::GetStaticMesh(SphereMeshName.ToWideString());
    if (!SphereMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to load sphere mesh: %s"), *SphereMeshName);
        return;
    }

    SphereComponent->SetStaticMesh(SphereMesh);
    
    // Position sphere offset from other components
    FVector SphereLocation = GetActorLocation() + FVector(0.0f, -100.0f, 0.0f);  // -100 units on Y axis
    SphereComponent->SetWorldLocation(SphereLocation);
    SphereComponent->SetWorldRotation(FRotator(0.0f, 0.0f, 0.0f));
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Sphere component created"));

    // ===== STEP 10: Create third cloth component =====
    ClothComponent3 = AddComponent<UClothMeshComponent>(TEXT("ClothComponent3"));
    if (!ClothComponent3)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to create third cloth component"));
        return;
    }

    // Reuse first cloth mesh for third cloth
    ClothComponent3->SourceStaticMesh = ClothSourceMesh;
    ClothComponent3->SimulationMeshReductionRatio = 0.20f;
    ClothComponent3->bPreserveBoundaryEdges = true;
    ClothComponent3->bPreserveUVSeams = true;
    ClothComponent3->DecimationMethod = EClothDecimationMethod::Voronoi;
    
    // Physics parameters
    ClothComponent3->bGenerateDistanceConstraints = true;
    ClothComponent3->bGenerateBendConstraints = true;
    ClothComponent3->bGenerateAreaConstraints = true;
    ClothComponent3->bGenerateEdgeCollisions = true;
    
    // Simulation parameters
    ClothComponent3->StretchStiffness = 0.9f;
    ClothComponent3->BendStiffness = 0.1f;
    ClothComponent3->AreaStiffness = 0.001f;
    ClothComponent3->TotalMass = 1.0f;
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Third cloth component created"));

    // Generate cloth asset for third cloth
    ClothComponent3->GenerateClothAsset();
    
    if (!ClothComponent3->GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: Failed to generate third cloth asset"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Third cloth asset generated"));
    UE_LOG(ELogLevel::Display, TEXT("  Simulation vertices: %d"), ClothComponent3->GeneratedClothAsset->RestPositions.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Render vertices: %d"), ClothComponent3->GeneratedClothAsset->RenderRestPositions.Num());

    // Register third cloth with cloth world
    ClothComponent3->RegisterWithClothWorld();
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Third cloth registered with cloth world"));

    // ===== STEP 11: Attach single vertex of third cloth to sphere =====
    UClothAsset* ClothAsset3 = ClothComponent3->GeneratedClothAsset;
    if (!ClothAsset3)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothAttachmentActor: No third cloth asset available for attachment"));
        return;
    }

    // Find the topmost vertex (highest Y value)
    uint32 TopVertexIndex = 0;
    float MaxY3 = -FLT_MAX;

    for (int32 i = 0; i < ClothAsset3->RestPositions.Num(); ++i)
    {
        if (ClothAsset3->RestPositions[i].Y > MaxY3)
        {
            MaxY3 = ClothAsset3->RestPositions[i].Y;
            TopVertexIndex = i;
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Found topmost vertex at index %d (Y = %.2f)"),
        TopVertexIndex, MaxY3);

    // Attach the single vertex to the sphere
    FTransform LocalTransform;
    LocalTransform.SetTranslation(FVector::ZeroVector);  // Attach at sphere center
    LocalTransform.SetRotation(FQuat::Identity);
    LocalTransform.SetScale3D(FVector::OneVector);

    ClothComponent3->BindAttachmentToComponent(
        TopVertexIndex,
        SphereComponent,
        LocalTransform,
        1.0f,   // Stiffness (1.0 = hard constraint)
        0.0f    // AttachDistance (0.0 = kinematic, no stretch)
    );

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Successfully attached single vertex to sphere"));

    // Store initial sphere location for circular motion
    InitialSphereLocation = SphereComponent->GetComponentLocation();

    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Attached %d vertices to pole component"), TopVertices.Num());
    UE_LOG(ELogLevel::Display, TEXT("TestClothAttachmentActor: Initialization complete!"));
    UE_LOG(ELogLevel::Display, TEXT("  - First pole: Complex circular motion"));
    UE_LOG(ELogLevel::Display, TEXT("  - Second pole: Back-and-forth on Y axis"));
    UE_LOG(ELogLevel::Display, TEXT("  - Sphere: Circular motion with single-vertex cloth"));
    
    bInitialized = true;
}

void ATestClothAttachmentActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!bInitialized || !PoleComponent || !PoleComponent2 || !SphereComponent)
        return;
    
    AnimationTime += DeltaTime;
    
    // ===== First pole: Complex circular motion =====
    const float Speed = 2.5f;
    const float MoveRadius = 40.0f;
    const float SwayAngleScale = 30.0f;

    float Time = AnimationTime * Speed;

    // Calculate oscillating offsets
    float OffsetY = FMath::Sin(Time) * MoveRadius;
    float OffsetX = FMath::Cos(Time * 2.0f) * (MoveRadius * 0.3f);
    float OffsetZ = FMath::Sin(Time * 1.5f) * (MoveRadius * 0.15f);

    // Apply offsets relative to the initial location (not current location)
    FVector NewLocation = InitialPoleLocation + FVector(OffsetX, OffsetY, OffsetZ);

    float RollAngle = -FMath::Cos(Time) * SwayAngleScale;
    float PitchAngle = FMath::Sin(Time * 1.5f) * (SwayAngleScale * 0.2f);

    FRotator NewRotation = FRotator(PitchAngle, 0.0f, RollAngle);

    PoleComponent->SetWorldLocation(NewLocation);
    PoleComponent->SetWorldRotation(NewRotation);

    // ===== Second pole: Simple back-and-forth motion on Y axis =====
    const float Speed2 = 1.0f;
    const float MoveRange2 = 50.0f;  // Move 50 units back and forth

    float Time2 = AnimationTime * Speed2;

    // Simple sine wave motion on Y axis only
    float OffsetX2 = FMath::Sin(Time2) * MoveRange2;

    // Apply offset relative to initial location
    FVector NewLocation2 = InitialPole2Location + FVector(OffsetX2, 0.0f, 0.0f);

    // Keep rotation fixed at 90 degrees
    FRotator NewRotation2 = FRotator(90.0f, 90.0f, 0.0f);

    PoleComponent2->SetWorldLocation(NewLocation2);
    PoleComponent2->SetWorldRotation(NewRotation2);

    // ===== Sphere: Circular motion in XY plane =====
    const float Speed3 = 1.5f;
    const float CircleRadius = 30.0f;  // Radius of circular motion

    float Time3 = AnimationTime * Speed3;

    // Circular motion in XY plane
    float CircleX = FMath::Cos(Time3) * CircleRadius;
    float CircleY = FMath::Sin(Time3) * CircleRadius;

    // Apply circular offset relative to initial location
    FVector NewLocation3 = InitialSphereLocation + FVector(CircleX, CircleY, 0.0f);

    SphereComponent->SetWorldLocation(NewLocation3);
}
