# Self-Collision System Refactor: Adaptive Spatial Hashing Implementation Plan

## Executive Summary

This plan details the refactoring of the spatial hash-based self-collision system to automatically compute adaptive grid parameters per cloth instance, eliminating hardcoded values and enabling natural collision behavior across cloths with different resolutions.

---

## Current State Analysis

### Identified Hardcoded Parameters

**Location**: [`ClothBatchedSolver.cpp:2296-2308`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:2296)

```cpp
void FClothBatchedSolver::UpdateSelfCollisionParams()
{
    // HARDCODED: Fixed grid bounds (5m × 5m × 5m)
    FVector gridMin = FVector(-5.0f, -5.0f, 0.0f);
    
    // HARDCODED: Assumed average edge length (10cm)
    float avgEdgeLength = 0.5f;
    
    // HARDCODED: Static cell size calculation
    float cellSize = avgEdgeLength * 2.0f;
    
    // Uses global config values (not per-cloth adaptive)
    SelfCollisionParams.GridDimX = Config.SelfCollisionGridDim;
    SelfCollisionParams.CollisionRadius = Config.SelfCollisionRadius;
    SelfCollisionParams.CollisionStiffness = Config.SelfCollisionStiffness;
}
```

**Location**: [`ClothSimulationData.h:56-59`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:56)

```cpp
// Absolute values (not adaptive)
float SelfCollisionRadius = 0.01f;      // 0.01 world units (1cm)
float SelfCollisionStiffness = 0.01f;   // Absolute stiffness value
uint32 SelfCollisionGridDim = 32;       // Fixed grid dimension
uint32 SelfCollisionMaxPerCell = 16;    // Fixed max particles per cell
```

### Existing Infrastructure to Reuse

#### ✅ Existing Structures (DO NOT DUPLICATE)

1. **[`FClothInstanceMetadata`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h:95)** - Already stores per-instance buffer ranges
   - Has: ParticleOffset, ParticleCount, ConstraintOffset, etc.
   - **EXTEND THIS** with adaptive parameters

2. **[`FClothConfig`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:20)** - Global configuration
   - Has: SelfCollisionRadius, SelfCollisionStiffness, SelfCollisionGridDim
   - **CONVERT TO MULTIPLIERS** instead of absolute values

3. **[`FClothSelfCollisionParams`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h:227)** - GPU constant buffer
   - Already has: GridMin, CellSize, GridDimensions, CollisionRadius
   - **KEEP AS-IS** (receives computed values)

4. **[`FClothInstanceParameters`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h:47)** - Per-instance GPU parameters
   - Already has per-instance offsets and counts
   - **COULD EXTEND** if per-instance self-collision params needed on GPU

#### ✅ Existing Functions (CHECK BEFORE CREATING)

