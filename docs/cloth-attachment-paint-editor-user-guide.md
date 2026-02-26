# Cloth Attachment Paint Editor - User Guide

## Overview

The **Cloth Attachment Paint Editor** allows you to attach cloth vertices to skeletal mesh bones without writing code. This guide explains how to use the system to create realistic cloth attachments for capes, skirts, hair, and other cloth simulations.

---

## Quick Start (5 Minutes)

### Step 1: Setup Your Scene

1. Place a `ACharacterClothTest` actor in your scene (or any actor with cloth)
2. The actor should contain:
   - `USkeletalMeshComponent` (your character with bones)
   - `UClothMeshComponent` (your cape/cloth)

### Step 2: Generate Cloth Asset

1. Select the `UClothMeshComponent` in the outliner
2. In the Detail Panel, ensure `SourceStaticMesh` is set to your cloth mesh
3. Click **"Generate Cloth Asset"** button
4. Wait for generation to complete (check console for "Cloth asset generated")

### Step 3: Open Attachment Editor

**Option A: ImGui Panel (Recommended)**
```cpp
// In editor, open ImGui window:
// Window → Cloth Attachment Paint Editor
```

**Option B: Console Commands**
```cpp
// Get your cloth component
UClothMeshComponent* ClothComp = /* your component */;

// Configure and execute
ClothComp->AttachmentZThreshold = 0.9f;
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Neck"));
ClothComp->AutoSelectTopVertices();
ClothComp->AssignBoneToSelection();
ClothComp->ApplyAttachmentPaintDataToAsset();
```

### Step 4: Select Vertices

In the ImGui panel:
1. Set **"Z Threshold"** slider to `0.9` (top 10% of vertices)
2. Click **"Auto-Select Top Vertices"**
3. Status shows: "Selected 42 vertices"

### Step 5: Assign Bone

1. Choose bone from **"Target Bone"** dropdown (e.g., "mixamorig:Neck")
2. Set **"Kinematic Weight"** to `1.0` (fully pinned)
3. Set **"Stiffness"** to `1.0` (hard constraint)
4. Set **"Attach Distance"** to `0.0` (kinematic, no stretch)
5. Click **"Assign Bone to Selection"**
6. Status shows: "Assigned bone 'mixamorig:Neck' to 42 vertices"

### Step 6: Apply and Test

1. Click **"Apply to Asset"**
2. Status shows: "Applied to asset - data will persist"
3. Enter **Play-In-Editor (PIE)**
4. Your cape should now attach to the character's neck and follow animation!

---

## Detailed Workflow

### Understanding the UI

The ImGui panel has 4 sections:

#### 1. Vertex Selection
- **Z Threshold Slider:** Select vertices by height (0.9 = top 10%)
- **Auto-Select Button:** Execute selection
- **Clear Selection Button:** Reset selection

#### 2. Bone Assignment
- **Target Bone Dropdown:** Choose which bone to attach to
- **Kinematic Weight:** How strongly attached (0.0 = free, 1.0 = pinned)
- **Stiffness:** Constraint strength (0.0 = soft, 1.0 = hard)
- **Attach Distance:** LRA stretch distance (0.0 = no stretch)
- **Assign Button:** Apply bone to selected vertices

#### 3. Current Attachments
- **Table View:** Shows all current attachments
- **Columns:** Vertex index, Bone name, Weight, Stiffness, Active status

#### 4. Actions
- **Apply to Asset:** Save data (persists across editor sessions)
- **Clear All Attachments:** Reset everything

---

## Common Use Cases

### Use Case 1: Simple Cape Attachment

**Goal:** Attach cape to character's neck

**Steps:**
1. Z Threshold: `0.9` (top 10%)
2. Target Bone: `mixamorig:Neck`
3. Kinematic Weight: `1.0` (fully pinned)
4. Stiffness: `1.0` (hard)
5. Attach Distance: `0.0` (no stretch)
6. Click: Auto-Select → Assign Bone → Apply

**Result:** Cape top attaches rigidly to neck, bottom flows freely

---

### Use Case 2: Soft Skirt Attachment

**Goal:** Attach skirt waistband to hips with natural stretch

**Steps:**
1. Z Threshold: `0.95` (top 5% - just the waistband)
2. Target Bone: `mixamorig:Hips`
3. Kinematic Weight: `0.7` (soft attachment)
4. Stiffness: `0.6` (60% stiffness)
5. Attach Distance: `15.0` (allow 15cm stretch)
6. Click: Auto-Select → Assign Bone → Apply

**Result:** Skirt loosely follows hips with natural stretch and bounce

