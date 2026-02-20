# Cloth Attachment System Refactoring: Asset-Instance Separation

## Executive Summary

This document outlines the architecture for refactoring the cloth attachment system to properly separate **asset-level** (shared, reusable) data from **instance-level** (per-component, runtime) data. The core problem is that attachments and InvMass modifications are currently stored in the asset, preventing multiple instances from sharing the same asset with different attachment configurations.

---

## 1. Current Architecture Analysis

### 1.1 Current Data Flow

```
UClothAsset (Shared)
├── RestPositions (simulation mesh)
├── InvMasses (PROBLEM: modified when attachments bind)
├── AttachmentsData (PROBLEM: instance-specific targets)
└── AttachmentIndices

UClothComponent (Instance)
├── ClothAsset* (reference to shared asset)
├── ClothInstanceHandle* (batched mode handle)
└── Attachments (cached copy, not used effectively)

FClothBatchManager
├── BuildKinematicAttachmentData()
│   └── Reads asset->AttachmentsData directly
└── UploadParticleData()
    └── Uploads asset->InvMasses directly
```

### 1.2 Identified Problems

**Problem 1: Shared InvMass Corruption**
- Asset stores `InvMasses` array
- When attachment binds vertex → sets `InvMasses[vertex] = 0` in asset
- All instances sharing the asset now have that vertex kinematic
- Other instances cannot have different attachment configurations

**Problem 2: Instance-Specific Attachment Targets in Asset**
- `AttachmentsData` contains `WorldPosition`, `DriverComponent`, `DriverActor`
- These are instance-specific (each component attaches to different actors/bones)
- Stored in asset prevents runtime changes without modifying shared data

**Problem 3: No Runtime Attachment API**
- Cannot bind/unbind attachments during PIE without modifying asset
- Re-decimation clears attachments (correct), but no way to rebind per-instance

**Problem 4: Attachment Workflow Confusion**
- Attachments created AFTER decimation (correct)
- But stored in asset (incorrect for instance-specific data)

### 1.3 Current GPU Data Flow

```
Asset Generation:
  UClothAsset::InvMasses (base values)
  UClothAsset::AttachmentsData (targets)
         ↓
Component Registration:
  FClothBatchManager::AddInstance()
    → UploadParticleData(asset->InvMasses)  // Uploads to GPU
    → BuildKinematicAttachmentData()        // Reads asset->AttachmentsData
         ↓
GPU Buffers:
  UnifiedInvMassBuffer (shared across all instances)
  AttachmentDataBuffer (GPU-based kinematic targets)
         ↓
Simulation:
  Shaders read InvMass to skip kinematic particles (InvMass == 0)
```

---

## 2. Proposed Architecture: Asset-Instance Separation

### 2.1 Design Principles

1. **Asset = Reusable Template**: Stores which vertices CAN be attached + default parameters
2. **Instance = Runtime State**: Stores what THIS instance attaches to + runtime InvMass
3. **InvMass Isolation**: Each instance maintains independent InvMass copy
4. **Attachment Flexibility**: Runtime binding/unbinding without asset modification

### 2.2 Data Structure Redesign

#### 2.2.1 Asset-Level Data (Shared, Immutable at Runtime)

```cpp
// ClothAsset.h
class UClothAsset : public UObject
{
public:
    // UNCHANGED: Simulation mesh (shared)
    TArray<FVector> RestPositions;
    TArray<uint32> Indices;
    
    // CHANGED: Base InvMass (never modified, source of truth)
    TArray<float> BaseInvMasses;  // Renamed from InvMasses
    
    // NEW: Attachment capability metadata (which vertices CAN be attached)
    TArray<FClothAttachmentCapability> AttachmentCapabilities;
    
    // REMOVED: AttachmentsData (moved to instance)
    // REMOVED: AttachmentIndices (moved to instance)
    
    // Constraints (unchanged)
    TArray<FClothDistanceConstraint> DistanceConstraints;
    TArray<FClothBendConstraint> BendConstraints;
    TArray<FClothAreaConstraint> AreaConstraints;
    TArray<FClothEdgeCollisionConstraint> EdgeCollisions;
};

// NEW: Attachment capability (asset-level metadata)
struct FClothAttachmentCapability
{
    uint32 SimVertexIndex;           // Which simulation vertex can be attached
    float DefaultStiffness = 1.0f;   // Default constraint stiffness
    float DefaultAttachDistance = 0.0f; // 0 = hard kinematic, >0 = LRA
    FString DebugName;               // Optional: "LeftShoulder", "RightHip", etc.
    
    FClothAttachmentCapability()
        : SimVertexIndex(0), DefaultStiffness(1.0f), DefaultAttachDistance(0.0f)
    {}
};
```

