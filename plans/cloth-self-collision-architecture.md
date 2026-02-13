# Cloth Self-Collision Architecture

## Overview

This document describes the self-collision detection and resolution system for cloth simulation in the EngineSIU engine. The implementation uses a **spatial hash grid** approach with a **two-pass GPU compute shader** architecture for efficient parallel collision detection.

---

## System Architecture

### High-Level Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    Simulation Frame                          │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  1. Update Self-Collision Parameters (Once per frame)        │
│     └─> UpdateSelfCollisionParams()                          │
│                                                               │
│  2. Substep Loop (Multiple iterations)                       │
│     ├─> Integration (predict positions)                      │
│     ├─> Collision with external objects                      │
│     ├─> Constraint Solver Loop                               │
│     │   ├─> Distance Constraints                             │
│     │   ├─> Self-Collision Detection & Resolution ◄──────────│
│     │   │   ├─> Pass 1: Build Spatial Hash Grid             │
│     │   │   └─> Pass 2: Solve Collisions                    │
│     │   └─> Apply Deltas                                     │
│     ├─> Kinematic Targets                                    │
│     └─> Finalize (update velocities)                         │
│                                                               │
│  3. Update Normals (Once per frame)                          │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Breakdown

### 1. CPU-Side Management (ClothBatchedSolver.cpp)

#### 1.1 Initialization

**Location**: [`AllocateSelfCollisionBuffers()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:950)

**Purpose**: Allocate GPU buffers for spatial hash grid

**Key Buffers**:
- **`SelfCollisionCellCountersBuffer`**: Atomic counters for each grid cell (uint32 per cell)
- **`SelfCollisionCellDataBuffer`**: Flat array storing particle indices per cell
- **`SelfCollisionParamsBuffer`**: Constant buffer with grid parameters

**Memory Calculation**:
```cpp
uint32 totalCells = gridDim³  // e.g., 64³ = 262,144 cells
uint32 maxPerCell = 16         // configurable
Memory = totalCells * sizeof(uint32)                    // Counters
       + totalCells * maxPerCell * sizeof(uint32)       // Data
       ≈ 1 MB + 16 MB = ~17 MB for 64³ grid
```

#### 1.2 Parameter Update

**Location**: [`UpdateSelfCollisionParams()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:2439)

**Frequency**: Once per frame (optimization - not per substep)

**Algorithm**:
1. **Collect parameters** from all active cloth instances:
   - Average edge length
   - Mesh bounds (min/max)
   - Adaptive cell size
   - Collision radius

2. **Compute median values** (robust to outliers):
   ```cpp
   avgEdgeLengths.Sort();
   int32 medianIdx = avgEdgeLengths.Num() / 2;
   float medianCellSize = cellSizes[medianIdx];
   float medianCollisionRadius = collisionRadii[medianIdx];
   ```

3. **Apply artist-controlled multipliers**:
   ```cpp
   finalCellSize = medianCellSize * Config.SelfCollisionCellSizeMultiplier;
   finalCollisionRadius = medianCollisionRadius * Config.SelfCollisionRadiusMultiplier;
   ```

4. **Compute global grid bounds**:
   ```cpp
   // Expand bounds with margin
   float margin = finalCellSize * 2.0f;
   FVector gridMin = globalMin - FVector(margin, margin, margin);
   FVector gridMax = globalMax + FVector(margin, margin, margin);
   ```

5. **Calculate grid dimensions**:
   ```cpp
   FVector extent = gridMax - gridMin;
   gridDimX = ceil(extent.X / finalCellSize);
   gridDimY = ceil(extent.Y / finalCellSize);
   gridDimZ = ceil(extent.Z / finalCellSize);
   // Clamped to MaxGridDim (128)
   ```

6. **Upload to GPU** via constant buffer

**Key Parameters**:
```cpp
struct FClothSelfCollisionParams {
    FVector GridMin;              // World-space grid origin
    float CellSize;               // Size of each grid cell
    uint32 GridDimX, GridDimY, GridDimZ;  // Grid dimensions
    uint32 MaxParticlesPerCell;   // Overflow limit
    float CollisionRadius;        // Particle collision radius
    float CollisionStiffness;     // Response strength
    uint32 bEnableSelfCollision;  // Toggle flag
};
```

