# Cloth Attachment Editor - Quick Start Guide

## 🚀 Get Started in 5 Minutes

This guide shows you **exactly how to use** the cloth attachment system **right now** with simple code snippets you can copy and paste.

---

## Prerequisites

✅ Scene with `ACharacterClothTest` actor (or similar)  
✅ Actor has `USkeletalMeshComponent` (character with bones)  
✅ Actor has `UClothMeshComponent` (cape/cloth)  
✅ Cloth asset is generated (`GenerateClothAsset()` called)

---

## Method 1: Simple Console Commands (Easiest)

### Step 1: Get Your Component

```cpp
// In editor console or C++ code:
UClothMeshComponent* ClothComp = /* your cloth component */;

// Example: Find by actor name
AActor* Actor = /* find your actor */;
UClothMeshComponent* ClothComp = Actor->FindComponentByClass<UClothMeshComponent>();
```

### Step 2: Configure Parameters

```cpp
// Set attachment parameters
ClothComp->AttachmentZThreshold = 0.9f;  // Top 10% of vertices
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Neck"));
ClothComp->KinematicWeight = 1.0f;       // Fully pinned
ClothComp->AttachmentStiffness = 1.0f;   // Hard constraint
ClothComp->AttachmentDistance = 0.0f;    // No stretch
```

### Step 3: Execute Workflow

```cpp
// Select vertices
ClothComp->AutoSelectTopVertices();

// Assign bone
ClothComp->AssignBoneToSelection();

// Save to asset
ClothComp->ApplyAttachmentPaintDataToAsset();
```

### Step 4: Test in PIE

Press **Play** button (or F8)

**Expected Result:** Cape attaches to character's neck and follows animation!

---

## Method 2: Copy-Paste Complete Script

### For Cape Attachment

```cpp
// ===== CAPE ATTACHMENT SCRIPT =====
// Copy this entire block and run in editor

// 1. Get component (adjust actor name as needed)
AActor* Actor = /* your actor */;
UClothMeshComponent* ClothComp = Actor->FindComponentByClass<UClothMeshComponent>();

if (!ClothComp)
{
    UE_LOG(ELogLevel::Error, TEXT("Cloth component not found!"));
    return;
}

// 2. Configure for cape (rigid collar)
ClothComp->AttachmentZThreshold = 0.9f;
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Neck"));
ClothComp->KinematicWeight = 1.0f;
ClothComp->AttachmentStiffness = 1.0f;
ClothComp->AttachmentDistance = 0.0f;

// 3. Execute
ClothComp->AutoSelectTopVertices();
ClothComp->AssignBoneToSelection();
ClothComp->ApplyAttachmentPaintDataToAsset();

// 4. Done! Test in PIE
UE_LOG(ELogLevel::Display, TEXT("Cape attachment complete - test in PIE!"));
```

### For Skirt Attachment

```cpp
// ===== SKIRT ATTACHMENT SCRIPT =====
// Soft waistband attachment with stretch

// 1. Get component
AActor* Actor = /* your actor */;
UClothMeshComponent* ClothComp = Actor->FindComponentByClass<UClothMeshComponent>();

// 2. Configure for skirt (soft waistband)
ClothComp->AttachmentZThreshold = 0.95f;  // Top 5% (waistband only)
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Hips"));
ClothComp->KinematicWeight = 0.7f;        // Soft attachment
ClothComp->AttachmentStiffness = 0.6f;    // 60% stiffness
ClothComp->AttachmentDistance = 15.0f;    // Allow 15cm stretch

// 3. Execute
ClothComp->AutoSelectTopVertices();
ClothComp->AssignBoneToSelection();
ClothComp->ApplyAttachmentPaintDataToAsset();

// 4. Done!
UE_LOG(ELogLevel::Display, TEXT("Skirt attachment complete - test in PIE!"));
```

---

## Method 3: Add to PostSpawnInitialize() (Automatic)

If you want attachments to be created automatically when the actor spawns:

### Edit CharacterClothTest.cpp

