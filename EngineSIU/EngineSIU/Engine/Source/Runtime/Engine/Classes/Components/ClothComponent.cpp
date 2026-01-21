/**
 * Cloth Component Implementation (Refactored)
 * Uses centralized ClothWorld manager instead of per-component solver
 * Now supports both Legacy and Batched modes
 */

#include "ClothComponent.h"
#include "Engine/ClothAsset.h"
#include "Cloth/ClothInstance.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "World/World.h"
#include "Engine/EditorEngine.h"
#include "Cloth/ClothPhysicsManager.h"

UClothComponent::UClothComponent()
    : ClothAsset(nullptr), ClothInstance(nullptr), ClothInstanceHandle(nullptr), bIsSimulating(false), bUseBatchedMode(false), bDebugDrawEnabled(false), AccumulatedForce(FVector::ZeroVector)
{
}

UClothComponent::~UClothComponent()
{
    // Unregister from ClothWorld
    FClothWorld *ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(GetWorld());
    if (!ClothWorld)
        return;

    if (bUseBatchedMode && ClothInstanceHandle)
    {
        ClothWorld->UnregisterClothInstanceBatched(ClothInstanceHandle);
        ClothInstanceHandle = nullptr;
    }
    else if (ClothInstance)
    {
        ClothWorld->UnregisterClothInstance(ClothInstance);
        ClothInstance = nullptr;
    }
}

void UClothComponent::InitializeComponent()
{
    Super::InitializeComponent();

    // Registration now happens in StartSimulation() to properly detect mode
    // This allows the mode to be set before any instances are created
}

void UClothComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // Note: We NO LONGER call solver here!
    // ClothWorld::Update() handles all simulation

    if (!bIsSimulating)
        return;

    // Update per-instance kinematic data based on mode
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode
        // Apply accumulated forces (TODO: implement force system for batched mode)
        if (AccumulatedForce.SizeSquared() > 0.0f)
        {
            // TODO: Add force support to batched system
            AccumulatedForce = FVector::ZeroVector;
        }

        // Update attachments
        if (Attachments.Num() > 0)
        {
            ClothInstanceHandle->UpdateKinematicTargets(Attachments);
        }
    }
    else if (ClothInstance)
    {
        // Legacy mode
        if (AccumulatedForce.SizeSquared() > 0.0f)
        {
            ClothInstance->AddExternalForce(AccumulatedForce);
            AccumulatedForce = FVector::ZeroVector;
        }

        if (Attachments.Num() > 0)
        {
            ClothInstance->UpdateAttachments(Attachments);
        }
    }
}

void UClothComponent::BeginPlay()
{
    Super::BeginPlay();

    // Auto-start simulation
    if (ClothAsset)
    {
        StartSimulation();
    }
}

void UClothComponent::SetClothAsset(UClothAsset *InAsset)
{
    if (ClothAsset == InAsset)
        return;

    ClothAsset = InAsset;

    // Unregister old instance
    FClothWorld *ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(GetWorld());
    if (ClothWorld)
    {
        if (bUseBatchedMode && ClothInstanceHandle)
        {
            ClothWorld->UnregisterClothInstanceBatched(ClothInstanceHandle);
            ClothInstanceHandle = nullptr;
        }
        else if (ClothInstance)
        {
            ClothWorld->UnregisterClothInstance(ClothInstance);
            ClothInstance = nullptr;
        }
    }

    // Registration will happen in StartSimulation() based on detected mode
}

void UClothComponent::StartSimulation()
{
    if (bIsSimulating)
        return;

    if (!ClothAsset)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: Cannot start simulation - missing asset"));
        return;
    }

    // Get or create ClothWorld
    FClothWorld *ClothWorld = GEngine->ClothPhysicsManager->CreateClothWorld(GetWorld());
    if (!ClothWorld || !ClothWorld->IsInitialized())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: ClothWorld not available"));
        return;
    }

    // Detect mode and register appropriately
    if (ClothWorld->GetSystemMode() == EClothSystemMode::Batched)
    {
        // Batched mode registration
        ClothInstanceHandle = ClothWorld->RegisterClothInstanceBatched(
            this, ClothAsset, ClothAsset->GetConfig(), EClothLODLevel::LOD_0);

        if (!ClothInstanceHandle)
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Failed to register in batched mode"));
            return;
        }

        ClothInstanceHandle->SetActive(true);
        bUseBatchedMode = true;
        bIsSimulating = true;
        UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Batched simulation started"));
    }
    else
    {
        // Legacy mode registration
        ClothInstance = ClothWorld->RegisterClothInstance(this, ClothAsset, ClothAsset->GetConfig());

        if (!ClothInstance)
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Failed to register in legacy mode"));
            return;
        }

        ClothInstance->SetActive(true);
        bUseBatchedMode = false;
        bIsSimulating = true;
        UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Legacy simulation started"));
    }
}

void UClothComponent::StopSimulation()
{
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        ClothInstanceHandle->SetActive(false);
    }
    else if (ClothInstance)
    {
        ClothInstance->SetActive(false);
    }

    bIsSimulating = false;
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Simulation stopped"));
}

void UClothComponent::ResetSimulation()
{
    // TODO: Implement reset for batched mode
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode reset not yet implemented
        UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: Reset not yet implemented for batched mode"));
    }
    else if (ClothInstance)
    {
        ClothInstance->Reset();
    }

    AccumulatedForce = FVector::ZeroVector;
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Simulation reset"));
}

void UClothComponent::AddForce(const FVector &Force)
{
    AccumulatedForce += Force;
}

void UClothComponent::AddImpulse(const FVector &Impulse)
{
    // TODO: Implement impulse for batched mode
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode - not yet implemented
    }
    else if (ClothInstance)
    {
        ClothInstance->AddExternalForce(Impulse);
    }
}

void UClothComponent::SetWind(const FVector &WindVelocity)
{
    // TODO: Implement wind for batched mode via instance parameters
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode - update via instance parameters
        FClothInstanceParameters params = ClothInstanceHandle->GetParameters();
        params.Wind = WindVelocity;
        ClothInstanceHandle->SetParameters(params);
    }
    else if (ClothInstance)
    {
        ClothInstance->SetWind(WindVelocity);
    }
}

void UClothComponent::SetGravity(const FVector &InGravity)
{
    // TODO: Implement gravity for batched mode via instance parameters
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode - update via instance parameters
        FClothInstanceParameters params = ClothInstanceHandle->GetParameters();
        params.Gravity = InGravity;
        ClothInstanceHandle->SetParameters(params);
    }
    else if (ClothInstance)
    {
        ClothInstance->SetGravity(InGravity);
    }
}

void UClothComponent::AttachToComponent(USceneComponent *Parent, FName SocketName)
{
    // TODO: Implement static attachment
}

void UClothComponent::AttachToSkeletalMesh(USkeletalMeshComponent *SkelMesh, const TArray<FName> &BoneNames)
{
    // TODO: Implement skeletal mesh attachment
}

void UClothComponent::SetClothConfig(const FClothConfig &InConfig)
{
    // TODO: Update instance config through ClothWorld
}

const FClothConfig &UClothComponent::GetClothConfig() const
{
    // TODO: Get config for batched mode
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode - would need to store config or retrieve from batch manager
        static FClothConfig DefaultConfig;
        return DefaultConfig;
    }
    else if (ClothInstance)
    {
        return ClothInstance->GetConfig();
    }

    static FClothConfig DefaultConfig;
    return DefaultConfig;
}
