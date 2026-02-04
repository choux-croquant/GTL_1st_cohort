# Cloth Production Rendering - Testing & Validation Guide

## Overview

This guide provides comprehensive testing procedures for validating the cloth production rendering system implementation.

---

## Pre-Testing Checklist

### 1. Compilation
```bash
# Build the project
# Verify no compilation errors related to cloth rendering
# Check that all new files are included in build
```

**Expected Result:**
- Clean compilation
- No linker errors
- Shaders compile successfully

### 2. Asset Preparation
Ensure you have cloth assets with render mesh data:
- `UClothAsset` with `bUseRenderMesh = true`
- `RenderRestPositions`, `RenderNormals`, `RenderUVs`, `RenderIndices` populated
- `SkinningWeights` array populated (one per render vertex)

**How to Generate:**
```cpp
// In ClothMeshComponent
SourceStaticMesh = /* assign your mesh */;
SimulationMeshReductionRatio = 0.1f;  // 10% reduction
GenerateClothAsset();  // This creates render mesh + skinning weights
```

---

## Test Phase 1: Single Instance Rendering

### Test 1.1: Basic Rendering
**Setup:**
- Create one cloth instance with a simple material (solid color)
- Ensure `bUseRenderMesh = true` in asset

**Validation:**
- [ ] Cloth renders without crashes
- [ ] Geometry appears correct (no missing triangles)
- [ ] Material color displays correctly
- [ ] Cloth animates smoothly
- [ ] No visual artifacts or seams

**Debug Steps if Failed:**
1. Check console for errors: "ClothBatchedSolver: Allocated render buffers"
2. Verify render data populated: Add breakpoint in `UClothMeshComponent::GetRenderData()`
3. Check shader compilation: Look for "Failed to compile Cloth Production" errors
4. Validate buffer uploads: Check logs for "Uploaded render mesh" messages

### Test 1.2: Material Textures
**Setup:**
- Assign material with textures (albedo, normal, roughness, metallic)
- Use test cloth mesh with proper UVs

**Validation:**
- [ ] Albedo texture displays correctly
- [ ] Normal mapping works (surface detail visible)
- [ ] Roughness affects specular highlights
- [ ] Metallic surfaces reflect correctly
- [ ] Emissive areas glow

**Debug Steps if Failed:**
1. Verify texture binding in `BindMaterial()`
2. Check material constant buffer update
3. Validate UV coordinates in render mesh
4. Use RenderDoc to inspect texture bindings

### Test 1.3: Two-Sided Rendering
**Setup:**
- Create a flag or cape (single-sided geometry)
- View from both front and back

**Validation:**
- [ ] Both sides of cloth are visible
- [ ] Lighting correct on both sides
- [ ] No backface culling artifacts
- [ ] Normals face correct direction

**Debug Steps if Failed:**
1. Check rasterizer state: `CullMode = D3D11_CULL_NONE`
2. Verify normal calculation in vertex shader
3. Inspect pixel shader lighting on backfaces

---

## Test Phase 2: Multiple Instance Rendering

### Test 2.1: Same Material Batching
**Setup:**
- Create 5-10 cloth instances with identical material
- Distribute in scene

**Validation:**
- [ ] All instances render correctly
- [ ] No z-fighting or overlap issues
- [ ] Performance acceptable (check FPS)
- [ ] Material binding optimized (check logs)

**Performance Metrics:**
- Target: <2ms GPU time for 10 instances
- CPU overhead: <0.1ms for sorting
- Draw calls: 1 per material group

**Debug Steps if Failed:**
1. Profile with GPU timing: Check "ClothProductionPass_GPU" counter
2. Verify material caching: `LastBoundMaterial` should prevent rebinds
3. Check instance sorting in `PrepareRenderArr()`

### Test 2.2: Different Materials
**Setup:**
- Create 5-10 cloth instances with different materials
- Mix textures and solid colors

**Validation:**
- [ ] Each instance displays correct material
- [ ] Material switching works correctly
- [ ] No texture bleeding between instances
- [ ] Performance acceptable

**Performance Metrics:**
- Draw calls: 1 per unique material
- State changes: Minimized by sorting

**Debug Steps if Failed:**
1. Verify material sorting groups correctly
2. Check material constant buffer updates per instance
3. Validate texture binding per material

---

## Test Phase 3: GPU Skinning Quality

### Test 3.1: Skinning Smoothness
**Setup:**
- Create cloth with visible deformation (flag waving, cape flowing)
- Observe during simulation

