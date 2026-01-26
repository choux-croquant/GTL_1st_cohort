# PhysixStudio to EngineSIU GPU Cloth Simulation - Complete Migration Plan

**Date Created**: 2026-01-26  
**Version**: 1.0  
**Target**: Migrate PhysixStudio's Vulkan-based GPU cloth simulation to EngineSIU's DirectX 11 batched architecture

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Phase 0: Analysis & Infrastructure Setup](#phase-0-analysis--infrastructure-setup)
3. [Phase 1: Core Simulation Data Structures & XPBD Foundation](#phase-1-core-simulation-data-structures--xpbd-foundation)
4. [Phase 2: Distance Constraint Solver with Graph Coloring](#phase-2-distance-constraint-solver-with-graph-coloring)
5. [Phase 3: Shear Constraints](#phase-3-shear-constraints)
6. [Phase 4: Area Constraints](#phase-4-area-constraints)
7. [Phase 5: Bend Constraint XPBD Upgrade](#phase-5-bend-constraint-xpbd-upgrade)
8. [Phase 6: Long Range Attachments (LRA)](#phase-6-long-range-attachments-lra)
9. [Phase 7: Self-Collision System](#phase-7-self-collision-system)
10. [Phase 8: Simulation Loop Restructure & Optimization](#phase-8-simulation-loop-restructure--optimization)
11. [Appendices](#appendices)

---

## Executive Summary

### Migration Objective

Port **PhysixStudio's** advanced GPU cloth simulation (Vulkan 1.4 + GLSL) to **EngineSIU's** DirectX 11 compute shader infrastructure while **preserving EngineSIU's existing batch management, actor/component architecture, and rendering pipeline**.

### Core Principles

1. **PhysixStudio as Simulation Blueprint**: All simulation logic, constraint formulations, XPBD parameters, and solver patterns come from PhysixStudio
2. **Preserve EngineSIU Infrastructure**: Keep `ClothBatchManager`, actor/component system, `ClothRenderPass`, and batch rendering
3. **Incremental & Testable**: Each phase compiles, runs, and can be visually validated
4. **Explicit API Translation**: Systematic Vulkan/GLSL → DirectX 11/HLSL mapping documented

### Scope: What Changes

**Replacing (Simulation Logic)**:
- Simulation loop structure → PhysixStudio's substep + iteration pattern
- Distance solver → Graph-colored XPBD direct-write
- Bend solver → PhysixStudio's dihedral angle XPBD
- **Add new**: Shear, area, LRA constraints
- **Add new**: Self-collision with spatial hashing

**Preserving (Infrastructure)**:
- [`ClothBatchManager.h/.cpp`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchManager.h) - Instance lifecycle
- [`ClothBatchTypes.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchTypes.h) - Batch metadata
- [`FClothInstanceCreationParams`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchTypes.h:120) - Instance API
- [`ClothRenderPass.h/.cpp`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Renderer\ClothRenderPass.h) - Rendering pipeline
- [`TestBatchedClothActor`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Classes\Actors\TestBatchedClothActor.h) - Test harness

### Expected Outcomes

After migration:
- PhysixStudio-quality cloth simulation (stable, realistic, artist-friendly)
- All PhysixStudio constraint types: stretch, shear, bend, area, LRA
- Full XPBD with compliance/beta parameters
- Graph-colored parallel constraint solving
- Self-collision support (optional, toggleable)
- **Maintained** batched multi-instance architecture

---

## Phase 0: Analysis & Infrastructure Setup

### 0.1 PhysixStudio Architecture Deep Dive

#### Simulation Pipeline

PhysixStudio follows this structure (from [`cloth_sim_pass.cpp`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_pass.cpp:143)):

```
Per Frame (60 Hz):
  └─ For each substep (default 10, ~600 Hz effective):
      ├─ Wind Force (optional, applied to triangles)
      ├─ Integrate (semi-implicit Euler, predict positions)
      ├─ Clear Lambdas (reset XPBD warm start state)
      ├─ Broad Phase (every N substeps, spatial hash rebuild)
      │   ├─ Build Hash (position → cell hash)
      │   ├─ Radix Sort (sort particles by hash)
      │   └─ Build Neighbors (query neighbor cells)
      ├─ Constraint Iteration Loop (default 4 iterations):
      │   ├─ Solve Stretch (graph-colored, DIRECT position write)
      │   ├─ Solve Shear (delta accumulation with atomics)
      │   ├─ Solve Bend (delta accumulation with atomics)
      │   ├─ Solve Area (delta accumulation with atomics)
      │   ├─ Solve Self-Collision (narrow phase, periodic)
      │   └─ Apply Deltas (average + relaxation factor)
      ├─ Solve LRA (long range attachments, ONCE per substep)
      ├─ Collide SDF (geometry collision vs. colliders)
      └─ Update Velocity (finalize from Δposition, apply damping)
  └─ Calculate Normals (triangle normals → vertex average, ONCE per frame)
```

**Key Insight**: PhysixStudio uses **two solving patterns**:
1. **Graph-colored direct write** (stretch only) - No atomics, maximum parallelism
2. **Delta accumulation** (shear, bend, area, self-collision) - Jacobi-style, uses atomics

#### Data Structure Analysis

**Particle State** ([`cloth_particle_manager.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_particle_manager.h:79)):
```cpp
std::vector<glm::vec4> positions_;        // Current position (w=1.0)
std::vector<glm::vec4> pred_positions_;   // Predicted position after integration
std::vector<glm::vec4> velocities_;       // Velocity (w unused)
std::vector<float> inverse_masses_;       // 1/mass (0.0 = fixed particle)
```

**Constraint Definitions** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:79)):

| Type | Struct | Size | Fields | Lambda | Reference |
|------|--------|------|--------|--------|-----------|
| **Distance/Stretch** | `Edge` | 16B | `i, j, rest, lambda` | Yes | Line 79 |
| **Shear** | `Shear` | 32B | `i0,i1,i2, rest_dot, lambda, padding[3]` | Yes | Line 91 |
| **Bend** | `Bend` | 32B | `i0,i1,i2,i3, rest_angle, lambda, padding[2]` | Yes | Line 103 |
| **Area** | `Area` | 32B | `i0,i1,i2, rest_area, rest_normal, lambda` | Yes | Line 113 |
| **LRA** | `lra_ids[], lra_r[]` | Variable | K entries per particle (K=2) | No | Line 146 |

**Delta Accumulation Buffers** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:631)):
```cpp
vk::raii::Buffer delta_x, delta_y, delta_z;  // float[numParticles]
vk::raii::Buffer delta_count;                 // uint[numParticles]
```

Used by: Shear, Bend, Area, Self-Collision constraints (all use `accumulate_delta` helper)

**Simulation Constants** ([`cloth_ubo.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_ubo.h:4)):
```cpp
struct SimParams {
    vec4 gravity;
    float dt;                    // Substep dt (frame_dt / substeps)
    float thickness;
    float friction;
    float max_speed;
    float global_damping;
    float relaxation_factor;     // Applied in apply_deltas
    float neighbor_friction;
    uint32_t num_particles;
    uint32_t num_edges, num_shears, num_bends, num_areas;
    uint32_t num_colliders;
    // ... spatial hash params
};
```

**Push Constants** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:20)):
```cpp
struct Solve {
    uint32_t base;       // Offset into constraint array
    uint32_t count;      // Number of constraints to process
    float compliance;    // XPBD compliance (alpha)
    float beta;          // XPBD damping parameter
    float stiffness;     // Artist-facing stiffness multiplier
    float padding[3];
};
```

#### Constraint Building Algorithms

**Graph Coloring for Distance Constraints** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:252)):
```cpp
// Greedy graph coloring algorithm
// Ensures no two constraints in same color share particles
// Typical output: 4-8 colors for rectangular cloth mesh

std::vector<uint32_t> vertMask(positions.size(), 0u);  // Bitmask per vertex
uint32_t maxColorUsed = 0;

for (auto& edge : stretch) {
    uint32_t usedMask = vertMask[edge.i] | vertMask[edge.j];
    
    // Find first free color
    uint32_t color = 0;
    while (usedMask & (1u << color)) ++color;
    
    edge.color = color;
    maxColorUsed = std::max(maxColorUsed, color);
    
    // Mark vertices as used by this color
    vertMask[edge.i] |= (1u << color);
    vertMask[edge.j] |= (1u << color);
}

// Sort constraints by color for efficient dispatch
std::sort(stretch.begin(), stretch.end(), 
    [](auto& a, auto& b) { return a.color < b.color; });
```

**Shear Constraint Building** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:309)):
```cpp
// One shear constraint per triangle
for (each triangle i0, i1, i2) {
    vec3 e1 = x1 - x0;
    vec3 e2 = x2 - x0;
    float restDot = dot(e1, e2);
    // Store {i0, i1, i2, restDot}
}
```

**Bend Constraint Building** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:346)):
```cpp
// For each shared edge with two adjacent triangles:
// Find opposite vertices (i2, i3)
// Compute rest dihedral angle using atan2(s, c)
float ComputeRestBendAngle(i0, i1, i2, i3, positions) {
    vec3 e = p1 - p0;  // Shared edge
    vec3 n1 = normalize(cross(p1-p0, p2-p0));  // Triangle 1 normal
    vec3 n2 = normalize(cross(p1-p0, p3-p0));  // Triangle 2 normal
    float c = dot(n1, n2);
    float s = dot(normalize(e), cross(n1, n2));
    return atan2(s, c);
}
```

**Area Constraint Building** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:434)):
```cpp
// One area constraint per triangle
for (each triangle i0, i1, i2) {
    vec3 e0 = p1 - p0;
    vec3 e1 = p2 - p0;
    vec3 restNormal = cross(e0, e1);
    float restArea = 0.5f * length(restNormal);
    vec3 normalizedRestNormal = restNormal / (2.0f * restArea);
    // Store {i0, i1, i2, restArea, normalizedRestNormal}
}
```

**LRA Building** ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:515)):
```cpp
// Long Range Attachment: K=2 nearest anchor particles per vertex
// Uses Dijkstra shortest path on constraint graph
// Typical anchors: top two corner vertices

For each vertex v:
    Find K=2 nearest anchors by graph distance
    Store anchor indices and distances (with slack multiplier 1.1)
    lra_ids[v*K+0] = nearestAnchor1
    lra_r[v*K+0] = graphDist1 * 1.1
    lra_ids[v*K+1] = nearestAnchor2
    lra_r[v*K+1] = graphDist2 * 1.1
```

#### Shader Analysis

**Integration Shader** ([`integrate.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\integrate.comp:78)):
```glsl
// PhysixStudio pattern
void main() {
    uint i = pc.solve.base + gid;
    if (w[i] == 0.0) {  // Fixed particle
        xp[i] = vec4(xi, 1.0);
        v[i] = vec4(0.0);
        return;
    }
    
    // Apply external forces
    vi += gravity * dt;
    vec3 xpi = xi + vi * dt;  // Predict position
    
    xp[i] = vec4(xpi, 1.0);
    v[i] = vec4(vi, 0.0);
    
    // Clear delta accumulators
    delta_x[i] = 0.0;
    delta_y[i] = 0.0;
    delta_z[i] = 0.0;
    delta_count[i] = 0;
}
```

**Stretch Solver** ([`solve_stretch.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\solve_stretch.comp:9)):
```glsl
// XPBD formulation with velocity-level damping (beta parameter)
void main() {
    Edge e = edges[eidx];
    vec3 d = xi - xj;
    float len = length(d);
    vec3 n = d / len;
    
    // XPBD parameters
    float alpha_tilde = compliance / (dt * dt);
    float beta_tilde = dt * dt * beta;
    float gamma = (alpha_tilde * beta_tilde) / dt;
    
    float C = len - rest;  // Constraint violation
    
    // Relative velocity contribution
    vec3 dpi = xi - prev_xi;
    vec3 dpj = xj - prev_xj;
    float rel = dot(n, dpi) + dot(-n, dpj);
    
    // Solve for lambda increment
    float denom = (1.0 + gamma) * wsum + alpha_tilde;
    float rhs = C + alpha_tilde * lambda_old + gamma * rel;
    float dlambda = -rhs / denom;
    
    // Apply stiffness multiplier and update lambda
    edges[eidx].lambda = lambda_old + dlambda * stiffness;
    
    // DIRECT position correction (no atomics!)
    vec3 corr = dlambda * n;
    if (wi > 0.0) xp[i].xyz += wi * corr;
    if (wj > 0.0) xp[j].xyz -= wj * corr;
}
```

**Shear Solver** ([`solve_shear.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\solve_shear.comp:16)):
```glsl
void main() {
    Shear s = shears[gid];
    vec3 e1 = x1 - x0;
    vec3 e2 = x2 - x0;
    
    float dotNow = dot(e1, e2);
    float C = dotNow - s.rest_dot;
    
    // Gradients
    vec3 grad0 = -(e1 + e2);
    vec3 grad1 = e2;
    vec3 grad2 = e1;
    
    // XPBD solve
    float alpha_tilde = compliance / (dt * dt);
    float wsum_grad = w0*dot(grad0,grad0) + w1*dot(grad1,grad1) + w2*dot(grad2,grad2);
    float denom = wsum_grad + alpha_tilde;
    float dlambda = -(C + alpha_tilde * lambda_old) / denom;
    
    shears[gid].lambda = lambda_old + dlambda * stiffness;
    
    // ATOMIC accumulation
    accumulate_delta(i0, w0 * dlambda * grad0);
    accumulate_delta(i1, w1 * dlambda * grad1);
    accumulate_delta(i2, w2 * dlambda * grad2);
}

// Helper
void accumulate_delta(uint i, vec3 corr) {
    atomicAdd(delta_x[i], corr.x);  // Requires GL_EXT_shader_atomic_float
    atomicAdd(delta_y[i], corr.y);
    atomicAdd(delta_z[i], corr.z);
    atomicAdd(delta_count[i], 1u);
}
```

**Apply Deltas** ([`apply_deltas.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\apply_deltas.comp:6)):
```glsl
void main() {
    if (w[i] == 0.0) {
        delta_x[i] = 0.0; delta_y[i] = 0.0; delta_z[i] = 0.0;
        delta_count[i] = 0u;
        return;
    }
    
    uint c = max(delta_count[i], 1u);
    vec3 corr = vec3(delta_x[i], delta_y[i], delta_z[i]) / c;  // Average
    xp[i].xyz += corr * relaxation_factor;  // Apply with relaxation
    
    // Clear for next iteration
    delta_x[i]=0.0; delta_y[i]=0.0; delta_z[i]=0.0; delta_count[i]=0u;
}
```

**Bend Solver** ([`solve_bend.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\solve_bend.comp:19)):
```glsl
// Dihedral angle bending constraint (isometric bending model)
void main() {
    // Compute current dihedral angle via atan(s, c)
    vec3 e = x1 - x0;  // Shared edge
    vec3 n1 = normalize(cross(x1-x0, x2-x0));
    vec3 n2 = normalize(cross(x1-x0, x3-x0));
    float c = dot(n1, n2);
    float s = dot(normalize(e), cross(n1, n2));
    float phi = atan(s, c);
    
    float C = phi - rest_angle;
    
    // Gradients (complex, from isometric bending paper)
    float A1 = length(cross(x1-x0, x2-x0));
    float A2 = length(cross(x1-x0, x3-x0));
    vec3 q2 = (cross(x1-x0, n2) + cross(n1, x1-x0)*c) / A1;
    vec3 q3 = (cross(x1-x0, n1) + cross(n2, x1-x0)*c) / A2;
    vec3 q1 = -(cross(x2-x0, n2) + cross(n1, x2-x0)*c) / A1
              -(cross(x3-x0, n1) + cross(n2, x3-x0)*c) / A2;
    vec3 q0 = -q1 - q2 - q3;
    
    // XPBD solve
    float wsum_grad = w0*dot(q0,q0) + w1*dot(q1,q1) + 
                      w2*dot(q2,q2) + w3*dot(q3,q3);
    float dlambda = -(C + alpha_tilde * lambda_old) / (wsum_grad + alpha_tilde);
    
    // ATOMIC accumulation
    accumulate_delta(i0, w0 * dlambda * q0);
    accumulate_delta(i1, w1 * dlambda * q1);
    accumulate_delta(i2, w2 * dlambda * q2);
    accumulate_delta(i3, w3 * dlambda * q3);
}
```

**Area Solver** ([`solve_area.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\solve_area.comp:16)):
```glsl
void main() {
    vec3 e1 = x1 - x0;
    vec3 e2 = x2 - x0;
    
    // Current area (projected onto rest normal)
    float a = 0.5 * dot(cross(e1, e2), area.rest_normal);
    float C = a - area.rest_area;
    
    // Gradients
    vec3 g1 = 0.5 * cross(area.rest_normal, e2);
    vec3 g2 = 0.5 * cross(e1, area.rest_normal);
    vec3 g0 = -g1 - g2;
    
    // XPBD solve
    float wsum_grad = w0*dot(g0,g0) + w1*dot(g1,g1) + w2*dot(g2,g2);
    float dlambda = -(C + alpha_tilde * lambda_old) / (wsum_grad + alpha_tilde);
    
    // ATOMIC accumulation
    accumulate_delta(i0, w0 * dlambda * g0);
    accumulate_delta(i1, w1 * dlambda * g1);
    accumulate_delta(i2, w2 * dlambda * g2);
}
```

**LRA Solver** ([`solve_lra.comp`](C:\Users\choif\source\repos\PhysixStudio\shaders\simulation\solve_lra.comp:8)):
```glsl
const uint K = 2u;  // Number of anchors per particle

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (w[i] == 0.0) return;
    
    vec3 xi = xp[i].xyz;
    
    for (uint k = 0; k < K; ++k) {
        uint idx = i * K + k;
        uint a = lra_ids[idx];
        if (a == 0xFFFFFFFFu) continue;  // Invalid anchor
        
        vec3 xa = xp[a].xyz;  // Anchor position
        float r = lra_rests[idx];  // Max distance
        
        vec3 dvec = xi - xa;
        float d = length(dvec);
        
        // If beyond max distance, project toward anchor
        if (d > r && d > 1e-8) {
            vec3 x_proj = xa + (r / d) * dvec;
            xi = mix(xi, x_proj, stiffness);  // Blend with stiffness
        }
    }
    
    xp[i].xyz = xi;  // DIRECT write (after all anchors processed)
}
```

### 0.2 EngineSIU Current State

**Batch Manager Architecture** ([`ClothBatchManager.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchManager.h:28)):
```
ClothWorld
  ├─ ClothBatchManager[LOD_0]
  ├─ ClothBatchManager[LOD_1]  
  └─ ClothBatchManager[LOD_2]
      ├─ ClothBatchedSolver (GPU simulation)
      ├─ TArray<ClothInstanceHandle*>
      ├─ TArray<FClothInstanceMetadata>
      └─ Unified GPU buffers
```

**Current Simulation Loop** ([`ClothBatchedSolver.cpp`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchedSolver.cpp)):
```cpp
void Simulate(float DeltaTime) {
    // Current structure (to be replaced with PhysixStudio pattern)
    for (int32 iter = 0; iter < Config.NumIterations; ++iter) {
        DispatchIntegration();
        ClearAccumulationBuffers();
        DispatchConstraintSolver();       // Distance (delta accumulation)
        DispatchBendConstraintSolver();   // Bend (delta accumulation)
        DispatchApplyDeltas();
        DispatchApplyKinematicTargets();
    }
    DispatchUpdateNormals();
}
```

**Current GPU Buffers**:
- Position ping-pong (already matches PhysixStudio)
- Velocity buffer (separate, matches PhysixStudio)
- Delta accumulation buffers (int3, matches pattern)
- **Missing**: Separate buffers for shear, area, LRA

**Current Constraints** ([`ClothGPUStructs.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothGPUStructs.h:46)):
```cpp
// Has: Distance, Bend, Kinematic
// Missing: Shear, Area, LRA
```

### 0.3 Architecture Comparison Table

| Aspect | PhysixStudio (Vulkan) | EngineSIU (DX11) | Migration Action |
|--------|----------------------|------------------|-----------------|
| **API** | Vulkan 1.4 Compute | DirectX 11 Compute | Map GLSL → HLSL |
| **Simulation Loop** | Substep (10) → Iter (4) → Constraints | Iter (5) → Constraints | Adopt PhysixStudio loop |
| **Thread Group Size** | 256 | 64 | Change to 256 |
| **Distance Constraints** | Graph-colored, direct write | Delta accumulation | Implement graph coloring |
| **Shear Constraints** | ✓ Delta accumulation | ✗ Missing | Add new |
| **Bend Constraints** | ✓ Dihedral angle XPBD | ✓ Basic | Upgrade formulation |
| **Area Constraints** | ✓ Delta accumulation | ✗ Missing | Add new |
| **LRA** | ✓ K=2 Dijkstra-based | Partial (basic kinematic) | Implement Dijkstra LRA |
| **Self-Collision** | ✓ Spatial hashing | ✗ Missing | Add spatial hash |
| **XPBD** | Full (alpha, beta, lambda) | Partial (compliance field exists) | Full XPBD parameters |
| **Atomics** | Float atomics (ext) | Int-scaled atomics | Keep EngineSIU pattern |
| **Batch Management** | Single instance (simple) | Multi-instance unified buffers | Keep EngineSIU batching |
| **Rendering** | Vulkan graphics pass | ClothRenderPass (DX11) | Keep EngineSIU rendering |

### 0.4 Vulkan → DirectX 11 API Translation Guide

#### Compute Shader Basics

| Vulkan GLSL | DirectX 11 HLSL | Notes |
|-------------|-----------------|-------|
| `#version 460` | `// HLSL Shader Model 5.0` | Version declaration |
| `layout(local_size_x=256) in;` | `[numthreads(256,1,1)]` | Thread group size |
| `void main()` | `void CSMain(uint3 DTid : SV_DispatchThreadID)` | Entry point |
| `gl_GlobalInvocationID.x` | `DTid.x` | Global thread ID |
| `gl_LocalInvocationID.x` | `GTid.x` (SV_GroupThreadID) | Local thread ID |
| `gl_WorkGroupID.x` | `Gid.x` (SV_GroupID) | Work group ID |

#### Buffer Bindings

| Vulkan GLSL | DirectX 11 HLSL | Notes |
|-------------|-----------------|-------|
| `layout(std430, set=1, binding=0) buffer X { vec4 x[]; };` | `RWStructuredBuffer<float4> X : register(u0);` | Read-write buffer |
| `layout(std430, set=1, binding=0) readonly buffer X` | `StructuredBuffer<float4> X : register(t0);` | Read-only buffer |
| `layout(std140, set=0, binding=0) uniform U` | `cbuffer Constants : register(b0)` | Uniform/constant buffer |
| `layout(push_constant) uniform PC` | `cbuffer PushConstants : register(b1)` | Push constants → cbuffer |

#### Atomic Operations

```glsl
// Vulkan GLSL (requires GL_EXT_shader_atomic_float)
atomicAdd(delta_x[i], correction.x);  // Float atomic
atomicAdd(delta_count[i], 1u);        // Uint atomic

// DirectX 11 HLSL (int-scaled pattern, as used in EngineSIU)
static const float kScale = 10000.0f;
int corrInt = int(correction.x * kScale);
InterlockedAdd(PositionDelta[i].x, corrInt);  // Int atomic
InterlockedAdd(PositionWeight[i], 1);

// Later (in apply shader):
float avgCorrection = float(PositionDelta[i].x) / kScale / max(PositionWeight[i], 1);
```

**CRITICAL NOTE**: DX11 doesn't support float atomics directly. EngineSIU's int-scaled pattern (`kScale=10000.0f`) works well and is already proven.

#### Memory Barriers

```glsl
// Vulkan GLSL
barrier();  // Memory fence + execution barrier

// DirectX 11 HLSL
GroupMemoryBarrierWithGroupSync();  // For shared memory
AllMemoryBarrierWithGroupSync();    // For UAVs

// DirectX 11 C++ (buffer barriers between dispatches)
ID3D11UnorderedAccessView* nullUAV = nullptr;
DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
// Unbind and rebind ensures visibility
```

### 0.5 Preparation Checklist

Before starting Phase 1:

#### Required C++ Structure Additions

**1. Add to [`ClothSimulationData.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothSimulationData.h)**:
```cpp
// After FClothBendConstraint definition (~line 111)

struct FClothShearConstraint
{
    uint32 ParticleA, ParticleB, ParticleC;
    float RestDot;
    float Compliance;
    float Lambda;
    
    FClothShearConstraint() 
        : ParticleA(0), ParticleB(0), ParticleC(0)
        , RestDot(0.0f), Compliance(1e-6f), Lambda(0.0f) {}
};

struct FClothAreaConstraint
{
    uint32 ParticleA, ParticleB, ParticleC;
    float RestArea;
    FVector RestNormal;  // Normalized
    float Compliance;
    float Lambda;
    
    FClothAreaConstraint()
        : ParticleA(0), ParticleB(0), ParticleC(0)
        , RestArea(0.0f), RestNormal(FVector::ZeroVector)
        , Compliance(1e-2f), Lambda(0.0f) {}
};

struct FClothLRAEntry
{
    uint32 AnchorIndex;   // 0xFFFFFFFF = invalid
    float RestDistance;    // Graph distance * slack (1.1)
    
    FClothLRAEntry() : AnchorIndex(0xFFFFFFFF), RestDistance(0.0f) {}
};
```

**2. Add to [`ClothGPUStructs.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothGPUStructs.h)**:
```cpp
// After FClothBendConstraintGPU (~line 72)

struct FClothShearConstraintGPU
{
    uint32 ParticleA, ParticleB, ParticleC;  // 12 bytes
    float RestDot;                            // 4 bytes
    float Compliance;                         // 4 bytes
    float Lambda;                             // 4 bytes
    float Padding0, Padding1;                 // 8 bytes
    // Total: 32 bytes
};

struct FClothAreaConstraintGPU
{
    uint32 ParticleA, ParticleB, ParticleC;  // 12 bytes
    float RestArea;                           // 4 bytes
    FVector RestNormal;                       // 12 bytes
    float Lambda;                             // 4 bytes
    // Total: 32 bytes
};

static_assert(sizeof(FClothShearConstraintGPU) == 32, "Size must be 32 bytes");
static_assert(sizeof(FClothAreaConstraintGPU) == 32, "Size must be 32 bytes");
```

**3. Extend [`ClothBatchTypes.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchTypes.h)**:
```cpp
// In FClothInstanceMetadata (~line 90), add:
uint32 ShearConstraintOffset;
uint32 ShearConstraintCount;
uint32 AreaConstraintOffset;
uint32 AreaConstraintCount;
uint32 LRAOffset;     // Offset into LRA id/distance buffers
uint32 LRACount;      // K * ParticleCount (K=2 typically)
uint32 NumColors;     // For graph-colored distance constraints

// In FClothInstanceParameters (~line 70), add:
uint32 ShearConstraintOffset;
uint32 ShearConstraintCount;
uint32 AreaConstraintOffset;
uint32 AreaConstraintCount;
uint32 LRAOffset;
uint32 LRACount;
uint32 NumColors;
uint32 Padding3;

// In FClothInstanceCreationParams (~line 120), add:
TArray<FClothShearConstraint> ShearConstraints;
TArray<FClothAreaConstraint> AreaConstraints;
TArray<FClothLRAEntry> LRAEntries;  // K per particle
```

**4. Add to [`ClothCommon.hlsli`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Shaders\Cloth\ClothCommon.hlsli)**:
```hlsl
// After FBendConstraint (~line 92)

struct FShearConstraint
{
    uint ParticleA, ParticleB, ParticleC;
    float RestDot;
    float Compliance;
    float Lambda;
    float Padding0, Padding1;
};

struct FAreaConstraint
{
    uint ParticleA, ParticleB, ParticleC;
    float RestArea;
    float3 RestNormal;
    float Lambda;
};
```

#### New Shader Files to Create

1. `EngineSIU\Shaders\Cloth\ClothSolveShear.hlsl`
2. `EngineSIU\Shaders\Cloth\ClothSolveArea.hlsl`
3. `EngineSIU\Shaders\Cloth\ClothSolveLRA.hlsl`
4. `EngineSIU\Shaders\Cloth\ClothClearLambdas.hlsl`
5. `EngineSIU\Shaders\Cloth\ClothBuildHash.hlsl` (Phase 7)
6. `EngineSIU\Shaders\Cloth\ClothBuildCell.hlsl` (Phase 7)
7. `EngineSIU\Shaders\Cloth\ClothBuildNeighbor.hlsl` (Phase 7)
8. `EngineSIU\Shaders\Cloth\ClothSolveSelfCollision.hlsl` (Phase 7)

#### Build System

- Ensure all new `.hlsl` files compile to `.cso`
- Register new compute shader entry points in `FDXDShaderManager`
- Verify compute shader model 5.0 support

### 0.6 Risk Assessment & Mitigation

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Buffer alignment mismatch | Crashes | Medium | Static asserts, careful testing |
| Graph coloring breaks batching | Correctness | Low | Per-instance coloring, careful offset handling |
| Integer atomic precision loss | Artifacts | Low | Use kScale=10000.0f (proven) |
| XPBD parameters differ | Behavior mismatch | Medium | Copy exact compliance/beta values from PhysixStudio |
| Substep timing issues | Frame drops | Medium | Test variable delta time, clamp substeps |
| Self-collision performance | Slowdown | High | Make optional, profile first |
| DX11 lacks float atomics | API limitation | None | Use proven int-scaled pattern |

---

## Phase 1: Core Simulation Data Structures & XPBD Foundation

### 1.1 Context Recap

**What's Done**: Analysis complete (Phase 0)  
**Current Behavior**: EngineSIU has basic batched cloth with distance and bend constraints using delta accumulation  
**Target**: Add all PhysixStudio constraint structures (shear, area, LRA) with proper XPBD lambda storage

**PhysixStudio Reference Files**:
- [`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:79) - Constraint struct definitions
- [`cloth_ubo.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_ubo.h:4) - Simulation parameters

**EngineSIU Target Files**:
- [`ClothSimulationData.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothSimulationData.h)
- [`ClothGPUStructs.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothGPUStructs.h)
- [`ClothCommon.hlsli`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Shaders\Cloth\ClothCommon.hlsli)
- [`ClothBatchTypes.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchTypes.h)

### 1.2 Goal Statement

Extend EngineSIU's data structures to support all PhysixStudio constraint types (shear, area, LRA) with proper XPBD lambda storage and graph coloring metadata for distance constraints.

### 1.3 Sub-Tasks

#### Sub-Task 1.1: Add Shear and Area Constraint Structures

**Files to Modify**:
- C++: [`ClothSimulationData.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothSimulationData.h)
- C++: [`ClothGPUStructs.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothGPUStructs.h)
- HLSL: [`ClothCommon.hlsli`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Shaders\Cloth\ClothCommon.hlsli)

**Implementation**:

Follow the structure definitions from Phase 0.5 preparation checklist. Add all three structures (shear, area, LRA) to each file.

**PhysixStudio Pattern**: Match exact byte layouts from [`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h):
- Shear: 32 bytes (line 91)
- Area: 32 bytes (line 113)
- LRA: Separate id and distance arrays (lines 146-151)

**Vulkan → HLSL Adaptation**:
- `glm::vec3` → `float3` in HLSL
- `uint32_t` → `uint` in HLSL
- Maintain padding for 16-byte alignment

**Checkpoints**:
- [ ] Project compiles after adding structures
- [ ] Static asserts pass for all GPU structures
- [ ] `sizeof(FClothShearConstraintGPU) == 32`
- [ ] `sizeof(FClothAreaConstraintGPU) == 32`
- [ ] HLSL structures match C++ byte layout exactly
- [ ] Existing cloth simulation still runs (no regression)

#### Sub-Task 1.2: Extend Instance Metadata for New Constraints

**Files to Modify**:
- [`ClothBatchTypes.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothBatchTypes.h)

**Implementation**:

Add fields to track shear, area, and LRA constraint ranges per instance:

```cpp
// In FClothInstanceMetadata (line ~90)
uint32 ShearConstraintOffset;
uint32 ShearConstraintCount;
uint32 AreaConstraintOffset;
uint32 AreaConstraintCount;
uint32 LRAOffset;
uint32 LRACount;
uint32 NumColors;  // For graph-colored distance constraints

// Update constructor
FClothInstanceMetadata()
    : /* existing... */
    , ShearConstraintOffset(0), ShearConstraintCount(0)
    , AreaConstraintOffset(0), AreaConstraintCount(0)
    , LRAOffset(0), LRACount(0), NumColors(0)
{}
```

```cpp
// In FClothInstanceParameters (line ~47) - GPU constant buffer
// Add after BendConstraintCount (line ~71):
uint32 ShearConstraintOffset;
uint32 ShearConstraintCount;
uint32 AreaConstraintOffset;
uint32 AreaConstraintCount;
uint32 LRAOffset;
uint32 LRACount;
uint32 NumColors;
uint32 Padding3;

// Verify size is multiple of 16 bytes
// Might need to adjust to 128 bytes total
```

```cpp
// In FClothInstanceCreationParams (line ~120)
TArray<FClothShearConstraint> ShearConstraints;
TArray<FClothAreaConstraint> AreaConstraints;
TArray<FClothLRAEntry> LRAEntries;  // K * NumParticles entries (K=2)
```

**Checkpoints**:
- [ ] `ClothBatchTypes.h` compiles
- [ ] `sizeof(FClothInstanceParameters)` is multiple of 16
- [ ] Instance creation API extended
- [ ] No build errors in dependent files

#### Sub-Task 1.3: Add Graph Coloring Metadata to Distance Constraints

**Files to Modify**:
- [`ClothSimulationData.h`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Engine\Source\Runtime\Engine\Cloth\ClothSimulationData.h)

**Implementation**:

PhysixStudio uses graph coloring for distance constraints ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:252)) to enable parallel solving without race conditions.

```cpp
// In FClothDistanceConstraint (line ~57), add field:
uint32 ColorGroup;  // Which color group (0 to NumColors-1)

// Update constructors
FClothDistanceConstraint()
    : /* existing... */
    , ColorGroup(0)  // Will be assigned during coloring
{}
```

**PhysixStudio Reference**: Graph coloring ensures no two constraints in the same color group share particles, allowing direct position writes without atomics.

**Checkpoints**:
- [ ] Distance constraint structure extended
- [ ] Project builds
- [ ] ColorGroup field not used yet (will be populated in Phase 2)

### 1.4 Sub-Task 1.4: Update HLSL Constant Buffer

**Files to Modify**:
- [`ClothCommon.hlsli`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Shaders\Cloth\ClothCommon.hlsli)

**Implementation**:

Add PhysixStudio-style parameters to constant buffer:

```hlsl
// In cbuffer ClothSimConstants (line ~10), add:
uint NumShearConstraints;
uint NumAreaConstraints;
uint NumLRAEntries;
float ComplianceStretch;   // XPBD compliance for each type
float ComplianceShear;
float ComplianceBend;
float ComplianceArea;
float BetaStretch;         // Velocity-level damping parameter
```

**PhysixStudio Pattern**: Per-constraint-type compliance and beta values ([`cloth_sim_data.h`](C:\Users\choif\source\repos\PhysixStudio\src\simulation\cloth_sim_data.h:56)).

**Checkpoints**:
- [ ] Constant buffer updated
- [ ] Total size is multiple of 16 bytes
- [ ] Shader compiles
- [ ] C++ struct `FClothSimConstants` updated to match

### 1.5 Phase Completion Criteria

- [x] All new constraint structures defined in C++, GPU structs, and HLSL
- [x] Byte alignment verified with static_assert
- [x] Instance metadata extended to track shear, area, LRA constraints
- [x] Graph coloring field added to distance constraints
- [x] HLSL constant buffer updated with new parameters
- [x] Project builds successfully
- [x] Existing cloth simulation runs without regression
- [x] All structure sizes verified: Shear=32B, Area=32B

---

## Phase 2: Distance Constraint Solver with Graph Coloring

### 2.1 Context Recap

**What's Done**: Constraint structures added (Phase 1), ColorGroup field in distance constraints  
**Current Behavior**: Distance constraints use delta accumulation (Jacobi-style) in [`ClothConstraintSolver.hlsl`](C:\Users\choif\source\repos\GTL_1st_cohort\EngineSIU\EngineSIU\Shaders\Cloth\ClothConstraintSolver.hlsl)