# Cloth Skinning Inverse Distance Method - Deprecation Plan

## Overview

This document outlines the deprecation timeline and strategy for the legacy **inverse distance weighting** method in favor of the superior **barycentric coordinate-based weighting** method.

## Deprecation Timeline

### Phase 1: Soft Deprecation (Months 0-3)
**Status:** Current  
**Goal:** Introduce barycentric method, encourage adoption

**Actions:**
- ✅ Implement barycentric method alongside inverse distance
- ✅ Set barycentric as default for new assets
- ✅ Add documentation and migration guide
- ✅ Provide console commands for easy switching
- [ ] Add deprecation notice to inverse distance usage
- [ ] Update all example code to use barycentric
- [ ] Create migration tools and scripts

**User Impact:** None - both methods fully supported

### Phase 2: Active Deprecation (Months 3-6)
**Goal:** Migrate existing assets, warn users

**Actions:**
- [ ] Add compiler warnings when using inverse distance
- [ ] Log deprecation warnings to console
- [ ] Provide automated migration tool
- [ ] Update all internal assets to barycentric
- [ ] Add telemetry to track method usage
- [ ] Create migration success metrics

**User Impact:** Warnings displayed, migration encouraged

**Example Warning:**
```cpp
if (Params.WeightingMethod == EClothWeightingMethod::InverseDistance)
{
    UE_LOG(ELogLevel::Warning, 
        TEXT("DEPRECATED: Inverse distance weighting is deprecated and will be removed in 6 months. "
             "Please migrate to barycentric weighting for better quality. "
             "Use 'cloth.skinning.method barycentric' or set WeightingMethod = EClothWeightingMethod::Barycentric"));
}
```

### Phase 3: Hard Deprecation (Months 6-9)
**Goal:** Remove from default builds, keep as optional

**Actions:**
- [ ] Remove inverse distance from default configuration
- [ ] Guard legacy code with preprocessor defines
- [ ] Update build system to optionally exclude legacy code
- [ ] Archive reference implementation
- [ ] Update documentation to mark as removed

**User Impact:** Must explicitly enable legacy code to use inverse distance

**Preprocessor Guards:**
```cpp
// In ClothSkinningWeightGenerator.h
#ifndef CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE
#define CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE 0  // Disabled by default
#endif

#if CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE
// Legacy inverse distance code
#endif
```

### Phase 4: Complete Removal (Months 9-12)
**Goal:** Fully remove legacy code

**Actions:**
- [ ] Remove all inverse distance code
- [ ] Remove legacy parameters from FClothSkinningParams
- [ ] Simplify GenerateSkinningWeights() to only support barycentric
- [ ] Remove FSimpleSpatialHash class
- [ ] Update all tests to remove inverse distance cases
- [ ] Clean up documentation

**User Impact:** Inverse distance method no longer available

## Migration Support

### Automated Migration Tool

**Create:** `ClothAssetMigrationTool.cpp`