**Validation:**
- [ ] Smooth deformation (no popping or jittering)
- [ ] No seams or discontinuities
- [ ] Render mesh follows simulation accurately
- [ ] No stretching artifacts

**Debug Steps if Failed:**
1. Validate skinning weights sum to 1.0
2. Check simulation mesh quality (not too coarse)
3. Verify global index conversion in `UploadSkinningWeights()`
4. Inspect skinning weight generation quality

### Test 3.2: Skinning Weight Visualization
**Setup:**
- Optional: Add debug visualization for skinning weights
- Color-code by number of influences or weight values

**Validation:**
- [ ] Most vertices have 3-4 influences
- [ ] Weights distributed smoothly
- [ ] No vertices with zero influences

**Debug Visualization Code (Optional):**
```hlsl
// In pixel shader, visualize skinning weights
float4 debugColor = float4(
    skinning.Weights[0],  // R
    skinning.Weights[1],  // G
    skinning.Weights[2],  // B
    1.0
);
return debugColor;
```

---

## Test Phase 4: Edge Cases

### Test 4.1: Large Mesh
**Setup:**
- Cloth with 10,000+ render vertices
- High triangle count

**Validation:**
- [ ] Buffers allocate successfully
- [ ] No memory errors
- [ ] Performance acceptable
- [ ] Rendering correct

**Debug Steps if Failed:**
1. Check buffer capacity: `AllocatedRenderVertexCapacity`
2. Verify lazy allocation triggers correctly
3. Monitor VRAM usage

### Test 4.2: Instance Add/Remove
**Setup:**
- Dynamically add and remove cloth instances during runtime
- Test buffer management

**Validation:**
- [ ] New instances render correctly
- [ ] Removed instances don't leave artifacts
- [ ] Buffer offsets remain valid
- [ ] No memory leaks

**Debug Steps if Failed:**
1. Verify metadata updates in `AddInstance()` and `RemoveInstance()`
2. Check buffer compaction logic
3. Validate offset calculations

### Test 4.3: Zero Render Mesh
**Setup:**
- Cloth asset with `bUseRenderMesh = false`
- Should fall back to debug rendering

**Validation:**
- [ ] No crashes
- [ ] Falls back to debug wireframe
- [ ] `bUseProductionRendering = false` in render data

---

## Test Phase 5: Visual Quality

### Test 5.1: Lighting Correctness
**Setup:**
- Place cloth under various lighting conditions
- Directional, point, and spot lights

**Validation:**
- [ ] PBR lighting looks correct
- [ ] Shadows render properly
- [ ] Specular highlights accurate
- [ ] Ambient occlusion works

### Test 5.2: Normal Mapping
**Setup:**
- Material with detailed normal map
- View at various angles

**Validation:**
- [ ] Surface detail visible
- [ ] Tangent space calculation correct
- [ ] No normal map artifacts
- [ ] Lighting responds to normal map

### Test 5.3: Animation Quality
**Setup:**
- Simulate cloth with wind or gravity
- Observe during motion

**Validation:**
- [ ] Smooth animation (60 FPS target)
- [ ] No stuttering or frame drops
- [ ] Deformation looks natural
- [ ] No temporal artifacts

---

## Test Phase 6: Performance Profiling

### GPU Profiling
**Tools:** RenderDoc, PIX, or built-in GPU timing

**Metrics to Capture:**
```
ClothProductionPass_GPU:
  - Vertex shader time
  - Pixel shader time
  - Total pass time
  
Per 10 instances:
  - Target: <2.0ms total
  - Vertex shader: <0.5ms
  - Pixel shader: <1.5ms
```

**Validation:**
- [ ] GPU time within target
- [ ] No GPU stalls
- [ ] Efficient buffer access
- [ ] Minimal state changes

### CPU Profiling
**Metrics to Capture:**
```
ClothProductionPass_CPU:
  - PrepareRenderArr time (sorting)
  - Material binding time
  - Constant buffer updates
  
Per 10 instances:
  - Target: <0.2ms total
  - Sorting: <0.05ms
  - Rendering: <0.15ms
```

**Validation:**
- [ ] CPU overhead minimal
- [ ] No CPU bottlenecks
- [ ] Sorting efficient

### Memory Profiling
**Metrics to Capture:**
```
VRAM Usage:
  - Render vertex buffer size
  - Render index buffer size
  - Skinning weight buffer size
  
Per 10 instances (10K render verts each):
  - Target: <50MB overhead
  - Vertices: ~3.2MB
  - Indices: ~1.2MB
  - Skinning: ~3.2MB
```