#### 1.3 Dispatch

**Location**: [`DispatchSelfCollision()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:2353)

**Frequency**: Multiple times per substep (inside constraint solver loop)

**Execution Flow**:
```cpp
// PASS 1: Build spatial hash grid
{
    // Clear cell counters to zero
    ClearUnorderedAccessViewUint(SelfCollisionCellCountersUAV, {0,0,0,0});
    
    // Bind shader and resources
    CSSetShader(SelfCollisionBuildGridCS);
    CSSetConstantBuffers({BatchSimConstantBuffer, SelfCollisionParamsBuffer});
    CSSetShaderResources({UnifiedPredictedSRV, UnifiedInvMassSRV});
    CSSetUnorderedAccessViews({SelfCollisionCellCountersUAV, SelfCollisionCellDataUAV});
    
    // Dispatch (one thread per particle)
    Dispatch(GetDispatchCount(ParticleCount, 256), 1, 1);
}

// PASS 2: Solve self-collisions
{
    // Bind shader and resources
    CSSetShader(SelfCollisionSolverCS);
    CSSetConstantBuffers({BatchSimConstantBuffer, SelfCollisionParamsBuffer});
    CSSetShaderResources({
        UnifiedPredictedSRV,           // Particle positions
        UnifiedInvMassSRV,             // Inverse masses
        SelfCollisionCellCountersSRV,  // Cell occupancy
        SelfCollisionCellDataSRV,      // Particle indices
        UnifiedIndexSRV                // For topology check
    });
    CSSetUnorderedAccessViews({
        UnifiedPositionDeltaUAV,       // Accumulate corrections
        UnifiedPositionWeightUAV       // Accumulate weights
    });
    
    // Dispatch (one thread per particle)
    Dispatch(GetDispatchCount(ParticleCount, 256), 1, 1);
}

// PASS 3: Apply corrections (handled by DispatchApplyDeltas)
```

---

### 2. GPU Shader Implementation

#### 2.1 Pass 1: Build Spatial Hash Grid

**File**: [`ClothSelfCollisionBuildGrid.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionBuildGrid.hlsl)

**Entry Point**: `BuildSpatialHashGridCS`

**Thread Configuration**: `[numthreads(256, 1, 1)]`

**Algorithm**:

```hlsl
// 1. Compute grid cell from particle position
uint3 GetGridCell(float3 position)
{
    float3 localPos = position - GridMin;
    uint3 cell = uint3(floor(localPos / CellSize));
    return clamp(cell, uint3(0,0,0), GridDimensions - uint3(1,1,1));
}

// 2. Hash 3D cell to 1D index
uint GetCellHash(uint3 cell)
{
    return cell.x + cell.y * GridDimensions.x + 
           cell.z * GridDimensions.x * GridDimensions.y;
}

// 3. Insert particle into grid
void BuildSpatialHashGridCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    
    // Skip out-of-bounds and kinematic particles
    if (particleIdx >= NumParticles || InvMass[particleIdx] == 0.0f)
        return;
    
    // Compute cell hash
    float3 pos = PredictedRead[particleIdx].Position;
    uint3 cell = GetGridCell(pos);
    uint cellHash = GetCellHash(cell);
    
    // Atomically increment counter and get slot
    uint slot;
    InterlockedAdd(CellCounters[cellHash], 1, slot);
    
    // Write particle index if space available
    if (slot < MaxParticlesPerCell)
    {
        uint writeIndex = cellHash * MaxParticlesPerCell + slot;
        CellData[writeIndex] = particleIdx;
    }
    // Note: Overflow particles are silently dropped
}
```

**Key Features**:
- **Atomic operations** ensure thread-safe insertion
- **Overflow handling**: Particles beyond `MaxParticlesPerCell` are dropped (acceptable trade-off)
- **Kinematic particles skipped**: Only dynamic particles participate

#### 2.2 Pass 2: Solve Self-Collisions