---

### Use Case 3: Multi-Bone Cape (Left/Right Shoulders)

**Goal:** Attach left side to LeftShoulder, right side to RightShoulder

**Steps:**

**First Pass (Left Side):**
1. Z Threshold: `0.9`
2. Click "Auto-Select Top Vertices"
3. **Manually filter selection** (in code or future UI):
   ```cpp
   // Keep only left-side vertices
   TArray<uint32> LeftVertices;
   for (uint32 Idx : ClothComp->SelectedVertexIndices)
   {
       if (ClothComp->GeneratedClothAsset->RestPositions[Idx].X < 0.0f)
           LeftVertices.Add(Idx);
   }
   ClothComp->SelectedVertexIndices = LeftVertices;
   ```
4. Target Bone: `mixamorig:LeftShoulder`
5. Click "Assign Bone to Selection"

**Second Pass (Right Side):**
1. Repeat steps 1-3 but filter for `X > 0.0f`
2. Target Bone: `mixamorig:RightShoulder`
3. Click "Assign Bone to Selection"

**Final:**
4. Click "Apply to Asset"

**Result:** Cape left side follows LeftShoulder, right side follows RightShoulder

---

### Use Case 4: Hair Attachment

**Goal:** Attach hair roots to head bone

**Steps:**
1. Z Threshold: `0.98` (top 2% - just the roots)
2. Target Bone: `mixamorig:Head`
3. Kinematic Weight: `1.0` (fully pinned)
4. Stiffness: `1.0` (hard)
5. Attach Distance: `0.0` (no stretch)
6. Click: Auto-Select → Assign Bone → Apply

**Result:** Hair roots stay attached to head, hair flows naturally

---

## Parameter Reference

### Z Threshold

**Range:** 0.0 - 1.0  
**Default:** 0.9

**Meaning:**
- `1.0` = Select only the topmost vertex
- `0.9` = Select top 10% of vertices
- `0.8` = Select top 20% of vertices
- `0.5` = Select top 50% of vertices
- `0.0` = Select all vertices

**Use Cases:**
- **Cape:** 0.85 - 0.95 (collar area)
- **Skirt:** 0.95 - 0.98 (waistband only)
- **Hair:** 0.95 - 0.99 (roots only)
- **Full cloth:** 0.0 (attach everything - rare)

---

### Kinematic Weight

**Range:** 0.0 - 1.0  
**Default:** 1.0

**Meaning:**
- `1.0` = Fully pinned (vertex follows bone exactly)
- `0.7` = Soft attachment (70% influence)
- `0.5` = Half influence (balanced)
- `0.3` = Weak attachment (30% influence)
- `0.0` = Free (no attachment)

**Use Cases:**
- **Rigid attachment:** 1.0 (cape collar, armor straps)
- **Soft attachment:** 0.5-0.8 (skirt waistband, loose clothing)
- **Very soft:** 0.2-0.4 (flowing scarves, ribbons)

---

### Stiffness

**Range:** 0.0 - 1.0  
**Default:** 1.0

**Meaning:**
- `1.0` = Hard constraint (no give)
- `0.7` = Firm but flexible
- `0.5` = Moderate flexibility
- `0.3` = Very flexible
- `0.0` = No constraint (free)

**Use Cases:**
- **Hard:** 1.0 (metal armor, rigid attachments)
- **Firm:** 0.7-0.9 (leather, thick fabric)
- **Soft:** 0.4-0.6 (silk, thin fabric)
- **Very soft:** 0.1-0.3 (ribbons, hair)

---

### Attach Distance (LRA)

**Range:** 0.0 - ∞  
**Default:** 0.0  
**Units:** Centimeters (or engine units)

**Meaning:**
- `0.0` = Kinematic (vertex locked to bone, no stretch)
- `10.0` = Allow 10cm stretch before constraint activates
- `20.0` = Allow 20cm stretch
- `50.0` = Allow 50cm stretch (very loose)

**Use Cases:**
- **Rigid:** 0.0 (cape collar, armor)
- **Slight stretch:** 5.0-10.0 (tight clothing)
- **Moderate stretch:** 15.0-25.0 (loose clothing, skirts)
- **High stretch:** 30.0-50.0 (elastic materials, bungee cords)

---

## Troubleshooting

### Problem: "No cloth component selected"

**Cause:** UI doesn't have a cloth component reference

**Solution:**
```cpp
// Set cloth component
ClothPaintUI->SetClothComponent(YourClothComponent);
ClothPaintUI->SetSkeletalMeshComponent(YourSkeletalMeshComponent);
```

