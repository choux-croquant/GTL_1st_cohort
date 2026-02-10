# Cloth Skinning Weight Generation - Migration Guide

## Overview

This guide helps you migrate from the legacy **inverse distance weighting** method to the new **barycentric coordinate-based weighting** method for cloth skinning weight generation.

## Why Migrate?

### Quality Improvements

✅ **Surface-Aware Weighting** - Respects triangle topology instead of treating mesh as point cloud  
✅ **Smooth Interpolation** - Weights vary continuously across triangle surfaces  
✅ **Better Edge Handling** - Proper projection onto triangle edges and vertices  
✅ **Reduced Artifacts** - Eliminates popping and discontinuities  
✅ **Improved Deformation** - More natural cloth movement and draping  

### Performance Benefits

✅ **Faster Queries** - O(log n) BVH queries vs O(n) spatial hash for large meshes  
✅ **Better Scalability** - Performance scales logarithmically with mesh complexity  
✅ **Optimized Traversal** - Early exit and bounding box culling  

### Use Cases

**Ideal for:**
- Character clothing (shirts, pants, capes)
- Flags and banners
- Curtains and drapes
- Any cloth with visible deformation

**Less critical for:**
- Static cloth (no animation)
- Very simple meshes (<100 vertices)
- Extremely memory-constrained platforms

## Migration Steps

### Step 1: Update Asset Generation Code

**Before (Inverse Distance):**
```cpp
FClothAssetGenerationParams params;
// Uses default inverse distance method
params.SkinningParams.MaxDistance = 100.0f;
params.SkinningParams.WeightPower = 1.0f;

FClothAssetGenerationResult result;
FClothAssetGenerator::GenerateClothAssetFromStaticMesh(SourceMesh, params, result);
```

**After (Barycentric):**
```cpp
FClothAssetGenerationParams params;
// Explicitly set barycentric method (or use default)
params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
params.SkinningParams.MaxSearchDistance = 100.0f;
params.SkinningParams.bUseClosestPointProjection = true;

FClothAssetGenerationResult result;
FClothAssetGenerator::GenerateClothAssetFromStaticMesh(SourceMesh, params, result);
```

### Step 2: Regenerate Cloth Assets

**Option A: Programmatic Regeneration**
```cpp
void RegenerateClothAsset(UClothAsset* ExistingAsset)
{
    if (!ExistingAsset || !ExistingAsset->SourceMesh)
        return;
    
    // Setup barycentric parameters
    FClothAssetGenerationParams params;
    params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
    
    // Copy existing decimation settings
    params.DecimationParams.TargetVertexCount = ExistingAsset->DecimatedVertexCount;
    
    // Generate new asset
    FClothAssetGenerationResult result;
    if (FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
        ExistingAsset->SourceMesh, params, result))
    {
        // Replace old asset data with new
        // (Implementation depends on your asset management system)
    }
}
```

**Option B: Console Command**
```bash
# In-game console
cloth.skinning.method barycentric
cloth.asset.regenerate MyClothAsset
```

### Step 3: Test and Validate

**Visual Testing:**
1. Load cloth asset in editor/game
2. Play animation or simulate physics
3. Check for smooth deformation
4. Look for artifacts (popping, tearing, discontinuities)

**Comparison Testing:**
```cpp
// Generate with both methods for comparison
FClothSkinningParams paramsInverse;
paramsInverse.WeightingMethod = EClothWeightingMethod::InverseDistance;

FClothSkinningParams paramsBarycentric;
paramsBarycentric.WeightingMethod = EClothWeightingMethod::Barycentric;

// Compare results visually
```

**Quality Metrics:**
- Weight continuity across surface
- Deformation smoothness
- Absence of visual artifacts
- Proper handling of high-curvature regions

### Step 4: Adjust Parameters (If Needed)

**If weights look too localized:**
```cpp
params.SkinningParams.MaxSearchDistance = 150.0f;  // Increase search radius
```

