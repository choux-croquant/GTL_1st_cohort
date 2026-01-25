# Batched Cloth Attachment Debugging Guide

## Issue: All Cloths Follow One Driver

Despite setting up 5 separate drivers and 5 separate cloths, all cloths behave as if attached to a single driver.

---

## Diagnostic Logging Added

### In TestBatchedClothActor::CreateTestCloth
**File**: [`TestBatchedClothActor.cpp:196-244`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestBatchedClothActor.cpp:196)

Added logging to verify:
1. Driver exists before attachment setup
2. Driver position when attachments created
3. DriverComponent pointer value
4. Number of attachments added to each ClothAsset

**Look for**:
```
CreateTestCloth[0]: Setting up 20 attachments to driver @ (0.0, 0.0, 0.0), DriverPtr=0x...
CreateTestCloth[1]: Setting up 18 attachments to driver @ (0.0, 150.0, 0.0), DriverPtr=0x...
CreateTestCloth[2]: Setting up 16 attachments to driver @ (0.0, 300.0, 0.0), DriverPtr=0x...
```

**If all DriverPtr values are the SAME**: That's the bug!
**If all driver positions are (0,0,0)**: Drivers not positioned correctly
**If DriverPtr is different but positions same**: Drivers exist but not moved

---

### In ClothBatchManager::UpdateKinematicTargets
**File**: [`ClothBatchManager.cpp:566-670`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp:566)

Added logging to verify:
1. Different AssetPtr for each instance
2. Different DriverPtr for each attachment
3. Different DriverLoc (driver world location)
4. Different TargetPos (final attachment position)

**Look for** (First 5 frames):
```
ClothBatchManager[LOD0]: UpdateKinematicTargets - Processing 5 instances
  Instance 0: 20 attachments, ParticleOffset=0, AssetPtr=0x...
    Attachment[0]: ParticleIdx=0, DriverPtr=0x..., DriverLoc=(0,0,0), TargetPos=(0,0,0)
  Instance 1: 18 attachments, ParticleOffset=400, AssetPtr=0x...
    Attachment[0]: ParticleIdx=400, DriverPtr=0x..., DriverLoc=(0,150,0), TargetPos=(0,150,0)
  Instance 2: 16 attachments, ParticleOffset=724, AssetPtr=0x...
    Attachment[0]: ParticleIdx=724, DriverPtr=0x..., DriverLoc=(0,300,0), TargetPos=(0,300,0)
```

**Key Checks**:
- ✅ AssetPtr should be DIFFERENT for each instance
- ✅ DriverPtr should be DIFFERENT for each instance  
- ✅ DriverLoc should be DIFFERENT for each instance
- ✅ TargetPos should be DIFFERENT for each instance

**If any are the SAME**: That's the source of the bug!

---

## Possible Causes and Solutions

### Cause 1: All Drivers at Same Position
**Symptom**: DriverLoc is (0,0,0) for all instances
**Diagnosis**: Driver spawn/positioning failed
**Check**: TestBatchedClothActor spawn logs - "Spawned driver X at..."

**Solution**: Already implemented - drivers spawn at distinct positions
- Verify logs show different positions
- Check AttachmentDrivers[i]->GetActorLocation() returns different values

---

### Cause 2: All Attachments Reference Same Driver Component
**Symptom**: DriverPtr is identical (same pointer value) for all instances
**Diagnosis**: CreateTestCloth sets wrong driver or all assets share same attachment array

**Potential Issues**:
```cpp
// Line 221 in CreateTestCloth - uses AttachmentDrivers[Index]
attachment.DriverComponent = AttachmentDrivers[Index]->GetStaticMeshComponent();

// Possible bug: Is Index correct? Is it the loop variable or cloth instance index?
```

**Check**:
- In `CreateTestCloth(int32 Index, ...)`, verify `Index` parameter is used
- In PostSpawnInitialize loop: `CreateTestCloth(i, 20 - i * 2, ...)` - verify `i` passed correctly
- In CreateTestCloth: `AttachmentDrivers[Index]` - verify Index is the instance index (0-4)

---

