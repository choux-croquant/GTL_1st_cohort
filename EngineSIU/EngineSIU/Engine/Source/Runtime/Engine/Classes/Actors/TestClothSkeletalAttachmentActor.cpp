/**
 * Test Cloth Skeletal Attachment Actor Implementation
 * Demonstrates cloth attached to animated skeletal mesh bones
 */

#include "TestClothSkeletalAttachmentActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/ClothAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/AssetManager.h"
#include "Classes/Engine/FObjLoader.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "UObject/Casts.h"
#include "Userinterface/Console.h"

ATestClothSkeletalAttachmentActor::ATestClothSkeletalAttachmentActor()
    : CharacterMesh(nullptr)
    , CapeCloth(nullptr)
    , AnimationTime(0.0f)
    , InitialCharacterLocation(FVector::ZeroVector)
    , bInitialized(false)
{
}

void ATestClothSkeletalAttachmentActor::PostSpawnInitialize()
{
    if (bInitialized)
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestClothSkeletalAttachmentActor: Already initialized"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Initializing skeletal cloth attachment test..."));

    // ===== STEP 1: Create the skeletal mesh component (character) =====
    CharacterMesh = AddComponent<USkeletalMeshComponent>(TEXT("CharacterMesh"));
    CharacterMesh->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));

    if (!CharacterMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to create character mesh component"));
        return;
    }

    // Load skeletal mesh asset (same as CharacterClothTest)
    CharacterMesh->SetSkeletalMeshAsset(UAssetManager::Get().GetSkeletalMesh(FName("Contents/GameJamEnemy/GameJamEnemy")));
    
    if (CharacterMesh->GetSkeletalMeshAsset())
    {
        UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Loaded skeletal mesh asset"));
        
        // Set up animation (same as CharacterClothTest)
        CharacterMesh->StateMachineFileName = TEXT("LuaScripts/Animations/EnemyStateMachine.lua");
        CharacterMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        CharacterMesh->SetAnimClass(UClass::FindClass(FName("ULuaScriptAnimInstance")));
        
        // Load physics asset
        UObject* AssetObject = UAssetManager::Get().GetAsset(EAssetType::PhysicsAsset, FName("Contents/PhysicsAsset/UPhysicsAsset_212"));
        if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(AssetObject))
        {
            CharacterMesh->GetSkeletalMeshAsset()->SetPhysicsAsset(PhysicsAsset);
            UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Physics asset loaded"));
        }
        
        // Enable simulation
        CharacterMesh->bSimulate = true;
    }
    else
    {
        UE_LOG(ELogLevel::Warning, TEXT("TestClothSkeletalAttachmentActor: No skeletal mesh asset loaded"));
        UE_LOG(ELogLevel::Warning, TEXT("  Test will continue but bone attachments may not work"));
    }

    CharacterMesh->SetWorldLocation(GetActorLocation());
    CharacterMesh->SetWorldRotation(FRotator(0.0f, 0.0f, 0.0f));
    
    // Store initial location for animation
    InitialCharacterLocation = CharacterMesh->GetComponentLocation();
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Character mesh component created"));

    // ===== STEP 2: Create the cloth component (cape) =====
    CapeCloth = AddComponent<UClothMeshComponent>(TEXT("CapeCloth"));
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

    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Cloth asset generated"));
    UE_LOG(ELogLevel::Display, TEXT("  Simulation vertices: %d"), CapeCloth->GeneratedClothAsset->RestPositions.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Render vertices: %d"), CapeCloth->GeneratedClothAsset->RenderRestPositions.Num());

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
        int32 BoneIndex = CharacterMesh->GetBoneIndex(BoneName);
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
                CharacterMesh,
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

void ATestClothSkeletalAttachmentActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!bInitialized || !CharacterMesh)
        return;
    
    AnimationTime += DeltaTime;
}