1. **Constraint Generation** - [`ClothAssetGenerator.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothAssetGenerator.h:155)
   - `GeneratePhysicsConstraints()` - Already computes edge lengths for constraints
   - **REUSE** this logic for average edge length computation

2. **Mesh Analysis** - Check if exists in `ClothAssetGenerator` or `ClothMeshDecimator`
   - May already have AABB computation
   - May already have edge length analysis

#### ❌ Missing Components (NEED TO CREATE)

1. **Per-Cloth Adaptive Parameters Storage** - Extend `FClothInstanceMetadata`
2. **Mesh Analysis Functions** - If not in existing code
3. **Adaptive Parameter Computation** - New logic in `UpdateSelfCollisionParams()`
4. **Validation Functions** - New diagnostic checks

---

## Implementation Plan

### Phase 0: Codebase Analysis ✅ COMPLETED

**Findings**:
- ✅ `FClothInstanceMetadata` exists and should be extended
- ✅ `FClothConfig` exists and needs multiplier conversion
- ✅ `FClothSelfCollisionParams` exists and is correct
- ✅ Constraint generation exists in `ClothAssetGenerator`
- ❌ No existing average edge length computation function found
- ❌ No existing dynamic AABB computation for runtime

---

### Phase 1: Implement Mesh Analysis Functions

**Goal**: Create utility functions to compute adaptive parameters from mesh topology.

#### 1.1 Add to `FClothInstanceMetadata` (Extend Existing)

**File**: [`ClothBatchTypes.h:95`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h:95)

```cpp
struct FClothInstanceMetadata
{
    // ... existing fields ...
    
    // NEW: Adaptive self-collision parameters (computed at initialization)
    float AvgEdgeLength;        // Average edge length in world units
    FVector MeshBoundsMin;      // Dynamic AABB min
    FVector MeshBoundsMax;      // Dynamic AABB max
    float AdaptiveCellSize;     // Computed: AvgEdgeLength × 1.5
    float AdaptiveCollisionRadius; // Computed: AvgEdgeLength × 0.5
    uint32 AdaptiveGridDimX;    // Computed from bounds and cell size
    uint32 AdaptiveGridDimY;
    uint32 AdaptiveGridDimZ;
    uint32 AdaptiveMaxPerCell;  // Computed from particle density
    
    FClothInstanceMetadata()
        : /* existing initializers */
        , AvgEdgeLength(0.0f)
        , MeshBoundsMin(FVector::ZeroVector)
        , MeshBoundsMax(FVector::ZeroVector)
        , AdaptiveCellSize(0.0f)
        , AdaptiveCollisionRadius(0.0f)
        , AdaptiveGridDimX(0)
        , AdaptiveGridDimY(0)
        , AdaptiveGridDimZ(0)
        , AdaptiveMaxPerCell(0)
    {
    }
};
```

#### 1.2 Create Mesh Analysis Utility Class

**New File**: `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshAnalysis.h`

```cpp
#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"

/**
 * Cloth Mesh Analysis Utilities
 * Provides functions to analyze mesh topology and compute adaptive parameters
 */
class FClothMeshAnalysis
{
public:
    /**
     * Compute average edge length from mesh topology
     * @param Positions - Vertex positions (world space)
     * @param Indices - Triangle indices
     * @return Average edge length in world units
     */
    static float ComputeAverageEdgeLength(
        const TArray<FVector>& Positions,
        const TArray<uint32>& Indices
    );
    
    /**
     * Compute axis-aligned bounding box from positions
     * @param Positions - Vertex positions (world space)
     * @param OutMin - Output AABB min
     * @param OutMax - Output AABB max
     */
    static void ComputeAABB(
        const TArray<FVector>& Positions,
        FVector& OutMin,
        FVector& OutMax
    );
    
    /**
     * Compute adaptive spatial hash parameters for self-collision
     * Based on Müller et al. recommendations
     * 
     * @param AvgEdgeLength - Average edge length
     * @param BoundsMin - Mesh AABB min
     * @param BoundsMax - Mesh AABB max
     * @param ParticleCount - Number of particles
     * @param OutCellSize - Output cell size (AvgEdgeLength × 1.0~1.5)
     * @param OutCollisionRadius - Output collision radius (AvgEdgeLength × 0.5)
     * @param OutGridMin - Output grid min (BoundsMin - margin)
     * @param OutGridMax - Output grid max (BoundsMax + margin)
     * @param OutGridDimX/Y/Z - Output grid dimensions
     * @param OutMaxPerCell - Output max particles per cell
     */
    static void ComputeAdaptiveSpatialHashParams(
        float AvgEdgeLength,
        const FVector& BoundsMin,
        const FVector& BoundsMax,
        uint32 ParticleCount,
        float& OutCellSize,
        float& OutCollisionRadius,
        FVector& OutGridMin,
        FVector& OutGridMax,
        uint32& OutGridDimX,
        uint32& OutGridDimY,
        uint32& OutGridDimZ,
        uint32& OutMaxPerCell
    );
    
    /**
     * Estimate average particles per cell for capacity planning
     */
    static uint32 EstimateAverageParticlesPerCell(
        uint32 ParticleCount,
        uint32 GridDimX,
        uint32 GridDimY,
        uint32 GridDimZ
    );
};
```

**New File**: `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshAnalysis.cpp`

```cpp
#include "ClothMeshAnalysis.h"
#include "Core/Math/MathUtility.h"
#include "Engine/UserInterface/Console.h"
#include <unordered_set>

float FClothMeshAnalysis::ComputeAverageEdgeLength(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices)
{
    if (Positions.Num() == 0 || Indices.Num() < 3)
        return 0.0f;
    
    // Use hash set to avoid counting edges twice
    std::unordered_set<uint64> uniqueEdges;
    float totalLength = 0.0f;
    uint32 edgeCount = 0;
    
    // Iterate through triangles
    for (int32 i = 0; i < Indices.Num(); i += 3)
    {
        uint32 idx0 = Indices[i];
        uint32 idx1 = Indices[i + 1];
        uint32 idx2 = Indices[i + 2];
        
        // Process three edges of triangle
        uint32 edges[3][2] = {
            {idx0, idx1},
            {idx1, idx2},
            {idx2, idx0}
        };
        
        for (int32 e = 0; e < 3; ++e)
        {
            uint32 a = edges[e][0];
            uint32 b = edges[e][1];
            
            // Ensure consistent ordering (a < b)
            if (a > b) std::swap(a, b);
            
            // Create unique edge key
            uint64 edgeKey = (static_cast<uint64>(a) << 32) | b;
            
            // Skip if already processed
            if (uniqueEdges.find(edgeKey) != uniqueEdges.end())
                continue;
            
            uniqueEdges.insert(edgeKey);
            
            // Compute edge length
            if (a < static_cast<uint32>(Positions.Num()) && 
                b < static_cast<uint32>(Positions.Num()))
            {
                float length = (Positions[b] - Positions[a]).Size();
                totalLength += length;
                edgeCount++;
            }
        }
    }
    
    if (edgeCount == 0)
        return 0.0f;
    
    return totalLength / edgeCount;
}

void FClothMeshAnalysis::ComputeAABB(
    const TArray<FVector>& Positions,
    FVector& OutMin,
    FVector& OutMax)
{
    if (Positions.Num() == 0)
    {
        OutMin = FVector::ZeroVector;
        OutMax = FVector::ZeroVector;
        return;
    }
    
    OutMin = Positions[0];
    OutMax = Positions[0];
    
    for (int32 i = 1; i < Positions.Num(); ++i)
    {
        OutMin.X = FMath::Min(OutMin.X, Positions[i].X);
        OutMin.Y = FMath::Min(OutMin.Y, Positions[i].Y);
        OutMin.Z = FMath::Min(OutMin.Z, Positions[i].Z);
        
        OutMax.X = FMath::Max(OutMax.X, Positions[i].X);
        OutMax.Y = FMath::Max(OutMax.Y, Positions[i].Y);
        OutMax.Z = FMath::Max(OutMax.Z, Positions[i].Z);
    }
}

void FClothMeshAnalysis::ComputeAdaptiveSpatialHashParams(
    float AvgEdgeLength,
    const FVector& BoundsMin,
    const FVector& BoundsMax,
    uint32 ParticleCount,
    float& OutCellSize,
    float& OutCollisionRadius,
    FVector& OutGridMin,
    FVector& OutGridMax,
    uint32& OutGridDimX,
    uint32& OutGridDimY,
    uint32& OutGridDimZ,
    uint32& OutMaxPerCell)
{
    // Müller et al. recommendations:
    // - Cell size: 1.0~1.5 × average edge length
    // - Collision radius: 0.5 × average edge length
    
    OutCellSize = AvgEdgeLength * 1.5f;
    OutCollisionRadius = AvgEdgeLength * 0.5f;
    
    // Add margin to bounds (2× cell size for safety)
    float margin = OutCellSize * 2.0f;
    OutGridMin = BoundsMin - FVector(margin, margin, margin);
    OutGridMax = BoundsMax + FVector(margin, margin, margin);
    
    // Compute grid dimensions
    FVector extent = OutGridMax - OutGridMin;
    OutGridDimX = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.X / OutCellSize)));
    OutGridDimY = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Y / OutCellSize)));
    OutGridDimZ = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Z / OutCellSize)));
    
    // Clamp grid dimensions to reasonable limits (prevent excessive memory)
    const uint32 MaxGridDim = 128;
    OutGridDimX = FMath::Min(OutGridDimX, MaxGridDim);
    OutGridDimY = FMath::Min(OutGridDimY, MaxGridDim);
    OutGridDimZ = FMath::Min(OutGridDimZ, MaxGridDim);
    
    // Compute max particles per cell
    uint32 avgPerCell = EstimateAverageParticlesPerCell(
        ParticleCount, OutGridDimX, OutGridDimY, OutGridDimZ);
    
    // Use 3× average as max (with minimum of 32)
    OutMaxPerCell = FMath::Max(32u, avgPerCell * 3);
}

