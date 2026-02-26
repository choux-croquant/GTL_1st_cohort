#include "CharacterClothTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SocketComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCustomNotify.h"
#include "Animation/AnimSoundNotify.h"
#include "Engine/Contents/AnimInstance/LuaScriptAnimInstance.h"
#include "Actors/Player.h"
#include "UObject/UObjectIterator.h"
#include "Engine/SkeletalMesh.h"
#include "Userinterface/Console.h"
#include "Engine/Engine.h"
#include "Physics/PhysicsManager.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Classes/Engine/FObjLoader.h"
#include "Engine/ClothAsset.h"

ACharacterClothTest::ACharacterClothTest()
    : bInitialized(false)
{
}
void ACharacterClothTest::PostSpawnInitialize()
{
    Super::PostSpawnInitialize();

    // SetActorTickInEditor(true);
    USkeletalMeshComponent* SkeletalMeshComponent = AddComponent<USkeletalMeshComponent>("SkeletalMeshComponent");
    SkeletalMeshComponent->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));

    SkeletalMeshComponent->SetSkeletalMeshAsset(UAssetManager::Get().GetSkeletalMesh(FName("Contents/GameJamEnemy/GameJamEnemy")));
    SkeletalMeshComponent->StateMachineFileName = TEXT("LuaScripts/Animations/EnemyStateMachine.lua");
    SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    SkeletalMeshComponent->SetAnimClass(UClass::FindClass(FName("ULuaScriptAnimInstance")));

    UObject* AssetObject = UAssetManager::Get().GetAsset(EAssetType::PhysicsAsset, FName("Contents/PhysicsAsset/UPhysicsAsset_212"));

    if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(AssetObject))
    {
        SkeletalMeshComponent->GetSkeletalMeshAsset()->SetPhysicsAsset(PhysicsAsset);
    }

    SkeletalMeshComponent->bSimulate = true;
    // first = "Contents/Run/Armature|mixamo.com.001"
    UAnimSequence* IdleAnim = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Enemy_Idle/Armature|Enemy_Idle")));
    //UAnimSequence* RunAnim = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Human/SlowRun")));
    UAnimSequence* ReactionAnim = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Enemy_Impact/Armature|Enemy_Impact")));

    UAnimSequence* Horizontal1 = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Horizontal1/Armature|Horizontal1")));
    UAnimSequence* Horizontal2 = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Horizontal2/Armature|Horizontal2")));
    UAnimSequence* Vertical1 = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Vertical1/Armature|Vertical1")));

    // ===== STEP 2: Create the cloth component (cape) =====
    UClothMeshComponent* CapeCloth = AddComponent<UClothMeshComponent>(TEXT("CapeCloth"));
    if (!CapeCloth)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to create cape cloth component"));
        return;
    }

    // Load cloth source mesh
    FString ClothMeshName = "Contents/Cape2/Cape2.obj";
    UStaticMesh* ClothSourceMesh = FObjManager::GetStaticMesh(ClothMeshName.ToWideString());
    if (!ClothSourceMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to load cloth mesh: %s"), *ClothMeshName);
        return;
    }

    // Configure cloth mesh component
    CapeCloth->SourceStaticMesh = ClothSourceMesh;
    CapeCloth->SimulationMeshReductionRatio = 0.50f;  // Reduce to ~400 vertices
    CapeCloth->bPreserveBoundaryEdges = true;
    CapeCloth->bPreserveUVSeams = true;
    CapeCloth->DecimationMethod = EClothDecimationMethod::Voronoi;

    // Physics parameters
    CapeCloth->bGenerateDistanceConstraints = true;
    CapeCloth->bGenerateBendConstraints = true;
    CapeCloth->bGenerateAreaConstraints = true;
    CapeCloth->bGenerateEdgeCollisions = true;

    // Simulation parameters - lighter and more flexible for cape
    CapeCloth->StretchStiffness = 0.85f;
    CapeCloth->BendStiffness = 0.05f;
    CapeCloth->AreaStiffness = 0.001f;
    CapeCloth->TotalMass = 0.5f;  // Light cape

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Cape cloth component created"));

    // ===== STEP 3: Generate cloth asset =====
    CapeCloth->GenerateClothAsset();

    if (!CapeCloth->GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to generate cloth asset"));
        return;
    }

    // ===== STEP 4: Register with cloth world =====
    CapeCloth->RegisterWithClothWorld();

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Cape registered with cloth world"));

    // ===== STEP 5: Attach top vertices of cape to character bones =====
    UClothAsset* CapeAsset = CapeCloth->GeneratedClothAsset;
    if (!CapeAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: No cloth asset available for attachment"));
        return;
    }

    // Find vertices to attach (top 10% of vertices by Y coordinate)
    TArray<uint32> TopVertices;
    float MaxY = -FLT_MAX;
    float MinY = FLT_MAX;

    // Find Y range
    for (const FVector& Pos : CapeAsset->RestPositions)
    {
        MaxY = FMath::Max(MaxY, Pos.Y);
        MinY = FMath::Min(MinY, Pos.Y);
    }

    float YRange = MaxY - MinY;
    float AttachThreshold = MaxY - (YRange * 0.10f);  // Top 10%

    // Collect top vertices
    for (int32 i = 0; i < CapeAsset->RestPositions.Num(); ++i)
    {
        if (CapeAsset->RestPositions[i].Y >= AttachThreshold)
        {
            TopVertices.Add(i);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Found %d vertices to attach (Y >= %.2f)"),
        TopVertices.Num(), AttachThreshold);

    if (TopVertices.Num() == 0)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestClothSkeletalAttachmentActor: No vertices found for attachment"));
        bInitialized = true;
        return;
    }

    // Try to find valid bones in the skeletal mesh
    TArray<FName> BoneNamesToTry;
    BoneNamesToTry.Add(FName(TEXT("mixamorig:Neck")));

    FName ValidBoneName = NAME_None;
    int32 ValidBoneIndex = INDEX_NONE;

    // Try to find a valid bone
    for (FName BoneName : BoneNamesToTry)
    {
        int32 BoneIndex = SkeletalMeshComponent->GetBoneIndex(BoneName);
        if (BoneIndex != INDEX_NONE)
        {
            ValidBoneName = BoneName;
            ValidBoneIndex = BoneIndex;
            UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Found valid bone '%s' (index %d)"),
                *BoneName.ToString(), BoneIndex);
            break;
        }
    }

    // Attach vertices
    if (ValidBoneName != NAME_None)
    {
        // BONE ATTACHMENT MODE: Attach to skeletal mesh bone
        UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Using BONE ATTACHMENT mode"));

        for (uint32 VertexIndex : TopVertices)
        {
            // Create attachment with local offset
            FTransform LocalTransform;
            LocalTransform.SetTranslation(FVector(0.0f, 0.0f, -10.0f));
            LocalTransform.SetRotation(FQuat::Identity);
            LocalTransform.SetScale3D(FVector::OneVector);

            // Bind attachment to bone
            CapeCloth->BindAttachmentToBone(
                VertexIndex,
                SkeletalMeshComponent,
                ValidBoneName,
                LocalTransform,
                1.0f,   // Stiffness (1.0 = hard constraint)
                0.0f    // AttachDistance (0.0 = kinematic, no stretch)
            );
        }

        UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Successfully attached %d vertices to bone '%s'"),
            TopVertices.Num(), *ValidBoneName.ToString());
    }
    else
    {
        UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Failed to attach, Bone not defined (fallback)"),
            TopVertices.Num());
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Total attachments: %d"),
        CapeCloth->GetAttachmentCount());

    bInitialized = true;
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Initialization complete!"));
}

