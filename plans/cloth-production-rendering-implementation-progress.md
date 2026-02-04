# Cloth Production Rendering System - Implementation Progress

## Status: Phases 1-4 Complete, Ready for Integration

**Date:** 2026-02-04  
**Implementation:** Production cloth rendering with GPU skinning

---

## ✅ Completed Phases

### Phase 1: GPU Buffer Infrastructure ✅ COMPLETE

**Created Files:**
1. [`ClothGPURenderStructs.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPURenderStructs.h) - GPU-compatible structures
   - `FClothSkinningWeightGPU` (32 bytes) - Maps render vertices to sim vertices
   - `FClothInstanceConstants` (96 bytes) - Per-instance constant buffer
   - `FClothRenderVertex` (32 bytes) - Render mesh vertex structure

2. [`ClothGPURenderStructs.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPURenderStructs.cpp) - Helper implementations
   - `SetWorldMatrix()` - Matrix conversion helper

**Modified Files:**
1. [`FClothBatchedSolver.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h)
   - Added render buffer declarations
   - Added `AllocateRenderBuffers()` method
   - Added `UploadRenderMeshData()` and `UploadSkinningWeights()` methods
   - Added accessor methods for render buffers

2. [`FClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)
   - Implemented `AllocateRenderBuffers()` - Creates unified render vertex/index/skinning buffers
   - Implemented `UploadRenderMeshData()` - Uploads render mesh to GPU
   - Implemented `UploadSkinningWeights()` - Uploads skinning weights with global index conversion
   - Added buffer initialization and release code

3. [`FClothInstanceMetadata`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h:95)
   - Added `RenderVertexOffset`, `RenderVertexCount`
   - Added `RenderIndexOffset`, `RenderIndexCount`

4. [`FClothBatchManager.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.h)
   - Added `TotalRenderVertexCount`, `TotalRenderIndexCount`
   - Added `AllocatedRenderVertexCapacity`, `AllocatedRenderIndexCapacity`

5. [`FClothBatchManager.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp)
   - Updated constructor to initialize render mesh tracking variables
   - Extended `AddInstance()` to upload render mesh data and skinning weights
   - Transforms render mesh to world space
   - Converts skinning weight indices to global indices

---

### Phase 2: Asset Integration ✅ COMPLETE

**Modified Files:**
1. [`FClothInstanceCreationParams`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h:129)
   - Added `bUseRenderMesh` flag
   - Added `RenderRestPositions`, `RenderNormals`, `RenderUVs`, `RenderIndices`
   - Added `SkinningWeights` array

2. [`FClothRenderData`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.h:25)
   - Added `UnifiedRenderIndexBuffer`
   - Added `SkinningWeightBufferSRV`
   - Added `RenderVertexOffset`, `RenderVertexCount`, `RenderIndexOffset`, `RenderIndexCount`
   - Added `bUseProductionRendering` flag

3. [`UClothMeshComponent::GetRenderData()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp:62)
   - Extended to populate production rendering data
   - Checks if asset has render mesh (`bUseRenderMesh`)
   - Retrieves render buffers and offsets from batch manager

4. [`FClothWorld::RegisterClothInstanceBatched()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothWorld.cpp:167)
   - Extended to populate render mesh data from asset
   - Passes render mesh arrays to creation params

---

### Phase 3: Shader Development ✅ COMPLETE

**Created Files:**
1. [`ClothProductionVertexShader.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionVertexShader.hlsl)
   - GPU skinning implementation with Linear Blend Skinning (LBS)
   - Samples up to 4 simulation vertices per render vertex
   - Blends positions and normals based on skinning weights
   - Transforms to clip space
   - Generates tangent for normal mapping

2. [`ClothProductionPixelShader.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionPixelShader.hlsl)
   - Full PBR material evaluation
   - Samples albedo, normal, metallic, roughness, emissive textures
   - Applies normal mapping
   - Uses existing PBR lighting system
   - Supports two-sided rendering

**Shader Resources:**
- **Vertex Shader:**
  - t9: SimPositionBuffer (dynamic simulation positions)
  - t10: SimNormalBuffer (dynamic simulation normals)
  - t11: SkinningWeightBuffer (static skinning weights)
  - b10: ClothInstanceConstants (per-instance offsets)
  - b12: ObjectBuffer (world transform)
  - b13: CameraBuffer (view/projection)

