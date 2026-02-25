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

    // ===== STEP 5: Attach top vertices of cape to character bones =====
    UClothAsset* CapeAsset = CapeCloth->GeneratedClothAsset;
    if (!CapeAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: No cloth asset available for attachment"));
        return NewActor;
    }

    // Find vertices to attach (top vertices by Z coordinate)
    TArray<uint32> TopVertices;
    float MaxZ = -FLT_MAX;
    float MinZ = FLT_MAX;

    // Find Z range
    for (const FVector& Pos : CapeAsset->RestPositions)
    {
        MaxZ = FMath::Max(MaxZ, Pos.Z);
        MinZ = FMath::Min(MinZ, Pos.Z);
    }

    float ZRange = MaxZ - MinZ;
    float AttachThreshold = MaxZ - (ZRange * 0.10f);  // Top 10% by Z

    // Collect top vertices
    for (int32 i = 0; i < CapeAsset->RestPositions.Num(); ++i)
    {
        if (CapeAsset->RestPositions[i].Z >= AttachThreshold)
        {
            TopVertices.Add(i);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Found %d vertices to attach (Z >= %.2f)"),
        TopVertices.Num(), AttachThreshold);

    if (TopVertices.Num() == 0)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestClothSkeletalAttachmentActor: No vertices found for attachment"));
        bInitialized = true;
        return NewActor;
    }

    // Try to find Neck bone
    FName NeckBoneName = FName(TEXT("mixamorig:Neck"));
    int32 NeckBoneIndex = SkeletalMeshComponent->GetBoneIndex(NeckBoneName);

    if (NeckBoneIndex == INDEX_NONE)
    {
        UE_LOG(ELogLevel::Warning,
            TEXT("TestClothSkeletalAttachmentActor: Neck bone not found, skipping attachment"));
    }
    else
    {
        UE_LOG(ELogLevel::Display,
            TEXT("TestClothSkeletalAttachmentActor: Using BONE ATTACHMENT mode (Neck bone)"));

        // Get the neck bone transform to calculate proper local offsets
        FTransform NeckBoneTransform = SkeletalMeshComponent->GetBoneTransform(NeckBoneIndex);
        FMatrix NeckBoneMatrix = NeckBoneTransform.ToMatrixWithScale();
        FMatrix InverseNeckBoneMatrix = FMatrix::Inverse(NeckBoneMatrix);

        // Attach all top vertices to the Neck bone with local offsets that preserve rest shape
        for (uint32 VertexIndex : TopVertices)
        {
            // Get the vertex rest position in world space (assuming cloth is at origin initially)
            FVector VertexRestPos = CapeAsset->RestPositions[VertexIndex];
            
            // Transform vertex position to bone local space
            // This preserves the original shape by calculating the offset from the bone
            FVector LocalOffset = InverseNeckBoneMatrix.TransformPosition(VertexRestPos);

            FQuat LocalRotation = NeckBoneTransform.GetRotation().Inverse();

            FTransform LocalTransform;
            LocalTransform.SetTranslation(LocalOffset);
            //LocalTransform.SetTranslation(FVector::OneVector);
            //LocalTransform.SetRotation(FQuat::Identity);
            LocalTransform.SetRotation(LocalRotation);
            LocalTransform.SetScale3D(FVector::OneVector);

            CapeCloth->BindAttachmentToBone(
                VertexIndex,
                SkeletalMeshComponent,
                NeckBoneName,
                LocalTransform,
                1.0f,   // Stiffness (1.0 = hard constraint)
                0.0f    // AttachDistance (0.0 = kinematic, no stretch)
            );
        }

        UE_LOG(ELogLevel::Display,
            TEXT("TestClothSkeletalAttachmentActor: Successfully attached %d vertices to Neck bone with shape-preserving offsets"),
            TopVertices.Num());
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Total attachments: %d"),
        CapeCloth->GetAttachmentCount());

    bInitialized = true;
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Initialization complete!"));

    return NewActor;
}
