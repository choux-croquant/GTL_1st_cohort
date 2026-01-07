/**
 * Cloth Component Implementation (Refactored)
 * Uses centralized ClothWorld manager instead of per-component solver
 */

#include "ClothComponent.h"
#include "Engine/ClothAsset.h"
#include "Cloth/ClothInstance.h"
#include "Cloth/ClothWorld.h"
#include "World/World.h"

IMPLEMENT_CLASS(UClothComponent)

UClothComponent::UClothComponent()
    : ClothAsset(nullptr), ClothInstance(nullptr), bIsSimulating(false), bDebugDrawEnabled(false), AccumulatedForce(FVector::ZeroVector)
{
}

UClothComponent::~UClothComponent()
{
    // Unregister from ClothWorld
    if (ClothInstance)
    {
        FClothWorld *ClothWorld = GetClothWorld(GetWorld());
        if (ClothWorld)
        {
            ClothWorld->UnregisterClothInstance(ClothInstance);
        }
        // Note: ClothWorld owns and will delete the instance
        ClothInstance = nullptr;
    }
}

void UClothComponent::InitializeComponent()
{
    Super::InitializeComponent();

    // Register with ClothWorld if we have an asset
    if (ClothAsset)
    {
        FClothWorld *ClothWorld = GetOrCreateClothWorld(GetWorld());
        if (ClothWorld && ClothWorld->IsInitialized())
        {
            ClothInstance = ClothWorld->RegisterClothInstance(this, ClothAsset, ClothAsset->GetConfig());
        }
    }
}

void UClothComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // Note: We NO LONGER call solver here!
    // ClothWorld::Update() handles all simulation

    // Instead, we only update per-instance kinematic data
    if (!bIsSimulating || !ClothInstance)
        return;

    // Apply accumulated forces to the instance
    if (AccumulatedForce.SizeSquared() > 0.0f)
    {
        ClothInstance->AddExternalForce(AccumulatedForce);
        AccumulatedForce = FVector::ZeroVector;
    }

    // Update attachments (if any)
    if (Attachments.Num() > 0)
    {
        ClothInstance->UpdateAttachments(Attachments);
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
    if (ClothInstance)
    {
        FClothWorld *ClothWorld = GetClothWorld(GetWorld());
        if (ClothWorld)
        {
            ClothWorld->UnregisterClothInstance(ClothInstance);
        }
        ClothInstance = nullptr;
    }

    // Register new instance
    if (InAsset)
    {
        FClothWorld *ClothWorld = GetOrCreateClothWorld(GetWorld());
        if (ClothWorld && ClothWorld->IsInitialized())
        {
            ClothInstance = ClothWorld->RegisterClothInstance(this, InAsset, InAsset->GetConfig());
        }
    }
}

void UClothComponent::StartSimulation()
{
    if (bIsSimulating)
        return;

    if (!ClothAsset || !ClothInstance)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: Cannot start simulation - missing asset or instance"));
        return;
    }

    ClothInstance->SetActive(true);
    bIsSimulating = true;
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Simulation started"));
}

void UClothComponent::StopSimulation()
{
    if (ClothInstance)
    {
        ClothInstance->SetActive(false);
    }
    bIsSimulating = false;
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Simulation stopped"));
}

void UClothComponent::ResetSimulation()
{
    if (ClothInstance)
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
    if (ClothInstance)
    {
        ClothInstance->AddExternalForce(Impulse);
    }
}

void UClothComponent::SetWind(const FVector &WindVelocity)
{
    if (ClothInstance)
    {
        ClothInstance->SetWind(WindVelocity);
    }
}

void UClothComponent::SetGravity(const FVector &InGravity)
{
    if (ClothInstance)
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
    if (ClothInstance)
    {
        return ClothInstance->GetConfig();
    }

    static FClothConfig DefaultConfig;
    return DefaultConfig;
}