**File**: [`ClothSelfCollisionSolver.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionSolver.hlsl)

**Entry Point**: `SolveSelfCollisionsCS`

**Thread Configuration**: `[numthreads(256, 1, 1)]`

**Algorithm**:

```hlsl
void SolveSelfCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    
    // Skip out-of-bounds and kinematic particles
    if (particleIdx >= NumParticles || InvMass[particleIdx] == 0.0f)
        return;
    
    float3 posA = PredictedRead[particleIdx].Position;
    uint3 cellA = GetGridCell(posA);
    
    // Query 3×3×3 neighborhood (27 cells)
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = int3(cellA) + int3(dx, dy, dz);
        
        // Bounds check
        if (any(neighborCell < 0) || any(neighborCell >= GridDimensions))
            continue;
        
        uint cellHash = GetCellHash(uint3(neighborCell));
        uint cellCount = min(CellCounters[cellHash], MaxParticlesPerCell);
        
        // Check all particles in this cell
        for (uint i = 0; i < cellCount; i++)
        {
            uint particleIdxB = CellData[cellHash * MaxParticlesPerCell + i];
            
            // Skip self and topologically adjacent particles
            if (particleIdxB == particleIdx)
                continue;
            if (AreTopologicallyAdjacent(particleIdx, particleIdxB))
                continue;
            
            float3 posB = PredictedRead[particleIdxB].Position;
            
            // Collision detection
            float3 diff = posB - posA;
            float dist = length(diff);
            float minDist = 2.0f * CollisionRadius;
            
            if (dist < minDist && dist > EPSILON)
            {
                // Mass-weighted separation response
                float3 normal = diff / dist;
                float penetration = minDist - dist;
                
                float wSum = invMassA + invMassB;
                float3 correction = normal * penetration * CollisionStiffness;
                float3 corrA = -correction * (invMassA / wSum);
                float3 corrB = +correction * (invMassB / wSum);
                
                // Atomic accumulation (scaled to int for thread safety)
                int3 deltaA = int3(corrA * kScale);
                int3 deltaB = int3(corrB * kScale);
                
                InterlockedAdd(PositionDelta[particleIdx].x, deltaA.x);
                InterlockedAdd(PositionDelta[particleIdx].y, deltaA.y);
                InterlockedAdd(PositionDelta[particleIdx].z, deltaA.z);
                InterlockedAdd(PositionWeight[particleIdx], 1);
                
                InterlockedAdd(PositionDelta[particleIdxB].x, deltaB.x);
                InterlockedAdd(PositionDelta[particleIdxB].y, deltaB.y);
                InterlockedAdd(PositionDelta[particleIdxB].z, deltaB.z);
                InterlockedAdd(PositionWeight[particleIdxB], 1);
            }
        }
    }
}
```

**Key Features**:

1. **Spatial Query**: 3×3×3 neighborhood (27 cells)
   - Ensures all particles within `2 * CollisionRadius` are checked
   - Cell size typically set to `2 * CollisionRadius` for optimal coverage

2. **Topology Filtering**:
   ```hlsl
   bool AreTopologicallyAdjacent(uint idxA, uint idxB)
   {
       // Simple heuristic: particles within 1 index are likely adjacent
       return abs(int(idxA) - int(idxB)) <= 1;
   }
   ```
   - Prevents self-collision between directly connected vertices
   - **TODO**: Replace with proper adjacency buffer lookup for accuracy

3. **Mass-Weighted Response**:
   ```hlsl
   float wSum = invMassA + invMassB;
   float3 corrA = -correction * (invMassA / wSum);
   float3 corrB = +correction * (invMassB / wSum);
   ```
   - Heavier particles move less
   - Kinematic particles (invMass = 0) don't move

4. **Jacobi Accumulation Pattern**:
   - Corrections accumulated atomically to `PositionDelta` buffer
   - Applied in separate pass (`ApplyDeltas`) for stability
   - Fixed-point scaling (`kScale = 10000.0f`) for integer atomic operations

---

## Data Structures

### GPU Buffers

```cpp
// Spatial hash grid
RWStructuredBuffer<uint> CellCounters;     // [totalCells]
RWStructuredBuffer<uint> CellData;         // [totalCells * MaxPerCell]

// Particle data
StructuredBuffer<FClothParticle> PredictedRead;  // [NumParticles]
StructuredBuffer<float> InvMass;                 // [NumParticles]
Buffer<uint> Indices;                            // [NumTriangles * 3]

