# Cloth Asset Regeneration Issues - Detailed Fix Implementation Plan

## Overview

This document provides specific, actionable implementation steps to fix the two critical issues identified in [`cloth-asset-regeneration-issues-diagnosis.md`](cloth-asset-regeneration-issues-diagnosis.md):

1. **Render Mesh Buffer Corruption** - Buffer reallocation destroys previous instance data
2. **Simulation Mesh Parameter Interference** - Regeneration affects existing ClothAssets

## Fix 1: Render Mesh Buffer Preservation During Reallocation

### Problem Location
[`FClothBatchedSolver::UploadRenderMeshData()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:1684) at lines 1709-1736

### Current Buggy Code
```cpp
if (needsAllocation || needsReallocation)
{
    // Release old buffers if reallocating
    if (needsReallocation)
    {
        SAFE_RELEASE(UnifiedRenderVertexBuffer);  // ❌ DESTROYS ALL DATA
        SAFE_RELEASE(UnifiedRenderIndexBuffer);   // ❌ DESTROYS ALL DATA
        SAFE_RELEASE(UnifiedSkinningWeightBuffer);
        SAFE_RELEASE(UnifiedTriangleSkinningWeightBuffer);
        SAFE_RELEASE(UnifiedRenderVertexSRV);
        SAFE_RELEASE(UnifiedRenderIndexSRV);
        SAFE_RELEASE(SkinningWeightsSRV);
        SAFE_RELEASE(TriangleSkinningWeightsSRV);
        
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Reallocating render buffers..."));
    }
    
    // Allocate with growth factor
    uint32 newVertexCapacity = FMath::Max(requiredVertexCapacity, AllocatedRenderVertexCapacity) * 2;
    uint32 newIndexCapacity = FMath::Max(requiredIndexCapacity, AllocatedRenderIndexCapacity) * 2;
    
    if (!AllocateRenderBuffers(newVertexCapacity, newIndexCapacity))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to allocate render buffers"));
        return;
    }
}
```

### Fixed Code - Step 1: Preserve Old Buffer References

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)  
**Location**: Lines 1709-1736 in `UploadRenderMeshData()`

```cpp
if (needsAllocation || needsReallocation)
{
    // STEP 1: Store old buffer references and capacities BEFORE releasing
    ID3D11Buffer* oldVertexBuffer = nullptr;
    ID3D11Buffer* oldIndexBuffer = nullptr;
    ID3D11Buffer* oldSkinningBuffer = nullptr;
    ID3D11Buffer* oldTriangleSkinningBuffer = nullptr;
    uint32 oldVertexCapacity = 0;
    uint32 oldIndexCapacity = 0;
    
    if (needsReallocation)
    {
        // Save old buffer pointers (don't release yet!)
        oldVertexBuffer = UnifiedRenderVertexBuffer;
        oldIndexBuffer = UnifiedRenderIndexBuffer;
        oldSkinningBuffer = UnifiedSkinningWeightBuffer;
        oldTriangleSkinningBuffer = UnifiedTriangleSkinningWeightBuffer;
        oldVertexCapacity = AllocatedRenderVertexCapacity;
        oldIndexCapacity = AllocatedRenderIndexCapacity;
        
        // Release SRVs (they'll be recreated)
        SAFE_RELEASE(UnifiedRenderVertexSRV);
        SAFE_RELEASE(UnifiedRenderIndexSRV);
        SAFE_RELEASE(SkinningWeightsSRV);
        SAFE_RELEASE(TriangleSkinningWeightsSRV);
        
        // Clear buffer pointers (but don't release buffers yet)
        UnifiedRenderVertexBuffer = nullptr;
        UnifiedRenderIndexBuffer = nullptr;
        UnifiedSkinningWeightBuffer = nullptr;
        UnifiedTriangleSkinningWeightBuffer = nullptr;
        
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Reallocating render buffers - Old: V=%u I=%u, Required: V=%u I=%u"),
               oldVertexCapacity, oldIndexCapacity, requiredVertexCapacity, requiredIndexCapacity);
    }
    
    // STEP 2: Allocate new larger buffers
    uint32 newVertexCapacity = FMath::Max(requiredVertexCapacity, AllocatedRenderVertexCapacity) * 2;
    uint32 newIndexCapacity = FMath::Max(requiredIndexCapacity, AllocatedRenderIndexCapacity) * 2;
    
    if (!AllocateRenderBuffers(newVertexCapacity, newIndexCapacity))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to allocate render buffers"));
        
        // Restore old buffers on failure
        if (needsReallocation)
        {
            UnifiedRenderVertexBuffer = oldVertexBuffer;
            UnifiedRenderIndexBuffer = oldIndexBuffer;
            UnifiedSkinningWeightBuffer = oldSkinningBuffer;
            UnifiedTriangleSkinningWeightBuffer = oldTriangleSkinningBuffer;
            AllocatedRenderVertexCapacity = oldVertexCapacity;
            AllocatedRenderIndexCapacity = oldIndexCapacity;
        }
        return;
    }
    
    // STEP 3: Copy old data to new buffers (if reallocating)
    if (needsReallocation && oldVertexBuffer && oldIndexBuffer)
    {
        // Copy vertex buffer data
        if (oldVertexCapacity > 0)
        {
            D3D11_BOX srcBox;
            srcBox.left = 0;
            srcBox.right = oldVertexCapacity * sizeof(FClothRenderVertex);
            srcBox.top = 0;
            srcBox.bottom = 1;
            srcBox.front = 0;
            srcBox.back = 1;
            
            Graphics->DeviceContext->CopySubresourceRegion(
                UnifiedRenderVertexBuffer,  // Destination (new buffer)
                0,                          // Dest subresource
                0, 0, 0,                    // Dest X, Y, Z
                oldVertexBuffer,            // Source (old buffer)
                0,                          // Source subresource
                &srcBox                     // Source box
            );
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u render vertices from old buffer"),
                   oldVertexCapacity);
        }
        
        // Copy index buffer data
        if (oldIndexCapacity > 0)
        {
            D3D11_BOX srcBox;
            srcBox.left = 0;
            srcBox.right = oldIndexCapacity * sizeof(uint32);
            srcBox.top = 0;
            srcBox.bottom = 1;
            srcBox.front = 0;
            srcBox.back = 1;
            
            Graphics->DeviceContext->CopySubresourceRegion(
                UnifiedRenderIndexBuffer,   // Destination (new buffer)
                0,                          // Dest subresource
                0, 0, 0,                    // Dest X, Y, Z
                oldIndexBuffer,             // Source (old buffer)
                0,                          // Source subresource
                &srcBox                     // Source box
            );
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u render indices from old buffer"),
                   oldIndexCapacity);
        }
        
        // Copy skinning weight buffer data (legacy K-nearest neighbor)
        if (oldSkinningBuffer && oldVertexCapacity > 0)
        {
            D3D11_BOX srcBox;
            srcBox.left = 0;
            srcBox.right = oldVertexCapacity * sizeof(FClothSkinningWeightGPU);
            srcBox.top = 0;
            srcBox.bottom = 1;
            srcBox.front = 0;
            srcBox.back = 1;
            
            Graphics->DeviceContext->CopySubresourceRegion(
                UnifiedSkinningWeightBuffer,
                0,
                0, 0, 0,
                oldSkinningBuffer,
                0,
                &srcBox
            );
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u legacy skinning weights from old buffer"),
                   oldVertexCapacity);
        }
        
        // Copy triangle skinning weight buffer data
        if (oldTriangleSkinningBuffer && oldVertexCapacity > 0)
        {
            D3D11_BOX srcBox;
            srcBox.left = 0;
            srcBox.right = oldVertexCapacity * sizeof(FClothSkinningWeightTriangleGPU);
            srcBox.top = 0;
            srcBox.bottom = 1;
            srcBox.front = 0;
            srcBox.back = 1;
            
            Graphics->DeviceContext->CopySubresourceRegion(
                UnifiedTriangleSkinningWeightBuffer,
                0,
                0, 0, 0,
                oldTriangleSkinningBuffer,
                0,
                &srcBox
            );
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u triangle skinning weights from old buffer"),
                   oldVertexCapacity);
        }
        
        // STEP 4: Now safe to release old buffers
        SAFE_RELEASE(oldVertexBuffer);
        SAFE_RELEASE(oldIndexBuffer);
        SAFE_RELEASE(oldSkinningBuffer);
        SAFE_RELEASE(oldTriangleSkinningBuffer);
        
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Buffer reallocation complete - New capacity: V=%u I=%u"),
               newVertexCapacity, newIndexCapacity);
    }
}
```

### Key Changes Explained

1. **Store old buffer pointers** before releasing them
2. **Allocate new buffers** with larger capacity
3. **Copy old data** using `CopySubresourceRegion()` - GPU-accelerated copy
4. **Release old buffers** only after successful copy
5. **Error handling** - restore old buffers if allocation fails

### Testing This Fix

```cpp
// Test Case: Sequential asset generation
UClothMeshComponent* Cloth1 = CreateCloth();
Cloth1->GenerateClothAsset();  // Allocates initial buffer