#### 2.2.2 Instance-Level Data (Per-Component, Mutable)

```cpp
// ClothComponent.h
class UClothComponent : public USceneComponent
{
public:
    // Asset reference (unchanged)
    UClothAsset* ClothAsset;
    
    // NEW: Per-instance attachment bindings
    TArray<FClothAttachmentBinding> AttachmentBindings;
    
    // NEW: Runtime InvMass (copy of asset's BaseInvMasses, modified per-instance)
    TArray<float> RuntimeInvMasses;
    
    // API for runtime attachment management
    void BindAttachment(uint32 SimVertexIndex, const FClothAttachmentTarget& Target);
    void UnbindAttachment(uint32 SimVertexIndex);
    void ClearAllAttachments();
    void UpdateAttachmentTarget(uint32 SimVertexIndex, const FClothAttachmentTarget& NewTarget);
    
private:
    void InitializeRuntimeInvMasses(); // Called on asset load
    void UpdateRuntimeInvMass(uint32 VertexIndex, bool bIsAttached);
};

// NEW: Instance-specific attachment binding
struct FClothAttachmentBinding
{
    uint32 SimVertexIndex;              // Which vertex is attached
    FClothAttachmentTarget Target;      // What it's attached to (instance-specific)
    float Stiffness = 1.0f;             // Per-instance override
    float AttachDistance = 0.0f;        // Per-instance override
    bool bIsActive = true;              // Can disable without unbinding
    
    FClothAttachmentBinding()
        : SimVertexIndex(0), Stiffness(1.0f), AttachDistance(0.0f), bIsActive(true)
    {}
};

// NEW: Attachment target (instance-specific)
struct FClothAttachmentTarget
{
    EClothAttachmentType Type = EClothAttachmentType::WorldPosition;
    
    // Driver references (instance-specific)
    USceneComponent* DriverComponent = nullptr;
    AActor* DriverActor = nullptr;
    
    // Skeletal mesh attachment
    FName BoneName;
    int32 BoneIndex = -1;
    FTransform LocalOffset = FTransform::Identity;
    
    // World position (cached, auto-updated from driver)
    FVector WorldPosition = FVector::ZeroVector;
    
    FClothAttachmentTarget() {}
};
```

#### 2.2.3 Batch Manager Changes

```cpp
// ClothBatchManager.h
class FClothBatchManager
{
private:
    // NEW: Per-instance InvMass tracking
    struct FInstanceInvMassRange
    {
        uint32 Offset;  // Offset into unified InvMass buffer
        uint32 Count;   // Number of particles
    };
    TMap<FClothInstanceHandle*, FInstanceInvMassRange> InstanceInvMassRanges;
    
    // NEW: Per-instance attachment tracking
    TMap<FClothInstanceHandle*, TArray<FClothAttachmentBinding>> InstanceAttachments;
    
    void UpdateInstanceInvMass(FClothInstanceHandle* Instance);
    void UpdateInstanceAttachments(FClothInstanceHandle* Instance);
};
```

### 2.3 Data Flow Redesign

```
Asset Generation (Editor):
  UClothAsset::BaseInvMasses (immutable base values)
  UClothAsset::AttachmentCapabilities (which vertices CAN attach)
         ↓
Component Initialization:
  UClothComponent::InitializeRuntimeInvMasses()
    → RuntimeInvMasses = ClothAsset->BaseInvMasses.Copy()
         ↓
Runtime Attachment Binding (PIE/Game):
  UClothComponent::BindAttachment(vertexIdx, target)
    → AttachmentBindings.Add({vertexIdx, target})
    → RuntimeInvMasses[vertexIdx] = 0.0f  // Only affects THIS instance
    → MarkAttachmentsDirty()
         ↓
Batch Registration:
  FClothBatchManager::AddInstance(component)
    → UploadParticleData(component->RuntimeInvMasses)  // Per-instance InvMass
    → BuildKinematicAttachmentData(component->AttachmentBindings)
         ↓
GPU Buffers:
  UnifiedInvMassBuffer[instanceOffset..instanceOffset+count] = instance->RuntimeInvMasses
  AttachmentDataBuffer = per-instance attachment targets
         ↓
Runtime Updates:
  UClothComponent::UpdateAttachmentTarget()
    → Modifies AttachmentBindings
    → FClothBatchManager::UpdateInstanceAttachments()
    → GPU buffer update (only affected instance range)
```

---

## 3. Per-Instance InvMass Management

### 3.1 InvMass Lifecycle

