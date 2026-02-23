# Skeletal Mesh Cloth Attachment - Implementation Plan

## Executive Summary

This plan outlines the implementation and testing of skeletal mesh bone attachment features for the cloth simulation system. The feature allows cloth vertices to be attached to animated skeletal mesh bones, enabling realistic cloth-on-character scenarios (capes, skirts, hair, etc.).

**Current Status**: The API is defined but not fully implemented. [`UClothComponent::BindAttachmentToBone()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:373) exists but is commented out (lines 384-403).

**Goal**: Complete the implementation and create a test actor to verify skeletal mesh attachments work correctly with animated characters.

---

## Current Architecture Analysis

### ✅ What's Already Working

1. **GPU-Based Kinematic Target System** (P1 Optimization)
   - [`ClothComputeKinematicTargets.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothComputeKinematicTargets.hlsl) - GPU shader for kinematic constraints
   - [`FClothBatchManager::BuildKinematicAttachmentData()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:866) - Builds attachment data
   - [`FClothBatchManager::UpdateKinematicTargetsGPU()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:997) - Updates transforms per frame
   - Component deduplication system (lines 868-908) - Reduces transform uploads

2. **Attachment Type System**
   - [`EClothAttachmentType`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:236) enum with 3 types:
     - `WorldPosition` - Static world position ✅ Working
     - `ActorTransform` - Follow actor/component ✅ Working
     - `SkeletalBone` - Follow bone transform ❌ **Not Implemented**

3. **Data Structures**
   - [`FClothAttachmentTarget`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:269) - Has bone fields (BoneName, BoneIndex, LocalOffset)
   - [`FClothAttachmentBinding`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:293) - Instance-level binding
   - [`FKinematicAttachmentGPU`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h) - GPU data structure

4. **Runtime Attachment API**
   - [`BindAttachmentToWorldPosition()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:346) ✅ Working
   - [`BindAttachmentToComponent()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:360) ✅ Working
   - [`BindAttachmentToBone()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:373) ❌ **Stubbed Out**

### ❌ What's Missing

1. **Bone Transform Resolution**
   - No bone index lookup from bone name
   - No bone transform retrieval from skeletal mesh
   - No bone transform caching/updating

2. **GPU Shader Support**
   - [`ClothComputeKinematicTargets.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothComputeKinematicTargets.hlsl) handles Type 0 (WorldPosition) and Type 1 (ActorTransform)
   - No Type 2 (SkeletalBone) handling in shader
   - Currently Type 1 is used for both ActorTransform and SkeletalBone (line 913 in ClothBatchManager.cpp)

3. **BindAttachmentToBone Implementation**
   - Function body is completely commented out (lines 384-403)
   - Missing bone validation
   - Missing bone transform computation

4. **Test Actor**
   - No dedicated test actor for skeletal mesh attachments
   - Existing [`ATestClothAttachmentActor`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothAttachmentActor.h) only tests component attachments

---

## Implementation Strategy

### Phase 1: Bone Transform Infrastructure

**Goal**: Add bone transform resolution and caching to the system.

#### 1.1 Extend USkeletalMeshComponent API

**File**: [`SkeletalMeshComponent.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h)

Add bone query methods:
```cpp
// Get bone index by name
int32 GetBoneIndex(FName BoneName) const;

// Get bone world transform
FTransform GetBoneTransform(int32 BoneIndex) const;
FTransform GetBoneTransform(FName BoneName) const;

// Get all bone world transforms (for batch updates)
void GetBoneWorldTransforms(TArray<FTransform>& OutTransforms) const;
```

**Implementation Notes**:
- Use existing `RefBonePoseTransforms` (line 59) as base
- Apply animation pose from `BonePoseContext` (line 147)
- Transform to world space using component transform
- Cache results per frame to avoid redundant calculations

#### 1.2 Bone Transform Caching in ClothBatchManager

**File**: [`ClothBatchManager.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp)

Extend the component deduplication system to cache bone transforms:

```cpp
// In FClothBatchManager private members:
struct FSkeletalMeshBoneCache
{
    TWeakObjectPtr<USkeletalMeshComponent> SkeletalMesh;
    TArray<FTransform> BoneWorldTransforms;
    uint32 LastUpdateFrame;
};

TMap<USkeletalMeshComponent*, FSkeletalMeshBoneCache> SkeletalMeshCaches;
```