---

### Problem: "No vertices selected"

**Cause:** Auto-select didn't find any vertices

**Solutions:**
1. Check Z-threshold is reasonable (try 0.5 to select more vertices)
2. Verify cloth mesh has vertices with varying Z coordinates
3. Check cloth asset is generated (`bAssetGenerated == true`)

---

### Problem: "No bones available"

**Cause:** Skeletal mesh component not set or has no bones

**Solutions:**
1. Set skeletal mesh component: `ClothPaintUI->SetSkeletalMeshComponent(SkelMesh)`
2. Verify skeletal mesh has bones (check in skeletal mesh viewer)
3. Manually add bone name if dropdown is empty

---

### Problem: Attachments don't work in PIE

**Cause:** Multiple possible causes

**Solutions:**
1. Verify you clicked **"Apply to Asset"** before entering PIE
2. Check console logs for "Applied X attachments"
3. Verify bone names are correct (case-sensitive!)
4. Check `Duplicate()` method calls `ApplyAttachmentPaintData()`
5. Ensure attachment data count > 0 in asset

---

### Problem: Cloth stretches or distorts

**Cause:** LocalOffset computation or attachment parameters

**Solutions:**
1. Try different stiffness values (start with 1.0)
2. Check attach distance is appropriate (0.0 for rigid)
3. Verify bone transforms are correct
4. Check cloth rest positions are reasonable

---

## Advanced Techniques

### Technique 1: Gradient Attachment (Soft to Hard)

**Goal:** Smooth transition from pinned to free

**Method:**
1. Select top 5% of vertices
2. Assign with Weight=1.0, Stiffness=1.0
3. Select next 5% of vertices (Z-threshold 0.90)
4. Assign with Weight=0.7, Stiffness=0.7
5. Select next 5% (Z-threshold 0.85)
6. Assign with Weight=0.4, Stiffness=0.4

**Result:** Smooth gradient from rigid to flowing

---

### Technique 2: Multi-Bone Blending (Manual)

**Goal:** Different bones for different regions

**Method:**
1. Auto-select top vertices
2. Manually filter to left side (X < 0)
3. Assign to LeftShoulder
4. Auto-select again
5. Manually filter to right side (X > 0)
6. Assign to RightShoulder
7. Auto-select again
8. Manually filter to center (|X| < 10)
9. Assign to Neck

**Result:** Cape follows multiple bones naturally

---

### Technique 3: Elastic Waistband

**Goal:** Skirt that stretches but returns to position

**Method:**
1. Select waistband vertices (Z-threshold 0.95)
2. Set Weight=0.8, Stiffness=0.7, Distance=20.0
3. Assign to Hips bone

**Result:** Waistband stretches up to 20cm but pulls back

---

## Console Commands Reference

### Basic Commands

```cpp
// Get component
UClothMeshComponent* ClothComp = Actor->FindComponentByClass<UClothMeshComponent>();

// Auto-select vertices
ClothComp->AttachmentZThreshold = 0.9f;
ClothComp->AutoSelectTopVertices();

// Assign bone
ClothComp->TargetBoneName = FName(TEXT("mixamorig:Neck"));
ClothComp->KinematicWeight = 1.0f;
ClothComp->AttachmentStiffness = 1.0f;
ClothComp->AttachmentDistance = 0.0f;
ClothComp->AssignBoneToSelection();

// Apply
ClothComp->ApplyAttachmentPaintDataToAsset();
```

### Advanced Commands

```cpp
// Clear all attachments
ClothComp->ClearAttachmentPaintData();

// Check attachment count
int32 Count = ClothComp->GeneratedClothAsset->AttachmentPaintData.Num();
UE_LOG(ELogLevel::Display, TEXT("Attachment count: %d"), Count);

// Manually inspect attachment data
for (const FClothAttachmentPaintData& Data : ClothComp->GeneratedClothAsset->AttachmentPaintData)
{
    UE_LOG(ELogLevel::Display, TEXT("Vertex %d → Bone '%s' (Weight=%.2f)"),
        Data.SimVertexIndex, *Data.BoneName.ToString(), Data.KinematicWeight);
}
```

---

## Best Practices

### 1. Start Simple

**Recommendation:** Begin with single-bone, rigid attachments

**Example:**
- Z-Threshold: 0.9
- Bone: Neck
- Weight: 1.0
- Stiffness: 1.0
- Distance: 0.0

**Benefit:** Easy to understand, predictable behavior

---

### 2. Test Frequently

**Recommendation:** Test in PIE after each change