```cpp
// Component initialization
void UClothComponent::SetClothAsset(UClothAsset* InAsset)
{
    ClothAsset = InAsset;
    InitializeRuntimeInvMasses();
}

void UClothComponent::InitializeRuntimeInvMasses()
{
    if (!ClothAsset) return;
    
    // Copy base InvMass from asset (never modified)
    RuntimeInvMasses = ClothAsset->BaseInvMasses;
    
    // Apply any pre-existing attachment bindings
    for (const FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.bIsActive)
        {
            RuntimeInvMasses[binding.SimVertexIndex] = 0.0f;
        }
    }
}

// Attachment binding
void UClothComponent::BindAttachment(uint32 SimVertexIndex, 
                                     const FClothAttachmentTarget& Target)
{
    // Validate vertex index
    if (SimVertexIndex >= RuntimeInvMasses.Num())
    {
        UE_LOG(Error, "Invalid vertex index for attachment");
        return;
    }
    
    // Check if already bound
    for (FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (binding.SimVertexIndex == SimVertexIndex)
        {
            // Update existing binding
            binding.Target = Target;
            binding.bIsActive = true;
            RuntimeInvMasses[SimVertexIndex] = 0.0f;
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
    for (const FClothAttachmentCapability& cap : ClothAsset->AttachmentCapabilities)
    {
        if (cap.SimVertexIndex == SimVertexIndex)
        {
            newBinding.Stiffness = cap.DefaultStiffness;
            newBinding.AttachDistance = cap.DefaultAttachDistance;
            break;
        }
    }
    
    AttachmentBindings.Add(newBinding);
    RuntimeInvMasses[SimVertexIndex] = 0.0f;
    MarkAttachmentsDirty();
}

// Attachment unbinding
void UClothComponent::UnbindAttachment(uint32 SimVertexIndex)
{
    for (int32 i = 0; i < AttachmentBindings.Num(); ++i)
    {
        if (AttachmentBindings[i].SimVertexIndex == SimVertexIndex)
        {
            AttachmentBindings.RemoveAt(i);
            
            // Restore base InvMass from asset
            RuntimeInvMasses[SimVertexIndex] = ClothAsset->BaseInvMasses[SimVertexIndex];
            MarkAttachmentsDirty();
            return;
        }
    }
}

void UClothComponent::MarkAttachmentsDirty()
{
    if (ClothInstanceHandle)
    {
        // Notify batch manager to update GPU buffers
        FClothBatchManager* batchMgr = ClothInstanceHandle->GetBatchManager();
        batchMgr->UpdateInstanceInvMass(ClothInstanceHandle);
        batchMgr->UpdateInstanceAttachments(ClothInstanceHandle);
    }
}
```

### 3.2 GPU Buffer Update Strategy

```cpp
// ClothBatchManager.cpp
void FClothBatchManager::UpdateInstanceInvMass(FClothInstanceHandle* Instance)
{
    UClothComponent* component = Instance->GetOwnerComponent();
    const FClothInstanceMetadata& metadata = Instance->GetMetadata();
    
    // Get instance's buffer range
    uint32 offset = metadata.ParticleOffset;
    uint32 count = metadata.ParticleCount;
    
    // Update only this instance's range in unified buffer
    D3D11_BOX destBox;
    destBox.left = offset * sizeof(float);
    destBox.right = (offset + count) * sizeof(float);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;
    
    Graphics->DeviceContext->UpdateSubresource(
        BatchedSolver->GetInvMassBuffer(),
        0,
        &destBox,
        component->RuntimeInvMasses.GetData(),
        0,
        0
    );
}

void FClothBatchManager::UpdateInstanceAttachments(FClothInstanceHandle* Instance)
{
    // Rebuild attachment data for this instance
    bAttachmentDataDirty = true;  // Triggers full rebuild in next update
    
    // Alternative: Incremental update (optimization for later)
    // - Track per-instance attachment ranges
    // - Update only affected range in AttachmentDataBuffer
}
```

---

## 4. Attachment Binding/Unbinding API

### 4.1 Public API Design

