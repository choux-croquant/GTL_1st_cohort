# Cloth Attachment System - Testing Guide

## Overview

This guide explains how to test the new cloth attachment system that properly separates asset-level and instance-level data.

**Implementation Document:** [`cloth-attachment-refactoring-implementation-complete.md`](cloth-attachment-refactoring-implementation-complete.md:1)

---

## Test Methods Added to TestBatchedClothActor

All test methods have been added to [`TestBatchedClothActor.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestBatchedClothActor.cpp:203).

### 1. TestAttachmentIndependence()

**Purpose:** Verify that multiple instances can have different attachments without affecting each other.

**What it tests:**
- Attaches different vertices on 3 different instances
- Verifies each instance has independent InvMass state
- Verifies asset BaseInvMasses remains unchanged

**Expected Results:**
```
Instance 1: Vertex 0 attached, InvMass[0]=0.0000
Instance 1: Vertex 5 free, InvMass[5]>0.0000
Instance 2: Vertex 0 free, InvMass[0]>0.0000
Instance 2: Vertex 5 attached, InvMass[5]=0.0000
Instance 3: Vertex 10 attached, InvMass[10]=0.0000
Asset BaseInvMass[0]>0.0000 (unchanged)
```

**How to run:**
```cpp
// In console or code
TestBatchedClothActor->TestAttachmentIndependence();
```

### 2. TestRuntimeAttachmentChanges()

**Purpose:** Verify runtime bind/unbind operations work correctly.

**What it tests:**
- Bind attachment → verify InvMass=0
- Unbind attachment → verify InvMass restored
- Multiple bind/unbind cycles → verify restoration
- Update target → verify attachment remains active

**Expected Results:**
```
Test 1: Binding attachment
  Original InvMass[0]=0.XXXX
  After bind: InvMass[0]=0.0000
  IsAttached=1

Test 2: Unbinding attachment
  After unbind: InvMass[0]=0.XXXX (restored)
  IsAttached=0

Test 3: Multiple cycles (10x)
  After 10 cycles: InvMass[0]=0.XXXX (still correct)

Test 4: Update target
  After target update: InvMass[0]=0.0000 (still attached)
  IsAttached=1
```

**How to run:**
```cpp
TestBatchedClothActor->TestRuntimeAttachmentChanges();
```

### 3. TestInvMassIsolation()

**Purpose:** Verify per-instance InvMass isolation when same vertex is attached on multiple instances.

**What it tests:**
- Attaches vertex 0 on 5 different instances
- Unbinds from instance 2 only
- Verifies other instances unaffected
- Verifies asset unchanged

**Expected Results:**
```
After attaching vertex 0 on 5 instances:
  Instance 0: InvMass[0]=0.0000
  Instance 1: InvMass[0]=0.0000
  Instance 2: InvMass[0]=0.0000
  Instance 3: InvMass[0]=0.0000
  Instance 4: InvMass[0]=0.0000

After unbinding instance 2:
  Instance 0: InvMass[0]=0.0000, Attached=1
  Instance 1: InvMass[0]=0.0000, Attached=1
  Instance 2: InvMass[0]=0.XXXX, Attached=0  ← ONLY THIS CHANGED
  Instance 3: InvMass[0]=0.0000, Attached=1
  Instance 4: InvMass[0]=0.0000, Attached=1

Asset BaseInvMass[0]=0.XXXX (unchanged)
```

**How to run:**
```cpp
TestBatchedClothActor->TestInvMassIsolation();
```

### 4. TestAttachmentPatterns()

**Purpose:** Demonstrate various attachment patterns on different instances.

**What it tests:**
- Pattern 1: Single corner (1 attachment per instance)
- Pattern 2: Two corners (2 attachments per instance)
- Pattern 3: Four corners (4 attachments per instance)
- Pattern 4: Soft attachment with LRA (slack constraint)

**Expected Results:**
```
Pattern 1: Single corner attachment (3 instances)
  Instance 0: Attached vertex 0, count=1
  Instance 1: Attached vertex 0, count=1
  Instance 2: Attached vertex 0, count=1

Pattern 2: Two corner attachment (3 instances)
  Instance 3: Attached vertices 0,10, count=2
  Instance 4: Attached vertices 0,10, count=2
  Instance 5: Attached vertices 0,10, count=2

Pattern 3: Four corner attachment (3 instances)
  Instance 6: Attached vertices 0,5,10,15, count=4
  Instance 7: Attached vertices 0,5,10,15, count=4
  Instance 8: Attached vertices 0,5,10,15, count=4

Pattern 4: Soft attachment with LRA (1 instance)
  Instance 9: Soft attachment (stiffness=0.8, distance=10.0)

Asset BaseInvMasses unchanged: YES
```

**How to run:**
```cpp
TestBatchedClothActor->TestAttachmentPatterns();
```

---

## Running the Tests

### Method 1: Console Commands

If console commands are set up:
```
TestAttachmentIndependence
TestRuntimeAttachmentChanges
TestInvMassIsolation
TestAttachmentPatterns
```

### Method 2: Code Integration

Add to `PostSpawnInitialize()`:
```cpp
void ATestBatchedClothActor::PostSpawnInitialize()
{
    // ... existing initialization code ...
    
    // Run attachment tests after cloth initialization
    if (bClothInitialized)
    {
        UE_LOG(ELogLevel::Display, TEXT("\n\n=== RUNNING ATTACHMENT SYSTEM TESTS ===\n"));
        
        TestAttachmentIndependence();
        TestRuntimeAttachmentChanges();
        TestInvMassIsolation();
        TestAttachmentPatterns();
        
        UE_LOG(ELogLevel::Display, TEXT("\n=== ALL ATTACHMENT TESTS COMPLETE ===\n\n"));
    }
}
```

### Method 3: Manual Testing

Call methods directly from game code:
```cpp
// Get test actor
ATestBatchedClothActor* testActor = FindTestActor();

// Run specific test
testActor->TestAttachmentIndependence();
```

---

## Visual Verification

### Expected Visual Behavior

**Pattern 1 (Single Corner):**
- Cloth hangs from one corner
- Other corners fall with gravity
- Cloth swings like a flag

**Pattern 2 (Two Corners):**
- Cloth hangs from two corners
- Forms a curtain-like shape
- Bottom edge falls freely

**Pattern 3 (Four Corners):**
- Cloth stretched between four points
- Forms a flat surface
- Center sags slightly

**Pattern 4 (Soft Attachment):**
- Cloth attached but with slack
- Can move up to 10cm from target
- Bounces back when stretched

### Debug Visualization

Add to test methods for visual debugging:
```cpp
// Draw attachment points
for (const FClothAttachmentBinding& binding : cloth->GetAttachmentBindings())
{
    if (!binding.bIsActive) continue;
    
    FVector targetPos = binding.Target.WorldPosition;
    DrawDebugSphere(GetWorld(), targetPos, 5.0f, 8, FColor::Red, false, 5.0f);
    
    // Draw line from vertex to target (requires vertex position access)
    // FVector vertexPos = GetSimulatedVertexPosition(binding.SimVertexIndex);
    // DrawDebugLine(GetWorld(), vertexPos, targetPos, FColor::Green, false, 5.0f);
}
```

---

## Validation Checklist

### Functional Tests

- [ ] **Test 1: Attachment Independence**
  - [ ] Instance 1 vertex 0 has InvMass=0
  - [ ] Instance 2 vertex 5 has InvMass=0
  - [ ] Other vertices have InvMass>0
  - [ ] Asset BaseInvMasses unchanged

- [ ] **Test 2: Runtime Changes**
  - [ ] Bind sets InvMass=0
  - [ ] Unbind restores original InvMass
  - [ ] Multiple cycles maintain correctness
  - [ ] Update target keeps attachment active

- [ ] **Test 3: InvMass Isolation**
  - [ ] All 5 instances have vertex 0 attached
  - [ ] Unbind from instance 2 only
  - [ ] Instances 0,1,3,4 still attached
  - [ ] Instance 2 InvMass restored
  - [ ] Asset unchanged

- [ ] **Test 4: Attachment Patterns**
  - [ ] Pattern 1: 1 attachment per instance
  - [ ] Pattern 2: 2 attachments per instance
  - [ ] Pattern 3: 4 attachments per instance
  - [ ] Pattern 4: Soft attachment with LRA
  - [ ] All patterns coexist independently

### Performance Tests

- [ ] **GPU Update Cost**
  - [ ] InvMass update <0.1ms per instance
  - [ ] Attachment rebuild <1ms for all instances
  - [ ] No frame rate impact during bind/unbind

- [ ] **Memory Usage**
  - [ ] RuntimeInvMasses: ~4KB per instance
  - [ ] AttachmentBindings: ~100 bytes per attachment
  - [ ] Total overhead acceptable

### Edge Cases

- [ ] **Null Checks**
  - [ ] Bind with null component → error logged
  - [ ] Bind with null asset → error logged
  - [ ] Unbind non-existent attachment → warning logged

- [ ] **Invalid Indices**
  - [ ] Bind with vertex index >= count → error logged
  - [ ] No crash or corruption

- [ ] **Missing Bones**
  - [ ] BindAttachmentToBone with invalid bone → error logged
  - [ ] No crash

---

## Expected Console Output

### Successful Test Run

```
=== RUNNING ATTACHMENT SYSTEM TESTS ===

=== Testing Attachment Independence ===
Instance 1: Vertex 0 attached, InvMass[0]=0.0000 (should be 0.0)
Instance 1: Vertex 5 free, InvMass[5]=0.0025 (should be >0.0)
Instance 2: Vertex 0 free, InvMass[0]=0.0025 (should be >0.0)
Instance 2: Vertex 5 attached, InvMass[5]=0.0000 (should be 0.0)
Instance 3: Vertex 10 attached, InvMass[10]=0.0000 (should be 0.0)
Asset BaseInvMass[0]=0.0025 (should be >0.0, unchanged)
Asset BaseInvMass[5]=0.0025 (should be >0.0, unchanged)
=== Attachment Independence Test Complete ===

=== Testing Runtime Attachment Changes ===
Test 1: Binding attachment to vertex 0
  Original InvMass[0]=0.0025
ClothComponent: Bound attachment for vertex 0 (Stiffness: 1.00, Distance: 0.00)
  After bind: InvMass[0]=0.0000 (should be 0.0)
  IsAttached=1 (should be 1)
Test 2: Unbinding attachment from vertex 0
ClothComponent: Unbound attachment for vertex 0
  After unbind: InvMass[0]=0.0025 (should be 0.0025)
  IsAttached=0 (should be 0)
Test 3: Multiple bind/unbind cycles (10x)
  After 10 cycles: InvMass[0]=0.0025 (should be 0.0025)
Test 4: Updating attachment target
  After target update: InvMass[0]=0.0000 (should still be 0.0)
  IsAttached=1 (should still be 1)
ClothComponent: Cleared 1 attachments
=== Runtime Attachment Changes Test Complete ===

=== Testing InvMass Isolation ===
After attaching vertex 0 on 5 instances:
  Instance 0: InvMass[0]=0.0000
  Instance 1: InvMass[0]=0.0000
  Instance 2: InvMass[0]=0.0000
  Instance 3: InvMass[0]=0.0000
  Instance 4: InvMass[0]=0.0000
Unbinding vertex 0 from instance 2 only...
ClothComponent: Unbound attachment for vertex 0
After unbinding instance 2:
  Instance 0: InvMass[0]=0.0000, Attached=1
  Instance 1: InvMass[0]=0.0000, Attached=1
  Instance 2: InvMass[0]=0.0025, Attached=0  ← ONLY THIS CHANGED
  Instance 3: InvMass[0]=0.0000, Attached=1
  Instance 4: InvMass[0]=0.0000, Attached=1
Asset BaseInvMass[0]=0.0025 (should be >0.0, unchanged)
=== InvMass Isolation Test Complete ===

=== Testing Various Attachment Patterns ===
Pattern 1: Single corner attachment (3 instances)
  Instance 0: Attached vertex 0, count=1
  Instance 1: Attached vertex 0, count=1
  Instance 2: Attached vertex 0, count=1
Pattern 2: Two corner attachment (3 instances)
  Instance 3: Attached vertices 0,10, count=2
  Instance 4: Attached vertices 0,10, count=2
  Instance 5: Attached vertices 0,10, count=2
Pattern 3: Four corner attachment (3 instances)
  Instance 6: Attached vertices 0,5,10,15, count=4
  Instance 7: Attached vertices 0,5,10,15, count=4
  Instance 8: Attached vertices 0,5,10,15, count=4
Pattern 4: Soft attachment with LRA (1 instance)
  Instance 9: Soft attachment (stiffness=0.8, distance=10.0)
Verifying pattern independence:
  Instance 0: 1 attachments
  Instance 3: 2 attachments
  Instance 6: 4 attachments
  Instance 9: 1 attachments
Asset BaseInvMasses unchanged: YES
=== Attachment Patterns Test Complete ===

=== ALL ATTACHMENT TESTS COMPLETE ===
```

---

## Manual Testing Scenarios

### Scenario 1: Basic Attachment

**Setup:**
```cpp
UClothMeshComponent* cloth = ClothMeshes[0];
FVector attachPos = cloth->GetComponentLocation() + FVector(0, 0, 100);
```

**Test:**
```cpp
// Attach top corner
cloth->BindAttachmentToWorldPosition(0, attachPos);
```

**Verify:**
- Cloth hangs from attached vertex
- Other vertices fall with gravity
- InvMass[0] = 0.0
- Other InvMass values > 0.0

### Scenario 2: Multiple Instances

**Setup:**
```cpp
UClothMeshComponent* cloth1 = ClothMeshes[0];
UClothMeshComponent* cloth2 = ClothMeshes[1];
```

**Test:**
```cpp
// Attach different vertices
cloth1->BindAttachmentToWorldPosition(0, cloth1->GetComponentLocation() + FVector(0, 0, 100));
cloth2->BindAttachmentToWorldPosition(10, cloth2->GetComponentLocation() + FVector(0, 0, 100));
```

**Verify:**
- Cloth1 hangs from vertex 0
- Cloth2 hangs from vertex 10
- Cloth1 InvMass[10] > 0.0 (not attached)
- Cloth2 InvMass[0] > 0.0 (not attached)
- Asset BaseInvMasses all > 0.0

### Scenario 3: Runtime Toggle

**Setup:**
```cpp
UClothMeshComponent* cloth = ClothMeshes[0];
FVector attachPos = cloth->GetComponentLocation() + FVector(0, 0, 100);
```

**Test:**
```cpp
// Bind
cloth->BindAttachmentToWorldPosition(0, attachPos);
// Wait 2 seconds
// Unbind
cloth->UnbindAttachment(0);
// Wait 2 seconds
// Re-bind
cloth->BindAttachmentToWorldPosition(0, attachPos);
```

**Verify:**
- Cloth alternates between hanging and falling
- InvMass correctly set/restored each time
- No memory leaks or corruption

### Scenario 4: Soft Attachment (LRA)

**Setup:**
```cpp
UClothMeshComponent* cloth = ClothMeshes[0];
FVector attachPos = cloth->GetComponentLocation() + FVector(0, 0, 100);
```

**Test:**
```cpp
// Soft attachment with 10cm slack
cloth->BindAttachmentToWorldPosition(0, attachPos, 0.8f, 10.0f);
```

**Verify:**
- Cloth vertex can move up to 10cm from target
- Bounces back when stretched beyond 10cm
- Softer than hard kinematic (stiffness=0.8)

---

## Debugging Tips

### Check InvMass Values

```cpp
UClothMeshComponent* cloth = ClothMeshes[0];
const TArray<float>& runtimeInvMasses = cloth->GetRuntimeInvMasses();
const TArray<float>& baseInvMasses = SharedClothAsset->GetBaseInvMasses();

UE_LOG(ELogLevel::Display, TEXT("Vertex 0:"));
UE_LOG(ELogLevel::Display, TEXT("  Runtime InvMass: %.6f"), runtimeInvMasses[0]);
UE_LOG(ELogLevel::Display, TEXT("  Base InvMass: %.6f"), baseInvMasses[0]);
UE_LOG(ELogLevel::Display, TEXT("  Is Attached: %d"), cloth->IsVertexAttached(0) ? 1 : 0);
```

### Check Attachment Bindings

```cpp
UClothMeshComponent* cloth = ClothMeshes[0];
const TArray<FClothAttachmentBinding>& bindings = cloth->GetAttachmentBindings();

UE_LOG(ELogLevel::Display, TEXT("Attachment count: %d"), bindings.Num());
for (int32 i = 0; i < bindings.Num(); ++i)
{
    const FClothAttachmentBinding& binding = bindings[i];
    UE_LOG(ELogLevel::Display, TEXT("  [%d] Vertex %d, Active=%d, Stiffness=%.2f"),
           i, binding.SimVertexIndex, binding.bIsActive ? 1 : 0, binding.Stiffness);
}
```

### Check GPU Buffer State

```cpp
FClothInstanceHandle* handle = cloth->GetClothInstanceHandle();
if (handle)
{
    const FClothInstanceMetadata& metadata = handle->GetMetadata();
    UE_LOG(ELogLevel::Display, TEXT("GPU Buffer Range:"));
    UE_LOG(ELogLevel::Display, TEXT("  Particle Offset: %d"), metadata.ParticleOffset);
    UE_LOG(ELogLevel::Display, TEXT("  Particle Count: %d"), metadata.ParticleCount);
    UE_LOG(ELogLevel::Display, TEXT("  Kinematic Target Offset: %d"), metadata.KinematicTargetOffset);
    UE_LOG(ELogLevel::Display, TEXT("  Kinematic Target Count: %d"), metadata.KinematicTargetCount);
}
```

---

## Common Issues & Solutions

### Issue 1: InvMass Not Updating

**Symptom:** RuntimeInvMasses[vertex] doesn't change to 0.0 after binding

**Possible Causes:**
- RuntimeInvMasses not initialized
- Vertex index out of bounds
- MarkAttachmentsDirty() not called

**Solution:**
```cpp
// Check initialization
if (cloth->GetRuntimeInvMasses().Num() == 0)
{
    UE_LOG(ELogLevel::Error, TEXT("RuntimeInvMasses not initialized!"));
}

// Check vertex index
if (vertexIndex >= cloth->GetRuntimeInvMasses().Num())
{
    UE_LOG(ELogLevel::Error, TEXT("Vertex index out of bounds!"));
}
```

### Issue 2: Attachments Not Affecting Simulation

**Symptom:** Cloth falls even though vertex is attached

**Possible Causes:**
- GPU buffer not updated
- Batch manager not reading component bindings
- Attachment data not rebuilt

**Solution:**
```cpp
// Check if attachment data is dirty
FClothBatchManager* batchMgr = handle->GetBatchManager();
// Manually trigger rebuild
batchMgr->UpdateInstanceAttachments(handle);
```

### Issue 3: Multiple Instances Affecting Each Other

**Symptom:** Attaching vertex on instance1 affects instance2

**Possible Causes:**
- Still using asset InvMasses instead of RuntimeInvMasses
- GPU buffer ranges overlapping
- Batch manager reading from asset

**Solution:**
- Verify AddInstance() uses component->RuntimeInvMasses
- Verify BuildKinematicAttachmentData() uses component->AttachmentBindings
- Check metadata.ParticleOffset is unique per instance

---

## Performance Benchmarks

### Expected Performance

**InvMass Update (per instance):**
- 1000 vertices: ~0.01ms
- 5000 vertices: ~0.05ms
- **Target:** <0.1ms

**Attachment Rebuild (all instances):**
- 100 attachments: ~0.1ms
- 500 attachments: ~0.5ms
- **Target:** <1ms

**Per-Frame Overhead:**
- No additional cost (InvMass is read-only during simulation)
- **Target:** 0ms

### Profiling Commands

```cpp
// Enable profiling
QUICK_SCOPE_CYCLE_COUNTER(UpdateInstanceInvMass);

// Check results in profiler
// Look for "UpdateInstanceInvMass" in frame profiler
```

---

## Success Criteria

### ✅ Functional Requirements

1. **Multiple instances share asset with different attachments**
   - Test: TestAttachmentIndependence()
   - Verify: Each instance has different attachment configuration

2. **Per-instance InvMass isolation**
   - Test: TestInvMassIsolation()
   - Verify: Unbinding from one instance doesn't affect others

3. **Runtime attachment changes**
   - Test: TestRuntimeAttachmentChanges()
   - Verify: Bind/unbind works during PIE without asset modification

4. **Asset remains immutable**
   - Test: All tests
   - Verify: BaseInvMasses never changes

### ✅ Performance Requirements

1. **GPU update cost acceptable**
   - Target: <0.1ms per instance
   - Measure: Profile UpdateInstanceInvMass()

2. **No frame rate regression**
   - Target: Same FPS as before refactoring
   - Measure: Compare with/without attachments

3. **Memory overhead acceptable**
   - Target: <5KB per instance
   - Measure: RuntimeInvMasses + AttachmentBindings size

---

## Next Steps After Testing

1. **If tests pass:**
   - Merge refactoring branch
   - Update documentation
   - Remove deprecated fields in future version

2. **If tests fail:**
   - Debug specific failure
   - Check console logs for errors
   - Verify GPU buffer updates
   - Check metadata ranges

3. **Optimization opportunities:**
   - Implement incremental attachment updates
   - Add attachment pooling
   - Implement lazy GPU updates

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-19  
**Status:** Ready for Testing