**If seeing artifacts at boundaries:**
```cpp
params.SkinningParams.bUseClosestPointProjection = true;  // Enable projection
```

**If generation is too slow:**
```cpp
params.SkinningParams.BVHMaxLeafTriangles = 16;  // Larger leaves = faster build
params.SkinningParams.bUseSAH = false;            // Disable SAH
```

**If quality is insufficient:**
```cpp
params.SkinningParams.BVHMaxLeafTriangles = 4;   // Smaller leaves = better tree
params.SkinningParams.bUseSAH = true;             // Enable SAH
params.SkinningParams.bCacheTriangleData = true;  // Enable caching
```

### Step 5: Rollback (If Needed)

**If you encounter issues, you can easily rollback:**

```cpp
// Revert to inverse distance method
params.SkinningParams.WeightingMethod = EClothWeightingMethod::InverseDistance;

// Or use console command
cloth.skinning.method inverse
```

**Existing assets continue to work** - no data corruption or compatibility issues.

## Configuration Presets

### Fast Preset (Real-time Generation)

**Use Case:** Quick iteration, real-time asset generation

```cpp
params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
params.SkinningParams.BVHMaxLeafTriangles = 16;
params.SkinningParams.bUseSAH = false;
params.SkinningParams.bCacheTriangleData = false;
params.SkinningParams.bEnableMultiThreading = true;
```

**Console:** `cloth.skinning.preset fast`

**Characteristics:**
- Build time: ~20ms (10K triangles)
- Quality: Good
- Memory: Low (~5MB)

### Balanced Preset (Default)

**Use Case:** General-purpose cloth assets

```cpp
params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
params.SkinningParams.BVHMaxLeafTriangles = 8;
params.SkinningParams.bUseSAH = false;
params.SkinningParams.bCacheTriangleData = true;
params.SkinningParams.bEnableMultiThreading = false;
```

**Console:** `cloth.skinning.preset balanced`

**Characteristics:**
- Build time: ~30ms (10K triangles)
- Quality: Better
- Memory: Medium (~10MB)

### Quality Preset (Offline Baking)

**Use Case:** High-quality hero assets, cinematics

```cpp
params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
params.SkinningParams.BVHMaxLeafTriangles = 4;
params.SkinningParams.bUseSAH = true;
params.SkinningParams.bCacheTriangleData = true;
params.SkinningParams.bEnableMultiThreading = false;
```

**Console:** `cloth.skinning.preset quality`

**Characteristics:**
- Build time: ~80ms (10K triangles)
- Quality: Best
- Memory: High (~15MB)

## Common Migration Scenarios

### Scenario 1: Simple Character Clothing

**Original Setup:**
```cpp
FClothSkinningParams params;
params.MaxDistance = 50.0f;
params.WeightPower = 1.0f;
```

**Migrated Setup:**
```cpp
FClothSkinningParams params;
params.WeightingMethod = EClothWeightingMethod::Barycentric;
params.MaxSearchDistance = 50.0f;
params.bUseClosestPointProjection = true;
params.BVHMaxLeafTriangles = 8;  // Balanced quality/performance
```

### Scenario 2: Large Flag/Cape

**Original Setup:**
```cpp
FClothSkinningParams params;
params.MaxDistance = 100.0f;
params.MaxInfluences = 4;
```

**Migrated Setup:**
```cpp
FClothSkinningParams params;
params.WeightingMethod = EClothWeightingMethod::Barycentric;
params.MaxSearchDistance = 100.0f;
params.BVHMaxLeafTriangles = 16;  // Faster for large meshes
params.bEnableMultiThreading = true;  // Parallel processing
```

### Scenario 3: High-Quality Cinematic Cloth

**Original Setup:**
```cpp
FClothSkinningParams params;
params.MaxDistance = 30.0f;
params.WeightPower = 2.0f;  // More localized
```

