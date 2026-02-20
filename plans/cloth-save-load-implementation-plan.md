# Cloth Save/Load Implementation Plan

## Overview

This plan outlines the implementation of save/load functionality for cloth simulation components in the custom engine. The goal is to enable complete scene persistence including cloth state, allowing developers to create test scenes and reduce iteration costs.

## Current Architecture Analysis

### Scene Manager Architecture

The [`SceneManager`](EngineSIU/EngineSIU/Engine/Source/Editor/UnrealEd/SceneManager.h) uses a JSON-based serialization system:

**Data Flow:**
```
UWorld → FSceneData → JSON String → File
File → JSON String → FSceneData → UWorld
```

**Key Structures:**
- [`FSceneData`](EngineSIU/EngineSIU/Engine/Source/Editor/UnrealEd/SceneManager.cpp:93-103) - Top-level scene container
- [`FActorSaveData`](EngineSIU/EngineSIU/Engine/Source/Editor/UnrealEd/SceneManager.cpp:79-91) - Per-actor data
- [`FComponentSaveData`](EngineSIU/EngineSIU/Engine/Source/Editor/UnrealEd/SceneManager.cpp:68-76) - Per-component data with property map

**Serialization Pattern:**
All components implement:
- `GetProperties(TMap<FString, FString>& OutProperties)` - Serialize to string map
- `SetProperties(const TMap<FString, FString>& InProperties)` - Deserialize from string map

### Cloth System Architecture

**Key Classes:**
1. **[`UClothComponent`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h:21-126)** - Base component for cloth simulation
   - Owns [`ClothAsset`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.h:21-148) reference
   - Owns [`FClothInstanceHandle`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothInstanceHandle.h:23-66) (batched mode)
   - Contains runtime data: `RuntimeInvMasses`, `AttachmentBindings`
   - Simulation state: `bIsSimulating`, `bUseBatchedMode`

2. **[`UClothAsset`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.h:21-148)** - Asset data (immutable template)
   - Mesh data: `RestPositions`, `Indices`, `RenderRestPositions`, etc.
   - Constraints: `DistanceConstraints`, `BendConstraints`, `AreaConstraints`
   - Attachment capabilities: `AttachmentCapabilities`
   - Already has `SaveToFile`/`LoadFromFile` methods

3. **[`FClothInstanceHandle`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothInstanceHandle.h:23-66)** - Runtime instance handle
   - Metadata: `FClothInstanceMetadata` (positions, velocities, etc.)
   - Parameters: `FClothInstanceParameters`
   - LOD state: `CurrentLOD`, `PendingLOD`

4. **[`FClothWorld`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothWorld.h:30-115)** - Per-world cloth manager
   - Manages all cloth instances
   - Owns batch managers per LOD

## Problem Analysis

### What Needs to be Saved

#### 1. Component-Level Data (Per-Instance)
- **Asset Reference**: Path to ClothAsset file
- **Simulation State**: `bIsSimulating`, `bUseBatchedMode`
- **Runtime InvMasses**: Per-instance modified masses
- **Attachment Bindings**: Instance-specific attachments with targets
- **Transform**: Inherited from `USceneComponent`

#### 2. Runtime Simulation State (Optional but Recommended)
- **Current Positions**: Deformed vertex positions
- **Velocities**: Current particle velocities
- **LOD State**: Current and pending LOD levels
- **Simulation Time**: Accumulated time for deterministic replay

#### 3. Attachment Data (Complex - Contains Pointers)
- **Attachment Bindings**: Array of `FClothAttachmentBinding`
  - `SimVertexIndex` (uint32) ✓ Simple
  - `Target.Type` (enum) ✓ Simple
  - `Target.DriverComponent` (USceneComponent*) ⚠️ **Pointer - needs ID mapping**
  - `Target.DriverActor` (AActor*) ⚠️ **Pointer - needs ID mapping**
  - `Target.BoneName` (FName) ✓ Simple
  - `Target.BoneIndex` (int32) ✓ Simple (can be recomputed)
  - `Target.LocalOffset` (FTransform) ✓ Simple
  - `Target.WorldPosition` (FVector) ✓ Simple
  - `Stiffness`, `AttachDistance`, `bIsActive` ✓ Simple

### Challenges