void ACharacterClothTest::BeginPlay()
{
    Super::BeginPlay();
    GetComponentByClass<USkeletalMeshComponent>()->bSimulate = false;
    InitialTransform = RootComponent->GetComponentTransform();
}

void ACharacterClothTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bInitialized)
        return;
}

UObject* ACharacterClothTest::Duplicate(UObject* InOuter)
{
    ACharacterClothTest* NewActor = Cast<ACharacterClothTest>(Super::Duplicate(InOuter));

    USkeletalMeshComponent* SkeletalMeshComponent = NewActor->GetComponentByClass<USkeletalMeshComponent>();
    SkeletalMeshComponent->SetRelativeLocation(FVector(-1.5f, 1.0f, -6.5f));
    SkeletalMeshComponent->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));

    UClothMeshComponent* CapeCloth = NewActor->AddComponent<UClothMeshComponent>(TEXT("CapeCloth"));
    if (!CapeCloth)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to create cape cloth component"));
        return NewActor;
    }

    // Load cloth source mesh
    FString ClothMeshName = "Contents/Cape2/Cape2.obj";
    UStaticMesh* ClothSourceMesh = FObjManager::GetStaticMesh(ClothMeshName.ToWideString());
    if (!ClothSourceMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to load cloth mesh: %s"), *ClothMeshName);
        return NewActor;
    }

    // Configure cloth mesh component
    CapeCloth->SourceStaticMesh = ClothSourceMesh;
    CapeCloth->SimulationMeshReductionRatio = 0.80f;  // Reduce to ~400 vertices
    CapeCloth->bPreserveBoundaryEdges = true;
    CapeCloth->bPreserveUVSeams = true;
    CapeCloth->DecimationMethod = EClothDecimationMethod::Voronoi;

    // Physics parameters
    CapeCloth->bGenerateDistanceConstraints = true;
    CapeCloth->bGenerateBendConstraints = true;
    CapeCloth->bGenerateAreaConstraints = true;
    CapeCloth->bGenerateEdgeCollisions = true;

    // Simulation parameters - lighter and more flexible for cape
    CapeCloth->StretchStiffness = 0.85f;
    CapeCloth->BendStiffness = 0.05f;
    CapeCloth->AreaStiffness = 0.001f;
    CapeCloth->TotalMass = 0.5f;  // Light cape

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Cape cloth component created"));

    // ===== STEP 3: Generate cloth asset =====
    CapeCloth->GenerateClothAsset();

    if (!CapeCloth->GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to generate cloth asset"));
        return NewActor;
    }

    // ===== STEP 4: Register with cloth world =====
    CapeCloth->RegisterWithClothWorld();

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Cape registered with cloth world"));

    // ===== STEP 5: Apply attachment data from asset (NEW: Data-Driven) =====
    // Replace hardcoded Z-threshold and bone assignment with data from AttachmentPaintData
    ApplyAttachmentPaintData(CapeCloth, SkeletalMeshComponent);
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Total attachments: %d"),
        CapeCloth->GetAttachmentCount());

    bInitialized = true;
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Initialization complete!"));

    return NewActor;
}

