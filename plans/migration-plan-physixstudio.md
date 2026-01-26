# PhysixStudio to EngineSIU Cloth Simulation Migration Plan

**Date Created**: 2026-01-26  
**Target**: Migrate PhysixStudio's GPU cloth simulation to EngineSIU's DirectX 11 batched system

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Phase 0: Analysis & Infrastructure Setup](#phase-0-analysis--infrastructure-setup)
3. [Phase 1: Core Simulation Data Structures](#phase-1-core-simulation-data-structures)
4. [Phase 2: Distance Constraint Solver with Graph Coloring](#phase-2-distance-constraint-solver-with-graph-coloring)
5. [Phase 3: Shear Constraints](#phase-3-shear-constraints)
6. [Phase 4: Area Constraints](#phase-4-area-constraints)
7. [Phase 5: Bend Constraint Improvements](#phase-5-bend-constraint-improvements)
8. [Phase 6: Long Range Attachments (LRA)](#phase-6-long-range-attachments-lra)
9. [Phase 7: Self-Collision System](#phase-7-self-collision-system)
10. [Phase 8: Finalization & Optimization](#phase-8-finalization--optimization)
11. [Appendices](#appendices)

---

## Executive Summary

### Migration Objective

Port PhysixStudio's advanced GPU cloth simulation system (Vulkan/GLSL) to EngineSIU's DirectX 11 compute shader infrastructure while **preserving EngineSIU's existing batch management, actor/component structure, and rendering pipeline**.

### Key Principles

1. **PhysixStudio as Simulation Blueprint**: For all simulation logic, constraint formulations, and solver patterns, PhysixStudio is the source of truth
2. **Preserve EngineSIU Infrastructure**: Maintain batch management (ClothBatchManager), actor/component architecture, and rendering pipeline (ClothRenderPass)
3. **Incremental & Testable**: Each phase can be compiled, run, and visually validated
4. **API Translation**: Systematic mapping of Vulkan/GLSL patterns to DirectX 11/HLSL

### What We're Replacing

**From EngineSIU** (will be modified/replaced):
- Simulation loop structure → PhysixStudio's substep + iteration pattern
- Distance constraint solver → PhysixStudio's XPBD with graph coloring
- Missing constraints → Add shear, area, LRA like PhysixStudio
- Velocity update → PhysixStudio's finalize pattern

**Keeping from EngineSIU** (minimal changes):
- `ClothBatchManager` - Instance lifecycle management
- `ClothBatchTypes.h` - Batch metadata structures
- `FClothInstanceCreationParams` - Instance creation API
- `ClothRenderPass` - Rendering pipeline
- `TestBatchedClothActor` - Test harness
- Buffer allocation and management patterns

### Expected Outcomes

After migration, EngineSIU will have:
- PhysixStudio-quality cloth simulation (stable, realistic, artist-friendly)
- All PhysixStudio constraint types (stretch, shear, bend, area, LRA)
- XPBD compliance and stability
- Graph-colored parallel constraint solving
- Self-collision support
- Maintained batched multi-instance architecture

---

## Phase 0: Analysis & Infrastructure Setup

### 0.1 PhysixStudio Architecture Analysis

#### Simulation Pipeline Overview

PhysixStudio uses a **fixed timestep substep structure**:

```
Per Frame:
  ├─ For each substep (default 10):
  │   ├─ Wind force computation (optional)
  │   ├─ Integrate (semi-implicit Euler)
  │   ├─ Clear lambdas (XPBD warm start reset)
  │   ├─ Broad Phase (periodic, hash-based spatial grid)
  │   ├─ For each iteration (default 4):
  │   │   ├─ Solve Stretch (graph-colored, direct write)
  │   │   ├─ Solve Shear (delta accumulation)
  │   │   ├─ Solve Bend (delta accumulation)
  │   │   ├─ Solve Area (delta accumulation)
  │   │   ├─ Solve Self-Collision (narrow phase)
  │   │   └─ Apply Deltas (with relaxation factor)
  │   ├─ Solve LRA (long range attachments, once per substep)
  │   ├─ Collide SDF (geometry collision)
  │   └─ Update Velocity (finalize with damping)
  └─ Calculate Normals (triangle → vertex averaging)
```

**Key Files**:
- **Simulation Loop**: `C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_pass.cpp` (lines 143-576)
- **Data Structures**: `C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h`
- **Constraint Building**: `cloth_sim_data.h` (lines 180-586) - BuildStretchConstraints, BuildShearConstraints, BuildBendConstraints, BuildAreaConstraints, BuildLRAConstraints

#### PhysixStudio Constraint Types

| Constraint Type | Structure Size | Lambda Storage | Solving Pattern | Purpose |
|----------------|----------------|----------------|-----------------|---------|
| **Stretch/Distance** | 16 bytes | Yes (warm start) | Graph-colored, direct write | Prevent stretching |
| **Shear** | 32 bytes | Yes | Delta accumulation | Prevent shearing deformation |
| **Bend** | 32 bytes | Yes | Delta accumulation | Bending resistance |
| **Area** | 32 bytes | Yes | Delta accumulation | Preserve triangle area |
| **LRA** | Variable (2 ids + 2 dists per particle) | No | Direct projection | Long-range spring attachment |

**Stretch Constraint** (`cloth_sim_data.h` Edge struct, line 79):
```cpp
struct Edge {
    uint32_t i;        // Particle A
    uint32_t j;        // Particle B
    float rest;        // Rest length
    float lambda;      // XPBD lambda
};
```

**Shear Constraint** (`cloth_sim_data.h` Shear struct, line 91):
```cpp
struct Shear {
    uint32_t i0, i1, i2;  // Triangle vertices
    float rest_dot;       // Rest dot product (e1·e2)
    float lambda;         // XPBD lambda
    float padding[3];
};
```

**Bend Constraint** (`cloth_sim_data.h` Bend struct, line 103):
```cpp
struct Bend {
    uint32_t i0, i1, i2, i3;  // i0-i1: shared edge, i2-i3: opposite vertices
    float rest_angle;          // Rest dihedral angle
    float lambda;
    float padding[2];
};
```

**Area Constraint** (`cloth_sim_data.h` Area struct, line 113):
```cpp
struct Area {
    uint32_t i0, i1, i2;  // Triangle vertices
    float rest_area;       // Rest area
    vec3 rest_normal;      // Rest normal (normalized)
    float lambda;
};
```

#### PhysixStudio Data Buffers

**Particle Buffers** (`cloth_particle_manager.h`):
- `position` (vec4): Current position + padding
- `pred_position` (vec4): Predicted position after integration
- `velocity` (vec4): Velocity + padding
- `inverse_mass` (float): 0.0 = fixed particle

**Constraint Buffers**:
- `edges`: All distance constraints (with graph coloring metadata)
- `shears`: All shear constraints
- `bends`: All bend constraints  
- `areas`: All area constraints
- `lra_ids`, `lra_r`: Long range attachment data (K anchors per particle)

**Delta Accumulation Buffers** (for Jacobi-style solving):
- `delta_x`, `delta_y`, `delta_z` (float): Position corrections
- `delta_count` (uint): Number of contributing constraints

**Auxiliary Buffers**:
- `colliders`: Collision primitives (sphere, plane, capsule)
- `particle_hash`, `sorted_indice`, `starts`, `ends`: Spatial hashing for self-collision
- `tri_normals`: Per-triangle normals
- `vertex_tri_offsets`, `vertex_tri_indices`: Vertex adjacency for normal calc

#### PhysixStudio Shader Patterns

**Thread Group Size**: 256 threads per group (consistent across all shaders)

**Common Vulkan → HLSL Mappings**:
```glsl
// Vulkan GLSL
layout(local_size_x = 256) in;
layout(std430, set=1, binding=0) buffer X { vec4 x[]; };
layout(push_constant) uniform PC { ... } pc;
uint gid = gl_GlobalInvocationID.x;

// DirectX 11 HLSL equivalent
[numthreads(256, 1, 1)]
RWStructuredBuffer<float4> x : register(u0);
cbuffer Constants : register(b0) { ... }
uint gid = DTid.x; // from SV_DispatchThreadID
```

**Atomic Operations**:
```glsl
// Vulkan (requires GL_EXT_shader_atomic_float)
atomicAdd(delta_x[i], corr.x);

// DX11 HLSL (integer-scaled float atomics)
int deltaInt = int(corr.x * kScale);
InterlockedAdd(PositionDelta[i].x, deltaInt);
// Later: float avgDelta = float(PositionDelta[i].x) / kScale / count;
```

### 0.2 EngineSIU Current State Analysis

#### Existing Cloth System

**Key Files**:
- **Batch Manager**: `EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchManager.h/.cpp`
- **Batched Solver**: `EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchedSolver.h/.cpp`
- **Types**: `EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchTypes.h`
- **GPU Structs**: `EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothGPUStructs.h`
- **Simulation Data**: `EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothSimulationData.h`

**Batch Management Architecture**:
```
ClothWorld (top-level manager)
  └─ ClothBatchManager (per LOD level)
      ├─ ClothBatchedSolver (GPU simulation)
      ├─ TArray<ClothInstanceHandle*> (instance tracking)
      └─ Unified GPU buffers (particles, constraints, etc.)
```

**Current Simulation Loop** (`ClothBatchedSolver::Simulate`):
```cpp
// Simplified current pattern
For each iteration:
  ├─ DispatchIntegration()
  ├─ ClearAccumulationBuffers()
  ├─ DispatchConstraintSolver()        // Distance constraints
  ├─ DispatchBendConstraintSolver()    // Bend constraints
  ├─ DispatchApplyDeltas()             // Apply corrections
  └─ DispatchApplyKinematicTargets()   // Apply attachments
DispatchUpdateNormals()                 // Tri + vertex normals
```

**Current GPU Buffers**:
- `UnifiedPositionBuffer[2]`: Ping-pong position buffers
- `UnifiedVelocityBuffer`: Velocities
- `UnifiedInvMassBuffer`: Inverse masses
- `UnifiedConstraintBuffer`: Distance constraints
- `UnifiedBendConstraintBuffer`: Bend constraints
- `UnifiedKinematicTargetBuffer`: Attachment targets
- `UnifiedPositionDeltaBuffer`: Delta accumulation (int3, scaled)
- `UnifiedPositionWeightBuffer`: Delta weights
- `InstanceParameterBuffer`: Per-instance params

**Current Shaders** (`EngineSIU\Shaders\Cloth\`):
- `ClothIntegrate.hlsl`: Basic semi-implicit Euler
- `ClothConstraintSolver.hlsl`: Distance constraints with delta accumulation
- `ClothBendConstraintSolver.hlsl`: Bend constraints
- `ClothApplyDelta.hlsl`: Apply accumulated deltas
- `ClothApplyKinematicTargets.hlsl`: Kinematic attachment
- `ClothFinalize.hlsl`: Velocity update from position change
- `ClothUpdateNormals.hlsl`: Normal computation

#### What's Missing from EngineSIU

Compared to PhysixStudio:
1. **Shear constraints** - Not implemented
2. **Area constraints** - Not implemented
3. **Long Range Attachments (LRA)** - Basic kinematic only
4. **Graph coloring for distance constraints** - Uses delta accumulation (slower)
5. **XPBD compliance parameters** - Partially implemented
6. **Self-collision** - Not implemented
7. **Substep structure** - Basic, not matching PhysixStudio's pattern
8. **Proper velocity finalization** - Simpler than PhysixStudio

### 0.3 Architecture Comparison Table

| Aspect | PhysixStudio (Vulkan) | EngineSIU (DX11) | Migration Strategy |
|--------|----------------------|------------------|-------------------|
| **API** | Vulkan 1.4 Compute | DirectX 11 Compute | Map GLSL to HLSL |
| **Simulation Loop** | Substep → Iterations → Constraints | Iterations → Constraints | Adopt PhysixStudio's loop |
| **Thread Group Size** | 256 | 64 | Change to 256 to match |
| **Distance Constraints** | Graph-colored, direct write | Delta accumulation | Implement graph coloring |
| **Shear Constraints** | ✓ (delta accumulation) | ✗ Missing | Add PhysixStudio implementation |
| **Bend Constraints** | ✓ (delta accumulation) | ✓ (basic) | Upgrade to PhysixStudio formulation |
| **Area Constraints** | ✓ (delta accumulation) | ✗ Missing | Add PhysixStudio implementation |
| **LRA** | ✓ (K=2 anchors) | ✗ (basic kinematic) | Implement PhysixStudio LRA |
| **Self-Collision** | ✓ (spatial hashing) | ✗ Missing | Defer to Phase 7 |
| **Atomics** | Float atomics (ext) | Int-scaled atomics | Keep EngineSIU pattern |
| **XPBD** | Full (alpha_tilde, beta) | Partial (compliance) | Upgrade to PhysixStudio XPBD |
| **Batch Management** | Single instance | Multi-instance unified buffers | Keep EngineSIU batching |
| **Rendering** | Vulkan pipeline | DX11 ClothRenderPass | Keep EngineSIU rendering |

### 0.4 Preparation Checklist

#### New C++ Structures Needed

**Add to `ClothSimulationData.h`**:
```cpp
// Shear constraint (matches PhysixStudio)
struct FClothShearConstraint {
    uint32 ParticleA, ParticleB, ParticleC;  // Triangle vertices
    float RestDot;      // Rest dot product (e1·e2)
    float Compliance;
    float Lambda;
    float Padding[2];
};

// Area constraint (matches PhysixStudio)
struct FClothAreaConstraint {
    uint32 ParticleA, ParticleB, ParticleC;  // Triangle vertices
    float RestArea;
    FVector RestNormal;  // Pre-normalized
    float Lambda;
};

// LRA entry (matches PhysixStudio K=2 pattern)
struct FClothLRAEntry {
    uint32 AnchorParticleIndex;  // 0xFFFFFFFF if invalid
    float RestDistance;           // Graph distance in rest pose
};
```

**Add to `ClothGPUStructs.h`**:
```cpp
// GPU-compatible shear constraint (32 bytes)
struct FClothShearConstraintGPU {
    uint32 ParticleA, ParticleB, ParticleC;
    float RestDot;
    float Compliance;
    float Lambda;
    float Padding0, Padding1;
};

// GPU-compatible area constraint (32 bytes)
struct FClothAreaConstraintGPU {
    uint32 ParticleA, ParticleB, ParticleC;
    float RestArea;
    FVector RestNormal;  // 12 bytes
    float Lambda;        // 4 bytes
    // Total: 32 bytes
};

// LRA data (separate buffers)
// lra_ids: StructuredBuffer<uint> (particleCount * K entries)
// lra_r: StructuredBuffer<float> (particleCount * K entries)
```

**Modify `ClothBatchTypes.h`**:
```cpp
// Add to FClothInstanceMet