uint32 FClothMeshAnalysis::EstimateAverageParticlesPerCell(
    uint32 ParticleCount,
    uint32 GridDimX,
    uint32 GridDimY,
    uint32 GridDimZ)
{
    uint32 totalCells = GridDimX * GridDimY * GridDimZ;
    if (totalCells == 0)
        return 0;
    
    // Assume uniform distribution (conservative estimate)
    return (ParticleCount + totalCells - 1) / totalCells;
}
```

---

### Phase 2: Extend Metadata Structures

**Goal**: Store computed adaptive parameters per cloth instance.

#### 2.1 Update `FClothInstanceMetadata` (Already shown in Phase 1.1)

#### 2.2 Add Computation Call During Instance Creation

**File**: `ClothBatchManager.cpp` (or wherever instances are created)

```cpp
// After uploading particle data, compute adaptive parameters
FClothInstanceMetadata& metadata = InstanceMetadata[handle.Index];

// Compute average edge length
metadata.AvgEdgeLength = FClothMeshAnalysis::ComputeAverageEdgeLength(
    params.RestPositions, params.Indices);

// Compute AABB (from current positions, not rest positions)
FClothMeshAnalysis::ComputeAABB(
    params.RestPositions, 
    metadata.MeshBoundsMin, 
    metadata.MeshBoundsMax);