### Cause 3: ClothAsset Shared Across Instances
**Symptom**: AssetPtr is identical for all instances
**Diagnosis**: All components use same ClothAsset instead of separate assets

**Check Constructor** ([`TestBatchedClothActor.cpp:26-27`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestBatchedClothActor.cpp:26)):
```cpp
FString AssetName = FString::Printf(TEXT("TestClothAsset_%d"), i);
ClothAssets[i] = FObjectFactory::ConstructObject<UClothAsset>(this, *AssetName);
```

This should create 5 separate assets. Verify:
- Each ClothAssets[i] has different pointer value
- ClothMeshes[i]->SetClothAsset(ClothAssets[i]) uses correct index

---

### Cause 4: AttachmentData Array Getting Overwritten
**Symptom**: AssetPtr different, but DriverPtr same
**Diagnosis**: All assets' AttachmentsData arrays point to same driver

**Possible Issue**:
```cpp
// If ClothAsset is shared or if there's reference copying
ClothAssets[0]->AttachmentsData[0].DriverComponent = Driver0  ✓
ClothAssets[1]->AttachmentsData[0].DriverComponent = Driver1  ✓
// But later something overwrites or all point to last set value
```

**Solution**: Verify in CreateTestCloth that we're adding to correct asset:
```cpp
// Line 220 (now 240):
ClothAssets[Index]->AddAttachmentData(attachment);  // Uses Index, should be correct
```

---

## Action Items to Debug

### Step 1: Run with Diagnostic Logging
Build and run, check console output for:

**During CreateTestCloth**:
```
CreateTestCloth[0]: Setting up 20 attachments to driver @ (0, 0, 0), DriverPtr=0xAAAAAAA
CreateTestCloth[0]: Added 20 attachments to ClothAsset 0xBBBBBBB (Driver=0xAAAAAAA)

CreateTestCloth[1]: Setting up 18 attachments to driver @ (0, 150, 0), DriverPtr=0xCCCCCCC
CreateTestCloth[1]: Added 18 attachments to ClothAsset 0xDDDDDDD (Driver=0xCCCCCCC)
```

**During UpdateKinematicTargets**:
```
Instance 0: AssetPtr=0xBBBBBBB
  Attachment[0]: DriverPtr=0xAAAAAAA, DriverLoc=(0,0,0)
  
Instance 1: AssetPtr=0xDDDDDDD
  Attachment[0]: DriverPtr=0xCCCCCCC, DriverLoc=(0,150,0)
```

### Step 2: Identify Mismatch
Compare pointer values:
- If CreateTestCloth shows different DriverPtr but UpdateKinematicTargets shows same → Storage issue
- If CreateTestCloth shows same DriverPtr → Setup issue (all using AttachmentDrivers[0]?)
- If DriverLoc is same for all → Position issue (drivers not moved)

### Step 3: Add Breakpoints
**Breakpoint 1**: TestBatchedClothActor.cpp line 221 (now 234 after edits)
```cpp
attachment.DriverComponent = driverMeshComp;
```
Check values when Index = 0, 1, 2, 3, 4:
- Is driverMeshComp different for each Index?
- Is AttachmentDrivers[Index] returning different actors?

**Breakpoint 2**: ClothBatchManager.cpp line 619 (approx, in UpdateKinematicTargets)
```cpp
if (attachment.DriverComponent != nullptr)
```
Check values for each instance:
- Is attachment.DriverComponent the same or different?
- Is attachment.DriverComponent->GetComponentTransform() returning different transforms?

---

## Hypotheses and Tests

### Hypothesis 1: Wrong Index Used in CreateTestCloth
```cpp
// If CreateTestCloth accidentally uses a hardcoded index or wrong variable
attachment.DriverComponent = AttachmentDrivers[0]->GetStaticMeshComponent();  // WRONG - always 0
// Instead of:
attachment.DriverComponent = AttachmentDrivers[Index]->GetStaticMeshComponent();  // CORRECT
```

**Test**: Check CreateTestCloth line 221 - does it use `Index` or a constant?
**Current Code**: Uses `AttachmentDrivers[Index]` ✅ Correct

---