**Why**: Avoid querying bone transforms multiple times per frame when multiple cloth vertices attach to the same skeletal mesh.

---

### Phase 2: Complete BindAttachmentToBone Implementation

**Goal**: Implement the commented-out function body.

#### 2.1 Implement Bone Attachment Logic

**File**: [`ClothComponent.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:373)

Uncomment and complete the implementation:

```cpp
void UClothComponent::BindAttachmentToBone(uint32 SimVertexIndex, 
                                          USkeletalMeshComponent* SkeletalMesh,
                                          FName BoneName, 
                                          const FTransform& LocalOffset,
                                          float Stiffness, 
                                          float AttachDistance)
{
    if (!SkeletalMesh)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Cannot bind to null skeletal mesh"));
        return;
    }
    
    // STEP 1: Resolve bone index
    int32 BoneIndex = SkeletalMesh->GetBoneIndex(BoneName);
    if (BoneIndex == INDEX_NONE)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothComponent: Bone '%s' not found in skeletal mesh"),
               *BoneName.ToString());
        return;
    }
    
    // STEP 2: Create attachment target
    FClothAttachmentTarget Target;
    Target.Type = EClothAttachmentType::SkeletalBone;
    Target.DriverComponent = SkeletalMesh;
    Target.BoneName = BoneName;
    Target.BoneIndex = BoneIndex;
    Target.LocalOffset = LocalOffset;
    
    // STEP 3: Get initial world position from bone transform
    FTransform BoneTransform = SkeletalMesh->GetBoneTransform(BoneIndex);
    Target.WorldPosition = BoneTransform.TransformPosition(LocalOffset.GetLocation());
    
    // STEP 4: Bind attachment using common path
    BindAttachment(SimVertexIndex, Target, Stiffness, AttachDistance);
    
    UE_LOG(ELogLevel::Display, TEXT("ClothComponent: Bound vertex %d to bone '%s' (index %d)"),
           SimVertexIndex, *BoneName.ToString(), BoneIndex);
}
```

**Key Points**:
- Validate bone exists before binding
- Store both bone name and index for efficiency
- Compute initial world position for consistency
- Reuse existing `BindAttachment()` helper (line 109)

---

### Phase 3: GPU Shader Enhancement

**Goal**: Add proper skeletal bone handling in the GPU shader.

#### 3.1 Add Bone Transform Type to Shader

**File**: [`ClothComputeKinematicTargets.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothComputeKinematicTargets.hlsl)

Currently handles Type 0 (WorldPosition) and Type 1 (ActorTransform). Add Type 2 (SkeletalBone):

```hlsl
[numthreads(256, 1, 1)]
void ComputeKinematicTargetsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint attachIdx = DTid.x;
    if (attachIdx >= NumKinematicTargets)
        return;
    
    FKinematicAttachment attachment = Attachments[attachIdx];
    uint particleIdx = attachment.ParticleIndex;
    FClothParticle particle = PredictedRW[particleIdx];

    // Type 0: WorldPosition (static)
    if (attachment.Type == 0) 
    {
        // ... existing code ...
    }
    // Type 1: ActorTransform (component-driven)
    else if (attachment.Type == 1) 
    {
        // ... existing code ...
    }
    // Type 2: SkeletalBone (bone-driven) - NEW
    else if (attachment.Type == 2)
    {
        uint componentIdx = attachment.ComponentIndex;
        float4x4 boneTransform = ComponentTransforms[componentIdx];
        
        // Transform local offset to world space
        float4 localPosHomogeneous = float4(attachment.LocalOffset, 1.0f);
        float3 worldPos = mul(boneTransform, localPosHomogeneous).xyz;
        
        // Apply kinematic constraint (same logic as Type 1)
        if (attachment.Stiffness > 0.99f)
        {
            particle.Position = worldPos;
        }
        else if (attachment.Stiffness > 0.0f)
        {
            float3 delta = worldPos - particle.Position;
            particle.Position += delta * attachment.Stiffness;
        }
        
        // Long Range Attachment (LRA)
        if (attachment.AttachDistance > 0.0f)
        {
            float3 delta = worldPos - particle.Position;
            float distance = length(delta);
            
            if (distance > attachment.AttachDistance)
            {
                float3 direction = delta / distance;
                float overshoot = distance - attachment.AttachDistance;
                particle.Position += direction * overshoot * attachment.Stiffness;
            }
        }
        
        PredictedRW[particleIdx] = particle;
    }
}
```

