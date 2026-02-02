# Cloth Rendering Overlap Issue - Root Cause and Fix

## Problem Description
All cloth instances are rendering at the same position (overlapping at origin) instead of being distributed in the scene.

## Root Cause Analysis

### Current Flow (BROKEN)
1. `TestBatchedClothActor::CreateTestCloth()` creates particles in **LOCAL space** (around origin)
2. `ClothBatchManager::AddInstance()` uploads particles **AS-IS in local space**
3. Simulation runs in **WORLD space** (kinematic targets are world positions)
4. All instances' particles start at origin → overlap
5. Vertex shader applies `WorldTransform`, but it's **Identity** for batched mode

### Why They're All At Origin
```cpp
// TestBatchedClothActor.cpp:175-177
pos.X = 0.0f;  // All instances start at local origin!
pos.Y = Spacing * (x - GridSize / 2.0f);
pos.Z = Spacing * (GridSize / 2.0f - y);
```

```cpp
// ClothBatchManager.cpp:182-186
BatchedSolver->UploadParticleData(
    Params.RestPositions,  // ❌ Still in LOCAL space!
    Params.InvMasses,
    instanceIDs,
    metadata.ParticleOffset);
```

## Solution

### Approach: Simulate in World Space
Transform particles to world space during upload, so each instance simulates at its correct location.

### Required Changes

#### 1. Pass World Transform Through Registration Chain
- Add `WorldTransform` to `FClothInstanceCreationParams`
- Get transform from component during registration

#### 2. Transform Particles in AddInstance
- Apply world transform to all initial particle positions
- Transform kinematic target positions to world space
- Particles now start at correct world locations

#### 3. Update Vertex Shader for Batched Mode
- For batched mode: use Identity transform (particles already in world space)
- For legacy mode: keep using WorldTransform (particles in local space)

## Implementation Details

### File Changes
1. `ClothBatchTypes.h` - Add WorldTransform to FClothInstanceCreationParams
2. `ClothWorld.cpp` - Pass component's world transform during registration
3. `ClothBatchManager.cpp` - Transform particles to world space during upload
4. `ClothVertexShader.hlsl` - Use Identity for batched mode
5. `ClothMeshComponent.cpp` - Always update WorldTransform

### Why This Works
- Each instance's particles start at their world position
- Simulation runs in world space (kinematic targets are world positions)
- Vertex shader doesn't double-transform (already in world space)
- Instances stay separated as intended