```cpp
/**
 * Automated tool to migrate cloth assets from inverse distance to barycentric
 */
class FClothAssetMigrationTool
{
public:
    struct FMigrationReport
    {
        int32 TotalAssets = 0;
        int32 SuccessfulMigrations = 0;
        int32 FailedMigrations = 0;
        int32 SkippedAssets = 0;
        TArray<FString> FailedAssetNames;
        TArray<FString> ErrorMessages;
    };
    
    /**
     * Migrate all cloth assets in project
     */
    static FMigrationReport MigrateAllAssets(bool bDryRun = false)
    {
        FMigrationReport report;
        TArray<UClothAsset*> allAssets = FindAllClothAssets();
        
        for (UClothAsset* asset : allAssets)
        {
            report.TotalAssets++;
            
            // Check if already using barycentric
            if (IsUsingBarycentricWeights(asset))
            {
                report.SkippedAssets++;
                continue;
            }
            
            // Attempt migration
            if (bDryRun)
            {
                UE_LOG(ELogLevel::Display, TEXT("Would migrate: %s"), *asset->GetName());
                report.SuccessfulMigrations++;
            }
            else
            {
                FString error;
                if (MigrateSingleAsset(asset, error))
                {
                    report.SuccessfulMigrations++;
                }
                else
                {
                    report.FailedMigrations++;
                    report.FailedAssetNames.Add(asset->GetName());
                    report.ErrorMessages.Add(error);
                }
            }
        }
        
        return report;
    }
    
    /**
     * Migrate single cloth asset
     */
    static bool MigrateSingleAsset(UClothAsset* Asset, FString& OutError)
    {
        if (!Asset || !Asset->SourceMesh)
        {
            OutError = "Invalid asset or missing source mesh";
            return false;
        }
        
        // Setup barycentric parameters
        FClothAssetGenerationParams params;
        params.SkinningParams.WeightingMethod = EClothWeightingMethod::Barycentric;
        params.SkinningParams.MaxSearchDistance = 100.0f;
        params.SkinningParams.bUseClosestPointProjection = true;
        params.SkinningParams.bFallbackToInverseDistance = true;
        
        // Preserve existing decimation settings
        params.DecimationParams.TargetVertexCount = Asset->DecimatedVertexCount;
        
        // Generate new asset
        FClothAssetGenerationResult result;
        if (!FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
            Asset->SourceMesh, params, result))
        {
            OutError = result.ErrorMessage;
            return false;
        }
        
        // Update asset with new weights
        Asset->SkinningWeights = result.Asset->SkinningWeights;
        Asset->MarkPackageDirty();
        
        UE_LOG(ELogLevel::Display, TEXT("Migrated asset: %s"), *Asset->GetName());
        return true;
    }
    
private:
    static TArray<UClothAsset*> FindAllClothAssets()
    {
        // Implementation depends on asset management system
        TArray<UClothAsset*> assets;
        // ... find all cloth assets in project
        return assets;
    }
    
    static bool IsUsingBarycentricWeights(UClothAsset* Asset)
    {
        // Check if asset was generated with barycentric method
        // Could check metadata, weight patterns, or version number
        return false;  // Placeholder
    }
};
```

### Console Commands for Migration

```bash
# Dry run - show what would be migrated
cloth.migration.dryrun

# Migrate all assets
cloth.migration.all

# Migrate specific asset
cloth.migration.asset MyClothAsset

# Show migration report
cloth.migration.report
```

## Deprecation Warnings

### Phase 2: Warning Messages

**Console Warning:**
```cpp
void WarnAboutDeprecation()
{
    static bool bWarningShown = false;
    if (!bWarningShown)
    {
        UE_LOG(ELogLevel::Warning, 
            TEXT("╔════════════════════════════════════════════════════════════╗"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║ DEPRECATION WARNING: Inverse Distance Skinning Weights    ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║                                                            ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║ The inverse distance weighting method is deprecated.      ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║ Please migrate to barycentric weighting for better        ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║ quality and performance.                                  ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║                                                            ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║ Migration: cloth.skinning.method barycentric              ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("║ Documentation: docs/cloth-skinning-migration-guide.md     ║"));
        UE_LOG(ELogLevel::Warning, 
            TEXT("╚════════════════════════════════════════════════════════════╝"));
        
        bWarningShown = true;
    }
}
```

**Compile-Time Warning:**
```cpp
#if CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE
#pragma message("WARNING: Legacy inverse distance weighting is enabled. This will be removed in future versions.")
#endif
```

### Phase 3: Build System Changes

**Add to project configuration:**
```cmake
# CMakeLists.txt or equivalent
option(CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE "Enable legacy inverse distance weighting" OFF)

if(CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE)
    add_definitions(-DCLOTH_ENABLE_LEGACY_INVERSE_DISTANCE=1)
else()
    add_definitions(-DCLOTH_ENABLE_LEGACY_INVERSE_DISTANCE=0)
endif()
```