**Alternative Approach**: Since bone transforms are just component transforms, we can reuse Type 1 logic. The key difference is that bone transforms are per-bone, not per-component.

**Recommended**: Keep Type 1 for both ActorTransform and SkeletalBone, but ensure bone transforms are uploaded correctly in the ComponentTransforms buffer.

---

### Phase 4: Bone Transform Upload System

**Goal**: Upload bone transforms to GPU efficiently.

#### 4.1 Extend BuildKinematicAttachmentData

**File**: [`ClothBatchManager.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:866)

Modify the attachment building to handle skeletal bones:

```cpp
void FClothBatchManager::BuildKinematicAttachmentData()
{
    ComponentIndexMap.Empty();
    UniqueComponents.Empty();
    TArray<FKinematicAttachmentGPU> attachmentData;
    
    // NEW: Track skeletal mesh components and their bone indices
    TMap<USkeletalMeshComponent*, TArray<int32>> SkeletalMeshBones;

    for (FClothInstanceHandle *Handle : Instances)
    {
        if (!Handle || !Handle->GetOwnerComponent())
            continue;

        const FClothInstanceMetadata &metadata = Handle->GetMetadata();
        UClothComponent *owner = Handle->GetOwnerComponent();
        const TArray<FClothAttachmentBinding> &bindings = owner->GetAttachmentBindings();

        for (const FClothAttachmentBinding &binding : bindings)
        {
            if (!binding.bIsActive)
                continue;
            
            const FClothAttachmentTarget &target = binding.Target;
            
            // ... existing WorldPosition and ActorTransform handling ...
            
            // NEW: SkeletalBone handling
            else if (target.Type == EClothAttachmentType::SkeletalBone)
            {
                USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(target.DriverComponent);
                if (!SkelMesh || target.BoneIndex == INDEX_NONE)
                    continue;
                
                // Create unique key for this bone (not just component)
                // Format: ComponentIndex * 1000 + BoneIndex
                // This allows up to 1000 bones per skeletal mesh
                
                uint32 ComponentIndex;
                uint32* ComponentIndexPtr = ComponentIndexMap.Find(SkelMesh);
                
                if (!ComponentIndexPtr)
                {
                    ComponentIndex = UniqueComponents.Num();
                    ComponentIndexMap.Add(SkelMesh, ComponentIndex);
                    UniqueComponents.Add(SkelMesh);
                    SkeletalMeshBones.Add(SkelMesh, TArray<int32>());
                }
                else
                {
                    ComponentIndex = *ComponentIndexPtr;
                }
                
                // Track which bones we need from this skeletal mesh
                TArray<int32>& BoneIndices = SkeletalMeshBones[SkelMesh];
                int32 BoneSlot = BoneIndices.Find(target.BoneIndex);
                if (BoneSlot == INDEX_NONE)
                {
                    BoneSlot = BoneIndices.Add(target.BoneIndex);
                }
                
                // Build GPU attachment data
                FKinematicAttachmentGPU gpuAttachment;
                gpuAttachment.Type = 2; // SkeletalBone type
                gpuAttachment.ComponentIndex = ComponentIndex * 1000 + BoneSlot; // Encode bone slot
                gpuAttachment.ParticleIndex = binding.SimVertexIndex + metadata.ParticleOffset;
                gpuAttachment.Stiffness = binding.Stiffness;
                gpuAttachment.AttachDistance = binding.AttachDistance;
                gpuAttachment.LocalOffset = target.LocalOffset.GetTranslation();
                gpuAttachment.Padding = 0.0f;

                attachmentData.Add(gpuAttachment);
            }
        }
    }
    
    // Store skeletal mesh bone tracking for transform updates
    CachedSkeletalMeshBones = SkeletalMeshBones;
    
    // ... rest of existing code ...
}
```

#### 4.2 Update Bone Transforms Per Frame

**File**: [`ClothBatchManager.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:997)

Extend `UpdateKinematicTargetsGPU()` to upload bone transforms:

```cpp
void FClothBatchManager::UpdateKinematicTargetsGPU(float DeltaTime)
{
    if (!BatchedSolver)
        return;

    // Rebuild attachment data if needed
    if (bAttachmentDataDirty)
    {
        BuildKinematicAttachmentData();
    }

    // Collect component transforms (existing code)
    TArray<FMatrix> componentTransforms;
    componentTransforms.Reserve(UniqueComponents.Num() * 100); // Reserve space for bones too
    
    // Upload regular component transforms
    for (const TWeakObjectPtr<USceneComponent> &Component : UniqueComponents)
    {
        if (Component.IsValid())
        {
            USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Component.Get());
            
            if (SkelMesh && CachedSkeletalMeshBones.Contains(SkelMesh))
            {
                // Upload bone transforms for this skeletal mesh
                const TArray<int32>& BoneIndices = CachedSkeletalMeshBones[SkelMesh];
                
                for (int32 BoneIndex : BoneIndices)
                {
                    FTransform BoneTransform = SkelMesh->GetBoneTransform(BoneIndex);
                    componentTransforms.Add(BoneTransform.ToMatrixWithScale());
                }
            }
            else
            {
                // Regular component transform
                componentTransforms.Add(Component->GetWorldMatrix());
            }
        }
        else
        {
            // Component destroyed - add identity
            componentTransforms.Add(FMatrix::Identity);
        }
    }

    if (componentTransforms.Num() > 0)
    {
        BatchedSolver->UploadComponentTransforms(componentTransforms);
    }
}
```

**Key Insight**: We pack bone transforms into the same `ComponentTransforms` buffer, using the encoding scheme `ComponentIndex * 1000 + BoneSlot` to index into the correct bone transform.

---

### Phase 5: Test Actor Implementation

**Goal**: Create a dedicated test actor to verify skeletal mesh attachments.

#### 5.1 Create Test Actor Class

**File**: `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothSkeletalAttachmentActor.h`

```cpp
#pragma once
#include "Actor.h"

class USkeletalMeshComponent;
class UClothMeshComponent;
class UAnimSequenceBase;

/**
 * Test actor for skeletal mesh cloth attachments
 * Demonstrates cloth attached to animated character bones
 */
class ATestClothSkeletalAttachmentActor : public AActor
{
    DECLARE_CLASS(ATestClothSkeletalAttachmentActor, AActor)

public:
    ATestClothSkeletalAttachmentActor();
    virtual void PostSpawnInitialize() override;
    virtual void Tick(float DeltaTime) override;

private:
    // Character with skeletal mesh
    USkeletalMeshComponent* CharacterMesh;
    
    // Cloth component (cape attached to character)
    UClothMeshComponent* CapeCloth;
    
    // Animation
    UAnimSequenceBase* IdleAnimation;
    
    bool bInitialized;
    float AnimationTime;
    
    void SetupCharacter();
    void SetupCape();
    void AttachCapeToCharacter();
};
```

#### 5.2 Implement Test Actor

**File**: `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothSkeletalAttachmentActor.cpp`

```cpp
#include "TestClothSkeletalAttachmentActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"
#include "Engine/ClothPhysicsManager.h"
#include "Cloth/ClothWorld.h"
#include "Cloth/ClothAssetGenerator.h"

ATestClothSkeletalAttachmentActor::ATestClothSkeletalAttachmentActor()
    : CharacterMesh(nullptr)
    , CapeCloth(nullptr)
    , IdleAnimation(nullptr)
    , bInitialized(false)
    , AnimationTime(0.0f)
{
    bTickEnabled = true;
}

void ATestClothSkeletalAttachmentActor::PostSpawnInitialize()
{
    Super::PostSpawnInitialize();
    
    SetupCharacter();
    SetupCape();
    AttachCapeToCharacter();
    
    bInitialized = true;
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Initialized"));
}

void ATestClothSkeletalAttachmentActor::SetupCharacter()
{
    // Create skeletal mesh component
    CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
    CharacterMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    
    // Load character mesh (replace with your actual character mesh path)
    // For testing, use any skeletal mesh with a spine/shoulder bone
    // CharacterMesh->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("Path/To/Character.fbx")));
    
    // Load and play idle animation
    // IdleAnimation = LoadObject<UAnimSequenceBase>(nullptr, TEXT("Path/To/IdleAnim.fbx"));
    // if (IdleAnimation)
    // {
    //     CharacterMesh->PlayAnimation(IdleAnimation, true);
    // }
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Character setup complete"));
}

void ATestClothSkeletalAttachmentActor::SetupCape()
{
    // Create cloth component
    CapeCloth = CreateDefaultSubobject<UClothMeshComponent>(TEXT("CapeCloth"));
    CapeCloth->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
    
    // Position cape behind character
    CapeCloth->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f)); // At shoulder height
    
    // Generate cape cloth asset
    FClothAssetGenerationParams Params;
    Params.SourceMeshPath = TEXT("Contents/TestClothMesh/Cape.obj"); // Replace with actual cape mesh
    Params.DecimationPercentage = 0.15f; // 15% of original vertices
    Params.TotalMass = 0.5f; // Light cape
    Params.StretchStiffness = 0.85f;
    Params.BendStiffness = 0.05f;
    Params.AreaStiffness = 0.001f;
    
    UClothAsset* CapeAsset = FClothAssetGenerator::GenerateClothAsset(Params);
    if (!CapeAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Failed to generate cape asset"));
        return;
    }
    
    CapeCloth->SetClothAsset(CapeAsset);
    
    // Register with cloth world
    FClothWorld* ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(GetWorld());
    if (ClothWorld)
    {
        ClothWorld->RegisterClothComponent(CapeCloth);
        CapeCloth->StartSimulation();
    }
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Cape setup complete"));
}

void ATestClothSkeletalAttachmentActor::AttachCapeToCharacter()
{
    if (!CapeCloth || !CharacterMesh || !CapeCloth->GetClothAsset())
    {
        UE_LOG(ELogLevel::Error, TEXT("TestClothSkeletalAttachmentActor: Cannot attach cape - missing components"));
        return;
    }
    
    UClothAsset* CapeAsset = CapeCloth->GetClothAsset();
    
    // Find top vertices of cape (shoulder area)
    TArray<uint32> ShoulderVertices;
    float MaxZ = -FLT_MAX;
    float MinZ = FLT_MAX;
    
    for (const FVector& Pos : CapeAsset->RestPositions)
    {
        MaxZ = FMath::Max(MaxZ, Pos.Z);
        MinZ = FMath::Min(MinZ, Pos.Z);
    }
    
    float ZRange = MaxZ - MinZ;
    float AttachThreshold = MaxZ - (ZRange * 0.10f); // Top 10% of vertices
    
    for (int32 i = 0; i < CapeAsset->RestPositions.Num(); ++i)
    {
        if (CapeAsset->RestPositions[i].Z >= AttachThreshold)
        {
            ShoulderVertices.Add(i);
        }
    }
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Found %d shoulder vertices to attach"),
           ShoulderVertices.Num());
    
    // Attach vertices to spine/shoulder bones
    // Distribute vertices between left and right shoulders
    int32 MidPoint = ShoulderVertices.Num() / 2;
    
    for (int32 i = 0; i < ShoulderVertices.Num(); ++i)
    {
        uint32 VertexIndex = ShoulderVertices[i];
        FVector VertexWorldPos = CapeCloth->GetWorldLocation() + CapeAsset->RestPositions[VertexIndex];
        
        // Determine which bone to attach to based on vertex position
        FName BoneName;
        if (i < MidPoint)
        {
            BoneName = FName(TEXT("LeftShoulder")); // Replace with actual bone name
        }
        else
        {
            BoneName = FName(TEXT("RightShoulder")); // Replace with actual bone name
        }
        
        // Calculate local offset from bone
        // For now, use a simple offset - in production, compute from bone transform
        FTransform LocalOffset;
        LocalOffset.SetLocation(FVector(0.0f, 0.0f, -10.0f)); // Slightly below bone
        LocalOffset.SetRotation(FQuat::Identity);
        LocalOffset.SetScale3D(FVector::OneVector);
        
        // Bind attachment to bone
        CapeCloth->BindAttachmentToBone(
            VertexIndex,           // Which simulation vertex to attach
            CharacterMesh,         // Skeletal mesh component
            BoneName,              // Bone name
            LocalOffset,           // Local offset from bone
            1.0f,                  // Stiffness (1.0 = hard constraint)
            0.0f                   // AttachDistance (0.0 = kinematic)
        );
    }
    
    UE_LOG(ELogLevel::Display, TEXT("TestClothSkeletalAttachmentActor: Attached %d vertices to character bones"),
           ShoulderVertices.Num());
}

void ATestClothSkeletalAttachmentActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (!bInitialized)
        return;
    
    AnimationTime += DeltaTime;
    
    // Optional: Add character movement for more dynamic testing
    // For example, rotate character or move around
}
```