1. **Pointer Serialization**: Attachment targets reference other components/actors
2. **GPU State**: Simulation state lives on GPU (positions, velocities)
3. **Asset References**: Need to serialize asset file paths
4. **Batched Architecture**: Instance handles don't own GPU resources
5. **Reconstruction Order**: Components must be created before attachments can be resolved

## Design: Cloth Serialization System

### Serialization Data Structures

```cpp
// Component-level properties (stored in FComponentSaveData.Properties map)
{
    // Asset reference
    "ClothAssetPath": "Saved/ClothAsset1.clothasset",
    
    // Simulation state
    "bIsSimulating": "true",
    "bUseBatchedMode": "true",
    "CurrentLOD": "0",
    
    // Runtime InvMasses (serialized as comma-separated floats)
    "RuntimeInvMassesCount": "256",
    "RuntimeInvMasses": "1.0,1.0,0.0,0.0,1.0,...",
    
    // Attachment bindings count
    "AttachmentBindingsCount": "4",
    
    // Per-attachment data (indexed)
    "Attachment_0_VertexIndex": "0",
    "Attachment_0_Type": "SkeletalBone",
    "Attachment_0_DriverComponentID": "SkeletalMeshComponent_123",
    "Attachment_0_BoneName": "spine_01",
    "Attachment_0_LocalOffset": "X=0 Y=0 Z=0|P=0 Y=0 R=0|X=1 Y=1 Z=1",
    "Attachment_0_Stiffness": "1.0",
    "Attachment_0_AttachDistance": "0.0",
    "Attachment_0_bIsActive": "true",
    
    // Optional: Runtime simulation state
    "SaveSimulationState": "true",
    "CurrentPositionsCount": "256",
    "CurrentPositions": "X=1.0 Y=2.0 Z=3.0,X=4.0 Y=5.0 Z=6.0,...",
    "CurrentVelocitiesCount": "256",
    "CurrentVelocities": "X=0.1 Y=0.2 Z=0.3,X=0.4 Y=0.5 Z=0.6,...",
}
```

### Pointer Resolution Strategy

**Problem**: Attachment targets contain pointers to other components/actors

**Solution**: Use component/actor IDs (names) for serialization, resolve on load

**Pattern** (following existing SceneManager approach):
1. **Save**: Store component/actor name (ID) as string
2. **Load Phase 1**: Create all actors and components
3. **Load Phase 2**: Resolve attachment references by looking up IDs

**Implementation**:
```cpp
// Save: Convert pointer to ID
if (Target.DriverComponent)
{
    OutProperties.Add("Attachment_N_DriverComponentID", Target.DriverComponent->GetName());
}

// Load: Resolve ID to pointer (after all components created)
const FString* DriverID = InProperties.Find("Attachment_N_DriverComponentID");
if (DriverID)
{
    // Look up component in actor's component list
    UActorComponent* FoundComp = FindComponentByID(*DriverID);
    Target.DriverComponent = Cast<USceneComponent>(FoundComp);
}
```

### GPU State Handling

**Challenge**: Simulation state (positions, velocities) lives on GPU

**Options**:

**Option A: Save GPU State (Recommended for Test Scenes)**
- **Pros**: Exact state restoration, useful for debugging artifacts
- **Cons**: Larger file size, requires GPU readback
- **Use Case**: Creating test scenes to reproduce specific cloth states

**Option B: Reset Simulation on Load**
- **Pros**: Smaller file size, simpler implementation
- **Cons**: Cloth starts from rest pose, not preserved state
- **Use Case**: General scene saving where initial state is acceptable

**Recommendation**: Implement Option A with a flag to enable/disable

```cpp
// In ClothComponent
bool bSaveSimulationState = true; // Configurable

void GetProperties(TMap<FString, FString>& OutProperties)
{
    if (bSaveSimulationState && ClothInstanceHandle)
    {
        // Read back GPU state
        TArray<FVector> CurrentPositions;
        TArray<FVector> CurrentVelocities;
        ReadbackGPUState(CurrentPositions, CurrentVelocities);
        
        // Serialize
        SerializeVectorArray("CurrentPositions", CurrentPositions, OutProperties);
        SerializeVectorArray("CurrentVelocities", CurrentVelocities, OutProperties);
    }
}
```

## Implementation Plan