// Compute adaptive spatial hash parameters
FClothMeshAnalysis::ComputeAdaptiveSpatialHashParams(
    metadata.AvgEdgeLength,
    metadata.MeshBoundsMin,
    metadata.MeshBoundsMax,
    metadata.ParticleCount,
    metadata.AdaptiveCellSize,
    metadata.AdaptiveCollisionRadius,
    /* OutGridMin/Max computed internally */,
    metadata.AdaptiveGridDimX,
    metadata.AdaptiveGridDimY,
    metadata.AdaptiveGridDimZ,
    metadata.AdaptiveMaxPerCell);

// Log computed parameters
UE_LOG(ELogLevel::Display, 
    TEXT("Cloth Instance %d: AvgEdgeLength=%.3f, CellSize=%.3f, CollisionRadius=%.3f, Grid=%ux%ux%u"),
    handle.Index, metadata.AvgEdgeLength, metadata.AdaptiveCellSize, 
    metadata.AdaptiveCollisionRadius,
    metadata.AdaptiveGridDimX, metadata.AdaptiveGridDimY, metadata.AdaptiveGridDimZ);
```

---

### Phase 3: Refactor `UpdateSelfCollisionParams()`

**Goal**: Remove hardcoded values and use computed adaptive parameters.

**File**: [`ClothBatchedSolver.cpp:2296`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:2296)

#### 3.1 Add Access to Instance Metadata

The solver needs access to instance metadata. Options:

**Option A**: Pass metadata array to solver during initialization
**Option B**: Compute unified parameters from all active instances (recommended for batched solver)

#### 3.2 Refactored `UpdateSelfCollisionParams()`

```cpp
void FClothBatchedSolver::UpdateSelfCollisionParams(
    const TArray<FClothInstanceMetadata>& InstanceMetadata)
{
    if (!Graphics || !Graphics->DeviceContext || !SelfCollisionParamsBuffer)
        return;
    
    if (InstanceMetadata.Num() == 0)
        return;
    
    // STRATEGY: Use median or weighted average of all active instances
    // This ensures the spatial hash works reasonably well for all cloths
    
    TArray<float> avgEdgeLengths;
    TArray<float> cellSizes;
    TArray<float> collisionRadii;
    FVector globalMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    FVector globalMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    
    // Collect parameters from all active instances
    for (const FClothInstanceMetadata& meta : InstanceMetadata)
    {
        if (!meta.bIsActive || meta.AvgEdgeLength == 0.0f)
            continue;
        
        avgEdgeLengths.Add(meta.AvgEdgeLength);
        cellSizes.Add(meta.AdaptiveCellSize);
        collisionRadii.Add(meta.AdaptiveCollisionRadius);
        
        // Expand global bounds
        globalMin.X = FMath::Min(globalMin.X, meta.MeshBoundsMin.X);
        globalMin.Y = FMath::Min(globalMin.Y, meta.MeshBoundsMin.Y);
        globalMin.Z = FMath::Min(globalMin.Z, meta.MeshBoundsMin.Z);
        
        globalMax.X = FMath::Max(globalMax.X, meta.MeshBoundsMax.X);
        globalMax.Y = FMath::Max(globalMax.Y, meta.MeshBoundsMax.Y);
        globalMax.Z = FMath::Max(globalMax.Z, meta.MeshBoundsMax.Z);
    }
    
    if (avgEdgeLengths.Num() == 0)
    {
        UE_LOG(ELogLevel::Warning, TEXT("No active cloth instances for self-collision"));
        return;
    }
    
    // Use MEDIAN for robustness (not affected by outliers)
    avgEdgeLengths.Sort();
    cellSizes.Sort();
    collisionRadii.Sort();
    
    int32 medianIdx = avgEdgeLengths.Num() / 2;
    float medianCellSize = cellSizes[medianIdx];
    float medianCollisionRadius = collisionRadii[medianIdx];
    
    // Apply multipliers from config (artist control)
    float finalCellSize = medianCellSize * Config.SelfCollisionCellSizeMultiplier;
    float finalCollisionRadius = medianCollisionRadius * Config.SelfCollisionRadiusMultiplier;
    
    // Add margin to global bounds
    float margin = finalCellSize * 2.0f;
    FVector gridMin = globalMin - FVector(margin, margin, margin);
    FVector gridMax = globalMax + FVector(margin, margin, margin);
    
    // Compute grid dimensions
    FVector extent = gridMax - gridMin;
    uint32 gridDimX = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.X / finalCellSize)));
    uint32 gridDimY = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Y / finalCellSize)));
    uint32 gridDimZ = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Z / finalCellSize)));
    
    // Clamp to reasonable limits
    const uint32 MaxGridDim = 128;
    gridDimX = FMath::Min(gridDimX, MaxGridDim);
    gridDimY = FMath::Min(gridDimY, MaxGridDim);
    gridDimZ = FMath::Min(gridDimZ, MaxGridDim);
    
    // Fill params
    SelfCollisionParams.GridMin = gridMin;
    SelfCollisionParams.CellSize = finalCellSize;
    SelfCollisionParams.GridDimX = gridDimX;
    SelfCollisionParams.GridDimY = gridDimY;
    SelfCollisionParams.GridDimZ = gridDimZ;
    SelfCollisionParams.MaxParticlesPerCell = Config.SelfCollisionMaxPerCell;
    SelfCollisionParams.CollisionRadius = finalCollisionRadius;
    SelfCollisionParams.CollisionStiffness = Config.SelfCollisionStiffness * Config.SelfCollisionStiffnessMultiplier;
    SelfCollisionParams.bEnableSelfCollision = Config.bEnableSelfCollision ? 1 : 0;
    SelfCollisionParams.Padding = 0;
    
    // Upload to GPU
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(SelfCollisionParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &SelfCollisionParams, sizeof(FClothSelfCollisionParams));
        Graphics->DeviceContext->Unmap(SelfCollisionParamsBuffer, 0);
    }
    
    // Log computed parameters
    UE_LOG(ELogLevel::Display, 
        TEXT("Self-Collision: GridMin=(%.1f,%.1f,%.1f), CellSize=%.3f, Grid=%ux%ux%u, Radius=%.3f"),
        gridMin.X, gridMin.Y, gridMin.Z, finalCellSize,
        gridDimX, gridDimY, gridDimZ, finalCollisionRadius);
}
```

---

### Phase 4: Convert `FClothConfig` to Multipliers

**Goal**: Change from absolute values to multipliers for artist control.

**File**: [`ClothSimulationData.h:56`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:56)

#### 4.1 Update `FClothConfig`

```cpp
struct FClothConfig
{
    // ... existing fields ...
    
