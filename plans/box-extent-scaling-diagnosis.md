# Box Extent Scaling Issue - Root Cause Analysis

## Problem Statement

In [`ExtractBoxFromShape()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp:441), the line:

```cpp
Source.CachedExtents = FVector(boxGeom.halfExtents.x * 0.5, boxGeom.halfExtents.y * 0.5, boxGeom.halfExtents.z * 0.5);
```

Sometimes matches the Editor visualization, but sometimes requires a different multiplier. This inconsistency is causing confusion.

## Root Cause Analysis

### The Core Issue: Semantic Mismatch Between Systems

The problem stems from **inconsistent interpretation of "Extent" across different parts of the engine**:

1. **PhysX `PxBoxGeometry::halfExtents`**: Already represents **half-extents** (distance from center to face)
2. **Editor Visualization (`AggregateGeomAttributes::Extent`)**: Also represents **half-extents** (see line 45 in PhysicsAsset.h: `// Half Extent`)
3. **Your Code**: Multiplying by `0.5` again, creating **quarter-extents**

### Evidence from Codebase

#### 1. PhysicsAsset.h Definition
```cpp
// Line 45 in PhysicsAsset.h
UPROPERTY_WITH_FLAGS(EditAnywhere, FVector, Extent, = FVector(1,1,1)) // Half Extent
```
**Interpretation**: The editor stores and displays **half-extents**.

#### 2. Editor Visualization Code
```cpp
// Line 728 in EditorRenderPass.cpp - StaticMeshComponent boxes
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent / 2.0f).ToMatrixWithScale()
```
**Interpretation**: The editor **divides by 2** when creating the transform scale, suggesting `GeomAttribute.Extent` is a half-extent, and they want quarter-extent for some reason (likely a bug or different convention).

#### 3. BoxComponent Visualization
```cpp
// Lines 705, 713 in EditorRenderPass.cpp
b.Extent = BoxComponent->GetBoxExtent();
```
**Interpretation**: `GetBoxExtent()` returns half-extents directly without modification.

#### 4. Cloth Collider Visualization (PIE Mode)
```cpp
// Line 1179 in EditorRenderPass.cpp
b.Extent = Source.CachedExtents;
```
**Interpretation**: Uses `CachedExtents` directly without modification.

### Why the Inconsistency Occurs

The inconsistency happens because **different geometry sources use different conventions**:

| Source Type | Extent Meaning | Needs Conversion? |
|-------------|----------------|-------------------|
| **PhysX PxBoxGeometry** | `halfExtents` = half-extents | ❌ No (already half) |
| **AggregateGeomAttributes** | `Extent` = half-extents | ❌ No (already half) |
| **BoxComponent** | `BoxExtent` = half-extents | ❌ No (already half) |
| **Your Current Code** | `halfExtents * 0.5` = quarter-extents | ✅ **BUG** |

### The Specific Problem

```cpp
// CURRENT CODE (WRONG):
Source.CachedExtents = FVector(boxGeom.halfExtents.x * 0.5, boxGeom.halfExtents.y * 0.5, boxGeom.halfExtents.z * 0.5);
```

**What's happening:**
- PhysX gives you: `halfExtents = 50` (box is 100 units wide, 50 from center to edge)
- You multiply by 0.5: `CachedExtents = 25` (quarter-extent)
- Visualization expects: `50` (half-extent)
- **Result**: Box appears **half the size** it should be

### When It "Works"

The `* 0.5` multiplier might appear correct in cases where:

1. **The PhysX shape was created with doubled extents** (compensating for the bug)
2. **The visualization code also has a bug** that doubles the extent (two wrongs make a right)
3. **You're comparing against StaticMeshComponent boxes** which use `Extent / 2.0f` (line 728), creating the same quarter-extent bug

## Solution

### Recommended Fix

**Remove the `* 0.5` multiplier entirely:**

```cpp
// CORRECT CODE:
Source.CachedExtents = FVector(boxGeom.halfExtents.x, boxGeom.halfExtents.y, boxGeom.halfExtents.z);
```

Or more concisely:
```cpp
Source.CachedExtents = FVector(boxGeom.halfExtents.x, boxGeom.halfExtents.y, boxGeom.halfExtents.z);
```

### Why This Is Correct

1. **PhysX Convention**: `PxBoxGeometry::halfExtents` is already a half-extent
2. **Engine Convention**: All visualization code expects half-extents in `CachedExtents`
3. **Consistency**: Matches how spheres and capsules are handled (no extra scaling)

### Verification Steps

After applying the fix:

1. **Create a box collider** with known dimensions (e.g., 100x100x100 units)
2. **Check PhysX values**: `halfExtents` should be (50, 50, 50)
3. **Check cached values**: `CachedExtents` should be (50, 50, 50)
4. **Check visualization**: Box should render at correct size in PIE mode
5. **Compare with Editor mode**: Box should match the editor gizmo size

## Additional Findings

### Potential Bug in StaticMeshComponent Visualization

Line 728 in [`EditorRenderPass.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:728):

```cpp
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent / 2.0f).ToMatrixWithScale()
```

This divides the extent by 2, which seems incorrect if `GeomAttribute.Extent` is already a half-extent. This might be:
- A compensating bug for another issue
- A different convention for StaticMeshComponent
- An actual bug that should be fixed

**Recommendation**: Investigate this separately, but don't let it influence the cloth collision fix.

## Implementation Plan

### Step 1: Fix ExtractBoxFromShape
```cpp
void FClothCollisionManager::ExtractBoxFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex)
{
    if (!Shape)
        return;
    
    physx::PxBoxGeometry boxGeom;
    if (!Shape->getBoxGeometry(boxGeom))
        return;
    
    physx::PxTransform localPose = Shape->getLocalPose();
    
    FClothColliderSource Source;
    Source.Type = EClothColliderType::Box;
    Source.Component = Component;
    Source.ElementIndex = ElementIndex;
    Source.CachedTransform = Component->GetComponentTransform();
    Source.CachedLocalCenter = FVector(localPose.p.x, localPose.p.y, localPose.p.z);
    Source.CachedLocalRotation = FQuat(localPose.q.x, localPose.q.y, localPose.q.z, localPose.q.w);
    
    // FIX: Use halfExtents directly (already half-extents, no need to divide by 2)
    Source.CachedExtents = FVector(boxGeom.halfExtents.x, boxGeom.halfExtents.y, boxGeom.halfExtents.z);
    
    Source.bIsDirty = true;
    Source.GPUBufferIndex = ColliderSources.Num();
    
    ColliderSources.Add(Source);
}
```

### Step 2: Verify Visualization Code

The visualization code in [`RenderClothColliders()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:1179) is correct:

```cpp
b.Extent = Source.CachedExtents;  // Uses half-extents directly
```

### Step 3: Test Cases

1. **Box with 100x100x100 full size**
   - PhysX: `halfExtents = (50, 50, 50)`
   - Expected: `CachedExtents = (50, 50, 50)`
   - Visual: 100x100x100 box in both Editor and PIE

2. **Box with 200x50x100 full size**
   - PhysX: `halfExtents = (100, 25, 50)`
   - Expected: `CachedExtents = (100, 25, 50)`
   - Visual: 200x50x100 box in both Editor and PIE

## Conclusion

**The `* 0.5` multiplier is incorrect** because:
1. PhysX already provides half-extents
2. The visualization expects half-extents
3. The engine convention uses half-extents throughout

**The fix is simple**: Remove the `* 0.5` multiplier and use the PhysX `halfExtents` values directly.

If you're still seeing size mismatches after this fix, the problem is likely:
- Incorrect PhysX shape creation (doubled extents at source)
- Scale being applied twice in the transform chain
- A different bug in the visualization shader

But the fundamental issue is that **you're creating quarter-extents when you should be using half-extents**.