**Workflow:**
1. Make attachment changes
2. Click "Apply to Asset"
3. Enter PIE
4. Observe behavior
5. Exit PIE
6. Adjust parameters
7. Repeat

**Benefit:** Catch issues early, iterate quickly

---

### 3. Use Soft Attachments for Natural Motion

**Recommendation:** Use Weight < 1.0 for flowing cloth

**Example (Flowing Cape):**
- Weight: 0.6
- Stiffness: 0.5
- Distance: 15.0

**Benefit:** More natural, less rigid motion

---

### 4. Save Often

**Recommendation:** Click "Apply to Asset" frequently

**Reason:** Data only persists after applying

**Workflow:**
- After each bone assignment
- Before testing in PIE
- Before closing editor

---

### 5. Document Your Settings

**Recommendation:** Keep notes on what works

**Example:**
```
Cape Collar:
- Z-Threshold: 0.92
- Bone: mixamorig:Neck
- Weight: 1.0
- Stiffness: 1.0
- Distance: 0.0
- Result: Perfect rigid attachment

Cape Mid-Section:
- Z-Threshold: 0.85
- Bone: mixamorig:Spine2
- Weight: 0.6
- Stiffness: 0.5
- Distance: 20.0
- Result: Nice flowing motion
```

---

## Common Bone Names (Mixamo Rig)

### Upper Body
- `mixamorig:Hips` - Pelvis/waist
- `mixamorig:Spine` - Lower spine
- `mixamorig:Spine1` - Mid spine
- `mixamorig:Spine2` - Upper spine
- `mixamorig:Neck` - Neck
- `mixamorig:Head` - Head

### Shoulders and Arms
- `mixamorig:LeftShoulder` - Left shoulder
- `mixamorig:RightShoulder` - Right shoulder
- `mixamorig:LeftArm` - Left upper arm
- `mixamorig:RightArm` - Right upper arm
- `mixamorig:LeftForeArm` - Left forearm
- `mixamorig:RightForeArm` - Right forearm

### Legs
- `mixamorig:LeftUpLeg` - Left thigh
- `mixamorig:RightUpLeg` - Right thigh
- `mixamorig:LeftLeg` - Left shin
- `mixamorig:RightLeg` - Right shin

**Note:** Bone names are case-sensitive!

---

## Workflow Examples

### Example 1: Knight's Cape

**Requirements:**
- Attach to shoulders and neck
- Rigid at collar, flowing at bottom

**Workflow:**

**Step 1: Collar (Rigid)**
```
Z-Threshold: 0.92
Bone: mixamorig:Neck
Weight: 1.0
Stiffness: 1.0
Distance: 0.0
→ Auto-Select → Assign → (Don't apply yet)
```

**Step 2: Shoulders (Firm)**
```
Z-Threshold: 0.88
Bone: mixamorig:Spine2
Weight: 0.8
Stiffness: 0.8
Distance: 5.0
→ Auto-Select → Assign → Apply to Asset
```

**Result:** Collar stays rigid, shoulders have slight give, bottom flows freely

---

### Example 2: Wizard's Robe

**Requirements:**
- Attach to waist
- Allow natural draping and stretch

**Workflow:**
```
Z-Threshold: 0.95 (waistband only)
Bone: mixamorig:Hips
Weight: 0.7
Stiffness: 0.6
Distance: 20.0
→ Auto-Select → Assign → Apply
```

**Result:** Robe follows hips loosely, allows natural draping

---

### Example 3: Character Hair

**Requirements:**
- Attach roots to head
- Allow hair to flow

**Workflow:**
```
Z-Threshold: 0.98 (just the roots)
Bone: mixamorig:Head
Weight: 1.0
Stiffness: 1.0
Distance: 0.0
→ Auto-Select → Assign → Apply
```

**Result:** Hair roots stay attached, hair flows naturally

---

## Tips and Tricks

### Tip 1: Preview Before Applying

**Technique:** Check attachment count before applying

```cpp
// After assigning bone
int32 Count = ClothComp->GeneratedClothAsset->AttachmentPaintData.Num();
UE_LOG(ELogLevel::Display, TEXT("Will apply %d attachments"), Count);

// If count looks wrong, clear and try again
if (Count > 100)  // Too many!
{
    ClothComp->ClearAttachmentPaintData();
    // Adjust Z-threshold and try again
}
```

---

### Tip 2: Use Console Logs

**Technique:** Monitor console for feedback

**Expected Output:**
```
AutoSelectTopVertices: Selected 42 vertices (Z >= 95.23, threshold=0.90)
AssignBoneToSelection: Assigned bone 'mixamorig:Neck' to 42 vertices
ApplyAttachmentPaintDataToAsset: Saved 42 attachment entries to asset
```