    // Self-collision parameters (REFACTORED: Now use multipliers)
    bool bEnableSelfCollision = false;
    
    // MULTIPLIERS (applied to adaptive base values)
    float SelfCollisionRadiusMultiplier = 1.0f;      // × (AvgEdgeLength × 0.5)
    float SelfCollisionStiffnessMultiplier = 0.3f;   // × adaptive base stiffness
    float SelfCollisionCellSizeMultiplier = 1.0f;    // × (AvgEdgeLength × 1.5)
    
    // Grid capacity (still absolute, but computed adaptively per instance)
    uint32 SelfCollisionMaxPerCell = 32;   // Max particles per cell (safety limit)
    
    // DEPRECATED (kept for backwards compatibility, but ignored)
    float SelfCollisionRadius = 0.01f;      // DEPRECATED: Use multiplier instead
    float SelfCollisionStiffness = 0.01f;   // DEPRECATED: Use multiplier instead
    uint32 SelfCollisionGridDim = 32;       // DEPRECATED: Computed adaptively
};
```

#### 4.2 Update Serialization

```cpp
inline FArchive& operator<<(FArchive& Ar, FClothConfig& Cfg)
{
    // ... existing serialization ...
    
    // Self-collision parameters
    Ar << Cfg.bEnableSelfCollision;
    Ar << Cfg.SelfCollisionRadiusMultiplier;
    Ar << Cfg.SelfCollisionStiffnessMultiplier;
    Ar << Cfg.SelfCollisionCellSizeMultiplier;
    Ar << Cfg.SelfCollisionMaxPerCell;
    
    // Deprecated (for backwards compatibility)
    Ar << Cfg.SelfCollisionRadius;
    Ar << Cfg.SelfCollisionStiffness;
    Ar << Cfg.SelfCollisionGridDim;
    
    return Ar;
}
```

#### 4.3 Add Migration Logic

```cpp
// In ClothConfig initialization or loading
void FClothConfig::MigrateDeprecatedParams()
{
    // If multipliers are at default (1.0) but old absolute values are set,
    // attempt to convert (best effort)
    if (SelfCollisionRadiusMultiplier == 1.0f && SelfCollisionRadius != 0.01f)
    {
        // Assume old value was for 10cm average edge length
        // New: Radius = AvgEdgeLength × 0.5 × Multiplier
        // Old: Radius = 0.01 (absolute)
        // Solve: Multiplier = OldRadius / (0.1 × 0.5) = OldRadius / 0.05
        SelfCollisionRadiusMultiplier = SelfCollisionRadius / 0.05f;
        
        UE_LOG(ELogLevel::Warning, 
            TEXT("Migrated deprecated SelfCollisionRadius %.3f to multiplier %.2f"),
            SelfCollisionRadius, SelfCollisionRadiusMultiplier);
    }
}
```

---

### Phase 5: Add Validation and Diagnostics

**Goal**: Automatic sanity checks and comprehensive logging.

#### 5.1 Create Validation Function

**File**: `ClothMeshAnalysis.h` (add to class)

```cpp
/**
 * Validate self-collision setup and log warnings
 * @return true if valid, false if critical issues detected
 */