#### 5.3 Test Scenarios

**Scenario 1: Static Character with Cape**
- Character in T-pose or idle animation
- Cape attached to shoulder bones
- Verify cape hangs naturally and follows bone positions

**Scenario 2: Animated Character**
- Character playing walk/run animation
- Cape should follow shoulder movement
- No stretching or tearing at attachment points

**Scenario 3: Multiple Bone Attachments**
- Attach different cloth vertices to different bones
- Verify each vertex follows its respective bone
- Test with complex animations (jumping, attacking)

---

## Implementation Checklist

### Code Implementation

- [ ] Add bone query methods to [`USkeletalMeshComponent`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h)
  - [ ] `GetBoneIndex(FName BoneName)`
  - [ ] `GetBoneTransform(int32 BoneIndex)`
  - [ ] `GetBoneWorldTransforms(TArray<FTransform>&)`

- [ ] Implement [`UClothComponent::BindAttachmentToBone()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:373)
  - [ ] Uncomment function body
  - [ ] Add bone index resolution
  - [ ] Add bone transform computation
  - [ ] Add validation and error handling

- [ ] Extend [`FClothBatchManager::BuildKinematicAttachmentData()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:866)
  - [ ] Add SkeletalBone case handling
  - [ ] Implement bone tracking system
  - [ ] Add bone index encoding (ComponentIndex * 1000 + BoneSlot)

- [ ] Extend [`FClothBatchManager::UpdateKinematicTargetsGPU()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:997)
  - [ ] Add bone transform upload
  - [ ] Implement per-frame bone transform caching
  - [ ] Handle skeletal mesh component detection

- [ ] Update [`ClothComputeKinematicTargets.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothComputeKinematicTargets.hlsl)
  - [ ] Add Type 2 (SkeletalBone) handling
  - [ ] OR verify Type 1 works for bones (recommended)

### Test Actor

- [ ] Create `TestClothSkeletalAttachmentActor.h`
- [ ] Create `TestClothSkeletalAttachmentActor.cpp`
- [ ] Implement character setup
- [ ] Implement cape setup
- [ ] Implement bone attachment logic
- [ ] Add test scenarios

### Testing & Validation

- [ ] Test with static character (T-pose)
- [ ] Test with animated character (idle, walk, run)
- [ ] Test with multiple bones (left/right shoulders)
- [ ] Test with different stiffness values
- [ ] Test with Long Range Attachment (AttachDistance > 0)
- [ ] Verify no stretching at attachment points
- [ ] Verify performance (bone transform overhead)
- [ ] Test with multiple cloth instances attached to same character

### Documentation

- [ ] Update [`cloth-attachment-test-actor-guide.md`](../docs/cloth-attachment-test-actor-guide.md)
- [ ] Add skeletal mesh attachment examples
- [ ] Document bone naming conventions
- [ ] Add troubleshooting section for bone attachments

---

## Technical Considerations

### Performance