UClothMeshComponent* Cloth2 = CreateCloth();
Cloth2->GenerateClothAsset();  // May trigger reallocation

// Verify Cloth1 still renders correctly after Cloth2's reallocation
ASSERT(Cloth1->GetRenderData().UnifiedRenderVertexBuffer != nullptr);
ASSERT(Cloth1->IsRenderingCorrectly());
```

## Fix 2: Proper Cleanup During Asset Regeneration

### Problem Location
[`UClothMeshComponent::GenerateClothAsset()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp:166)

### Current Buggy Code
```cpp
void UClothMeshComponent::GenerateClothAsset()
{
    // Validate setup
    FString errorMessage;
    if (!ValidateSetup(errorMessage))
    {
        LastErrorMessage = "Validation failed: " + errorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Starting cloth asset generation..."));

    // Build generation parameters
    FClothAssetGenerationParams params = BuildGenerationParams();

    // Generate asset
    FClothAssetGenerationResult result;
    bool success = FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
        SourceStaticMesh,
        params,
        result
    );

    if (success)
    {
        // Store generated asset
        if (GeneratedClothAsset)
        {
            GeneratedClothAsset = nullptr;  // ❌ Just nulls pointer
        }

        GeneratedClothAsset = result.Asset;
        // ... material setup ...

        // ❌ WRONG ORDER: Cleanup happens AFTER asset is created
        this->UnregisterFromClothWorld();
        this->RegisterWithClothWorld();
    }
}
```