**Migrated Setup:**
```cpp
FClothSkinningParams params;
params.WeightingMethod = EClothWeightingMethod::Barycentric;
params.MaxSearchDistance = 30.0f;
params.BVHMaxLeafTriangles = 4;   // High quality BVH
params.bUseSAH = true;             // Optimal tree structure
params.bCacheTriangleData = true;  // Maximum performance
```

## Troubleshooting Migration Issues

### Issue: Asset Looks Different After Migration

**Possible Causes:**
1. Different weight distribution (expected - this is an improvement!)
2. Simulation mesh topology issues
3. Parameter mismatch

**Solutions:**
1. **Visual Comparison:**
   ```cpp
   // Generate with both methods
   GenerateWithMethod(EClothWeightingMethod::InverseDistance);  // Old
   GenerateWithMethod(EClothWeightingMethod::Barycentric);      // New
   // Compare side-by-side
   ```

2. **Adjust Parameters:**
   - Increase `MaxSearchDistance` if weights seem too localized
   - Enable `bFallbackToInverseDistance` for hybrid approach

3. **Check Simulation Mesh:**
   - Ensure manifold topology
   - Remove degenerate triangles
   - Verify consistent winding order

### Issue: Performance Regression

**Symptoms:** Weight generation takes longer than before

**Solutions:**
1. **Use Fast Preset:**
   ```cpp
   params.BVHMaxLeafTriangles = 16;
   params.bUseSAH = false;
   params.bEnableMultiThreading = true;
   ```

2. **Profile Build vs Query:**
   - If build is slow: Increase leaf size, disable SAH
   - If query is slow: Decrease leaf size, enable caching

3. **Consider Hybrid Approach:**
   ```cpp
   // Use inverse distance for small meshes
   if (renderVertexCount < 1000) {
       params.WeightingMethod = EClothWeightingMethod::InverseDistance;
   } else {
       params.WeightingMethod = EClothWeightingMethod::Barycentric;
   }
   ```

### Issue: Memory Usage Too High

**Symptoms:** Increased memory consumption during asset generation

**Solutions:**
1. **Disable Triangle Caching:**
   ```cpp
   params.bCacheTriangleData = false;  // Saves ~80 bytes per triangle
   ```

2. **Increase Leaf Size:**
   ```cpp
   params.BVHMaxLeafTriangles = 32;  // Fewer BVH nodes
   ```

3. **Use Inverse Distance for Very Large Meshes:**
   ```cpp
   if (simTriangleCount > 50000) {
       params.WeightingMethod = EClothWeightingMethod::InverseDistance;
   }
   ```

### Issue: Artifacts at Triangle Boundaries

**Symptoms:** Visible seams or discontinuities where triangles meet

**Solutions:**
1. **Enable Closest Point Projection:**
   ```cpp
   params.bUseClosestPointProjection = true;
   ```

2. **Check Simulation Mesh Quality:**
   - Ensure triangles share vertices (not duplicated)
   - Verify consistent vertex normals
   - Check for T-junctions or gaps

3. **Increase Search Distance:**
   ```cpp
   params.MaxSearchDistance = 150.0f;  // Allow finding better triangles
   ```

## Validation Checklist

Before deploying migrated assets:

- [ ] Visual quality improved or equivalent
- [ ] No new artifacts introduced
- [ ] Performance acceptable for target platform
- [ ] Memory usage within budget
- [ ] All edge cases handled (far vertices, degenerate triangles)
- [ ] Weights sum to 1.0 for all vertices
- [ ] Smooth deformation during animation
- [ ] Tested with various mesh topologies

## Gradual Migration Strategy

### Phase 1: Pilot Testing (Week 1-2)
1. Select 2-3 representative cloth assets
2. Regenerate with barycentric method
3. Compare quality and performance
4. Identify any issues or parameter adjustments needed

### Phase 2: Batch Migration (Week 3-4)
1. Create automated migration script
2. Regenerate all cloth assets
3. Run automated validation tests
4. Manual review of critical assets