// Accumulation buffers (shared with constraint solver)
RWStructuredBuffer<int3> PositionDelta;    // [NumParticles]
RWStructuredBuffer<int> PositionWeight;    // [NumParticles]
```

### Constant Buffer

```cpp
cbuffer SelfCollisionParams : register(b1)
{
    float3 GridMin;              // Grid origin in world space
    float CellSize;              // Size of each cell
    uint3 GridDimensions;        // Grid resolution (e.g., 64×64×64)
    uint MaxParticlesPerCell;    // Overflow limit (e.g., 16)
    float CollisionRadius;       // Particle radius
    float CollisionStiffness;    // Response strength (0-1)
    uint bEnableSelfCollision;   // Toggle flag
};
```

---

## Performance Characteristics

### Complexity Analysis

**Pass 1 (Build Grid)**:
- **Time**: O(N) where N = number of particles
- **Memory**: O(G + G×M) where G = grid cells, M = max per cell
- **Bottleneck**: Atomic operations on `CellCounters`

**Pass 2 (Solve Collisions)**:
- **Time**: O(N × 27 × M) = O(N) for well-distributed particles
- **Worst Case**: O(N²) if all particles in same cell
- **Bottleneck**: Atomic operations on `PositionDelta`

### Memory Usage

For typical configuration:
- Grid: 64³ = 262,144 cells
- Max per cell: 16 particles
- Memory: ~17 MB

### Optimization Strategies

1. **Cell Size Tuning**:
   - Optimal: `CellSize ≈ 2 × CollisionRadius`
   - Too small: Wasted memory, more neighbor checks
   - Too large: Many particles per cell, O(N²) behavior

2. **Grid Dimension Limits**:
   - Clamped to 128³ maximum
   - Prevents excessive memory usage
   - Adjusts cell size if needed

3. **Overflow Handling**:
   - Particles beyond `MaxParticlesPerCell` are dropped
   - Acceptable for sparse distributions
   - Increase `MaxPerCell` if needed

4. **Frame-Level Optimization**:
   - Parameters updated once per frame (not per substep)
   - Reduces CPU overhead
   - Grid remains valid for small motions

---

## Configuration Parameters

### Artist-Facing Controls

```cpp
struct FClothConfig
{
    // Self-collision toggle
    bool bEnableSelfCollision = true;
    
    // Grid configuration
    uint32 SelfCollisionGridDim = 64;           // Base grid resolution
    uint32 SelfCollisionMaxPerCell = 16;        // Overflow limit
    
    // Adaptive parameters (multipliers)
    float SelfCollisionCellSizeMultiplier = 1.0f;      // Adjust cell size
    float SelfCollisionRadiusMultiplier = 1.0f;        // Adjust collision radius
    float SelfCollisionStiffnessMultiplier = 1.0f;     // Adjust response strength
    
