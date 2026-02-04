/**
 * Cloth Component Implementation (Refactored)
 * Uses centralized ClothWorld manager instead of per-component solver
 */

#include "ClothComponent.h"
#include "Engine/ClothAsset.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "World/World.h"
#include "Engine/EditorEngine.h"
#include "Cloth/ClothPhysicsManager.h"

UClothComponent::UClothComponent()
    : ClothAsset(nullptr), ClothInstanceHandle(nullptr), bIsSimulating(false), bUseBatchedMode(false), bDebugDrawEnabled(false), AccumulatedForce(FVector::ZeroVector)
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
    }
}

void UClothComponent::BeginPlay()
{
    Super::BeginPlay();
}

UObject* UClothComponent::Duplicate(UObject* InOuter)
{
    ThisClass* NewComponent = Cast<ThisClass>(Super::Duplicate(InOuter));

    return NewComponent;
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
    }
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
            this, ClothAsset, EClothLODLevel::LOD_0);

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
}

void UClothComponent::StopSimulation()
{
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        ClothInstanceHandle->SetActive(false);

        FClothWorld* ClothWorld = GEngine->ClothPhysicsManager->CreateClothWorld(GetWorld());
        if (!ClothWorld || !ClothWorld->IsInitialized())
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothComponent: ClothWorld not available"));
            return;
        }

        // Detect mode and register appropriately
        if (ClothWorld->GetSystemMode() == EClothSystemMode::Batched)
        {
            // Batched mode registration
            ClothWorld->UnregisterClothInstanceBatched(ClothInstanceHandle);

            UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Instance unregistered"));
        }
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
}

void UClothComponent::AttachToComponent(USceneComponent *Parent, FName SocketName)
{
    // TODO: Implement static attachment
}

void UClothComponent::AttachToSkeletalMesh(USkeletalMeshComponent *SkelMesh, const TArray<FName> &BoneNames)
{
    // TODO: Implement skeletal mesh attachment
}
