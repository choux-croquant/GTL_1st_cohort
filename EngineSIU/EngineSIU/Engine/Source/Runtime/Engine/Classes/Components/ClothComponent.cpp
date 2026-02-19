#include "ClothComponent.h"
#include "Engine/ClothAsset.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "World/World.h"
#include "Engine/EditorEngine.h"
#include "Cloth/ClothPhysicsManager.h"
#include "Cloth/ClothBatchManager.h"

UClothComponent::UClothComponent()
    : ClothAsset(nullptr), ClothInstanceHandle(nullptr), bIsSimulating(false), bUseBatchedMode(false), bAttachmentsDirty(false)
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

    // Initialize runtime InvMass from new asset
    InitializeRuntimeInvMasses();

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

    // Initialize runtime InvMass if not already done
    if (RuntimeInvMasses.Num() == 0)
    {
        InitializeRuntimeInvMasses();
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

    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Simulation reset"));
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

// ===== NEW: Runtime Attachment Management Implementation =====

void UClothComponent::InitializeRuntimeInvMasses()
{
    if (!ClothAsset)
    {
        RuntimeInvMasses.Empty();
        return;
    }
    
    // Copy base InvMass from asset (immutable source of truth)
    RuntimeInvMasses = ClothAsset->GetBaseInvMasses();
    
    // Apply any pre-existing attachment bindings
    for (const FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.bIsActive && binding.SimVertexIndex < (uint32)RuntimeInvMasses.Num())
        {
            RuntimeInvMasses[binding.SimVertexIndex] = 0.0f;
        }
    }
    
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Initialized RuntimeInvMasses (%d vertices)"),
           RuntimeInvMasses.Num());
}

void UClothComponent::UpdateRuntimeInvMass(uint32 VertexIndex, bool bIsAttached)
{
    if (!ClothAsset || VertexIndex >= (uint32)RuntimeInvMasses.Num())
        return;
    
    if (bIsAttached)
    {
        // Attached vertex becomes kinematic (InvMass = 0)
        RuntimeInvMasses[VertexIndex] = 0.0f;
    }
    else
    {
        // Restore base InvMass from asset
        const TArray<float>& baseInvMasses = ClothAsset->GetBaseInvMasses();
        if (VertexIndex < (uint32)baseInvMasses.Num())
        {
            RuntimeInvMasses[VertexIndex] = baseInvMasses[VertexIndex];
        }
    }
}

void UClothComponent::MarkAttachmentsDirty()
{
    bAttachmentsDirty = true;
    
    // Notify batch manager if in batched mode
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // NEW: Phase 3 - Call batch manager update methods
        FClothBatchManager* batchMgr = ClothInstanceHandle->GetBatchManager();
        if (batchMgr)
        {
            // Update InvMass buffer (per-instance range)
            batchMgr->UpdateInstanceInvMass(ClothInstanceHandle);
            
            // Update attachment data (triggers rebuild)
            batchMgr->UpdateInstanceAttachments(ClothInstanceHandle);
            
            UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Updated GPU buffers for attachment changes"));
        }
    }
}

void UClothComponent::BindAttachment(uint32 SimVertexIndex, const FClothAttachmentTarget& Target,
                                    float Stiffness, float AttachDistance)
{
    if (!ClothAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Cannot bind attachment - no asset"));
        return;
    }
    
    // Validate vertex index
    if (SimVertexIndex >= (uint32)RuntimeInvMasses.Num())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Invalid vertex index %d for attachment (max: %d)"),
               SimVertexIndex, RuntimeInvMasses.Num() - 1);
        return;
    }
    
    // Check if already bound - update existing binding
    for (FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.SimVertexIndex == SimVertexIndex)
        {
            UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Updating existing attachment for vertex %d"),
                   SimVertexIndex);
            binding.Target = Target;
            binding.bIsActive = true;
            
            // Update stiffness/distance if provided
            if (Stiffness >= 0.0f)
                binding.Stiffness = Stiffness;
            if (AttachDistance >= 0.0f)
                binding.AttachDistance = AttachDistance;
            
            UpdateRuntimeInvMass(SimVertexIndex, true);
            MarkAttachmentsDirty();
            return;
        }
    }
    
    // Create new binding
    FClothAttachmentBinding newBinding;
    newBinding.SimVertexIndex = SimVertexIndex;
    newBinding.Target = Target;
    newBinding.bIsActive = true;
    
    // Get default parameters from asset capability (if exists)
    const TArray<FClothAttachmentCapability>& capabilities = ClothAsset->GetAttachmentCapabilities();
    for (const FClothAttachmentCapability& cap : capabilities)
    {
        if (cap.SimVertexIndex == SimVertexIndex)
        {
            newBinding.Stiffness = cap.DefaultStiffness;
            newBinding.AttachDistance = cap.DefaultAttachDistance;
            UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Using capability defaults for vertex %d"),
                   SimVertexIndex);
            break;
        }
    }
    
    // Override with provided parameters
    if (Stiffness >= 0.0f)
        newBinding.Stiffness = Stiffness;
    if (AttachDistance >= 0.0f)
        newBinding.AttachDistance = AttachDistance;
    
    AttachmentBindings.Add(newBinding);
    UpdateRuntimeInvMass(SimVertexIndex, true);
    MarkAttachmentsDirty();
    
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Bound attachment for vertex %d (Stiffness: %.2f, Distance: %.2f)"),
           SimVertexIndex, newBinding.Stiffness, newBinding.AttachDistance);
}