### Fixed Code - Step 2: Cleanup Before Regeneration

**File**: [`ClothMeshComponent.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp)  
**Location**: Lines 166-241 in `GenerateClothAsset()`

```cpp
void UClothMeshComponent::GenerateClothAsset()
{
    // STEP 1: Unregister old instance FIRST (before generating new asset)
    bool bWasRegistered = false;
    if (bAssetGenerated && GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Unregistering old cloth instance before regeneration"));
        
        // Check if currently registered
        bWasRegistered = (ClothInstanceHandle != nullptr);
        
        // Unregister from simulation (removes from unified buffers)
        UnregisterFromClothWorld();
        
        // Ensure cleanup completes (flush any pending GPU operations)
        if (Graphics && Graphics->DeviceContext)
        {
            Graphics->DeviceContext->Flush();
        }
        
        // Clear old asset reference
        GeneratedClothAsset = nullptr;
        bAssetGenerated = false;
        
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Old cloth instance unregistered successfully"));
    }
    
    // STEP 2: Validate setup
    FString errorMessage;
    if (!ValidateSetup(errorMessage))
    {
        LastErrorMessage = "Validation failed: " + errorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Starting cloth asset generation..."));

    // STEP 3: Build generation parameters from current component settings
    FClothAssetGenerationParams params = BuildGenerationParams();

    // STEP 4: Generate new asset
    FClothAssetGenerationResult result;
    bool success = FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
        SourceStaticMesh,
        params,
        result
    );

    if (success)
    {
        // STEP 5: Store new generated asset
        GeneratedClothAsset = result.Asset;
        GeneratedClothAsset->SourceMeshName = SourceStaticMesh->GetRenderData()->ObjectName;
        bAssetGenerated = true;

        // STEP 6: Setup materials
        Materials.Empty();
        if (SourceStaticMesh)
        {
            const TArray<FStaticMaterial*>& sourceMaterials = SourceStaticMesh->GetMaterials();
            Materials.Reserve(sourceMaterials.Num());
            
            for (FStaticMaterial* staticMat : sourceMaterials)
            {
                if (staticMat && staticMat->Material)
                {
                    Materials.Add(staticMat->Material);
                }
                else
                {
                    Materials.Add(nullptr);
                }
            }
            
            UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Extracted %d materials from SourceStaticMesh"),
                   Materials.Num());
        }

        // STEP 7: Register new instance with ClothWorld
        // This uploads new simulation and render data to unified buffers
        RegisterWithClothWorld();
        
        // Update status
        LastErrorMessage = "";
        
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Asset generation and registration complete"));
    }
    else
    {
        // Generation failed
        bAssetGenerated = false;
        LastErrorMessage = "Generation failed: " + result.ErrorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
    }
}
```

### Key Changes Explained

1. **Unregister BEFORE generation** - Ensures old data is removed from unified buffers
2. **Flush GPU operations** - Ensures cleanup completes before new allocation
3. **Clear old asset** - Prevents stale references
4. **Generate new asset** - With current component parameters
5. **Register new instance** - Uploads fresh data to unified buffers

### Additional Fix: Improve UnregisterFromClothWorld

**File**: [`ClothMeshComponent.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp)  
**Location**: Find `UnregisterFromClothWorld()` method