**Visual Studio Project:**
```xml
<!-- EngineSIU.vcxproj -->
<PropertyGroup>
  <ClothEnableLegacyInverseDistance>false</ClothEnableLegacyInverseDistance>
</PropertyGroup>

<ItemDefinitionGroup>
  <ClCompile>
    <PreprocessorDefinitions>
      CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE=$(ClothEnableLegacyInverseDistance);
      %(PreprocessorDefinitions)
    </PreprocessorDefinitions>
  </ClCompile>
</ItemDefinitionGroup>
```

## Code Cleanup Plan

### Phase 3: Guard Legacy Code

**Files to Modify:**
- `ClothSkinningWeightGenerator.h`
- `ClothSkinningWeightGenerator.cpp`

**Example:**
```cpp
// In ClothSkinningWeightGenerator.h
#if CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE

    // Legacy inverse distance structures and functions
    struct FSimpleSpatialHash { ... };
    static bool GenerateInverseDistanceWeights(...);
    static void ComputeWeightsFromDistances(...);

#endif // CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE
```

### Phase 4: Complete Removal

**Files to Remove:**
- Legacy sections in `ClothSkinningWeightGenerator.cpp`
- `FSimpleSpatialHash` class
- `GenerateInverseDistanceWeights()` function
- `ComputeWeightsFromDistances()` function

**Files to Simplify:**
```cpp
// Simplified FClothSkinningParams (after removal)
struct FClothSkinningParams
{
    // Only barycentric parameters remain
    uint32 MaxInfluences = CLOTH_MAX_SKINNING_INFLUENCES;
    bool bNormalizeWeights = true;
    float MaxSearchDistance = 100.0f;
    bool bUseClosestPointProjection = true;
    int32 BVHMaxLeafTriangles = 8;
    bool bUseSAH = false;
    bool bCacheTriangleData = true;
    bool bEnableMultiThreading = false;
};

// Simplified GenerateSkinningWeights (after removal)
static bool GenerateSkinningWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const TArray<uint32>& SimIndices,
    const FClothSkinningParams& Params,
    FClothSkinningResult& OutResult
)
{
    // Only barycentric method remains
    return GenerateBarycentricWeights(RenderPositions, SimPositions, SimIndices, Params, OutResult);
}
```

## Telemetry and Monitoring

### Usage Tracking

**Add telemetry to track method usage:**
```cpp
struct FClothSkinningTelemetry
{
    static int32 BarycentricUsageCount;
    static int32 InverseDistanceUsageCount;
    static int32 FallbackCount;
    
    static void RecordMethodUsage(EClothWeightingMethod Method)
    {
        if (Method == EClothWeightingMethod::Barycentric)
            BarycentricUsageCount++;
        else
            InverseDistanceUsageCount++;
    }
    
    static void PrintReport()
    {
        int32 total = BarycentricUsageCount + InverseDistanceUsageCount;
        float barycentricPercent = (total > 0) ? (100.0f * BarycentricUsageCount / total) : 0.0f;
        
        UE_LOG(ELogLevel::Display, TEXT("=== Cloth Skinning Method Usage ==="));
        UE_LOG(ELogLevel::Display, TEXT("Barycentric: %d (%.1f%%)"), 
               BarycentricUsageCount, barycentricPercent);
        UE_LOG(ELogLevel::Display, TEXT("Inverse Distance: %d (%.1f%%)"), 
               InverseDistanceUsageCount, 100.0f - barycentricPercent);
        UE_LOG(ELogLevel::Display, TEXT("Fallbacks: %d"), FallbackCount);
    }
};
```

### Migration Progress Tracking

**Console command:**
```bash
# Show migration progress
cloth.migration.progress

# Output:
# Total cloth assets: 150
# Using barycentric: 120 (80%)
# Using inverse distance: 30 (20%)
# Migration target: 100% by Month 6
```

