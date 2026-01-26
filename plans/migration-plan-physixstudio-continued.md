# PhysixStudio Migration Plan - Remaining Phases

This document continues from the Phase 0 preparation checklist in `migration-plan-physixstudio.md`.

---

## Preparation Checklist (Continued)

**Modify `ClothBatchTypes.h`** - Add to FClothInstanceMetadata and FClothInstanceParameters:
```cpp
uint32 ShearConstraintOffset;
uint32 ShearConstraintCount;
uint32 AreaConstraintOffset;
uint32 AreaConstraintCount;
uint32 LRAOffset;
uint32 LRACount;
uint32 NumColors;  // For graph-colored distance constraints
```

**New HLSL Shaders Needed**:
- `ClothSolveShear.hlsl`
- `ClothSolveArea.hlsl`
- `ClothSolveLRA.hlsl`  
- `ClothClearLambdas.hlsl`

**Build System**: Ensure shader compilation for all new `.hlsl` files

---

## Phase 1: Core Simulation Data Structures

### Goal
Add shear, area, and LRA constraint structures matching PhysixStudio's layout

### Reference Files
- PhysixStudio: `cloth_sim_data.h` lines 91-145
- EngineSIU: `ClothSimulationData.h`, `ClothGPUStructs.h`, `ClothCommon.hlsli`

### Implementation Steps

1. **Add C++ structures** to `ClothSimulationData.h`
2. **Add GPU structures** to `ClothGPUStructs.h` (with 32-byte alignment)
3. **Add HLSL structures** to `ClothCommon.hlsli`
4. **Extend instance metadata** in `ClothBatchTypes.h`
5. **Add graph coloring field** to `FClothDistanceConstraint`

### Completion Criteria
- All structures defined with proper alignment
- Static asserts pass
- Project builds
- No regression in existing cloth simulation

---

## Phase 2: Distance Constraint Solver with Graph Coloring

### Goal
Implement PhysixStudio's graph-colored direct-write distance constraint solver

### Reference Files
- PhysixStudio: `cloth_sim_data.h` lines 180-307 (BuildStretchConstraints)
- PhysixStudio: `shaders/simulation/solve_stretch.comp`
- EngineSIU: `ClothConstraintSolver.hlsl`

### Implementation Steps

#### 2.1 Graph Coloring Algorithm (CPU)
Add to `ClothBatchManager.cpp`:
```cpp
uint32 ColorConstraintsGreedy(
    TArray<FClothDistanceConstraint>& Constraints,
    uint32 NumParticles,
    TArray<uint32>& OutColorOffsets);
```

Key logic:
- Use vertex bitmask to track particle usage per color
- Find first unused color for each constraint
- Sort constraints by color group
- Build color offset array for dispatch

#### 2.2 Update Solver Dispatch
Modify `ClothBatchedSolver::DispatchConstraintSolver()`:
- Loop through each color group
- Dispatch constraints of same color (no conflicts)
- Use direct position writes instead of delta accumulation

#### 2.3 Update Shader
Replace `ClothConstraintSolver.hlsl` with PhysixStudio XPBD formulation:
```hlsl
[numthreads(256, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    // PhysixStudio XPBD pattern
    // Direct writes (no atomics needed due to coloring)
    // alpha_tilde = compliance / (dt * dt)
    // lambda update with stiffness multiplier
}
```

### Completion Criteria
- Graph coloring produces 4-8 colors typically
- Constraints sorted by color
- Direct position writes work correctly
- Cloth stretching behavior matches PhysixStudio quality

---

## Phase 3: Shear Constraints

### Goal
Add shear constraint support to prevent triangle deformation

### Reference Files
- PhysixStudio: `cloth_sim_data.h` lines 309-344 (BuildShearConstraints)
- PhysixStudio: `shaders/simulation/solve_shear.comp`

### Implementation Steps

#### 3.1 Constraint Building
Add to `ClothBatchManager.cpp`:
```cpp
void BuildShearConstraints(
    const TArray<FVector>& RestPositions,
    const TArray<uint32>& Indices,
    TArray<FClothShearConstraint>& OutConstraints);
```

For each triangle:
- Compute rest dot product: `restDot = dot(e1, e2)` where e1, e2 are triangle edges
- Store as constraint

#### 3.2 Create Shader
New file: `ClothSolveShear.hlsl`
```hlsl
[numthreads(256, 1, 1)]
void SolveShearConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    // Load triangle vertices
    // Current dot product: dot(e1_current, e2_current)
    // Constraint: C = currentDot - restDot
    // Compute gradients
    // Accumulate deltas (using atomics)
}
```