Add validation to ensure complete cleanup:

```cpp
void UClothMeshComponent::UnregisterFromClothWorld()
{
    if (!ClothInstanceHandle)
    {
        UE_LOG(ELogLevel::Verbose, TEXT("ClothMeshComponent: Not registered, skipping unregister"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Unregistering from ClothWorld"));

    // Get batch manager before removing instance
    FClothBatchManager* batchManager = ClothInstanceHandle->GetBatchManager();
    
    // Remove instance from batch
    if (batchManager)
    {
        batchManager->RemoveInstance(ClothInstanceHandle);
        
        // CRITICAL: Ensure instance metadata is invalidated
        // This prevents stale data access
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Instance removed from batch manager"));
    }

    // Delete instance handle
    delete ClothInstanceHandle;
    ClothInstanceHandle = nullptr;
    
    // Clear simulation state
    bSimulate = false;
    
    UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Unregister complete"));
}
```

### Testing This Fix

```cpp
// Test Case: Regeneration with different parameters
UClothMeshComponent* Cloth = CreateCloth();

// Generate initial asset
Cloth->SimulationMeshReductionRatio = 0.1f;
Cloth->GenerateClothAsset();
uint32 originalVertexCount = Cloth->GetSimulationVertexCount();

// Regenerate with different parameters
Cloth->SimulationMeshReductionRatio = 0.2f;
Cloth->GenerateClothAsset();
uint32 newVertexCount = Cloth->GetSimulationVertexCount();

// Verify new simulation mesh has different vertex count
ASSERT(newVertexCount != originalVertexCount);
ASSERT(Cloth->IsSimulatingCorrectly());
```

## Fix 3: Improve RemoveInstance in ClothBatchManager

### Problem Location
[`FClothBatchManager::RemoveInstance()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:608)

### Enhancement: Add Metadata Invalidation

**File**: [`ClothBatchManager.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp)  
**Location**: Lines 608-646 in `RemoveInstance()`

Add after line 635 (after `InstanceToMetadataIndex.Remove(Instance)`):

```cpp
// Invalidate metadata to prevent stale access
FClothInstanceMetadata& metadata = InstanceMetadata[metadataIndex];
metadata.bIsActive = false;
metadata.ParticleCount = 0;
metadata.ConstraintCount = 0;
metadata.BendConstraintCount = 0;
metadata.KinematicTargetCount = 0;
metadata.TriangleCount = 0;
metadata.AreaConstraintCount = 0;
metadata.EdgeCollisionCount = 0;

// Mark metadata slot as free for reuse
metadata.ParticleOffset = 0xFFFFFFFF;  // Invalid offset marker
metadata.ConstraintOffset = 0xFFFFFFFF;
metadata.RenderVertexOffset = 0xFFFFFFFF;
metadata.RenderIndexOffset = 0xFFFFFFFF;

UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Invalidated metadata at index %d"),
       static_cast<int32>(LODLevel), metadataIndex);
```

## Implementation Checklist

### Phase 1: Render Mesh Buffer Preservation (Critical)

- [ ] **File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)
  - [ ] Modify `UploadRenderMeshData()` at lines 1709-1736
  - [ ] Add old buffer pointer storage
  - [ ] Add `CopySubresourceRegion()` calls for all buffers
  - [ ] Add error handling and rollback logic
  - [ ] Add detailed logging for debugging

### Phase 2: Proper Cleanup During Regeneration (Critical)

- [ ] **File**: [`ClothMeshComponent.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp)
  - [ ] Modify `GenerateClothAsset()` at lines 166-241
  - [ ] Move `UnregisterFromClothWorld()` before asset generation
  - [ ] Add GPU flush after unregister
  - [ ] Improve `UnregisterFromClothWorld()` with validation
  - [ ] Add state tracking for registration status

### Phase 3: Batch Manager Improvements (Enhancement)

- [ ] **File**: [`ClothBatchManager.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp)
  - [ ] Enhance `RemoveInstance()` at lines 608-646
  - [ ] Add metadata invalidation
  - [ ] Add invalid offset markers
  - [ ] Add validation checks in `AddInstance()`

## Testing Plan

### Test 1: Sequential Asset Generation
```cpp
void TestSequentialGeneration()
{
    // Create multiple cloth components
    TArray<UClothMeshComponent*> Cloths;
    for (int i = 0; i < 5; ++i)
    {
        auto* Cloth = CreateClothComponent();
        Cloth->GenerateClothAsset();
        Cloths.Add(Cloth);
    }
    
    // Verify all render correctly
    for (auto* Cloth : Cloths)
    {
        ASSERT(Cloth->IsRenderingCorrectly());
        ASSERT(Cloth->GetRenderData().UnifiedRenderVertexBuffer != nullptr);
    }
}
```