static bool ValidateSelfCollisionSetup(
    float CellSize,
    float CollisionRadius,
    const FVector& GridMin,
    const FVector& GridMax,
    uint32 GridDimX,
    uint32 GridDimY,
    uint32 GridDimZ,
    uint32 MaxParticlesPerCell,
    uint32 ParticleCount
);
```

**File**: `ClothMeshAnalysis.cpp` (implementation)

```cpp
bool FClothMeshAnalysis::ValidateSelfCollisionSetup(
    float CellSize,
    float CollisionRadius,
    const FVector& GridMin,
    const FVector& GridMax,
    uint32 GridDimX,
    uint32 GridDimY,
    uint32 GridDimZ,
    uint32 MaxParticlesPerCell,
    uint32 ParticleCount)
{
    bool bValid = true;
    
    // Check 1: Cell size should be >= 1.5× collision radius
    float minCellSize = CollisionRadius * 1.5f;
    if (CellSize < minCellSize)
    {
        UE_LOG(ELogLevel::Warning, 
            TEXT("Self-Collision: CellSize (%.3f) < 1.5×CollisionRadius (%.3f). May miss collisions!"),
            CellSize, minCellSize);
        bValid = false;
    }
    
    // Check 2: Grid should cover mesh bounds
    FVector extent = GridMax - GridMin;
    FVector computedExtent = FVector(
        GridDimX * CellSize,
        GridDimY * CellSize,
        GridDimZ * CellSize);
    
    if (computedExtent.X < extent.X || 
        computedExtent.Y < extent.Y || 
        computedExtent.Z < extent.Z)
    {
        UE_LOG(ELogLevel::Error, 
            TEXT("Self-Collision: Grid doesn't cover mesh bounds! Extent=(%.1f,%.1f,%.1f), Grid=(%.1f,%.1f,%.1f)"),
            extent.X, extent.Y, extent.Z,
            computedExtent.X, computedExtent.Y, computedExtent.Z);
        bValid = false;
    }
    
    // Check 3: Estimate if MaxParticlesPerCell is sufficient
    uint32 totalCells = GridDimX * GridDimY * GridDimZ;
    uint32 avgPerCell = EstimateAverageParticlesPerCell(
        ParticleCount, GridDimX, GridDimY, GridDimZ);
    
    if (MaxParticlesPerCell < avgPerCell * 2)
    {
        UE_LOG(ELogLevel::Warning, 
            TEXT("Self-Collision: MaxParticlesPerCell (%u) may be insufficient. Avg=%u, recommend %u"),
            MaxParticlesPerCell, avgPerCell, avgPerCell * 3);
    }
    
    // Check 4: Grid dimensions reasonable
    if (GridDimX > 128 || GridDimY > 128 || GridDimZ > 128)
    {
        UE_LOG(ELogLevel::Warning, 
            TEXT("Self-Collision: Very large grid (%ux%ux%u). Memory usage: ~%.2f MB"),
            GridDimX, GridDimY, GridDimZ,
            (totalCells * sizeof(uint32) + totalCells * MaxParticlesPerCell * sizeof(uint32)) / (1024.0f * 1024.0f));
    }
    
    return bValid;
}
```

#### 5.2 Call Validation in `UpdateSelfCollisionParams()`

```cpp
// At end of UpdateSelfCollisionParams()
bool bValid = FClothMeshAnalysis::ValidateSelfCollisionSetup(
    SelfCollisionParams.CellSize,
    SelfCollisionParams.CollisionRadius,
    SelfCollisionParams.GridMin,
    gridMax,  // Computed earlier
    SelfCollisionParams.GridDimX,
    SelfCollisionParams.GridDimY,
    SelfCollisionParams.GridDimZ,
    SelfCollisionParams.MaxParticlesPerCell,
    UsedParticleCount);