```cpp
// ClothComponent.h - Public API
class UClothComponent : public USceneComponent
{
public:
    // ===== Attachment Management API =====
    
    /**
     * Bind a simulation vertex to a world position
     * @param SimVertexIndex - Index into simulation mesh (NOT render mesh)
     * @param WorldPosition - Target position in world space
     * @param Stiffness - Constraint stiffness [0-1], default uses asset capability
     * @param AttachDistance - Max distance (0 = hard kinematic, >0 = LRA)
     */
    void BindAttachmentToWorldPosition(uint32 SimVertexIndex, 
                                       const FVector& WorldPosition,
                                       float Stiffness = -1.0f,
                                       float AttachDistance = 0.0f);
    
    /**
     * Bind a simulation vertex to a scene component
     * @param SimVertexIndex - Index into simulation mesh
     * @param TargetComponent - Component to follow
     * @param LocalOffset - Offset in component's local space
     */
    void BindAttachmentToComponent(uint32 SimVertexIndex,
                                   USceneComponent* TargetComponent,
                                   const FTransform& LocalOffset = FTransform::Identity,
                                   float Stiffness = -1.0f,
                                   float AttachDistance = 0.0f);
    
    /**
     * Bind a simulation vertex to a skeletal mesh bone
     * @param SimVertexIndex - Index into simulation mesh
     * @param SkeletalMesh - Target skeletal mesh component
     * @param BoneName - Name of bone to attach to
     * @param LocalOffset - Offset in bone's local space
     */
    void BindAttachmentToBone(uint32 SimVertexIndex,
                             USkeletalMeshComponent* SkeletalMesh,
                             FName BoneName,
                             const FTransform& LocalOffset = FTransform::Identity,
                             float Stiffness = -1.0f,
                             float AttachDistance = 0.0f);
    
    /**
     * Unbind a specific attachment
     * @param SimVertexIndex - Vertex to unbind
     * @return true if attachment was found and removed
     */
    bool UnbindAttachment(uint32 SimVertexIndex);
    
    /**
     * Clear all attachments for this instance
     */
    void ClearAllAttachments();
    
    /**
     * Update an existing attachment's target without unbinding
     * @param SimVertexIndex - Vertex to update
     * @param NewTarget - New target configuration
     * @return true if attachment was found and updated
     */
    bool UpdateAttachmentTarget(uint32 SimVertexIndex, 
                               const FClothAttachmentTarget& NewTarget);
    
    /**
     * Enable/disable an attachment without unbinding
     * @param SimVertexIndex - Vertex to enable/disable
     * @param bEnabled - true to enable, false to disable
     */
    void SetAttachmentEnabled(uint32 SimVertexIndex, bool bEnabled);
    
    /**
     * Query attachment state
     */
    bool IsVertexAttached(uint32 SimVertexIndex) const;
    int32 GetAttachmentCount() const { return AttachmentBindings.Num(); }
    const TArray<FClothAttachmentBinding>& GetAttachmentBindings() const 
    { 
        return AttachmentBindings; 
    }
    
    /**
     * Get attachment capabilities from asset (which vertices CAN be attached)
     */
    const TArray<FClothAttachmentCapability>& GetAttachmentCapabilities() const;
};
```

### 4.2 Usage Examples

```cpp
// Example 1: Attach cloth corners to world positions
void AClothActor::AttachClothCorners()
{
    UClothComponent* cloth = GetClothComponent();
    
    // Attach top-left corner to fixed world position
    cloth->BindAttachmentToWorldPosition(0, FVector(0, 0, 100));
    
    // Attach top-right corner to fixed world position
    cloth->BindAttachmentToWorldPosition(10, FVector(100, 0, 100));
}

// Example 2: Attach cloth to character skeleton
void ACharacter::AttachCapeToSkeleton()
{
    UClothComponent* cape = GetCapeComponent();
    USkeletalMeshComponent* mesh = GetMesh();
    
    // Attach cape vertices to spine bones
    cape->BindAttachmentToBone(0, mesh, "Spine3", FTransform::Identity);
    cape->BindAttachmentToBone(5, mesh, "LeftShoulder", FTransform::Identity);
    cape->BindAttachmentToBone(10, mesh, "RightShoulder", FTransform::Identity);
}

// Example 3: Runtime attachment changes
void AClothActor::ToggleAttachment()
{
    UClothComponent* cloth = GetClothComponent();
    
    if (cloth->IsVertexAttached(0))
    {
        cloth->UnbindAttachment(0);  // Release vertex
    }
    else
    {
        cloth->BindAttachmentToWorldPosition(0, FVector(0, 0, 100));  // Re-attach
    }
}

// Example 4: Soft attachment (LRA - Long Range Attachment)
void AClothActor::CreateSoftAttachment()
{
    UClothComponent* cloth = GetClothComponent();
    
    // Attach with max distance constraint (allows some movement)
    cloth->BindAttachmentToWorldPosition(
        0,                      // Vertex index
        FVector(0, 0, 100),    // Target position
        0.8f,                  // Stiffness (0.8 = slightly soft)
        10.0f                  // Max distance (10cm slack)
    );
}
```

---

## 5. GPU Buffer Update Strategy

### 5.1 Buffer Layout