### Phase 1: Basic Component Serialization

**Goal**: Serialize ClothComponent without runtime state

#### Step 1.1: Implement ClothComponent::GetProperties
```cpp
void UClothComponent::GetProperties(TMap<FString, FString>& OutProperties) const
{
    Super::GetProperties(OutProperties);
    
    // Asset reference
    if (ClothAsset)
    {
        OutProperties.Add(TEXT("ClothAssetPath"), ClothAsset->GetAssetPath());
    }
    
    // Simulation state
    OutProperties.Add(TEXT("bIsSimulating"), bIsSimulating ? TEXT("true") : TEXT("false"));
    OutProperties.Add(TEXT("bUseBatchedMode"), bUseBatchedMode ? TEXT("true") : TEXT("false"));
    
    // LOD state
    if (ClothInstanceHandle)
    {
        OutProperties.Add(TEXT("CurrentLOD"), FString::FromInt((int32)ClothInstanceHandle->GetCurrentLOD()));
    }
}
```

#### Step 1.2: Implement ClothComponent::SetProperties
```cpp
void UClothComponent::SetProperties(const TMap<FString, FString>& InProperties)
{
    Super::SetProperties(InProperties);
    
    // Asset reference
    const FString* AssetPath = InProperties.Find(TEXT("ClothAssetPath"));
    if (AssetPath && !AssetPath->IsEmpty())
    {
        // Load asset from file
        UClothAsset* LoadedAsset = LoadClothAssetFromFile(*AssetPath);
        if (LoadedAsset)
        {
            SetClothAsset(LoadedAsset);
        }
    }
    
    // Simulation state
    const FString* SimStr = InProperties.Find(TEXT("bIsSimulating"));
    if (SimStr)
    {
        bool bShouldSimulate = (*SimStr == TEXT("true"));
        if (bShouldSimulate)
        {
            StartSimulation();
        }
    }
}
```

### Phase 2: Runtime InvMass Serialization

**Goal**: Preserve per-instance mass modifications

#### Step 2.1: Add RuntimeInvMasses to GetProperties
```cpp
// In GetProperties
if (RuntimeInvMasses.Num() > 0)
{
    OutProperties.Add(TEXT("RuntimeInvMassesCount"), FString::FromInt(RuntimeInvMasses.Num()));
    
    FString InvMassesStr;
    for (int32 i = 0; i < RuntimeInvMasses.Num(); ++i)
    {
        if (i > 0) InvMassesStr += TEXT(",");
        InvMassesStr += FString::SanitizeFloat(RuntimeInvMasses[i]);
    }
    OutProperties.Add(TEXT("RuntimeInvMasses"), InvMassesStr);
}
```

#### Step 2.2: Add RuntimeInvMasses to SetProperties
```cpp
// In SetProperties
const FString* CountStr = InProperties.Find(TEXT("RuntimeInvMassesCount"));
const FString* DataStr = InProperties.Find(TEXT("RuntimeInvMasses"));
if (CountStr && DataStr)
{
    int32 Count = FCString::Atoi(**CountStr);
    RuntimeInvMasses.SetNum(Count);
    
    TArray<FString> Values;
    DataStr->ParseIntoArray(Values, TEXT(","));
    
    for (int32 i = 0; i < FMath::Min(Count, Values.Num()); ++i)
    {
        RuntimeInvMasses[i] = FCString::Atof(*Values[i]);
    }
}
```

### Phase 3: Attachment Binding Serialization

**Goal**: Serialize attachment bindings with pointer resolution