### Hypothesis 2: ClothAsset Pointer Aliasing
```cpp
// If all ClothMeshes somehow reference the same ClothAsset
ClothMeshes[0]->SetClothAsset(ClothAssets[0]);  ✓
ClothMeshes[1]->SetClothAsset(ClothAssets[0]);  ✗ BUG - should be ClothAssets[1]
```

**Test**: Check PostSpawnInitialize line 336 (approx)
**Current Code**:
```cpp
ClothMeshes[i]->SetClothAsset(ClothAssets[i]);  // Uses i, correct
```

---

### Hypothesis 3: Driver Position Not Updated
```cpp
// Drivers spawn at (0,0,0), then position calculation happens, but drivers never move
for (i : 0-4) {
    AttachmentDrivers[i]->SetActorLocation(DriverInitialPositions[i]);  // But DriverInitialPositions[i] still (0,0,0)?
}
```

**Test**: Check if DriverInitialPositions calculated before or after spawn
**Current Code (After Fix)**:
```cpp
// Lines 272-284: Calculate positions FIRST
for (int32 i = 0; i < NumClothInstances; ++i) {
    DriverInitialPositions[i] = GetActorLocation() + offset;  // Set here
}

// Lines 289-315: Spawn at calculated positions
AttachmentDrivers[i]->SetActorLocation(DriverInitialPositions[i]);  // Use here
```
Should be correct now!

---

## Expected Diagnostic Output

### Correct Behavior:
```
CreateTestCloth[0]: driver @ (0, 0, 0), DriverPtr=0x12345678
CreateTestCloth[1]: driver @ (0, 150, 0), DriverPtr=0x23456789  // Different pointer
CreateTestCloth[2]: driver @ (0, 300, 0), DriverPtr=0x34567890  // Different pointer

UpdateKinematicTargets:
  Instance 0: AssetPtr=0xAAAAAAAA, DriverPtr=0x12345678, DriverLoc=(0,0,0)
  Instance 1: AssetPtr=0xBBBBBBBB, DriverPtr=0x23456789, DriverLoc=(0,150,0)  // Different!
  Instance 2: AssetPtr=0xCCCCCCCC, DriverPtr=0x34567890, DriverLoc=(0,300,0)  // Different!
```

### Buggy Behavior (What User Sees):
```
CreateTestCloth[0]: driver @ (X, Y, Z), DriverPtr=0x12345678
CreateTestCloth[1]: driver @ (X, Y, Z), DriverPtr=0x12345678  // SAME pointer!
CreateTestCloth[2]: driver @ (X, Y, Z), DriverPtr=0x12345678  // SAME pointer!

UpdateKinematicTargets:
  Instance 0: DriverPtr=0x12345678, DriverLoc=(X,Y,Z)
  Instance 1: DriverPtr=0x12345678, DriverLoc=(X,Y,Z)  // Same location!
  Instance 2: DriverPtr=0x12345678, DriverLoc=(X,Y,Z)  // Same location!
```

---

## Next Steps

1. **Run with current changes and capture console output**
2. **Compare pointer values** (AssetPtr, DriverPtr)
3. **Compare positions** (driver locations, target positions)
4. **Report findings** - this will reveal exactly where the sharing happens

The diagnostic logging will pinpoint whether the issue is:
- Driver spawning/positioning
- Attachment setup (CreateTestCloth)
- Attachment storage (ClothAsset)
- Attachment retrieval (UpdateKinematicTargets)

---

## If Issue Persists

### Additional Fix: Verify Spawn Order Again
The current fix moves position calculation before spawn. But verify execution actually runs in that order. Add more logging:

```cpp
UE_LOG(..., "BEFORE spawn - DriverInitialPositions[%d] = (%f,%f,%f)", i, ...);
AttachmentDrivers[i] = world->SpawnActor<AStaticMeshActor>();
AttachmentDrivers[i]->SetActorLocation(DriverInitialPositions[i]);
FVector actualLoc = AttachmentDrivers[i]->GetActorLocation();
UE_LOG(..., "AFTER spawn - Driver %d actual location = (%f,%f,%f)", i, actualLoc.X, ...);
```

This will confirm if drivers are actually at different positions.