```
Unified InvMass Buffer (per-particle):
┌─────────────────────────────────────────────────────┐
│ Instance 0 InvMass | Instance 1 InvMass | Instance 2 │
│ [0.1, 0.1, 0.0...] | [0.1, 0.1, 0.1...] | [0.1, ...] │
└─────────────────────────────────────────────────────┘
  ↑                    ↑                    ↑
  Offset=0            Offset=1000          Offset=2000
  Count=1000          Count=1000           Count=1000
  
  Note: Instance 0 has vertex 2 attached (InvMass=0.0)
        Instance 1 has no attachments (all InvMass=0.1)
        Instances are INDEPENDENT
```

### 5.2 Update Strategies

#### Strategy 1: Full Rebuild (Current, Simple)
```cpp
void FClothBatchManager::BuildKinematicAttachmentData()
{
    // Rebuild entire attachment buffer from all instances
    TArray<FKinematicAttachmentGPU> attachmentData;
    
    for (FClothInstanceHandle* handle : Instances)
    {
        UClothComponent* owner = handle->GetOwnerComponent();
        const TArray<FClothAttachmentBinding>& bindings = owner->GetAttachmentBindings();
        
        for (const FClothAttachmentBinding& binding : bindings)
        {
            if (!binding.bIsActive) continue;
            
            FKinematicAttachmentGPU gpuAttachment;
            gpuAttachment.ParticleIndex = metadata.ParticleOffset + binding.SimVertexIndex;
            gpuAttachment.TargetPosition = ResolveAttachmentTarget(binding.Target);
            // ... fill other fields
            
            attachmentData.Add(gpuAttachment);
        }
    }
    
    BatchedSolver->UploadAttachmentData(attachmentData);
    bAttachmentDataDirty = false;
}
```

#### Strategy 2: Incremental Update (Optimization)
```cpp
void FClothBatchManager::UpdateInstanceAttachments(FClothInstanceHandle* Instance)
{
    // Track per-instance attachment ranges
    FInstanceAttachmentRange& range = InstanceAttachmentRanges[Instance];
    
    // Update only this instance's range in GPU buffer
    TArray<FKinematicAttachmentGPU> instanceAttachments;
    BuildInstanceAttachmentData(Instance, instanceAttachments);
    
    // Partial buffer update
    D3D11_BOX destBox;
    destBox.left = range.Offset * sizeof(FKinematicAttachmentGPU);
    destBox.right = (range.Offset + instanceAttachments.Num()) * sizeof(FKinematicAttachmentGPU);
    // ... update only affected range
}
```

### 5.3 Update Triggers

```cpp
// When to update GPU buffers:

1. Instance Registration:
   - Upload initial RuntimeInvMasses
   - Upload initial AttachmentBindings

2. Attachment Bind/Unbind:
   - Update RuntimeInvMasses (single vertex)
   - Rebuild attachment data (full or incremental)

3. Attachment Target Change:
   - Update attachment data only (InvMass unchanged)

4. Per-Frame Updates:
   - Kinematic target positions (existing GPU-based system)
   - No InvMass updates needed (only changes on bind/unbind)
```

---

## 6. Migration Strategy

### 6.1 Asset Migration

```cpp
// Migrate existing assets to new format
void UClothAsset::MigrateToNewFormat()
{
    // 1. Rename InvMasses → BaseInvMasses
    BaseInvMasses = InvMasses;
    InvMasses.Empty();  // Clear old array
    
    // 2. Convert AttachmentsData → AttachmentCapabilities
    for (const FClothAttachmentData& oldAttachment : AttachmentsData)
    {
        FClothAttachmentCapability capability;
        capability.SimVertexIndex = oldAttachment.ClothVertexIndex;
        capability.DefaultStiffness = oldAttachment.Stiffness;
        capability.DefaultAttachDistance = oldAttachment.AttachDistance;
        AttachmentCapabilities.Add(capability);
    }
    
    // 3. Clear instance-specific data from asset
    AttachmentsData.Empty();
    AttachmentIndices.Empty();
    
    UE_LOG(Display, "Migrated ClothAsset to new format");
}
```

### 6.2 Component Migration

```cpp
// Migrate existing components to use new API
void UClothComponent::MigrateAttachments()
{
    if (!ClothAsset) return;
    
    // Initialize runtime InvMass
    InitializeRuntimeInvMasses();
    
    // If component had cached attachments, convert them
    if (Attachments.Num() > 0)
    {
        for (const FClothAttachmentData& oldAttachment : Attachments)
        {
            FClothAttachmentTarget target;
            target.Type = oldAttachment.Type;
            target.DriverComponent = oldAttachment.DriverComponent;
            target.DriverActor = oldAttachment.DriverActor;
            target.BoneName = oldAttachment.BoneName;
            target.BoneIndex = oldAttachment.BoneIndex;
            target.LocalOffset = oldAttachment.LocalOffset;
            target.WorldPosition = oldAttachment.WorldPosition;
            
            BindAttachment(oldAttachment.ClothVertexIndex, target);
        }
        
        Attachments.Empty();  // Clear old cache
    }
}
```