#### Step 3.1: Add Attachment Serialization to GetProperties
```cpp
// In GetProperties
if (AttachmentBindings.Num() > 0)
{
    OutProperties.Add(TEXT("AttachmentBindingsCount"), FString::FromInt(AttachmentBindings.Num()));
    
    for (int32 i = 0; i < AttachmentBindings.Num(); ++i)
    {
        const FClothAttachmentBinding& Binding = AttachmentBindings[i];
        FString Prefix = FString::Printf(TEXT("Attachment_%d_"), i);
        
        // Vertex index
        OutProperties.Add(Prefix + TEXT("VertexIndex"), FString::FromInt(Binding.SimVertexIndex));
        
        // Target type
        OutProperties.Add(Prefix + TEXT("Type"), FString::FromInt((int32)Binding.Target.Type));
        
        // Driver references (convert pointers to IDs)
        if (Binding.Target.DriverComponent)
        {
            OutProperties.Add(Prefix + TEXT("DriverComponentID"), Binding.Target.DriverComponent->GetName());
        }
        if (Binding.Target.DriverActor)
        {
            OutProperties.Add(Prefix + TEXT("DriverActorID"), Binding.Target.DriverActor->GetName());
        }
        
        // Skeletal mesh data
        OutProperties.Add(Prefix + TEXT("BoneName"), Binding.Target.BoneName.ToString());
        OutProperties.Add(Prefix + TEXT("BoneIndex"), FString::FromInt(Binding.Target.BoneIndex));
        
        // Transform
        OutProperties.Add(Prefix + TEXT("LocalOffset"), Binding.Target.LocalOffset.ToString());
        OutProperties.Add(Prefix + TEXT("WorldPosition"), Binding.Target.WorldPosition.ToString());
        
        // Parameters
        OutProperties.Add(Prefix + TEXT("Stiffness"), FString::SanitizeFloat(Binding.Stiffness));
        OutProperties.Add(Prefix + TEXT("AttachDistance"), FString::SanitizeFloat(Binding.AttachDistance));
        OutProperties.Add(Prefix + TEXT("bIsActive"), Binding.bIsActive ? TEXT("true") : TEXT("false"));
    }
}
```

#### Step 3.2: Add Attachment Deserialization to SetProperties
```cpp
// In SetProperties (called AFTER all components are created)
const FString* BindingCountStr = InProperties.Find(TEXT("AttachmentBindingsCount"));
if (BindingCountStr)
{
    int32 Count = FCString::Atoi(**BindingCountStr);
    AttachmentBindings.Empty(Count);
    
    for (int32 i = 0; i < Count; ++i)
    {
        FString Prefix = FString::Printf(TEXT("Attachment_%d_"), i);
        FClothAttachmentBinding Binding;
        
        // Vertex index
        const FString* VertexStr = InProperties.Find(Prefix + TEXT("VertexIndex"));
        if (VertexStr) Binding.SimVertexIndex = FCString::Atoi(**VertexStr);
        
        // Target type
        const FString* TypeStr = InProperties.Find(Prefix + TEXT("Type"));
        if (TypeStr) Binding.Target.Type = (EClothAttachmentType)FCString::Atoi(**TypeStr);
        
        // Driver references (resolve IDs to pointers)
        const FString* DriverCompID = InProperties.Find(Prefix + TEXT("DriverComponentID"));
        if (DriverCompID && !DriverCompID->IsEmpty())
        {
            // Look up component in owner actor
            AActor* Owner = GetOwner();
            if (Owner)
            {
                UActorComponent* FoundComp = FindObject<UActorComponent>(Owner, **DriverCompID);
                Binding.Target.DriverComponent = Cast<USceneComponent>(FoundComp);
            }
        }
        
        // Skeletal mesh data
        const FString* BoneNameStr = InProperties.Find(Prefix + TEXT("BoneName"));
        if (BoneNameStr) Binding.Target.BoneName = FName(*(*BoneNameStr));
        
        const FString* BoneIndexStr = InProperties.Find(Prefix + TEXT("BoneIndex"));
        if (BoneIndexStr) Binding.Target.BoneIndex = FCString::Atoi(**BoneIndexStr);
        
        // Transform
        const FString* OffsetStr = InProperties.Find(Prefix + TEXT("LocalOffset"));
        if (OffsetStr) Binding.Target.LocalOffset.InitFromString(*OffsetStr);
        
        const FString* PosStr = InProperties.Find(Prefix + TEXT("WorldPosition"));
        if (PosStr) Binding.Target.WorldPosition.InitFromString(*PosStr);
        
        // Parameters
        const FString* StiffStr = InProperties.Find(Prefix + TEXT("Stiffness"));
        if (StiffStr) Binding.Stiffness = FCString::Atof(**StiffStr);
        
        const FString* DistStr = InProperties.Find(Prefix + TEXT("AttachDistance"));
        if (DistStr) Binding.AttachDistance = FCString::Atof(**DistStr);
        
        const FString* ActiveStr = InProperties.Find(Prefix + TEXT("bIsActive"));
        if (ActiveStr) Binding.bIsActive = (*ActiveStr == TEXT("true"));
        
        AttachmentBindings.Add(Binding);
    }
    
    // Mark attachments dirty to trigger GPU update
    MarkAttachmentsDirty();
}
```