void UClothComponent::BindAttachmentToWorldPosition(uint32 SimVertexIndex, const FVector& WorldPosition,
                                                   float Stiffness, float AttachDistance)
{
    FClothAttachmentTarget target;
    target.Type = EClothAttachmentType::WorldPosition;
    target.WorldPosition = WorldPosition;
    
    BindAttachment(SimVertexIndex, target, Stiffness, AttachDistance);
}

void UClothComponent::BindAttachmentToComponent(uint32 SimVertexIndex, USceneComponent* TargetComponent,
                                               const FTransform& LocalOffset, float Stiffness, float AttachDistance)
{
    if (!TargetComponent)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Cannot bind to null component"));
        return;
    }
    
    FClothAttachmentTarget target;
    target.Type = EClothAttachmentType::ActorTransform;
    target.DriverComponent = TargetComponent;
    target.LocalOffset = LocalOffset;
    //target.WorldPosition = TargetComponent->GetComponentTransform().TransformPosition(LocalOffset);
    
    BindAttachment(SimVertexIndex, target, Stiffness, AttachDistance);
}

void UClothComponent::BindAttachmentToBone(uint32 SimVertexIndex, USkeletalMeshComponent* SkeletalMesh,
                                          FName BoneName, const FTransform& LocalOffset,
                                          float Stiffness, float AttachDistance)
{
    if (!SkeletalMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Cannot bind to null skeletal mesh"));
        return;
    }
    
    // Resolve bone index
    //int32 boneIndex = SkeletalMesh->GetBoneIndex(BoneName);
    //if (boneIndex == INDEX_NONE)
    //{
    //    UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Bone '%s' not found in skeletal mesh"),
    //           *BoneName.ToString());
    //    return;
    //}
    //
    //FClothAttachmentTarget target;
    //target.Type = EClothAttachmentType::SkeletalBone;
    //target.DriverComponent = SkeletalMesh;
    //target.BoneName = BoneName;
    //target.BoneIndex = boneIndex;
    //target.LocalOffset = LocalOffset;
    //
    //// Get initial world position from bone transform
    //FTransform boneTransform = SkeletalMesh->GetBoneTransform(boneIndex);
    //target.WorldPosition = boneTransform.TransformPosition(LocalOffset.GetLocation());
    //
    //BindAttachment(SimVertexIndex, target, Stiffness, AttachDistance);
}

bool UClothComponent::UnbindAttachment(uint32 SimVertexIndex)
{
    for (int32 i = 0; i < AttachmentBindings.Num(); ++i)
    {
        if (AttachmentBindings[i].SimVertexIndex == SimVertexIndex)
        {
            AttachmentBindings.RemoveAt(i);
            UpdateRuntimeInvMass(SimVertexIndex, false);
            MarkAttachmentsDirty();
            
            UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Unbound attachment for vertex %d"),
                   SimVertexIndex);
            return true;
        }
    }
    
    UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: No attachment found for vertex %d"),
           SimVertexIndex);
    return false;
}

void UClothComponent::ClearAllAttachments()
{
    if (AttachmentBindings.Num() == 0)
        return;
    
    // Restore all InvMasses from asset
    if (ClothAsset)
    {
        RuntimeInvMasses = ClothAsset->GetBaseInvMasses();
    }
    
    int32 count = AttachmentBindings.Num();
    AttachmentBindings.Empty();
    MarkAttachmentsDirty();
    
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Cleared %d attachments"), count);
}

bool UClothComponent::UpdateAttachmentTarget(uint32 SimVertexIndex, const FClothAttachmentTarget& NewTarget)
{
    for (FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.SimVertexIndex == SimVertexIndex)
        {
            binding.Target = NewTarget;
            MarkAttachmentsDirty();
            
            UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Updated attachment target for vertex %d"),
                   SimVertexIndex);
            return true;
        }
    }
    
    UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: No attachment found for vertex %d"),
           SimVertexIndex);
    return false;
}

void UClothComponent::SetAttachmentEnabled(uint32 SimVertexIndex, bool bEnabled)
{
    for (FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.SimVertexIndex == SimVertexIndex)
        {
            if (binding.bIsActive != bEnabled)
            {
                binding.bIsActive = bEnabled;
                UpdateRuntimeInvMass(SimVertexIndex, bEnabled);
                MarkAttachmentsDirty();
                
                UE_LOG(ELogLevel::Display, TEXT("ClothComponent: %s attachment for vertex %d"),
                       bEnabled ? TEXT("Enabled") : TEXT("Disabled"), SimVertexIndex);
            }
            return;
        }
    }
    
    UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: No attachment found for vertex %d"),
           SimVertexIndex);
}

bool UClothComponent::IsVertexAttached(uint32 SimVertexIndex) const
{
    for (const FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.SimVertexIndex == SimVertexIndex && binding.bIsActive)
            return true;
    }
    return false;
}

const TArray<FClothAttachmentCapability>& UClothComponent::GetAttachmentCapabilities() const
{
    static TArray<FClothAttachmentCapability> emptyArray;
    
    if (!ClothAsset)
        return emptyArray;
    
    return ClothAsset->GetAttachmentCapabilities();
}

// DEPRECATED: Old attachment API (for backward compatibility)
void UClothComponent::AttachToComponent(USceneComponent *Parent, FName SocketName)
{
    // TODO: Implement static attachment using new API
    UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: AttachToComponent() is deprecated - use BindAttachmentToComponent()"));
}

void UClothComponent::AttachToSkeletalMesh(USkeletalMeshComponent *SkelMesh, const TArray<FName> &BoneNames)
{
    // TODO: Implement skeletal mesh attachment using new API
    UE_LOG(ELogLevel::Warning, TEXT("ClothComponent: AttachToSkeletalMesh() is deprecated - use BindAttachmentToBone()"));
}