### 6.3 Backward Compatibility

**Decision: NO backward compatibility** (as specified in requirements)
- Separate branch for refactoring
- Clean break from old system
- Existing assets will need regeneration
- Simpler implementation without legacy code paths

---

## 7. Validation Strategy

### 7.1 Test Cases

#### Test 1: Multiple Instances, Different Attachments
```cpp
void TestMultipleInstancesIndependentAttachments()
{
    // Setup: Create 3 instances of same asset
    UClothAsset* sharedAsset = CreateTestClothAsset();
    
    UClothComponent* instance1 = CreateClothComponent(sharedAsset);
    UClothComponent* instance2 = CreateClothComponent(sharedAsset);
    UClothComponent* instance3 = CreateClothComponent(sharedAsset);
    
    // Test: Attach different vertices on each instance
    instance1->BindAttachmentToWorldPosition(0, FVector(0, 0, 100));
    instance2->BindAttachmentToWorldPosition(5, FVector(50, 0, 100));
    instance3->BindAttachmentToWorldPosition(10, FVector(100, 0, 100));
    
    // Verify: Each instance has independent InvMass
    ASSERT_EQ(instance1->RuntimeInvMasses[0], 0.0f);   // Attached
    ASSERT_GT(instance1->RuntimeInvMasses[5], 0.0f);   // Not attached
    
    ASSERT_GT(instance2->RuntimeInvMasses[0], 0.0f);   // Not attached
    ASSERT_EQ(instance2->RuntimeInvMasses[5], 0.0f);   // Attached
    
    ASSERT_GT(instance3->RuntimeInvMasses[0], 0.0f);   // Not attached
    ASSERT_EQ(instance3->RuntimeInvMasses[10], 0.0f);  // Attached
    
    // Verify: Asset BaseInvMass unchanged
    for (float invMass : sharedAsset->BaseInvMasses)
    {
        ASSERT_GT(invMass, 0.0f);  // All non-zero (no attachments in asset)
    }
}
```

#### Test 2: Runtime Attachment Changes
```cpp
void TestRuntimeAttachmentChanges()
{
    UClothComponent* cloth = CreateClothComponent();
    
    // Initial state: No attachments
    ASSERT_FALSE(cloth->IsVertexAttached(0));
    ASSERT_GT(cloth->RuntimeInvMasses[0], 0.0f);
    
    // Bind attachment
    cloth->BindAttachmentToWorldPosition(0, FVector(0, 0, 100));
    ASSERT_TRUE(cloth->IsVertexAttached(0));
    ASSERT_EQ(cloth->RuntimeInvMasses[0], 0.0f);
    
    // Unbind attachment
    cloth->UnbindAttachment(0);
    ASSERT_FALSE(cloth->IsVertexAttached(0));
    ASSERT_EQ(cloth->RuntimeInvMasses[0], cloth->ClothAsset->BaseInvMasses[0]);
}
```

#### Test 3: InvMass Restoration
```cpp
void TestInvMassRestoration()
{
    UClothComponent* cloth = CreateClothComponent();
    float originalInvMass = cloth->RuntimeInvMasses[0];
    
    // Bind and unbind multiple times
    for (int i = 0; i < 10; ++i)
    {
        cloth->BindAttachmentToWorldPosition(0, FVector(0, 0, 100));
        ASSERT_EQ(cloth->RuntimeInvMasses[0], 0.0f);
        
        cloth->UnbindAttachment(0);
        ASSERT_EQ(cloth->RuntimeInvMasses[0], originalInvMass);
    }
}
```

#### Test 4: GPU Buffer Isolation
```cpp
void TestGPUBufferIsolation()
{
    // Create 2 instances
    UClothComponent* instance1 = CreateClothComponent();
    UClothComponent* instance2 = CreateClothComponent();
    
    // Attach vertex on instance1
    instance1->BindAttachmentToWorldPosition(0, FVector(0, 0, 100));
    
    // Simulate one frame
    ClothWorld->Update(0.016f);
    
    // Verify: Instance1 vertex 0 is kinematic (doesn't move)
    FVector pos1 = GetSimulatedPosition(instance1, 0);
    ASSERT_NEAR(pos1.Z, 100.0f, 0.1f);
    
    // Verify: Instance2 vertex 0 is dynamic (falls with gravity)
    FVector pos2 = GetSimulatedPosition(instance2, 0);
    ASSERT_LT(pos2.Z, 100.0f);  // Should have fallen
}
```