### Test 2: Parameter Change Regeneration
```cpp
void TestParameterChangeRegeneration()
{
    auto* Cloth1 = CreateClothComponent();
    Cloth1->SimulationMeshReductionRatio = 0.1f;
    Cloth1->GenerateClothAsset();
    
    auto originalSimMesh = Cloth1->GetSimulationMeshCopy();
    
    auto* Cloth2 = CreateClothComponent();
    Cloth2->SimulationMeshReductionRatio = 0.2f;
    Cloth2->GenerateClothAsset();
    
    // Verify Cloth1 unchanged
    auto currentSimMesh = Cloth1->GetSimulationMeshCopy();
    ASSERT(originalSimMesh.Equals(currentSimMesh));
}
```

### Test 3: Same Component Regeneration
```cpp
void TestSameComponentRegeneration()
{
    auto* Cloth = CreateClothComponent();
    
    // Generate with ratio 0.1
    Cloth->SimulationMeshReductionRatio = 0.1f;
    Cloth->GenerateClothAsset();
    uint32 vertexCount1 = Cloth->GetSimulationVertexCount();
    
    // Regenerate with ratio 0.2
    Cloth->SimulationMeshReductionRatio = 0.2f;
    Cloth->GenerateClothAsset();
    uint32 vertexCount2 = Cloth->GetSimulationVertexCount();
    
    // Verify different vertex counts
    ASSERT(vertexCount1 != vertexCount2);
    ASSERT(Cloth->IsRenderingCorrectly());
    ASSERT(Cloth->IsSimulatingCorrectly());
}
```

### Test 4: Buffer Reallocation Stress Test
```cpp
void TestBufferReallocationStress()
{
    TArray<UClothMeshComponent*> Cloths;
    
    // Create 20 cloths to force multiple reallocations
    for (int i = 0; i < 20; ++i)
    {
        auto* Cloth = CreateClothComponent();
        Cloth->GenerateClothAsset();
        Cloths.Add(Cloth);
        
        // Verify all previous cloths still render
        for (int j = 0; j <= i; ++j)
        {
            ASSERT(Cloths[j]->IsRenderingCorrectly());
        }
    }
}
```

## Performance Impact Analysis

### Buffer Copy Operations
- **Operation**: `CopySubresourceRegion()` - GPU-accelerated
- **Frequency**: Only during buffer reallocation (rare)
- **Cost**: O(n) where n = existing vertex/index count
- **Mitigation**: Growth factor (2x) reduces reallocation frequency

### Memory Overhead
- **Peak Usage**: 2x buffer size during reallocation (temporary)
- **Duration**: < 1ms (GPU copy is fast)
- **Impact**: Negligible for typical cloth counts

### Expected Performance
- **No reallocation**: 0% overhead (no changes)
- **With reallocation**: < 1ms additional time
- **Overall impact**: < 0.1% in typical scenarios

## Validation Checklist

After implementing fixes, verify:

- [ ] Multiple ClothAssets can be generated sequentially
- [ ] All previously generated assets continue rendering correctly
- [ ] Changing `SimulationMeshReductionRatio` doesn't affect existing assets
- [ ] Regenerating the same component works correctly
- [ ] Buffer reallocations preserve all instance data
- [ ] No memory leaks (use memory profiler)
- [ ] No GPU resource leaks (use GPU debugger)
- [ ] Performance impact is minimal (< 5% overhead)
- [ ] Logging provides clear debugging information

## Rollback Plan

If issues occur after implementation:

1. **Immediate**: Revert changes to [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)
2. **Fallback**: Use per-instance buffers instead of unified buffers (less efficient but safer)
3. **Debug**: Enable verbose logging to identify specific failure point
4. **Isolate**: Test each fix independently to identify problematic change

## Success Criteria

✅ **Fix is successful when**:
1. All test cases pass
2. No render corruption occurs
3. No simulation mesh interference occurs
4. Performance impact is acceptable
5. Memory usage is stable
6. No crashes or GPU errors

## Next Steps

1. Implement Phase 1 (Render mesh buffer preservation)
2. Test Phase 1 thoroughly
3. Implement Phase 2 (Cleanup during regeneration)
4. Test Phase 2 thoroughly
5. Implement Phase 3 (Batch manager improvements)
6. Run full test suite
7. Performance profiling
8. Code review and merge