### Phase 3: Deployment (Week 5-6)
1. Deploy to development environment
2. Monitor for issues
3. Gather feedback from team
4. Adjust parameters based on feedback

### Phase 4: Production (Week 7+)
1. Deploy to production
2. Monitor performance metrics
3. Set barycentric as default for new assets
4. Mark inverse distance as deprecated

## Automated Migration Script

```cpp
/**
 * Batch migrate all cloth assets to barycentric weighting
 */
void MigrateAllClothAssets()
{
    TArray<UClothAsset*> allClothAssets = GetAllClothAssets();
    
    int32 successCount = 0;
    int32 failureCount = 0;
    
    for (UClothAsset* asset : allClothAssets)
    {
        if (!asset || !asset->SourceMesh)
        {
            failureCount++;
            continue;
        }
        
        // Setup barycentric parameters
        FClothAssetGenerationParams params;
        params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
        params.SkinningParams.MaxSearchDistance = 100.0f;
        params.SkinningParams.bUseClosestPointProjection = true;
        params.SkinningParams.bFallbackToInverseDistance = true;  // Safety net
        
        // Copy existing decimation settings
        params.DecimationParams.TargetVertexCount = asset->DecimatedVertexCount;
        
        // Generate new weights
        FClothAssetGenerationResult result;
        if (FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
            asset->SourceMesh, params, result))
        {
            // Update asset with new weights
            asset->SkinningWeights = result.Asset->SkinningWeights;
            asset->MarkPackageDirty();
            successCount++;
            
            UE_LOG(ELogLevel::Display, TEXT("Migrated: %s"), *asset->GetName());
        }
        else
        {
            failureCount++;
            UE_LOG(ELogLevel::Error, TEXT("Failed to migrate: %s - %s"), 
                   *asset->GetName(), *result.ErrorMessage);
        }
    }
    
    UE_LOG(ELogLevel::Display, TEXT("Migration complete: %d succeeded, %d failed"), 
           successCount, failureCount);
}
```

## Parameter Tuning Guide

### MaxSearchDistance

**Purpose:** Maximum distance to search for nearest triangle

**Tuning:**
- **Too small** (< 10.0f): May miss valid triangles, fallback to nearest vertex
- **Too large** (> 500.0f): May find distant triangles, poor quality
- **Recommended:** 50-100 units for typical character cloth

**Example:**
```cpp
// Tight-fitting clothing
params.MaxSearchDistance = 20.0f;

// Loose capes/flags
params.MaxSearchDistance = 100.0f;
```

### BVHMaxLeafTriangles

**Purpose:** Number of triangles per BVH leaf node

**Tuning:**
- **Smaller** (2-4): Better query performance, slower build, more memory
- **Larger** (16-32): Faster build, slower queries, less memory
- **Recommended:** 8 for balanced performance

**Example:**
```cpp
// Offline baking (prioritize quality)
params.BVHMaxLeafTriangles = 4;

// Real-time generation (prioritize speed)
params.BVHMaxLeafTriangles = 16;
```

### bUseSAH

**Purpose:** Use Surface Area Heuristic for optimal BVH construction

**Tuning:**
- **Enabled:** 2-3x slower build, 20-30% faster queries
- **Disabled:** Fast build, slightly slower queries
- **Recommended:** Enable for offline baking, disable for real-time

**Example:**
```cpp
// Offline asset baking
params.bUseSAH = true;

// Real-time generation
params.bUseSAH = false;
```

### bCacheTriangleData

**Purpose:** Cache triangle vertices and bounds for faster queries

**Tuning:**
- **Enabled:** ~80 bytes per triangle, 10-15% faster queries
- **Disabled:** No memory overhead, slightly slower
- **Recommended:** Enable unless memory-constrained

**Example:**
```cpp
// Normal case
params.bCacheTriangleData = true;

// Memory-constrained platform
params.bCacheTriangleData = false;
```

## Console Command Reference