### 7.2 Visual Validation

```cpp
// Debug visualization for attachment testing
void UClothComponent::DebugDrawAttachments()
{
    for (const FClothAttachmentBinding& binding : AttachmentBindings)
    {
        if (!binding.bIsActive) continue;
        
        // Get vertex position
        FVector vertexPos = GetSimulatedVertexPosition(binding.SimVertexIndex);
        
        // Get target position
        FVector targetPos = ResolveAttachmentTarget(binding.Target);
        
        // Draw line from vertex to target
        DrawDebugLine(GetWorld(), vertexPos, targetPos, 
                     FColor::Green, false, -1.0f, 0, 2.0f);
        
        // Draw sphere at target
        DrawDebugSphere(GetWorld(), targetPos, 5.0f, 8, 
                       FColor::Red, false, -1.0f, 0, 1.0f);
    }
}
```

---

## 8. Implementation Plan

### Phase 1: Data Structure Refactoring
1. **Rename `UClothAsset::InvMasses` → `BaseInvMasses`**
   - Update all references in asset generation code
   - Update serialization

2. **Add `FClothAttachmentCapability` struct**
   - Define in [`ClothSimulationData.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:1)
   - Add serialization operator

3. **Add `UClothAsset::AttachmentCapabilities` array**
   - Update asset generation to populate capabilities
   - Remove `AttachmentsData` and `AttachmentIndices` from asset

4. **Add `UClothComponent::RuntimeInvMasses` array**
   - Add `InitializeRuntimeInvMasses()` method
   - Call on asset load/change

5. **Add `FClothAttachmentBinding` and `FClothAttachmentTarget` structs**
   - Define in [`ClothSimulationData.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:1)
   - Add to [`UClothComponent`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h:21)

### Phase 2: Attachment API Implementation
1. **Implement `UClothComponent::BindAttachment*()` methods**
   - `BindAttachmentToWorldPosition()`
   - `BindAttachmentToComponent()`
   - `BindAttachmentToBone()`

2. **Implement `UClothComponent::UnbindAttachment()`**
   - Remove binding from array
   - Restore base InvMass

3. **Implement `UClothComponent::UpdateAttachmentTarget()`**
   - Modify existing binding
   - Trigger GPU update

4. **Implement `UClothComponent::MarkAttachmentsDirty()`**
   - Notify batch manager of changes

### Phase 3: Batch Manager Integration
1. **Update `FClothBatchManager::AddInstance()`**
   - Upload `component->RuntimeInvMasses` instead of `asset->InvMasses`
   - Read `component->AttachmentBindings` instead of `asset->AttachmentsData`

2. **Implement `FClothBatchManager::UpdateInstanceInvMass()`**
   - Partial buffer update for single instance
   - Use D3D11_BOX for range update

3. **Update `FClothBatchManager::BuildKinematicAttachmentData()`**
   - Read from component bindings instead of asset
   - Support per-instance attachment targets

4. **Add `FClothBatchManager::UpdateInstanceAttachments()`**
   - Trigger attachment data rebuild
   - Optimize for incremental updates (optional)

### Phase 4: Asset Generation Updates
1. **Update `FClothAssetGenerator::GenerateClothAsset()`**
   - Generate `BaseInvMasses` (never modified)
   - Generate `AttachmentCapabilities` (metadata only)
   - Remove instance-specific attachment data

2. **Update asset serialization**
   - Serialize `BaseInvMasses` instead of `InvMasses`
   - Serialize `AttachmentCapabilities`
   - Remove `AttachmentsData` serialization

### Phase 5: Testing & Validation
1. **Implement test cases** (see Section 7.1)
2. **Add debug visualization** (see Section 7.2)
3. **Test multiple instances with different attachments**
4. **Test runtime attachment changes during PIE**
5. **Verify GPU buffer isolation**

### Phase 6: Documentation & Examples
1. **Update API documentation**
2. **Create usage examples** (see Section 4.2)
3. **Document migration process** (if needed)

---

## 9. Performance Considerations

### 9.1 Memory Impact

**Before (Current):**
```
Asset: InvMasses (N floats) + AttachmentsData (M attachments)
Component: Cached copy (unused)
GPU: Unified InvMass buffer (shared, corrupted)
```