**Bone Transform Overhead**:
- Each skeletal mesh may have 50-200 bones
- Only upload transforms for bones that have cloth attachments
- Cache bone transforms per frame (don't query multiple times)
- Use component deduplication (already implemented)

**Expected Cost**:
- Bone index lookup: ~0.01ms per attachment (one-time during binding)
- Bone transform query: ~0.001ms per bone per frame
- GPU upload: ~0.1ms for 100 bone transforms
- **Total overhead**: <0.5ms for typical character with cape

### Memory

**Additional Memory**:
- Bone index cache: 4 bytes per attachment
- Bone transform cache: 64 bytes per unique bone per frame
- **Total**: ~10KB for typical character with 100 bones

### Edge Cases

1. **Bone Not Found**
   - Validate bone name during binding
   - Log error and skip attachment
   - Don't crash or corrupt data

2. **Skeletal Mesh Destroyed**
   - Use `TWeakObjectPtr` for skeletal mesh references
   - Check validity before querying bone transforms
   - Fall back to identity transform if invalid

3. **Bone Index Changes**
   - Bone indices are stable within a skeletal mesh
   - If mesh is swapped, rebind attachments
   - Add validation in `SetSkeletalMesh()`

4. **Animation Disabled**
   - Bone transforms still valid (use bind pose)
   - Cloth will attach to static pose
   - No special handling needed

---

## Alternative Approaches

### Approach 1: Separate Bone Transform Buffer (Not Recommended)

Instead of packing bone transforms into `ComponentTransforms`, create a separate `BoneTransforms` buffer.

**Pros**:
- Cleaner separation of concerns
- Easier to debug

**Cons**:
- Requires shader changes
- More GPU memory
- More upload overhead
- Breaks existing deduplication system

**Verdict**: Not recommended. Reuse existing infrastructure.

### Approach 2: CPU-Side Bone Attachment (Not Recommended)

Compute bone-driven kinematic targets on CPU instead of GPU.

**Pros**:
- Simpler implementation
- No shader changes needed

**Cons**:
- Loses P1 optimization benefits (~2ms saved)
- Breaks batched architecture
- Requires per-instance updates

**Verdict**: Not recommended. Maintain GPU-based approach.

### Approach 3: Bone Transform Interpolation (Future Enhancement)

Interpolate between previous and current bone transforms for smoother motion.

**Pros**:
- Smoother cloth motion at low frame rates
- Reduces jitter

**Cons**:
- Adds complexity
- Requires storing previous transforms
- May not be necessary with high frame rates

**Verdict**: Consider for future optimization if needed.

---

## Success Criteria

### Functional Requirements

✅ **Bone Attachment Works**
- Cloth vertices can be attached to skeletal mesh bones
- Attachments follow bone animation correctly
- No visual artifacts (stretching, tearing, jitter)

✅ **API Complete**
- `BindAttachmentToBone()` fully implemented
- Bone validation and error handling
- Consistent with existing attachment API

✅ **Performance Acceptable**
- <0.5ms overhead for typical character
- No frame rate drops
- Scales with number of attachments, not total bones

### Test Requirements

✅ **Test Actor Works**
- Spawns successfully in PIE mode
- Cape attaches to character bones
- Animation plays correctly
- Cloth simulates naturally

✅ **Multiple Scenarios Tested**
- Static character
- Animated character
- Multiple bone attachments
- Different stiffness values

### Documentation Requirements

✅ **Implementation Documented**
- Code comments explain bone transform system
- Architecture decisions documented
- Performance characteristics noted

✅ **Usage Documented**
- Test actor usage guide
- Bone attachment examples
- Troubleshooting tips

---

## Timeline Estimate

**Phase 1: Bone Transform Infrastructure** - 2-3 hours
- Add USkeletalMeshComponent API
- Implement bone caching

**Phase 2: BindAttachmentToBone Implementation** - 1-2 hours
- Uncomment and complete function
- Add validation

**Phase 3: GPU Shader Enhancement** - 1 hour
- Verify Type 1 works for bones
- OR add Type 2 handling

**Phase 4: Bone Transform Upload System** - 2-3 hours
- Extend BuildKinematicAttachmentData
- Extend UpdateKinematicTargetsGPU

**Phase 5: Test Actor Implementation** - 2-3 hours
- Create test actor
- Implement attachment logic
- Test scenarios

**Total**: 8-12 hours

---

## Next Steps

1. **Review this plan** with the team
2. **Gather test assets** (character mesh, cape mesh, animations)
3. **Start with Phase 1** (bone transform infrastructure)
4. **Implement incrementally** and test each phase
5. **Create test actor** to verify functionality
6. **Document findings** and update this plan as needed

---

## References

- [`UClothComponent::BindAttachmentToBone()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp:373) - Function to implement
- [`ClothComputeKinematicTargets.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothComputeKinematicTargets.hlsl) - GPU kinematic shader
- [`FClothBatchManager::BuildKinematicAttachmentData()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:866) - Attachment data builder
- [`FClothBatchManager::UpdateKinematicTargetsGPU()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:997) - Transform updater
- [`EClothAttachmentType`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:236) - Attachment type enum
- [`cloth-attachment-test-actor-guide.md`](../docs/cloth-attachment-test-actor-guide.md) - Existing test actor guide