**If you see errors:**
```
AssignBoneToSelection: No vertices selected
→ Solution: Call AutoSelectTopVertices() first

ApplyAttachmentPaintData: Bone 'InvalidBone' not found
→ Solution: Check bone name spelling
```

---

### Tip 3: Iterate Quickly

**Technique:** Use fast iteration cycle

**Fast Workflow:**
1. Make changes in UI
2. Click "Apply to Asset"
3. Press PIE hotkey (F8 or similar)
4. Observe for 5 seconds
5. Press Stop PIE
6. Adjust parameters
7. Repeat

**Time per iteration:** ~10 seconds

---

### Tip 4: Start with High Stiffness

**Technique:** Begin with rigid attachments, then soften

**Rationale:**
- Rigid attachments are predictable
- Easy to see if attachment is working
- Can always soften later

**Workflow:**
1. First pass: Weight=1.0, Stiffness=1.0, Distance=0.0
2. Test in PIE
3. If too rigid, reduce to Weight=0.8, Stiffness=0.8
4. Test again
5. Continue reducing until motion feels natural

---

## Keyboard Shortcuts (Future)

**Planned shortcuts:**
- `Ctrl+A` - Auto-select top vertices
- `Ctrl+B` - Assign bone to selection
- `Ctrl+S` - Apply to asset
- `Ctrl+D` - Clear selection
- `Ctrl+Z` - Undo (future)
- `Ctrl+Y` - Redo (future)

**Current:** Use mouse clicks in ImGui panel

---

## FAQ

### Q: Can I attach one vertex to multiple bones?

**A:** Not currently. Each vertex can only attach to one bone. Future enhancement may add multi-bone blending.

**Workaround:** Use soft attachments (Weight < 1.0) for smoother transitions between bone regions.

---

### Q: How do I see which vertices are selected?

**A:** Currently via console logs. Phase 5 will add visual heatmap overlay.

**Current:** Check console for "Selected N vertices" message.

---

### Q: Can I undo bone assignments?

**A:** Not currently. Use "Clear All Attachments" to reset.

**Workaround:** Save your scene before making major changes.

---

### Q: Does attachment data save with the scene?

**A:** Yes! Attachment data is serialized to the `.clothasset` file and persists across editor sessions.

**Verification:** Restart editor, load scene, enter PIE - attachments should still work.

---

### Q: Can I reuse attachment data across multiple cloth instances?

**A:** Yes! Attachment data is stored in `UClothAsset`, so all instances using the same asset share the same attachments.

**Note:** If you need instance-specific attachments, duplicate the cloth asset first.

---

## Performance Guidelines

### Vertex Count Recommendations

| Cloth Type | Sim Vertices | Attachments | Performance |
|------------|--------------|-------------|-------------|
| Simple Cape | 200-500 | 20-50 | Excellent |
| Complex Cape | 500-1000 | 50-100 | Good |
| Full Robe | 1000-2000 | 100-200 | Acceptable |
| Very Complex | 2000+ | 200+ | May impact FPS |

**Recommendation:** Keep sim mesh under 1000 vertices for best performance.

---

### Attachment Count Guidelines

**Rule of Thumb:** Attach 5-15% of total vertices

**Examples:**
- 500 vertex cape → 25-75 attachments
- 1000 vertex robe → 50-150 attachments

**Why:** Too many attachments = less cloth motion, more rigid

---

## Conclusion

The Cloth Attachment Paint Editor provides a **simple, code-free workflow** for authoring cloth attachments. Key features:

✅ **ImGui UI** - Easy to use, immediately available  
✅ **Auto-Selection** - Z-threshold based vertex selection  
✅ **Bone Dropdown** - Choose from available bones  
✅ **Parameter Sliders** - Adjust weight, stiffness, distance  
✅ **Live Preview** - See attachments in table view  
✅ **Persistence** - Data saves to asset automatically  
✅ **PIE Integration** - Attachments apply automatically at runtime  

**Workflow Time:** 2-5 minutes per cloth asset  
**Iteration Time:** ~10 seconds per test  
**Learning Curve:** ~15 minutes to master basics  

Happy cloth editing!

---

**For Technical Support:** See [`plans/cloth-attachment-paint-implementation-summary.md`](plans/cloth-attachment-paint-implementation-summary.md)  
**For Advanced Features:** See [`plans/cloth-attachment-paint-editor-design.md`](plans/cloth-attachment-paint-editor-design.md)