## Rollback Plan

### If Critical Issues Arise

**Emergency Rollback Procedure:**

1. **Revert Default Method:**
   ```cpp
   // In ClothSkinningWeightGenerator.h
   EClothWeightingMethod WeightingMethod = EClothWeightingMethod::InverseDistance;  // Rollback
   ```

2. **Console Command:**
   ```bash
   cloth.skinning.method inverse
   cloth.skinning.preset legacy
   ```

3. **Regenerate Affected Assets:**
   ```cpp
   void RollbackAssets(const TArray<UClothAsset*>& AffectedAssets)
   {
       for (UClothAsset* asset : AffectedAssets)
       {
           FClothSkinningParams params;
           params.WeightingMethod = EClothWeightingMethod::InverseDistance;
           // Regenerate with inverse distance
       }
   }
   ```

4. **Communicate to Users:**
   - Send notification about rollback
   - Provide instructions for reverting
   - Explain issue and timeline for fix

## Success Criteria

### Phase 1 Success Metrics
- ✅ Barycentric method implemented and tested
- ✅ Documentation complete
- ✅ Migration guide available
- [ ] 25% of new assets using barycentric
- [ ] No critical bugs reported

### Phase 2 Success Metrics
- [ ] 75% of assets migrated to barycentric
- [ ] Positive user feedback
- [ ] Performance metrics acceptable
- [ ] Less than 5% fallback rate
- [ ] No blocking issues

### Phase 3 Success Metrics
- [ ] 95% of assets using barycentric
- [ ] Legacy code usage < 5%
- [ ] Build size reduced (legacy code excluded)
- [ ] No user complaints about removal

### Phase 4 Success Metrics
- [ ] 100% of assets using barycentric
- [ ] Legacy code completely removed
- [ ] Code size optimized
- [ ] Documentation updated
- [ ] No regression in quality or performance

## Risk Mitigation

### Risk 1: User Resistance to Migration

**Mitigation:**
- Provide clear benefits documentation
- Offer automated migration tools
- Support gradual migration (not forced)
- Maintain backward compatibility during transition
- Provide rollback options

### Risk 2: Performance Regression on Some Platforms

**Mitigation:**
- Extensive performance testing across platforms
- Provide quality presets (fast/balanced/quality)
- Allow per-asset method selection
- Keep inverse distance as fallback option
- Monitor telemetry for performance issues

### Risk 3: Quality Issues with Certain Mesh Types

**Mitigation:**
- Comprehensive testing with various mesh topologies
- Robust edge case handling
- Fallback to inverse distance when needed
- User-configurable parameters
- Quick response to reported issues

### Risk 4: Breaking Changes in Production

**Mitigation:**
- Long deprecation timeline (12 months)
- Multiple warning phases before removal
- Backward compatibility maintained
- Emergency rollback procedure
- Staged rollout (dev → staging → production)

## Communication Plan

### Month 0: Announcement

**Channels:** Email, documentation, release notes

**Message:**
```
New Feature: Barycentric Skinning Weights

We've introduced a new barycentric coordinate-based weighting method for cloth 
skinning that provides significantly better quality than the existing inverse 
distance method.

Benefits:
- Smoother cloth deformation
- Better surface awareness
- Reduced artifacts
- Improved performance for large meshes

The new method is now the default for new assets. Existing assets continue to 
work unchanged. We recommend migrating to the new method for improved quality.

Migration Guide: docs/cloth-skinning-migration-guide.md
```

### Month 3: Deprecation Notice

**Message:**
```
Deprecation Notice: Inverse Distance Skinning Weights

The inverse distance weighting method is now deprecated and will be removed in 
9 months. Please migrate your cloth assets to the barycentric method.

Migration is simple and automated. See the migration guide for details.

Automated Migration: cloth.migration.all
Migration Guide: docs/cloth-skinning-migration-guide.md
```