if (!bValid)
{
    UE_LOG(ELogLevel::Warning, 
        TEXT("Self-collision setup has issues. Consider adjusting multipliers."));
}
```

---

### Phase 6: Testing Strategy

#### 6.1 Test Cases

1. **Single Cloth - Fine Resolution** (0.05m avg edge length)
   - Verify: CellSize ≈ 0.075m, CollisionRadius ≈ 0.025m
   
2. **Single Cloth - Coarse Resolution** (1.0m avg edge length)
   - Verify: CellSize ≈ 1.5m, CollisionRadius ≈ 0.5m
   
3. **Multiple Cloths - Mixed Resolutions**
   - Fine cloth (0.1m) + Coarse cloth (1.0m)
   - Verify: Median parameters used, both cloths collide naturally
   
4. **Dynamic Bounds Update**
   - Move cloth significantly
   - Verify: Bounds update, grid still covers mesh

#### 6.2 Console Commands for Testing

Add debug commands to test adaptive parameters:

```cpp
// In Console.cpp or ClothWorld.cpp
CONSOLE_COMMAND(cloth.selfcollision.debug)
{
    // Log all computed adaptive parameters for all instances
    for (const FClothInstanceMetadata& meta : InstanceMetadata)
    {
        UE_LOG(ELogLevel::Display, 
            TEXT("Instance %d: AvgEdge=%.3f, CellSize=%.3f, Radius=%.3f, Grid=%ux%ux%u"),
            /* instance id */, meta.AvgEdgeLength, meta.AdaptiveCellSize,
            meta.AdaptiveCollisionRadius,
            meta.AdaptiveGridDimX, meta.AdaptiveGridDimY, meta.AdaptiveGridDimZ);
    }
}