- **Pixel Shader:**
  - t0-t8: MaterialTextures (albedo, normal, roughness, etc.)
  - b0: LightInfoBuffer (lighting data)
  - b1: MaterialBuffer (material properties)
  - b13: CameraBuffer (view position)

---

### Phase 4: Render Pass Implementation ✅ COMPLETE

**Created Files:**
1. [`ClothRenderPass.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/ClothRenderPass.h)
   - Production cloth render pass class
   - Inherits from `FRenderPassBase`
   - Manages production shaders and resources

2. [`ClothRenderPass.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/ClothRenderPass.cpp)
   - `CreateResource()` - Loads production shaders, creates input layout, constant buffers
   - `PrepareRenderArr()` - Collects cloth components with render meshes
   - `PrepareRender()` - Sets render state, binds common buffers
   - `RenderClothComponent()` - Renders individual cloth with GPU skinning
   - `BindMaterial()` - Binds material textures and constants
   - `CleanUpRender()` - Unbinds resources
   - Two-sided solid rasterizer state (D3D11_CULL_NONE)

**Key Features:**
- Material binding optimization (caches last bound material)
- Full PBR material support
- Two-sided rendering for cloth
- Per-instance constant buffer updates
- Proper resource binding/unbinding

---

## 🔄 Remaining Phases

### Phase 5: Renderer Integration (NEXT)

**Tasks:**
- [ ] Add `FClothRenderPass* ClothRenderPass` to `FRenderer`
- [ ] Initialize in `FRenderer::Initialize()`
- [ ] Add show flags: `SF_Cloth`, `SF_ClothDebug`
- [ ] Modify `RenderOpaque()` to call cloth render passes
- [ ] Ensure correct rendering order

**Files to Modify:**
- `Renderer.h`
- `Renderer.cpp`
- `ShowFlags.h` (if exists) or equivalent

---

### Phase 6: Batched Material Binding (Optimization)

**Tasks:**
- [ ] Sort cloth instances by material ID before rendering
- [ ] Batch instances with same material
- [ ] Profile draw call overhead

---

### Phase 7: Debug Visualization (Tools)

**Tasks:**
- [ ] Verify `FClothDebugRenderPass` still works
- [ ] Add editor UI for show flag toggles
- [ ] Test debug/production toggle

---

### Phase 8: Testing & Optimization (Quality)

**Tasks:**
- [ ] Test single instance rendering
- [ ] Test multiple batched instances
- [ ] Test material variety
- [ ] Test two-sided rendering
- [ ] GPU profiling
- [ ] Visual quality validation
- [ ] Performance benchmarking

---

## Technical Summary

### Data Flow (Implemented)

```
Asset Generation (Offline)
  ↓
UClothAsset (RenderMesh + SimMesh + SkinningWeights)
  ↓
ClothWorld::RegisterClothInstanceBatched()
  ↓
FClothBatchManager::AddInstance()
  ├─ Upload SimMesh → UnifiedPositionBuffer
  ├─ Upload RenderMesh → UnifiedRenderVertexBuffer
  ├─ Upload Indices → UnifiedRenderIndexBuffer
  └─ Upload SkinningWeights → UnifiedSkinningWeightBuffer (with global index conversion)
  ↓
UClothMeshComponent::GetRenderData()
  ├─ Populate simulation data (for debug)
  └─ Populate production data (for rendering)
  ↓
FClothRenderPass::Render()
  ├─ Bind simulation buffers (t9, t10)
  ├─ Bind skinning weights (t11)
  ├─ Bind material textures (t0-t8)
  ├─ Update per-instance constants (b10)
  └─ DrawIndexed(RenderIndexCount)
  ↓
GPU Vertex Shader (ClothProductionVertexShader.hlsl)
  ├─ Read skinning weights
  ├─ Sample 4 sim positions
  ├─ Blend: finalPos = Σ(weight[i] * simPos[i])
  └─ Transform to clip space
  ↓
GPU Pixel Shader (ClothProductionPixelShader.hlsl)
  ├─ Sample material textures
  ├─ Apply normal mapping
  ├─ PBR lighting calculation
  └─ Output final color
```

### Buffer Layout (Implemented)

