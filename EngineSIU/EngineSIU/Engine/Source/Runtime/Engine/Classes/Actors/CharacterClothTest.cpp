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
    SkeletalMeshComponent->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.3f));

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

    UAnimSequence* IdleAnim = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Enemy_Idle/Armature|Enemy_Idle")));
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
    FString ClothMeshName = "Contents/TestClothMesh/TestClothMesh.obj";
    UStaticMesh* ClothSourceMesh = FObjManager::GetStaticMesh(ClothMeshName.ToWideString());
    if (!ClothSourceMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to load cloth mesh: %s"), *ClothMeshName);
        return;
    }

    // Configure cloth mesh component
    CapeCloth->SourceStaticMesh = ClothSourceMesh;
    CapeCloth->SimulationMeshReductionRatio = 0.20f;  // Reduce to ~400 vertices
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
    UClothMeshComponent* CapeCloth = NewActor->AddComponent<UClothMeshComponent>(TEXT("CapeCloth"));
    if (!CapeCloth)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to create cape cloth component"));
        return NewActor;
    }

    // Load cloth source mesh
    FString ClothMeshName = "Contents/TestClothMesh/TestClothMesh.obj";
    UStaticMesh* ClothSourceMesh = FObjManager::GetStaticMesh(ClothMeshName.ToWideString());
    if (!ClothSourceMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to load cloth mesh: %s"), *ClothMeshName);
        return NewActor;
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
        return NewActor;
    }

    TopVertices.Sort([&CapeAsset](const uint32& A, const uint32& B) {
        const FVector& PosA = CapeAsset->RestPositions[A];
        const FVector& PosB = CapeAsset->RestPositions[B];
        return PosA.X < PosB.X;  // Ascending order by X
        });

    //// Try to find valid bones in the skeletal mesh
    //TArray<FName> BoneNamesToTry;
    //BoneNamesToTry.Add(FName(TEXT("mixamorig:Neck")));

    //FName ValidBoneName = NAME_None;
    //int32 ValidBoneIndex = INDEX_NONE;

    //// Try to find a valid bone
    //for (FName BoneName : BoneNamesToTry)
    //{
    //    int32 BoneIndex = SkeletalMeshComponent->GetBoneIndex(BoneName);
    //    if (BoneIndex != INDEX_NONE)
    //    {
    //        ValidBoneName = BoneName;
    //        ValidBoneIndex = BoneIndex;
    //        UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Found valid bone '%s' (index %d)"),
    //            *BoneName.ToString(), BoneIndex);
    //        break;
    //    }
    //}

    //// Attach vertices
    //if (ValidBoneName != NAME_None)
    //{
    //    // BONE ATTACHMENT MODE: Attach to skeletal mesh bone
    //    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Using BONE ATTACHMENT mode"));

    //    float i = 0.0f;
    //    float n = float(TopVertices.Num());
    //    for (uint32 VertexIndex : TopVertices)
    //    {
    //        // Create attachment with local offset
    //        FTransform LocalTransform;
    //        FVector LocalOffset = FVector(-50.0f + (i / n) * 100.0f, 0.0f, -25.0f);
    //        i++;
    //        LocalTransform.SetTranslation(LocalOffset);
    //        LocalTransform.SetRotation(FQuat::Identity);
    //        LocalTransform.SetScale3D(FVector::OneVector);

    //        // Bind attachment to bone
    //        CapeCloth->BindAttachmentToBone(
    //            VertexIndex,
    //            SkeletalMeshComponent,
    //            ValidBoneName,
    //            LocalTransform,
    //            1.0f,   // Stiffness (1.0 = hard constraint)
    //            0.0f    // AttachDistance (0.0 = kinematic, no stretch)
    //        );
    //    }

    //    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Successfully attached %d vertices to bone '%s'"),
    //        TopVertices.Num(), *ValidBoneName.ToString());
    //}
    //else
    //{
    //    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Failed to attach, Bone not defined (fallback)"),
    //        TopVertices.Num());
    //}

    // Try to find shoulder bones
    int32 LeftShoulderIndex = SkeletalMeshComponent->GetBoneIndex(FName(TEXT("mixamorig:LeftArm")));
    int32 RightShoulderIndex = SkeletalMeshComponent->GetBoneIndex(FName(TEXT("mixamorig:RightArm")));

    FName LeftShoulderName = (LeftShoulderIndex != INDEX_NONE) ? FName(TEXT("mixamorig:LeftArm")) : NAME_None;
    FName RightShoulderName = (RightShoulderIndex != INDEX_NONE) ? FName(TEXT("mixamorig:RightArm")) : NAME_None;

    if (LeftShoulderName == NAME_None || RightShoulderName == NAME_None || TopVertices.Num() == 0)
    {
        UE_LOG(ELogLevel::Warning,
            TEXT("TestClothSkeletalAttachmentActor: Shoulder bones not found or no TopVertices, skipping attachment"));
    }
    else
    {
        UE_LOG(ELogLevel::Display,
            TEXT("TestClothSkeletalAttachmentActor: Using BONE ATTACHMENT mode (Shoulders, 2 anchor points)"));

        // Index 0 -> LeftShoulder
        {
            uint32 VertexIndex = TopVertices[0];

            FVector LocalOffset(
                -5.0f,  // left side
                0.0f,   // a bit behind
                -5.0f   // slightly below
            );

            FTransform LocalTransform;
            LocalTransform.SetTranslation(LocalOffset);
            LocalTransform.SetRotation(FQuat::Identity);
            LocalTransform.SetScale3D(FVector::OneVector);

            CapeCloth->BindAttachmentToBone(
                VertexIndex,
                SkeletalMeshComponent,
                LeftShoulderName,
                LocalTransform,
                1.0f,
                0.0f
            );
        }

        // Last index -> RightShoulder (if there is more than one vertex)
        if (TopVertices.Num() > 1)
        {
            uint32 VertexIndex = TopVertices.Last();

            FVector LocalOffset(
                5.0f,   // right side
                0.0f,   // a bit behind
                -5.0f   // slightly below
            );

            FTransform LocalTransform;
            LocalTransform.SetTranslation(LocalOffset);
            LocalTransform.SetRotation(FQuat::Identity);
            LocalTransform.SetScale3D(FVector::OneVector);

            CapeCloth->BindAttachmentToBone(
                VertexIndex,
                SkeletalMeshComponent,
                RightShoulderName,
                LocalTransform,
                1.0f,
                0.0f
            );
        }

        UE_LOG(ELogLevel::Display,
            TEXT("TestClothSkeletalAttachmentActor: Attached TopVertices[0] to LeftShoulder and TopVertices[last] to RightShoulder"));
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Total attachments: %d"),
        CapeCloth->GetAttachmentCount());

    bInitialized = true;
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Initialization complete!"));

    return NewActor;
}