// ===== NEW: Data-Driven Attachment System =====

void ACharacterClothTest::ApplyAttachmentPaintData(
    UClothMeshComponent* ClothComp,
    USkeletalMeshComponent* SkelMeshComp
)
{
    if (!ClothComp || !SkelMeshComp)
    {
        UE_LOG(ELogLevel::Error, TEXT("ApplyAttachmentPaintData: Null component passed"));
        return;
    }

    UClothAsset* ClothAsset = ClothComp->GetClothAsset();
    if (!ClothAsset)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ApplyAttachmentPaintData: No cloth asset found"));
        return;
    }

    if (ClothAsset->AttachmentPaintData.Num() == 0)
    {
        UE_LOG(ELogLevel::Display, TEXT("ApplyAttachmentPaintData: No attachment paint data found (cloth will be free)"));
        return;
    }

    int32 SuccessCount = 0;
    int32 SkipCount = 0;

    UE_LOG(ELogLevel::Display, TEXT("ApplyAttachmentPaintData: Processing %d attachment entries"),
        ClothAsset->AttachmentPaintData.Num());

    for (const FClothAttachmentPaintData& PaintData : ClothAsset->AttachmentPaintData)
    {
        // Skip inactive or unassigned attachments
        if (!PaintData.bIsActive || !PaintData.bHasBoneAssignment)
        {
            SkipCount++;
            continue;
        }

        // Validate vertex index
        if (PaintData.SimVertexIndex >= (uint32)ClothAsset->RestPositions.Num())
        {
            UE_LOG(ELogLevel::Error, TEXT("ApplyAttachmentPaintData: Invalid vertex index %d (max %d)"),
                PaintData.SimVertexIndex, ClothAsset->RestPositions.Num());
            SkipCount++;
            continue;
        }

        // Validate bone exists
        int32 BoneIndex = SkelMeshComp->GetBoneIndex(PaintData.BoneName);
        if (BoneIndex == INDEX_NONE)
        {
            UE_LOG(ELogLevel::Warning, TEXT("ApplyAttachmentPaintData: Bone '%s' not found, skipping vertex %d"),
                *PaintData.BoneName.ToString(), PaintData.SimVertexIndex);
            SkipCount++;
            continue;
        }

        // Compute local offset from rest position and bone transform
        // This preserves the cloth shape regardless of initial transforms
        FVector VertexRestPos = ClothAsset->RestPositions[PaintData.SimVertexIndex];
        FTransform BoneTransform = SkelMeshComp->GetBoneTransform(BoneIndex);
        
        // Transform vertex from cloth local space to bone local space
        FVector LocalOffset = BoneTransform.InverseTransformPosition(VertexRestPos);

        // Create local transform
        FTransform LocalTransform;
        LocalTransform.SetTranslation(LocalOffset);
        LocalTransform.SetRotation(BoneTransform.GetRotation().Inverse());
        LocalTransform.SetScale3D(FVector::OneVector);

        // Bind attachment using paint data parameters
        ClothComp->BindAttachmentToBone(
            PaintData.SimVertexIndex,
            SkelMeshComp,
            PaintData.BoneName,
            LocalTransform,
            PaintData.Stiffness,
            PaintData.AttachDistance
        );

        SuccessCount++;

        // Log first few attachments for debugging
        if (SuccessCount <= 3)
        {
            UE_LOG(ELogLevel::Display, TEXT("ApplyAttachmentPaintData: Vertex %d → Bone '%s' (Weight=%.2f, Stiffness=%.2f)"),
                PaintData.SimVertexIndex, *PaintData.BoneName.ToString(),
                PaintData.KinematicWeight, PaintData.Stiffness);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("ApplyAttachmentPaintData: Applied %d attachments (%d skipped)"),
        SuccessCount, SkipCount);
}
