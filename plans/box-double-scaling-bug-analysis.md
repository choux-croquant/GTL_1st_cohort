# Box Double-Scaling Bug Analysis

## Problem Statement

In the Editor's [`RenderBoxInstanced()`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:728) function, when you double the Extent value (e.g., from 2 to 4), the rendered box appears **4x larger** instead of 2x larger.

## Root Cause: Double Scaling

The extent is being applied **twice** in the rendering pipeline:

### 1. CPU Side (EditorRenderPass.cpp, line 728)
```cpp
FMatrix WorldMatrix =
    FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent / 2.0f).ToMatrixWithScale()
    * StaticComp->GetWorldMatrix().GetMatrixWithoutScale();
b.WorldMatrix = WorldMatrix;
b.Extent = GeomAttribute.Extent;  // ← Extent stored again!
```

**What happens here:**
- `FTransform(..., GeomAttribute.Extent / 2.0f)` creates a transform with **scale = Extent / 2**
- This scale is baked into `WorldMatrix` via `ToMatrixWithScale()`
- Then `b.Extent = GeomAttribute.Extent` stores the **full extent again**

### 2. GPU Side (EditorShader.hlsl, line 103-106)
```hlsl
float3 Scale = DataBox[instanceID].Extent;  // ← Extent used as scale!

float4 localPos = mul(float4(input.position.xyz * Scale, 1.f), DataBox[instanceID].WorldMatrix);
```

**What happens here:**
- Vertex positions (range -1 to +1) are multiplied by `Scale` (the Extent)
- Then transformed by `WorldMatrix` (which already contains Extent/2 as scale)

### 3. Base Mesh (EditorRenderPass.cpp, line 88-97)
```cpp
const TArray<FVector> CubeFrameVertices = {
    { -1.f, -1.f, -1.f }, // vertices range from -1 to +1
    { -1.f, 1.f, -1.f },
    { 1.f, -1.f, -1.f },
    // ... etc
};
```

**Base cube has vertices from -1 to +1** (total size = 2 units per axis)

## The Math: Why 2x Extent = 4x Visual Size

Let's trace through with `GeomAttribute.Extent = FVector(4, 4, 4)`:

### Step 1: CPU Transform Creation
```cpp
FTransform(..., GeomAttribute.Extent / 2.0f)
// Scale = (4/2, 4/2, 4/2) = (2, 2, 2)
```
This creates a WorldMatrix with scale factor of **2**.

### Step 2: GPU Vertex Scaling
```hlsl
float3 Scale = DataBox[instanceID].Extent;  // Scale = (4, 4, 4)
float4 localPos = mul(float4(input.position.xyz * Scale, 1.f), WorldMatrix);
```

For a vertex at `(1, 0, 0)`:
1. `input.position.xyz * Scale` = `(1, 0, 0) * (4, 4, 4)` = `(4, 0, 0)`
2. `mul(..., WorldMatrix)` applies scale of 2 = `(8, 0, 0)`

**Final vertex position: 8 units from center**

### Step 3: Total Size Calculation

- Base vertex range: -1 to +1 (size = 2)
- After GPU scale (Extent = 4): -4 to +4 (size = 8)
- After WorldMatrix scale (Extent/2 = 2): -8 to +8 (size = **16**)

**Expected size with Extent=4**: 8 units (4 is half-extent, so full size = 8)
**Actual size**: 16 units (**2x too large**)

### Comparison: Extent 2 vs Extent 4

| Extent Value | GPU Scale | WorldMatrix Scale | Final Size | Expected Size | Ratio |
|--------------|-----------|-------------------|------------|---------------|-------|
| 2 | 2 | 1 | 4 | 4 | 1x |
| 4 | 4 | 2 | 16 | 8 | 2x |

When you double the extent (2→4):
- GPU scale doubles: 2→4
- WorldMatrix scale doubles: 1→2
- **Total effect: 2 × 2 = 4x size increase**

## Why This Happens

The rendering pipeline has **two different scaling mechanisms** that are both active:

1. **Transform-based scaling**: `FTransform(..., Extent/2)` bakes scale into WorldMatrix
2. **Explicit vertex scaling**: Shader multiplies vertices by Extent