**After (Proposed):**
```
Asset: BaseInvMasses (N floats) + AttachmentCapabilities (M capabilities)
Component: RuntimeInvMasses (N floats) + AttachmentBindings (M bindings)
GPU: Unified InvMass buffer (per-instance ranges, isolated)

Memory increase per instance: ~N floats + M bindings
For 1000-vertex cloth: ~4KB + attachment data
Acceptable for proper isolation
```

### 9.2 GPU Update Cost

**InvMass Updates:**
- Only on bind/unbind (rare)
- Partial buffer update (single instance range)
- Cost: ~0.01ms for 1000 vertices

**Attachment Data Updates:**
- On bind/unbind/target change
- Full rebuild (current) or incremental (optimized)
- Cost: ~0.1ms for 100 attachments (full rebuild)

**Per-Frame Cost:**
- No change (kinematic targets already GPU-based)
- InvMass buffer is read-only during simulation

### 9.3 Optimization Opportunities

1. **Incremental Attachment Updates**
   - Track per-instance attachment ranges
   - Update only affected range in GPU buffer
   - Reduces rebuild cost from O(total attachments) to O(instance attachments)

2. **Attachment Pooling**
   - Pre-allocate attachment buffer space per instance
   - Avoid full buffer reallocation on changes
   - Trade memory for update speed

3. **Lazy GPU Updates**
   - Batch multiple attachment changes
   - Update GPU once per frame (not per change)
   - Reduces API overhead

---

## 10. Alternative Designs Considered

### Alternative 1: Shared InvMass with Bitmask
**Idea:** Keep shared InvMass, use per-instance bitmask for attachments
```cpp
Asset: BaseInvMasses (shared)
Component: AttachmentMask (bitfield, which vertices are attached)
GPU: Compute InvMass on-the-fly (InvMass = mask[i] ? 0.0f : BaseInvMass[i])
```
**Rejected:** GPU overhead for per-particle conditional, complicates shaders

### Alternative 2: Attachment Layers
**Idea:** Asset defines attachment "slots", instances bind to slots
```cpp
Asset: AttachmentSlots[10] (predefined attachment points)
Component: SlotBindings[10] (what each slot attaches to)
```
**Rejected:** Less flexible, requires asset regeneration to add slots

### Alternative 3: Copy-on-Write InvMass
**Idea:** Share InvMass until first attachment, then copy
```cpp
Component: InvMassPtr (points to asset or local copy)
On first attachment: Copy asset InvMass, modify local copy
```
**Rejected:** Complexity, unclear ownership, harder to debug

**Selected Design:** Per-instance RuntimeInvMasses (simple, explicit, debuggable)

---

## 11. Summary

### Key Changes

| Component | Current | Proposed |
|-----------|---------|----------|
| **UClothAsset** | `InvMasses` (modified) | `BaseInvMasses` (immutable) |
| | `AttachmentsData` (instance-specific) | `AttachmentCapabilities` (metadata) |
| **UClothComponent** | `Attachments` (cached, unused) | `RuntimeInvMasses` (per-instance) |
| | | `AttachmentBindings` (per-instance) |
| **FClothBatchManager** | Reads asset data | Reads component data |
| **GPU Buffers** | Shared InvMass (corrupted) | Per-instance InvMass (isolated) |

### Benefits

✅ **Multiple instances share asset with different attachments**
✅ **Per-instance InvMass isolation** (no cross-contamination)
✅ **Runtime attachment changes** (bind/unbind during PIE)
✅ **Clean asset-instance separation** (asset = template, instance = state)
✅ **Backward compatible workflow** (attachments still created after decimation)

### Migration Path

1. Rename `InvMasses` → `BaseInvMasses` in asset
2. Convert `AttachmentsData` → `AttachmentCapabilities` (metadata only)
3. Add `RuntimeInvMasses` to component (copy of base)
4. Add `AttachmentBindings` to component (instance-specific targets)
5. Update batch manager to read from component instead of asset
6. Implement bind/unbind API for runtime changes

### Success Criteria

✅ Multiple instances of same asset with different attachment targets
✅ Each instance has independent InvMass state
✅ Runtime attachment target changes work without modifying asset
✅ Batched solver correctly processes per-instance attachments
✅ GPU buffer updates are efficient (partial updates)

---

## 12. Next Steps

1. **Review this architecture document** with team
2. **Approve design decisions** (especially per-instance InvMass approach)
3. **Create implementation branch** (separate from main)
4. **Implement Phase 1** (data structure refactoring)
5. **Test with simple case** (2 instances, different attachments)
6. **Iterate and refine** based on testing results

---

**Document Version:** 1.0  
**Author:** Roo (AI Architect)  
**Date:** 2026-02-19  
**Status:** Ready for Review