### Month 6: Removal Warning

**Message:**
```
Final Warning: Inverse Distance Removal in 6 Months

The inverse distance weighting method will be completely removed in 6 months.
If you haven't migrated yet, please do so now.

Current migration status: 80% complete
Remaining assets: 30

Use 'cloth.migration.progress' to check your project's status.
```

### Month 12: Removal Announcement

**Message:**
```
Inverse Distance Weighting Removed

The legacy inverse distance weighting method has been removed from the codebase.
All cloth assets should now use barycentric weighting.

If you encounter issues, please report them immediately.
```

## Archive and Reference

### Archival Strategy

**Create archive branch:**
```bash
git checkout -b archive/inverse-distance-skinning
git tag inverse-distance-final-version
```

**Archive documentation:**
- Move inverse distance docs to `docs/archive/`
- Keep reference implementation for historical comparison
- Maintain test cases for regression testing

**Archive location:**
```
docs/archive/
├── inverse-distance-implementation.md
├── inverse-distance-performance-benchmarks.md
└── inverse-distance-test-cases.md

code/archive/
├── ClothSkinningWeightGenerator_InverseDistance.cpp
└── ClothSkinningWeightGenerator_InverseDistance.h
```

## Checklist for Each Phase

### Phase 1 Checklist (Months 0-3)
- [x] Implement barycentric method
- [x] Create documentation
- [x] Create migration guide
- [ ] Add deprecation notices
- [ ] Update example code
- [ ] Create migration tools
- [ ] Gather initial feedback

### Phase 2 Checklist (Months 3-6)
- [ ] Add compiler warnings
- [ ] Add console warnings
- [ ] Run automated migration on internal assets
- [ ] Track usage telemetry
- [ ] Achieve 75% migration rate
- [ ] Address reported issues

### Phase 3 Checklist (Months 6-9)
- [ ] Guard legacy code with preprocessor
- [ ] Update build system
- [ ] Achieve 95% migration rate
- [ ] Archive reference implementation
- [ ] Update documentation

### Phase 4 Checklist (Months 9-12)
- [ ] Remove all legacy code
- [ ] Simplify API
- [ ] Update all tests
- [ ] Verify 100% migration
- [ ] Measure code size reduction
- [ ] Final documentation update

## Support During Transition

### Resources Available

**Documentation:**
- [Technical Documentation](cloth-skinning-barycentric-weighting.md)
- [Migration Guide](cloth-skinning-migration-guide.md)
- [Implementation Progress](../plans/cloth-skinning-barycentric-implementation-progress.md)

**Tools:**
- Automated migration script
- Console commands for testing
- Visualization tools
- Performance profiling

**Support Channels:**
- GitHub issues for bug reports
- Internal wiki for FAQs
- Team chat for quick questions
- Email for migration assistance

### Common Questions During Deprecation

**Q: Why is inverse distance being removed?**
A: Barycentric weighting provides significantly better quality with comparable or better performance. Maintaining two methods adds complexity and maintenance burden.

**Q: What if I can't migrate by the deadline?**
A: You can enable legacy code with `CLOTH_ENABLE_LEGACY_INVERSE_DISTANCE=1` during Phase 3. However, we strongly recommend migrating as the legacy code will not receive updates or bug fixes.

**Q: Will my old assets stop working?**
A: No. Assets store the generated weights, not the method used. Old assets continue to work. However, regenerating them with barycentric will improve quality.

**Q: Can I keep using inverse distance indefinitely?**
A: During Phase 3 (months 6-9), you can enable legacy code. After Phase 4 (month 12), the code is completely removed and unavailable.

---

**Last Updated:** 2026-02-10  
**Current Phase:** Phase 1 (Soft Deprecation)  
**Next Milestone:** Month 3 (Active Deprecation)  
**Final Removal:** Month 12