**Validation:**
- [ ] Memory usage within budget
- [ ] No memory leaks
- [ ] Buffers sized appropriately

---

## Test Phase 7: Debug Tools

### Test 7.1: Debug Overlay
**Setup:**
- Enable both production and debug rendering
- Toggle debug visualization

**Validation:**
- [ ] Production rendering works independently
- [ ] Debug wireframe overlays correctly
- [ ] Can toggle debug on/off
- [ ] No interference between modes

### Test 7.2: Show Flags
**Setup:**
- Toggle cloth rendering via show flags (when UI implemented)

**Validation:**
- [ ] SF_Cloth toggles production rendering
- [ ] SF_ClothDebug toggles debug overlay
- [ ] Flags work independently
- [ ] Zero cost when disabled

---

## Debugging Tools

### RenderDoc Capture
**Steps:**
1. Launch with RenderDoc
2. Capture frame with cloth rendering
3. Inspect draw calls

**What to Check:**
- Vertex shader inputs (skinning weights, sim positions)
- Constant buffer values (offsets, counts)
- Texture bindings (material textures)
- Draw call parameters (index count, offsets)
- Output (final rendered pixels)

### Console Commands
**Useful Commands:**
```
// Toggle cloth rendering
ShowFlag.Cloth 0/1

// Toggle debug visualization
ShowFlag.ClothDebug 0/1

// GPU timing
Stat GPU

// Memory stats
Stat Memory
```

### Logging
**Key Log Messages to Monitor:**
```
"ClothBatchedSolver: Allocated render buffers - Vertices: X, Indices: Y"
"ClothBatchedSolver: Uploaded render mesh - Vertices: X, Indices: Y"
"ClothBatchedSolver: Uploaded skinning weights - Count: X"
"ClothBatchManager: Uploaded render mesh - Vertices: X, Indices: Y"
"ClothRenderPass: Production rendering resources created successfully"
```

---

## Common Issues & Solutions

### Issue 1: Cloth Not Rendering
**Symptoms:** Cloth invisible or not appearing

**Checks:**
1. Verify `bUseRenderMesh = true` in asset
2. Check `bUseProductionRendering = true` in render data
3. Verify buffers allocated: Check logs for "Allocated render buffers"
4. Ensure ClothRenderPass initialized: Check "Production rendering resources created"

**Solution:**
- Regenerate cloth asset with render mesh enabled
- Verify asset has skinning weights
- Check component visibility flags

### Issue 2: Visual Artifacts
**Symptoms:** Seams, stretching, or discontinuities

**Checks:**
1. Validate skinning weights sum to 1.0
2. Check simulation mesh quality (not too coarse)
3. Verify global index conversion correct
4. Inspect skinning weight generation parameters

**Solution:**
- Increase simulation mesh density (reduce reduction ratio)
- Adjust skinning weight generation parameters
- Validate render mesh UVs and normals

### Issue 3: Performance Issues
**Symptoms:** Low FPS, GPU bottleneck

**Checks:**
1. Profile GPU time per pass
2. Check draw call count
3. Verify material batching working
4. Monitor VRAM usage

**Solution:**
- Reduce render mesh complexity
- Optimize material count
- Enable material batching
- Consider LOD system

### Issue 4: Material Not Displaying
**Symptoms:** Wrong colors, missing textures

**Checks:**
1. Verify material assigned to component
2. Check texture loading
3. Validate material constant buffer
4. Inspect shader resource bindings

**Solution:**
- Reassign material
- Verify texture paths
- Check material flags
- Use RenderDoc to inspect bindings

---

## Success Criteria Validation

### Functional Requirements
- [ ] Render mesh displays with correct geometry
- [ ] Textures and materials render correctly
- [ ] GPU skinning produces smooth deformation
- [ ] Skinning accurately follows simulation
- [ ] Batched rendering supports per-instance materials
- [ ] Both front and back faces render
- [ ] Debug visualization toggles independently
- [ ] No crashes or GPU errors

### Performance Requirements
- [ ] 60 FPS with 10+ instances
- [ ] <50MB VRAM overhead per 10 instances
- [ ] <0.1ms CPU overhead per frame
- [ ] <1 draw call per material group

### Quality Requirements
- [ ] No visible seams or discontinuities
- [ ] Proper depth sorting
- [ ] Correct lighting on both sides
- [ ] Material properties render as expected
- [ ] Smooth animation without stuttering
- [ ] Normal mapping works correctly

---

## Regression Testing