**File:** [`EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/CharacterClothTest.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/CharacterClothTest.cpp)

**Add after line 106 (after `RegisterWithClothWorld()`):**

```cpp
// ===== AUTO-CREATE ATTACHMENT DATA =====
UClothAsset* CapeAsset = CapeCloth->GeneratedClothAsset;
if (CapeAsset && CapeAsset->AttachmentPaintData.Num() == 0)
{
    // Only create if no data exists yet
    UE_LOG(ELogLevel::Display, TEXT("Auto-creating attachment data..."));

    // Find top 10% of vertices
    float MaxZ = -FLT_MAX, MinZ = FLT_MAX;
    for (const FVector& Pos : CapeAsset->RestPositions)
    {
        MaxZ = FMath::Max(MaxZ, Pos.Z);
        MinZ = FMath::Min(MinZ, Pos.Z);
    }
    float AttachThreshold = MaxZ - ((MaxZ - MinZ) * 0.10f);

    // Create paint data
    for (int32 i = 0; i < CapeAsset->RestPositions.Num(); ++i)
    {
        if (CapeAsset->RestPositions[i].Z >= AttachThreshold)
        {
            FClothAttachmentPaintData PaintData;
            PaintData.SimVertexIndex = i;
            PaintData.BoneName = FName(TEXT("mixamorig:Neck"));
            PaintData.bHasBoneAssignment = true;
            PaintData.KinematicWeight = 1.0f;
            PaintData.Stiffness = 1.0f;
            PaintData.AttachDistance = 0.0f;
            PaintData.bIsActive = true;
            PaintData.DebugLabel = FString::Printf(TEXT("AutoVertex_%d"), i);

            CapeAsset->AttachmentPaintData.Add(PaintData);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("Auto-created %d attachments"),
        CapeAsset->AttachmentPaintData.Num());
}
```

**Result:** Attachments are created automatically when actor spawns in editor!

---

## Verification Checklist

After running the scripts, verify:

### ✅ Console Output

You should see:
```
AutoSelectTopVertices: Selected 42 vertices (Z >= 95.23, threshold=0.90)
AssignBoneToSelection: Assigned bone 'mixamorig:Neck' to 42 vertices
ApplyAttachmentPaintDataToAsset: Saved 42 attachment entries to asset
```

### ✅ Attachment Count

```cpp
// Check attachment count
int32 Count = ClothComp->GeneratedClothAsset->AttachmentPaintData.Num();
UE_LOG(ELogLevel::Display, TEXT("Attachment count: %d"), Count);
// Should be > 0
```

### ✅ PIE Test

1. Enter PIE (Play-In-Editor)
2. Console should show:
   ```
   ApplyAttachmentPaintData: Processing 42 attachment entries
   ApplyAttachmentPaintData: Applied 42 attachments (0 skipped)
   ```
3. Cloth should attach to bone and follow animation

### ✅ Persistence Test

1. Save scene
2. Restart editor
3. Load scene
4. Enter PIE
5. Attachments should still work (data persisted!)

---

## Common Issues and Solutions

### Issue: "No cloth asset generated yet"

**Solution:**
```cpp
// Generate cloth asset first
ClothComp->GenerateClothAsset();

// Wait for generation to complete, then run attachment script
```

---

### Issue: "Bone 'mixamorig:Neck' not found"

**Solution:**
```cpp
// Check available bones in your skeletal mesh
// Common alternatives:
ClothComp->TargetBoneName = FName(TEXT("Neck"));           // Generic name
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Spine2")); // Upper spine
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Head"));   // Head
```

---

### Issue: Cloth doesn't move in PIE

**Possible Causes:**
1. Attachments not applied → Check console for "Applied X attachments"
2. All vertices attached → Reduce Z-threshold (try 0.9 instead of 0.5)
3. Stiffness too high → Try 0.7 instead of 1.0
4. Cloth simulation disabled → Check `bSimulate == true`

---

## Next Steps

### After Basic Attachment Works

1. **Experiment with Parameters**
   - Try different Z-thresholds (0.8, 0.9, 0.95)
   - Try different bones (Spine2, Hips, Shoulders)
   - Try soft attachments (Weight=0.7, Stiffness=0.6)
   - Try LRA (Distance=15.0)

2. **Try Multi-Bone Attachments**
   - Attach left side to LeftShoulder
   - Attach right side to RightShoulder
   - See Example 3 in main user guide

3. **Create Your Own Presets**
   - Save working parameter combinations
   - Document what works for different cloth types
   - Share with team

---

## Summary

**Simplest Workflow:**
```cpp
ClothComp->AttachmentZThreshold = 0.9f;
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Neck"));
ClothComp->AutoSelectTopVertices();
ClothComp->AssignBoneToSelection();
ClothComp->ApplyAttachmentPaintDataToAsset();
// Test in PIE!
```

**Time Required:** 30 seconds  
**Code Required:** 5 lines  
**Result:** Working cloth attachment!

---

**For More Details:** See [`docs/cloth-attachment-paint-editor-user-guide.md`](docs/cloth-attachment-paint-editor-user-guide.md)  
**For Technical Info:** See [`plans/cloth-attachment-paint-implementation-summary.md`](plans/cloth-attachment-paint-implementation-summary.md)