    // Base stiffness
    float SelfCollisionStiffness = 0.5f;        // Response strength (0-1)
};
```

### Tuning Guidelines

**Cell Size Multiplier**:
- `< 1.0`: Smaller cells, more memory, better accuracy
- `> 1.0`: Larger cells, less memory, may miss collisions

**Radius Multiplier**:
- `< 1.0`: Tighter collisions, may allow penetration
- `> 1.0`: Looser collisions, more separation

**Stiffness Multiplier**:
- `< 1.0`: Softer response, more penetration
- `> 1.0`: Harder response, more separation

---

## Integration with Simulation Loop

### Execution Order

```cpp
void SimulateSubstep(float DeltaTime)
{
    // 1. Predict positions
    DispatchIntegration(UsedParticleCount);
    
    // 2. External collisions
    DispatchCollisionSDF(UsedParticleCount);
    DispatchEdgeCollisionSDF(UsedEdgeCollisionCount);
    
    // 3. Constraint solver loop
    for (int iter = 0; iter < NumIterations; ++iter)
    {
        // Distance constraints
        DispatchConstraintSolver(UsedConstraintCount);
        
        // Self-collision (NEW)
        if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
        {
            DispatchSelfCollision(UsedParticleCount);
        }
        
        // Apply accumulated corrections
        DispatchApplyDeltas(UsedParticleCount);
    }
    
    // 4. Kinematic targets
    DispatchComputeKinematicTargets(UsedAttachmentCount);
    
    // 5. Finalize velocities
    DispatchFinalize(UsedParticleCount);
}
```

### Buffer Reuse

The self-collision solver **reuses existing buffers**:
- `UnifiedPositionDeltaUAV`: Shared with constraint solver
- `UnifiedPositionWeightUAV`: Shared with constraint solver
- `UnifiedPredictedSRV`: Read-only particle positions

This enables **seamless integration** with existing constraint solving pipeline.

---

## Known Limitations & Future Work

### Current Limitations

1. **Topology Check**:
   - Simple heuristic: `abs(idxA - idxB) <= 1`
   - May not work for non-grid meshes
   - **TODO**: Implement proper adjacency buffer

2. **Global Grid**:
   - Single grid for all cloth instances
   - May be suboptimal for widely separated cloths
   - **Future**: Per-instance grids with inter-instance collision

3. **Overflow Handling**:
   - Particles beyond `MaxPerCell` are dropped
   - Silent failure (no warning)
   - **Future**: Dynamic reallocation or warning system

4. **Fixed Grid**:
   - Grid parameters updated once per frame
   - May become invalid for fast-moving cloths
   - **Future**: Adaptive grid resizing based on motion

### Planned Enhancements

1. **Adjacency Buffer**:
   - Precompute edge connectivity
   - Accurate topology filtering
   - Prevents false positives

2. **Per-Instance Grids**:
   - Separate grids for each cloth
   - Better memory efficiency
   - Inter-instance collision support

3. **Dynamic Bounds**:
   - GPU-based bounds computation
   - Automatic grid resizing
   - Motion-based update triggers

4. **Friction Support**:
   - Tangential velocity damping
   - More realistic cloth-cloth interaction

---

## Debugging & Validation

### Validation Checks

**Location**: `FClothMeshAnalysis::ValidateSelfCollisionSetup()`

**Checks**:
1. Cell size vs collision radius ratio
2. Grid dimensions vs particle count
3. Memory usage estimation
4. Overflow risk assessment

### Debug Visualization

**Recommended Additions**:
- Grid cell visualization (wireframe boxes)
- Particle-to-cell mapping (color coding)
- Collision pair visualization (lines between colliding particles)
- Overflow indicator (highlight dropped particles)

### Performance Profiling

**Key Metrics**:
- Pass 1 time (grid build)
- Pass 2 time (collision solve)
- Cell occupancy histogram
- Overflow count per frame
- Atomic contention (via GPU profiler)

---

## References

### Related Files

**CPU Implementation**:
- [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp) - Main solver logic
- [`ClothBatchedSolver.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h) - Class definition

**GPU Shaders**:
- [`ClothSelfCollisionBuildGrid.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionBuildGrid.hlsl) - Pass 1
- [`ClothSelfCollisionSolver.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionSolver.hlsl) - Pass 2

**Supporting Systems**:
- [`ClothMeshAnalysis.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshAnalysis.cpp) - Parameter computation
- [`ClothApplyDelta.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothApplyDelta.hlsl) - Pass 3 (apply corrections)

### Algorithm Background

**Spatial Hashing**:
- Teschner et al. "Optimized Spatial Hashing for Collision Detection of Deformable Objects" (2003)
- Eitz & Lixu "Parallel Collision Detection for Deformable Objects" (2007)

**Position-Based Dynamics**:
- Müller et al. "Position Based Dynamics" (2007)
- Macklin & Müller "Position Based Fluids" (2013)

---

## Summary

The self-collision system provides **efficient, parallel collision detection** for cloth simulation using:

1. **Spatial Hash Grid**: O(N) average-case complexity
2. **Two-Pass GPU Architecture**: Build grid → Solve collisions
3. **Jacobi Accumulation**: Thread-safe, stable corrections
4. **Adaptive Parameters**: Automatic tuning based on mesh properties
5. **Seamless Integration**: Reuses existing constraint solver infrastructure

The implementation balances **performance, accuracy, and memory usage** for real-time cloth simulation with multiple instances.