### Existing Functionality
Verify these existing features still work:

- [ ] Debug wireframe rendering (FClothDebugRenderPass)
- [ ] Cloth simulation (batched solver)
- [ ] Collision detection
- [ ] Kinematic attachments
- [ ] LOD system
- [ ] Instance add/remove

---

## Performance Benchmarks

### Baseline (Debug Rendering)
- 10 instances: ~0.5ms GPU (wireframe)
- Draw calls: 10
- Memory: Simulation buffers only

### Target (Production Rendering)
- 10 instances: <2.0ms GPU (with materials)
- Draw calls: Minimize via material grouping
- Memory: <50MB overhead

### Measurement Points
1. **GPU Time:**
   - Use `QUICK_GPU_SCOPE_CYCLE_COUNTER(ClothProductionPass_GPU)`
   - Compare with baseline

2. **CPU Time:**
   - Use `QUICK_SCOPE_CYCLE_COUNTER(ClothProductionPass_CPU)`
   - Should be <0.2ms

3. **Memory:**
   - Monitor VRAM before/after cloth instances
   - Check buffer sizes match expectations

---

## Visual Validation Checklist

### Geometry
- [ ] Mesh topology correct
- [ ] No missing triangles
- [ ] No inverted faces
- [ ] Proper vertex count

### Materials
- [ ] Albedo color/texture correct
- [ ] Normal map detail visible
- [ ] Metallic surfaces reflective
- [ ] Roughness affects shininess
- [ ] Emissive areas glow

### Animation
- [ ] Smooth deformation
- [ ] Follows simulation accurately
- [ ] No lag or delay
- [ ] No popping or jittering

### Lighting
- [ ] Responds to directional lights
- [ ] Responds to point lights
- [ ] Responds to spot lights
- [ ] Shadows render correctly
- [ ] Ambient lighting works

---

## Advanced Testing

### Stress Test
**Setup:**
- 20+ cloth instances
- Various materials
- Complex lighting

**Validation:**
- [ ] System remains stable
- [ ] Performance degrades gracefully
- [ ] No memory leaks
- [ ] No buffer overflows

### Edge Cases
**Test Scenarios:**
1. Zero render vertices (should skip rendering)
2. Mismatched sim/render mesh (should still work)
3. Missing skinning weights (should log error)
4. Invalid material (should use default)
5. Extreme deformation (should clamp)

---

## Troubleshooting Guide

### Symptom: Black Cloth
**Possible Causes:**
- Material not bound
- Textures not loaded
- Lighting disabled
- Shader compilation failed

**Solution:**
- Check material assignment
- Verify texture paths
- Enable lighting
- Check shader compilation logs

### Symptom: Stretched/Distorted Mesh
**Possible Causes:**
- Skinning weights incorrect
- Global index conversion wrong
- Simulation mesh too coarse
- Transform mismatch

**Solution:**
- Regenerate skinning weights
- Verify index conversion: `globalIdx = localIdx + ParticleOffset`
- Increase simulation mesh density
- Check world space transformation

### Symptom: Flickering/Artifacts
**Possible Causes:**
- Z-fighting
- Buffer synchronization issue
- Incorrect depth testing
- Temporal instability

**Solution:**
- Adjust depth bias
- Verify buffer updates
- Check depth stencil state
- Profile frame timing

---

## Validation Checklist Summary

### Before Release
- [ ] All functional tests pass
- [ ] Performance meets targets
- [ ] Visual quality acceptable
- [ ] No crashes or errors
- [ ] Documentation complete
- [ ] Code reviewed

### Post-Release Monitoring
- [ ] User feedback collected
- [ ] Performance metrics tracked
- [ ] Bug reports addressed
- [ ] Optimization opportunities identified

---

## Next Steps After Testing

1. **If Tests Pass:**
   - Document any performance characteristics
   - Create user guide for artists
   - Plan future enhancements (LOD, compute skinning, etc.)

2. **If Tests Fail:**
   - Use this guide to debug issues
   - Fix bugs and re-test
   - Update implementation as needed

3. **Optimization Opportunities:**
   - Implement compute shader skinning
   - Add LOD system for render mesh
   - Implement indirect rendering
   - Add material instancing

---

## Contact/Support

For issues or questions during testing:
- Check implementation docs: `cloth-production-rendering-implementation-complete.md`
- Review original plan: `cloth-production-rendering-implementation.md`
- Inspect code comments in modified files
- Use RenderDoc/PIX for GPU debugging

The implementation is complete and ready for validation. Good luck with testing!