### Phase 4: GPU State Serialization (Optional)

**Goal**: Save/restore current simulation state

#### Step 4.1: Add GPU Readback Helper
```cpp
// In ClothComponent or ClothBatchManager
bool ReadbackGPUState(TArray<FVector>& OutPositions, TArray<FVector>& OutVelocities)
{
    if (!ClothInstanceHandle)
        return false;
    
    FClothBatchManager* BatchManager = ClothInstanceHandle->GetBatchManager();
    if (!BatchManager)
        return false;
    
    // Read back from GPU buffers
    // This requires adding readback functionality to ClothBatchManager
    return BatchManager->ReadbackInstanceState(ClothInstanceHandle->GetMetadataIndex(), 
                                                OutPositions, OutVelocities);
}
```

#### Step 4.2: Add State Serialization to GetProperties
```cpp
// In GetProperties
const FString* SaveStateStr = InProperties.Find(TEXT("bSaveSimulationState"));
bool bSaveState = SaveStateStr ? (*SaveStateStr == TEXT("true")) : true; // Default: save state

if (bSaveState && ClothInstanceHandle)
{
    TArray<FVector> CurrentPositions;
    TArray<FVector> CurrentVelocities;
    
    if (ReadbackGPUState(CurrentPositions, CurrentVelocities))
    {
        OutProperties.Add(TEXT("SaveSimulationState"), TEXT("true"));
        SerializeVectorArray(TEXT("CurrentPositions"), CurrentPositions, OutProperties);
        SerializeVectorArray(TEXT("CurrentVelocities"), CurrentVelocities, OutProperties);
    }
}
```

#### Step 4.3: Add State Restoration to SetProperties
```cpp
// In SetProperties
const FString* HasStateStr = InProperties.Find(TEXT("SaveSimulationState"));
if (HasStateStr && *HasStateStr == TEXT("true"))
{
    TArray<FVector> CurrentPositions;
    TArray<FVector> CurrentVelocities;
    
    DeserializeVectorArray(TEXT("CurrentPositions"), InProperties, CurrentPositions);
    DeserializeVectorArray(TEXT("CurrentVelocities"), InProperties, CurrentVelocities);
    
    // Upload to GPU after instance is created
    if (ClothInstanceHandle && CurrentPositions.Num() > 0)
    {
        UploadGPUState(CurrentPositions, CurrentVelocities);
    }
}
```

### Phase 5: SceneManager Integration

**Goal**: Ensure ClothComponent is properly handled by SceneManager

#### Step 5.1: Verify Component Registration
- Ensure `UClothComponent` is registered in the class system
- Verify `UClass::FindClass(FName("UClothComponent"))` works

#### Step 5.2: Test Load Order
- Components are created in Phase 1 of `LoadWorldFromData`
- Properties are set immediately after creation
- Attachment resolution happens in same phase (components already exist)

#### Step 5.3: Handle Asset Loading
```cpp
// Add helper to load ClothAsset from file path
UClothAsset* LoadClothAssetFromFile(const FString& FilePath)
{
    // Check if asset is already loaded
    UClothAsset* ExistingAsset = FindAssetByPath(FilePath);
    if (ExistingAsset)
        return ExistingAsset;
    
    // Load from file
    UClothAsset* NewAsset = NewObject<UClothAsset>();
    if (NewAsset->LoadFromFile(FilePath))
    {
        RegisterAsset(FilePath, NewAsset);
        return NewAsset;
    }
    
    return nullptr;
}
```

## Testing Strategy

### Test 1: Simple Cloth Save/Load
**Setup:**
1. Create scene with single cloth component
2. Set cloth asset
3. Start simulation
4. Save scene

**Verify:**
1. Load scene
2. Cloth component exists
3. Cloth asset is loaded
4. Simulation can start

### Test 2: Attached Cloth Save/Load
**Setup:**
1. Create scene with cloth + skeletal mesh
2. Attach cloth to bones
3. Save scene