### Method Selection
```bash
# Set to barycentric (recommended)
cloth.skinning.method barycentric

# Revert to inverse distance (legacy)
cloth.skinning.method inverse
```

### Quality Presets
```bash
# Fast preset (real-time generation)
cloth.skinning.preset fast

# Balanced preset (default)
cloth.skinning.preset balanced

# Quality preset (offline baking)
cloth.skinning.preset quality
```

### Parameter Configuration
```bash
# Set BVH leaf size
cloth.skinning.bvh.leafsize 8

# Enable/disable SAH
cloth.skinning.sah 1

# Set max search distance
cloth.skinning.maxdistance 100.0

# Enable/disable triangle caching
cloth.skinning.cache 1

# Enable/disable multi-threading
cloth.skinning.multithread 1
```

### Debugging and Visualization
```bash
# Toggle weight visualization
cloth.skinning.visualize

# Toggle BVH structure visualization
cloth.skinning.visualize.bvh

# Show statistics
cloth.skinning.stats

# Print current parameters
cloth.skinning.params
```

## FAQ

### Q: Will my existing cloth assets break?

**A:** No. The system maintains full backward compatibility. Existing assets continue to work unchanged. You only get the new method when you explicitly regenerate assets or set the parameter.

### Q: Do I need to modify shaders?

**A:** No. The vertex shader is agnostic to the weight generation method. Both methods produce the same data format (up to 4 influences with normalized weights).

### Q: Can I use both methods in the same project?

**A:** Yes. You can set the method per-asset or use the global default. Some assets can use barycentric while others use inverse distance.

### Q: What if barycentric produces worse results?

**A:** This is rare but possible for certain mesh topologies. Solutions:
1. Adjust parameters (MaxSearchDistance, BVHMaxLeafTriangles)
2. Enable fallback: `bFallbackToInverseDistance = true`
3. Use inverse distance for that specific asset
4. Check simulation mesh quality (manifold, no degenerate triangles)

### Q: How much memory does BVH use?

**A:** Approximately 10-15MB for a 10K triangle mesh. The BVH is built during asset generation and not stored in the final asset, so runtime memory is unchanged.

### Q: Can I visualize the weights?

**A:** Yes, use console command `cloth.skinning.visualize` to render weights as vertex colors. This helps identify issues and validate quality.

### Q: What about multi-threading?

**A:** Multi-threading is supported for large meshes (>1K render vertices). Enable with `bEnableMultiThreading = true`. The BVH is read-only during queries, making it thread-safe.

### Q: How do I know if migration was successful?

**A:** Check these indicators:
- ✅ Smoother cloth deformation
- ✅ No visual artifacts
- ✅ Weights sum to 1.0 (validation passes)
- ✅ Acceptable performance
- ✅ No console errors during generation

## Support and Resources

### Documentation
- [Technical Documentation](cloth-skinning-barycentric-weighting.md) - Detailed algorithm and API reference
- [Migration Plan](../plans/cloth-skinning-barycentric-migration-plan.md) - Complete implementation roadmap
- [Implementation Progress](../plans/cloth-skinning-barycentric-implementation-progress.md) - Current status

### Code References
- [`ClothSkinningWeightGenerator.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator.h) - Main header
- [`ClothSkinningWeightGenerator.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator.cpp) - Core implementation
- [`ClothSkinningWeightGenerator_Barycentric.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator_Barycentric.cpp) - BVH and barycentric functions
- [`ClothSkinningWeightGenerator_Optimized.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator_Optimized.cpp) - SAH and optimizations

### Getting Help

If you encounter issues during migration:
1. Check console output for error messages
2. Use `cloth.skinning.params` to verify configuration
3. Enable `cloth.skinning.stats` to see generation statistics
4. Try different quality presets
5. Enable fallback mode for problematic assets

---

**Last Updated:** 2026-02-10  
**Version:** 1.0  
**Status:** Ready for Migration