These compound multiplicatively: `FinalSize = BaseSize × Extent × (Extent/2) = BaseSize × Extent²/2`

## Solutions

You have **three options** to fix this:

### Option 1: Remove GPU Scaling (Recommended)

**Change shader to not scale vertices:**

```hlsl
// EditorShader.hlsl, line 106
// BEFORE:
float4 localPos = mul(float4(input.position.xyz * Scale, 1.f), DataBox[instanceID].WorldMatrix);

// AFTER:
float4 localPos = mul(float4(input.position.xyz, 1.f), DataBox[instanceID].WorldMatrix);
```

**And adjust CPU transform to use full extent:**

```cpp
// EditorRenderPass.cpp, line 728
// BEFORE:
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent / 2.0f)

// AFTER:
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent)
```

**Result**: WorldMatrix contains the full scale, shader just transforms vertices.

### Option 2: Remove CPU Scaling

**Change CPU to not scale the transform:**

```cpp
// EditorRenderPass.cpp, line 728
// BEFORE:
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent / 2.0f)

// AFTER:
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, FVector::OneVector)
```

**Keep shader scaling as-is:**
```hlsl
float4 localPos = mul(float4(input.position.xyz * Scale, 1.f), DataBox[instanceID].WorldMatrix);
```

**Result**: Shader does all the scaling via Extent.

### Option 3: Remove Extent from Constant Buffer

**Don't pass Extent to shader at all:**

```cpp
// EditorRenderPass.cpp, line 728
FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent)
// Don't set b.Extent (or set it to FVector::OneVector)
```

**And remove shader scaling:**
```hlsl
float4 localPos = mul(float4(input.position.xyz, 1.f), DataBox[instanceID].WorldMatrix);
```

**Result**: All scaling is in WorldMatrix only.

## Recommended Solution

**Option 1** is recommended because:
1. It's clearer: WorldMatrix contains the complete transform
2. It's more efficient: Less shader math
3. It's consistent with how other engines handle box rendering

## Implementation

### Step 1: Fix EditorRenderPass.cpp

```cpp
// Line 728 - Change from Extent/2.0f to Extent
FMatrix WorldMatrix =
    FTransform(GeomAttribute.Rotation, GeomAttribute.Offset, GeomAttribute.Extent).ToMatrixWithScale()
    * StaticComp->GetWorldMatrix().GetMatrixWithoutScale();
b.WorldMatrix = WorldMatrix;
b.Extent = FVector::OneVector;  // No longer needed for scaling
```

### Step 2: Fix EditorShader.hlsl

```hlsl
// Line 106 - Remove vertex scaling
float4 localPos = mul(float4(input.position.xyz, 1.f), DataBox[instanceID].WorldMatrix);
```

### Step 3: Verify Other Box Rendering Paths

Check these locations for similar issues:
- Line 704-705: `BoxComponent->GetBoxExtent()` path
- Line 712-713: Another BoxComponent path
- Line 1179: Cloth collider box rendering (PIE mode)

## Why the Original Code Had `/2.0f`

The `/2.0f` was likely an attempt to compensate for the fact that:
- `GeomAttribute.Extent` is a **half-extent** (distance from center to face)
- Base cube vertices range from -1 to +1 (size = 2)
- To get correct size: `vertices (-1 to +1) × Extent = (-Extent to +Extent)` ✓

But then the shader **also multiplies by Extent**, causing the double-scaling bug.

## Verification Test

After fixing, test with these values:

| Extent Y | Expected Full Height | Actual Height (should match) |
|----------|---------------------|------------------------------|
| 1 | 2 units | 2 units |
| 2 | 4 units | 4 units |
| 4 | 8 units | 8 units |
| 8 | 16 units | 16 units |

The ratio between any two extents should be linear (2x extent = 2x size), not quadratic.

## Summary

**The bug**: Extent is applied twice (once in WorldMatrix as Extent/2, once in shader as Extent)

**The effect**: Doubling extent causes 4x size increase (2 × 2 = 4)

**The fix**: Remove one of the scaling operations (recommended: remove shader scaling, use full Extent in WorldMatrix)