**Simulation Buffers (Existing):**
- `UnifiedPositionBuffer[MaxParticles]` - Dynamic simulation positions
- `UnifiedNormalBuffer[MaxParticles]` - Dynamic simulation normals
- `UnifiedIndexBuffer[MaxIndices]` - Simulation mesh indices (global)

**Production Rendering Buffers (NEW):**
- `UnifiedRenderVertexBuffer[MaxRenderVertices]` - Render mesh vertices (position, normal, UV)
- `UnifiedRenderIndexBuffer[MaxRenderIndices]` - Render mesh indices
- `UnifiedSkinningWeightBuffer[MaxRenderVertices]` - Skinning weights (4 influences per vertex)

### Key Implementation Details

1. **Global Index Conversion:**
   - Skinning weight indices are converted from local to global during upload
   - `globalIndex = localIndex + ParticleOffset`
   - Ensures correct sampling from unified simulation buffers

2. **World Space Transformation:**
   - Render mesh positions/normals transformed to world space during upload
   - Matches simulation mesh transformation
   - ClothWorldMatrix in shader is typically identity for batched mode

3. **Two-Sided Rendering:**
   - Rasterizer state: `D3D11_CULL_NONE`
   - Both front and back faces render
   - Critical for flags, capes, and other cloth

4. **Material System Integration:**
   - Uses existing material constant buffer (b1)
   - Binds textures to slots t0-t8
   - Supports full PBR workflow

---

## Next Steps

1. **Integrate into Renderer** (Phase 5)
   - Add ClothRenderPass to main renderer
   - Wire up show flags
   - Test rendering pipeline

2. **Optimize Material Binding** (Phase 6)
   - Implement material sorting
   - Reduce state changes

3. **Testing** (Phase 8)
   - Validate visual quality
   - Profile performance
   - Test edge cases

---

## Known Issues / Notes

1. **IntelliSense Errors:** Pre-existing UE_LOG errors in ClothBatchedSolver.cpp and ClothBatchManager.cpp are unrelated to this implementation (namespace issues with ELogLevel)

2. **FMatrix Include:** Using forward declaration in ClothGPURenderStructs.h to avoid circular dependencies

3. **Buffer Allocation:** Render buffers allocated on-demand when first render mesh is added (lazy allocation)

4. **Skinning Weight Validation:** Should add validation to ensure weights sum to 1.0 (future enhancement)

---

## Files Created (9 files)

1. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPURenderStructs.h`
2. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPURenderStructs.cpp`
3. `EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionVertexShader.hlsl`
4. `EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionPixelShader.hlsl`
5. `EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/ClothRenderPass.h`
6. `EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/ClothRenderPass.cpp`

## Files Modified (7 files)

1. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h`
2. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h`
3. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp`
4. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.h`
5. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchManager.cpp`
6. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.h`
7. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp`
8. `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothWorld.cpp`

---

## Implementation Highlights

### GPU Skinning Algorithm
```hlsl
// For each render vertex:
float3 skinnedPosition = float3(0, 0, 0);
for (int i = 0; i < 4; ++i)
{
    float weight = skinning.Weights[i];
    if (weight > 0.0)
    {
        uint simVertexIndex = skinning.SimVertexIndices[i];
        float3 simPos = SimPositionBuffer[simVertexIndex].xyz;
        skinnedPosition += weight * simPos;
    }
}
```

### Buffer Allocation Pattern
```cpp
// Lazy allocation on first use
if (!UnifiedRenderVertexBuffer)
{
    AllocateRenderBuffers(estimatedMaxVertices, estimatedMaxIndices);
}
```

### Skinning Weight Conversion
```cpp
// Convert local sim indices to global indices
for (const FClothSkinningWeight& localWeight : Params.SkinningWeights)
{
    FClothSkinningWeight globalWeight = localWeight;
    for (int i = 0; i < 4; ++i)
    {
        if (globalWeight.Weights[i] > 0.0f)
        {
            globalWeight.SimVertexIndices[i] += metadata.ParticleOffset;
        }
    }
    globalSkinningWeights.Add(globalWeight);
}
```

---

## Ready for Phase 5: Renderer Integration

The foundation is complete. Next step is to integrate the ClothRenderPass into the main renderer and add show flag support.