**Verify:**
1. Load scene
2. Attachment bindings are restored
3. Driver component references are resolved
4. Cloth follows skeletal mesh

### Test 3: Multiple Cloth Instances
**Setup:**
1. Create scene with 3+ cloth components
2. Different assets and configurations
3. Save scene

**Verify:**
1. Load scene
2. All cloth instances exist
3. Each has correct asset
4. No cross-contamination of state

### Test 4: Simulation State Preservation
**Setup:**
1. Create cloth scene
2. Simulate for several seconds
3. Save scene mid-simulation

**Verify:**
1. Load scene
2. Cloth positions match saved state
3. Velocities are preserved
4. Simulation continues smoothly

## Implementation Checklist

### Core Implementation
- [ ] Add `GetProperties` to `UClothComponent`
- [ ] Add `SetProperties` to `UClothComponent`
- [ ] Implement asset path serialization
- [ ] Implement RuntimeInvMasses serialization
- [ ] Implement AttachmentBindings serialization
- [ ] Implement pointer resolution for attachments
- [ ] Add GPU state readback (optional)
- [ ] Add GPU state upload (optional)

### Helper Functions
- [ ] `SerializeVectorArray` - Convert TArray<FVector> to string
- [ ] `DeserializeVectorArray` - Parse string to TArray<FVector>
- [ ] `LoadClothAssetFromFile` - Load asset by path
- [ ] `ReadbackGPUState` - Read positions/velocities from GPU
- [ ] `UploadGPUState` - Upload positions/velocities to GPU

### SceneManager Integration
- [ ] Verify ClothComponent class registration
- [ ] Test component creation order
- [ ] Test attachment resolution timing
- [ ] Add asset caching/management

### Testing
- [ ] Test simple cloth save/load
- [ ] Test attached cloth save/load
- [ ] Test multiple cloth instances
- [ ] Test simulation state preservation
- [ ] Test edge cases (missing assets, invalid references)

## Potential Issues and Solutions

### Issue 1: Attachment Resolution Timing
**Problem**: Attachments reference other components that may not exist yet

**Solution**: SceneManager already handles this - all components are created before properties are set

### Issue 2: GPU State Readback Performance
**Problem**: Reading back large cloth meshes from GPU is slow

**Solution**: 
- Make state saving optional (flag)
- Use async readback if available
- Only save state when explicitly requested

### Issue 3: Asset Path Management
**Problem**: Asset paths may be relative or absolute

**Solution**:
- Store relative paths from project root
- Resolve to absolute paths on load
- Add asset registry for loaded assets

### Issue 4: ClothWorld Initialization
**Problem**: ClothWorld may not be initialized when loading

**Solution**:
- Ensure ClothWorld is created before loading scene
- Defer simulation start until world is ready
- Add initialization checks in SetProperties

### Issue 5: Batched Mode State
**Problem**: Batched instances don't own GPU resources directly

**Solution**:
- Read state through BatchManager
- Store metadata index for later access
- Ensure batch manager is initialized before upload

## File Modifications Required

### Files to Modify
1. [`ClothComponent.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h) - Add GetProperties/SetProperties declarations
2. [`ClothComponent.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp) - Implement serialization
3. [`ClothBatchManager.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.h) - Add readback/upload methods (optional)
4. [`ClothBatchManager.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp) - Implement GPU state access (optional)

### Files to Reference
1. [`SceneManager.cpp`](EngineSIU/EngineSIU/Engine/Source/Editor/UnrealEd/SceneManager.cpp) - Understand serialization flow
2. [`StaticMeshComponent.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.cpp) - Reference implementation
3. [`SkeletalMeshComponent.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.cpp) - Complex component example

## Success Criteria

1. ✅ Cloth components can be saved to scene files
2. ✅ Cloth components can be loaded from scene files
3. ✅ Cloth assets are properly referenced and loaded
4. ✅ Attachment bindings are preserved and restored
5. ✅ Simulation state can be optionally preserved
6. ✅ Multiple cloth instances work correctly
7. ✅ No crashes or memory leaks
8. ✅ Test scenes can be created and reused

## Next Steps

After reviewing this plan:
1. Confirm the approach is acceptable
2. Prioritize which phases to implement first
3. Decide whether GPU state saving is required
4. Switch to Code mode to begin implementation