CONSOLE_COMMAND(cloth.selfcollision.multiplier <radius|stiffness|cellsize> <value>)
{
    // Dynamically adjust multipliers for testing
    if (param == "radius")
        Config.SelfCollisionRadiusMultiplier = value;
    else if (param == "stiffness")
        Config.SelfCollisionStiffnessMultiplier = value;
    else if (param == "cellsize")
        Config.SelfCollisionCellSizeMultiplier = value;
    
    UE_LOG(ELogLevel::Display, TEXT("Updated %s multiplier to %.2f"), param, value);
}
```

---

## File Modification Summary

### Files to Modify

1. **[`ClothBatchTypes.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchTypes.h)**
   - Extend `FClothInstanceMetadata` with adaptive parameters

2. **[`ClothSimulationData.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h)**
   - Convert `FClothConfig` self-collision params to multipliers
   - Add migration logic

3. **[`ClothBatchedSolver.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h)**
   - Update `UpdateSelfCollisionParams()` signature to accept metadata

4. **[`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)**
   - Refactor `UpdateSelfCollisionParams()` to compute adaptive values
   - Remove hardcoded values (lines 2303-2308)

5. **`ClothBatchManager.cpp`** (or instance creation location)
   - Call mesh analysis functions during instance creation
   - Store computed parameters in metadata

### Files to Create

1. **`ClothMeshAnalysis.h`** - New utility class for mesh analysis
2. **`ClothMeshAnalysis.cpp`** - Implementation of analysis functions

---

## Integration Points

### Batched Solver Architecture

The refactored system maintains the batched architecture:

1. **Per-Instance Computation** (at creation time)
   - Each cloth computes its own optimal parameters
   - Stored in `FClothInstanceMetadata`

2. **Unified Grid** (at runtime)
   - Solver uses median/weighted average of all active instances
   - Single spatial hash grid covers all cloths
   - Updated once per frame (not per substep)

3. **Artist Control**
   - Multipliers in `FClothConfig` allow global tuning
   - Per-instance parameters provide automatic baseline

### Memory Considerations

- **Per-Instance Overhead**: +48 bytes per instance (adaptive params in metadata)
- **No GPU Changes**: GPU buffers remain unchanged
- **Computation Cost**: O(E) edge iteration at instance creation (one-time cost)

---

## Success Criteria

✅ **No hardcoded spatial hash parameters remain**
- All values computed from mesh topology

✅ **Each cloth automatically computes optimal collision parameters**
- Based on actual average edge length

✅ **Artists can tune collision behavior via intuitive multipliers**
- Multipliers scale adaptive base values

✅ **Self-collision works naturally across different resolutions**
- Fine cloth (0.1m edges) and coarse cloth (1.0m edges) both work

✅ **System logs clear diagnostic information**
- Validation warnings for potential issues
- Debug commands for testing

✅ **Minimal code duplication**
- Reuses existing `FClothInstanceMetadata`
- Extends existing `FClothConfig`
- No duplicate structures

---

## Backwards Compatibility

1. **Config Migration**: Old absolute values converted to multipliers (best effort)
2. **Default Multipliers**: 1.0 maintains existing behavior if no migration needed
3. **Deprecated Fields**: Kept in `FClothConfig` but marked deprecated

---

## Future Enhancements

1. **Dynamic Bounds Update**: Recompute AABB when cloth moves significantly
2. **Per-Instance Grids**: Separate spatial hash per cloth (more memory, better accuracy)
3. **GPU-Based AABB**: Compute bounds on GPU from particle buffer
4. **Topology-Aware Adjacency**: Replace heuristic with proper adjacency buffer

---

## References

- **Müller et al.**: "Position Based Dynamics" (spatial hash recommendations)
- **Velvet**: Cloth simulation system (adaptive parameter inspiration)
- **Existing Implementation**: [`ClothBatchedSolver.cpp:2296`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:2296)