#### 3.3 Integrate into Solver Loop
Add to `ClothBatchedSolver::SimulateSubstep()`:
```cpp
// After distance constraints, before ApplyDeltas
DispatchShearConstraintSolver(ShearConstraintCount);
```

### Completion Criteria
- Shear constraints prevent triangle skewing
- Visual difference: fabric doesn't collapse into thin lines
- Performance: minimal overhead per iteration

---

## Phase 4: Area Constraints

### Goal
Preserve triangle areas to prevent volume loss and maintain cloth thickness

### Reference Files
- PhysixStudio: `cloth_sim_data.h` lines 434-474 (BuildAreaConstraints)
- PhysixStudio: `shaders/simulation/solve_area.comp`

### Implementation Steps

#### 4.1 Constraint Building
Add to `ClothBatchManager.cpp`:
```cpp
void BuildAreaConstraints(
    const TArray<FVector>& RestPositions,
    const TArray<uint32>& Indices,
    TArray<FClothAreaConstraint>& OutConstraints);
```

For each triangle:
- Compute rest area: `0.5 * length(cross(e0, e1))`
- Compute rest normal: `normalize(cross(e0, e1))`
- Store both

#### 4.2 Create Shader
New file: `ClothSolveArea.hlsl`
```hlsl
[numthreads(256, 1, 1)]
void SolveAreaConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    // Current area = 0.5 * dot(cross(e1, e2), restNormal)
    // Constraint: C = currentArea - restArea
    // Gradients: 0.5 * cross(restNormal, edges)
    // Accumulate deltas
}
```

#### 4.3 Integrate into Solver Loop
Add after shear constraints in iteration loop

### Completion Criteria
- Cloth maintains volume under compression
- No artificial thinning or thickening
- Visual: more natural draping behavior

---

## Phase 5: Bend Constraint Improvements

### Goal
Upgrade bend constraints to PhysixStudio's dihedral angle formulation

### Reference Files
- PhysixStudio: `cloth_sim_data.h` lines 346-432 (BuildBendConstraints)
- PhysixStudio: `shaders/simulation/solve_bend.comp`

### Implementation Steps

#### 5.1 Update Constraint Building
Improve `BuildBendConstraints` to match PhysixStudio:
- Build edge-to-triangle map
- Find adjacent triangles
- Compute rest dihedral angle using atan2

#### 5.2 Update Shader
Modify `ClothBendConstraintSolver.hlsl`:
```hlsl
// PhysixStudio pattern:
// - Compute current dihedral angle
// - Use gradient from isometric bending model
// - XPBD formulation with compliance
// - Delta accumulation
```

### Completion Criteria
- Bending behavior matches PhysixStudio quality
- No creasing artifacts
- Natural fold behavior

---

## Phase 6: Long Range Attachments (LRA)

### Goal
Implement PhysixStudio's LRA system for soft kinematic attachment

### Reference Files
- PhysixStudio: `cloth_sim_data.h` lines 476-586 (BuildLRAConstraints)
- PhysixStudio: `shaders/simulation/solve_lra.comp`

### Implementation Steps

#### 6.1 Dijkstra Shortest Path
Add graph distance computation:
```cpp
TArray<float> ComputeGraphDistances(
    const TArray<FClothDistanceConstraint>& Edges,
    uint32 SourceParticle,
    uint32 NumParticles);
```

#### 6.2 LRA Building
For each particle, find K=2 nearest anchor particles by graph distance

#### 6.3 Create Shader
New file: `ClothSolveLRA.hlsl`
```hlsl
[numthreads(256, 1, 1)]
void SolveLRAConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    // For K anchors per particle:
    //   if distance > maxDistance:
    //     project toward anchor
    //   blend with stiffness parameter
}
```

### Completion Criteria
- Soft attachment to kinematic targets
- No hard snapping
- Cloth moves naturally with attachments

---

## Phase 7: Self-Collision System

### Goal
Add spatial hashing and self-collision constraints

### Reference Files
- PhysixStudio: `shaders/hash/build_hash.comp`, `build_cell.comp`, `build_neighbor.comp`
- PhysixStudio: `shaders/simulation/solve_self_collision.comp`
- PhysixStudio: `cloth_sim_pass.cpp` lines 206-347 (broad phase)

### Implementation Steps

#### 7.1 Spatial Hash Buffers
Add to `ClothBatchedSolver`:
```cpp
ID3D11Buffer* ParticleHashBuffer;
ID3D11Buffer* SortedIndicesBuffer;
ID3D11Buffer* CellStartBuffer;
ID3D11Buffer* CellEndBuffer;
ID3D11Buffer* NeighborBuffer;
```

#### 7.2 Hash Computation Shader
`ClothBuildHash.hlsl`:
```hlsl
// Compute hash = (x/