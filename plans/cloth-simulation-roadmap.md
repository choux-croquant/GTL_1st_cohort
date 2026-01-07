# Cloth Simulation Implementation Roadmap

## Overview

This roadmap provides a step-by-step implementation plan for the cloth simulation system, organized into 7 phases. Each phase is designed to compile and can be tested independently before moving to the next.

---

## Phase 1: GPU Solver Foundation (Core Compute Infrastructure)

**Goal**: Establish GPU compute shader infrastructure with basic Position-Based Dynamics solver

**Estimated Complexity**: Medium  
**Dependencies**: None  
**Deliverable**: Working GPU particle simulation with distance constraints

### 1.1 Create Data Structures

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h)

**Tasks**:
- [ ] Define `FClothSimulationData` struct
- [ ] Define `FClothConfig` struct with simulation parameters
- [ ] Define `FClothConstraint` struct for distance/bend constraints
- [ ] Define `FClothVertexPaintData` for per-vertex parameters

### 1.2 Create Shader Common Header

**Files to Create**:
- [`Shaders/Cloth/ClothCommon.hlsli`](EngineSIU/Shaders/Cloth/ClothCommon.hlsli)

**Tasks**:
- [ ] Define `ClothSimConstants` constant buffer structure
- [ ] Define `FClothParticle` struct matching C++ layout
- [ ] Define `FClothVelocity` struct
- [ ] Define `FDistanceConstraint` struct
- [ ] Add helper functions for constraint solving

### 1.3 Implement Integration Shader

**Files to Create**:
- [`Shaders/Cloth/ClothIntegrate.hlsl`](EngineSIU/Shaders/Cloth/ClothIntegrate.hlsl)

**Tasks**:
- [ ] Implement semi-implicit Euler integration
- [ ] Apply gravity and external forces
- [ ] Apply damping
- [ ] Handle fixed particles (invMass = 0)
- [ ] Use `[numthreads(64, 1, 1)]` thread group size

### 1.4 Implement Constraint Solver Shader

**Files to Create**:
- [`Shaders/Cloth/ClothConstraintSolver.hlsl`](EngineSIU/Shaders/Cloth/ClothConstraintSolver.hlsl)

**Tasks**:
- [ ] Implement distance constraint projection (PBD method)
- [ ] Add XPBD compliance calculation (optional path)
- [ ] Handle constraint stiffness per-constraint
- [ ] Add atomic operation considerations (document race conditions)
- [ ] Implement parallel Jacobi iteration approach

### 1.5 Implement Normal Update Shader

**Files to Create**:
- [`Shaders/Cloth/ClothUpdateNormals.hlsl`](EngineSIU/Shaders/Cloth/ClothUpdateNormals.hlsl)

**Tasks**:
- [ ] Calculate face normals from triangles
- [ ] Accumulate normals to vertices
- [ ] Normalize vertex normals
- [ ] Handle atomic operations for normal accumulation

### 1.6 Create Cloth Solver Class

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp)

**Tasks**:
- [ ] Implement constructor/destructor
- [ ] Implement `Initialize()` - create GPU resources
- [ ] Implement `Release()` - cleanup GPU resources
- [ ] Implement `CreateGPUResources()` - allocate buffers
- [ ] Implement `Simulate(float DeltaTime)` - main simulation loop
- [ ] Implement `DispatchIntegration()`
- [ ] Implement `DispatchConstraintSolver(int32 Iteration)`
- [ ] Implement `DispatchNormalUpdate()`
- [ ] Implement `UpdateConstantBuffers()`
- [ ] Add ping-pong buffer management
- [ ] Load and compile compute shaders
- [ ] Create UAVs and SRVs for all buffers

### 1.7 Add Constant Buffer Support

**Files to Modify**:
- [`Engine/Source/Runtime/Renderer/ShaderConstants.h`](EngineSIU/Engine/Source/Runtime/Renderer/ShaderConstants.h)

**Tasks**:
- [ ] Add `FClothSimConstants` struct (aligned to 16 bytes)
- [ ] Match HLSL constant buffer layout exactly
- [ ] Add padding where necessary

### 1.8 Testing Phase 1

**Test Cases**:
- [ ] Create simple cloth plane (10x10 grid)
- [ ] Initialize with rest positions
- [ ] Apply gravity only
- [ ] Verify particles fall correctly
- [ ] Pin top row (invMass = 0)
- [ ] Verify cloth hangs like curtain
- [ ] Add distance constraints
- [ ] Verify cloth maintains shape

---

## Phase 2: Cloth Component & Asset System

**Goal**: Create component system and asset data structures

**Estimated Complexity**: Medium  
**Dependencies**: Phase 1  
**Deliverable**: Cloth component that can be added to actors

### 2.1 Create Cloth Asset Class

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.h)
- [`Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Engine/ClothAsset.cpp)

**Tasks**:
- [ ] Implement `UClothAsset` class inheriting from `UObject`
- [ ] Add `DECLARE_CLASS` macro
- [ ] Add `FClothLODData` array for LOD support
- [ ] Add `FClothConfig` member
- [ ] Store rest positions, indices, inv masses
- [ ] Store distance and bend constraints
- [ ] Add serialization support in `SerializeAsset()`
- [ ] Add getter/setter methods

### 2.2 Create Base Cloth Component

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h)
- [`Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp)

**Tasks**:
- [ ] Inherit from [`UActorComponent`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ActorComponent.h)
- [ ] Add `DECLARE_CLASS` macro
- [ ] Override `InitializeComponent()`
- [ ] Override `TickComponent(float DeltaTime)`
- [ ] Override `BeginPlay()`
- [ ] Implement `SetClothAsset(UClothAsset*)`
- [ ] Create `FClothSolver` instance
- [ ] Implement `StartSimulation()` / `StopSimulation()`
- [ ] Implement `ResetSimulation()`
- [ ] Add `AddForce()`, `AddImpulse()`, `SetWind()` methods
- [ ] Add serialization support (`GetProperties`, `SetProperties`)

### 2.3 Create Renderable Cloth Component

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.h)
- [`Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothMeshComponent.cpp)

**Tasks**:
- [ ] Inherit from `UClothComponent`
- [ ] Add rendering data structures
- [ ] Implement `GetRenderData()` method
- [ ] Override `TickComponent()` to update render data
- [ ] Add material support (array of `UMaterial*`)
- [ ] Implement `GetNumMaterials()`, `GetMaterial()`
- [ ] Add debug draw mode enum and setter

### 2.4 Testing Phase 2

**Test Cases**:
- [ ] Create actor with `UClothMeshComponent`
- [ ] Assign cloth asset programmatically
- [ ] Verify component initializes without crash
- [ ] Verify simulation updates each frame
- [ ] Verify component serialization/deserialization
- [ ] Test start/stop simulation methods

---

## Phase 3: Rendering Integration

**Goal**: Render simulated cloth in the engine viewport

**Estimated Complexity**: Medium  
**Dependencies**: Phase 1, Phase 2  
**Deliverable**: Visible animated cloth in viewport

### 3.1 Create Cloth Vertex/Pixel Shaders

**Files to Create**:
- [`Shaders/ClothVertexShader.hlsl`](EngineSIU/Shaders/ClothVertexShader.hlsl)
- [`Shaders/ClothPixelShader.hlsl`](EngineSIU/Shaders/ClothPixelShader.hlsl)

**Tasks**:
- [ ] Create vertex shader reading from dynamic position buffer
- [ ] Read from GPU simulation position buffer (SRV)
- [ ] Transform vertices to clip space
- [ ] Output normals, UVs, tangents
- [ ] Create pixel shader with standard lighting
- [ ] Support PBR material parameters
- [ ] Handle texture sampling

### 3.2 Create Cloth Render Pass

**Files to Create**:
- [`Engine/Source/Runtime/Renderer/ClothRenderPass.h`](EngineSIU/Engine/Source/Runtime/Renderer/ClothRenderPass.h)
- [`Engine/Source/Runtime/Renderer/ClothRenderPass.cpp`](EngineSIU/Engine/Source/Runtime/Renderer/ClothRenderPass.cpp)

**Tasks**:
- [ ] Inherit from [`FRenderPassBase`](EngineSIU/Engine/Source/Runtime/Renderer/RenderPassBase.h)
- [ ] Follow pattern from [`FTileLightCullingPass`](EngineSIU/Engine/Source/Runtime/Renderer/TileLightCullingPass.h)
- [ ] Implement `Initialize()` - load shaders, create resources
- [ ] Implement `PrepareRenderArr()` - collect cloth components
- [ ] Implement `ClearRenderArr()` - clear component list
- [ ] Implement `Render()` - draw all cloth meshes
- [ ] Implement `PrepareRender()` - set render states
- [ ] Implement `CleanUpRender()` - restore render states
- [ ] Implement `CreateResource()` - create vertex/index buffers
- [ ] Bind simulation buffer SRVs as vertex shader inputs

### 3.3 Integrate Render Pass into Renderer

**Files to Modify**:
- [`Engine/Source/Runtime/Renderer/Renderer.h`](EngineSIU/Engine/Source/Runtime/Renderer/Renderer.h)
- [`Engine/Source/Runtime/Renderer/Renderer.cpp`](EngineSIU/Engine/Source/Runtime/Renderer/Renderer.cpp)

**Tasks**:
- [ ] Add `FClothRenderPass* ClothRenderPass` member
- [ ] Initialize in `FRenderer::Initialize()`
- [ ] Add to render pipeline in `Render()` method
- [ ] Place after opaque pass, before translucent
- [ ] Add cleanup in `Release()`

### 3.4 Create Debug Visualization

**Files to Create**:
- [`Engine/Source/Runtime/Renderer/ClothDebugRenderPass.h`](EngineSIU/Engine/Source/Runtime/Renderer/ClothDebugRenderPass.h)
- [`Engine/Source/Runtime/Renderer/ClothDebugRenderPass.cpp`](EngineSIU/Engine/Source/Runtime/Renderer/ClothDebugRenderPass.cpp)

**Tasks**:
- [ ] Create debug render pass for particles
- [ ] Render particles as points or small spheres
- [ ] Render constraints as lines
- [ ] Render normals as colored lines
- [ ] Add velocity visualization (color gradient)
- [ ] Use existing line rendering infrastructure
- [ ] Add debug mode toggle

### 3.5 Testing Phase 3

**Test Cases**:
- [ ] Create cloth actor in scene
- [ ] Verify cloth renders correctly
- [ ] Verify simulation updates visible geometry
- [ ] Test debug visualization modes
- [ ] Verify materials apply correctly
- [ ] Test with different lighting conditions

---

## Phase 4: External Forces & Wind System

**Goal**: Add wind and external force application

**Estimated Complexity**: Low-Medium  
**Dependencies**: Phase 1, Phase 2, Phase 3  
**Deliverable**: Cloth reacting to wind and external forces

### 4.1 Create Force Manager

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothForceManager.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothForceManager.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothForceManager.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothForceManager.cpp)

**Tasks**:
- [ ] Implement `FClothForceManager` singleton or per-world manager
- [ ] Add `RegisterCloth()` / `UnregisterCloth()` methods
- [ ] Add `RegisterForceField()` / `UnregisterForceField()` methods
- [ ] Implement `TickForces(float DeltaTime)`
- [ ] Implement `CalculateForcesAtPosition(FVector)` for sampling
- [ ] Manage active cloth and force field lists

### 4.2 Create Wind Component

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Classes/Components/WindComponent.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/WindComponent.h)
- [`Engine/Source/Runtime/Engine/Classes/Components/WindComponent.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/WindComponent.cpp)

**Tasks**:
- [ ] Inherit from [`USceneComponent`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/SceneComponent.h)
- [ ] Add wind parameters (strength, direction, turbulence, speed)
- [ ] Implement `GetWindVelocityAtPosition(FVector)` with falloff
- [ ] Add perlin noise for turbulence (optional)
- [ ] Register with force manager on begin play
- [ ] Add editor visualization (gizmo showing wind direction)

### 4.3 Update Cloth Solver for Forces

**Files to Modify**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp)

**Tasks**:
- [ ] Add `SetGravity(FVector)` method
- [ ] Add `SetWind(FVector)` method
- [ ] Add `AddExternalForce(FVector)` method
- [ ] Update constant buffer with force data
- [ ] Modify integration shader to apply forces

### 4.4 Update Integration Shader

**Files to Modify**:
- [`Shaders/Cloth/ClothIntegrate.hlsl`](EngineSIU/Shaders/Cloth/ClothIntegrate.hlsl)

**Tasks**:
- [ ] Add wind force application
- [ ] Add air drag calculation
- [ ] Add external force accumulation
- [ ] Consider per-particle wind variation

### 4.5 Create Force Field Component (Optional)

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Classes/Components/ForceFieldComponent.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ForceFieldComponent.h)
- [`Engine/Source/Runtime/Engine/Classes/Components/ForceFieldComponent.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ForceFieldComponent.cpp)

**Tasks**:
- [ ] Create base force field component
- [ ] Support radial, directional, vortex types
- [ ] Add radius and falloff parameters
- [ ] Implement force calculation per type

### 4.6 Testing Phase 4

**Test Cases**:
- [ ] Add wind component to scene
- [ ] Verify cloth reacts to wind direction
- [ ] Test wind strength parameter
- [ ] Test turbulence (if implemented)
- [ ] Add multiple wind sources, verify combination
- [ ] Test external impulse forces
- [ ] Verify gravity override works

---

## Phase 5: Asset Creation Pipeline

**Goal**: Tools to create cloth assets from static meshes

**Estimated Complexity**: Medium-High  
**Dependencies**: Phase 2  
**Deliverable**: Ability to convert static mesh to cloth asset

### 5.1 Create Cloth Asset Factory

**Files to Create**:
- [`Engine/Source/Editor/ClothAssetEditor/ClothAssetFactory.h`](EngineSIU/Engine/Source/Editor/ClothAssetEditor/ClothAssetFactory.h)
- [`Engine/Source/Editor/ClothAssetEditor/ClothAssetFactory.cpp`](EngineSIU/Engine/Source/Editor/ClothAssetEditor/ClothAssetFactory.cpp)

**Tasks**:
- [ ] Implement `CreateFromStaticMesh()` static method
- [ ] Copy vertex data from source mesh
- [ ] Generate simulation mesh (simplified)
- [ ] Calculate rest lengths for constraints
- [ ] Generate distance constraints (edges)
- [ ] Generate bend constraints (diagonal edges)
- [ ] Calculate inverse masses
- [ ] Set default config parameters

### 5.2 Implement Constraint Generation

**Tasks**:
- [ ] Build edge list from triangle mesh
- [ ] Identify unique edges for distance constraints
- [ ] Find diagonal edges for bend constraints
- [ ] Calculate rest lengths from rest positions
- [ ] Remove duplicate constraints

### 5.3 Implement Mesh Simplification (Optional)

**Tasks**:
- [ ] Implement mesh decimation algorithm
- [ ] Target triangle count parameter
- [ ] Preserve important edges (silhouette, UVs)
- [ ] Build render-to-sim vertex mapping
- [ ] Store LOD hierarchy

### 5.4 Create Vertex Painting Tool (Optional)

**Files to Create**:
- [`Engine/Source/Editor/ClothAssetEditor/ClothPainter.h`](EngineSIU/Engine/Source/Editor/ClothAssetEditor/ClothPainter.h)
- [`Engine/Source/Editor/ClothAssetEditor/ClothPainter.cpp`](EngineSIU/Engine/Source/Editor/ClothAssetEditor/ClothPainter.cpp)

**Tasks**:
- [ ] Create editor tool for painting vertex parameters
- [ ] Paint max distance
- [ ] Paint stiffness
- [ ] Paint fixed vertices (attachments)
- [ ] Add brush size/strength controls
- [ ] Add visual feedback (color overlay)

### 5.5 Testing Phase 5

**Test Cases**:
- [ ] Convert simple plane mesh to cloth asset
- [ ] Verify constraint generation is correct
- [ ] Verify edge topology is valid
- [ ] Test with complex mesh (shirt, cape)
- [ ] Verify serialization works
- [ ] Load saved cloth asset and simulate

---

## Phase 6: Skeletal & Static Mesh Attachment

**Goal**: Attach cloth to animated skeletal meshes and static objects

**Estimated Complexity**: High  
**Dependencies**: Phase 1-5  
**Deliverable**: Character wearing simulated cloth

### 6.1 Create Attachment Data Structures

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothAttachment.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothAttachment.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothAttachment.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothAttachment.cpp)

**Tasks**:
- [ ] Define `FClothAttachmentData` struct
- [ ] Store cloth vertex index
- [ ] Store bone name and index (for skeletal)
- [ ] Store local offset transform
- [ ] Store world position (for static)
- [ ] Add stiffness parameter

### 6.2 Implement Skeletal Mesh Attachment

**Files to Modify**:
- [`Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.h)
- [`Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/ClothComponent.cpp)

**Tasks**:
- [ ] Add `AttachToSkeletalMesh()` method
- [ ] Find closest bone for each attachment vertex
- [ ] Calculate local offset from bone transform
- [ ] Store attachment data array
- [ ] Update attachment positions each frame from bone transforms

### 6.3 Update Cloth Solver for Attachments

**Files to Modify**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp)

**Tasks**:
- [ ] Add `UpdateAttachmentConstraints()` method
- [ ] Create GPU buffer for attachment positions
- [ ] Upload attachment positions each frame
- [ ] Modify constraint solver to handle attachments as kinematic constraints

### 6.4 Create Attachment Constraint Shader

**Files to Create**:
- [`Shaders/Cloth/ClothAttachmentConstraints.hlsl`](EngineSIU/Shaders/Cloth/ClothAttachmentConstraints.hlsl)

**Tasks**:
- [ ] Read attachment buffer
- [ ] Apply kinematic position constraints
- [ ] Support stiffness parameter (soft vs. hard attachment)
- [ ] Run before or after other constraints

### 6.5 Implement Character Integration

**Files to Modify**:
- [`Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h)
- [`Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.cpp)

**Tasks**:
- [ ] Add method to get bone world transforms efficiently
- [ ] Add cloth component attachment helper
- [ ] Ensure bone transforms update before cloth tick

### 6.6 Testing Phase 6

**Test Cases**:
- [ ] Create character with skeletal mesh
- [ ] Attach cloth to character's shoulder
- [ ] Verify cloth follows character movement
- [ ] Test with animation playback
- [ ] Test with different bone attachments
- [ ] Verify cloth maintains shape during motion

---

## Phase 7: Collision Detection

**Goal**: Prevent cloth from intersecting with character and world geometry

**Estimated Complexity**: High  
**Dependencies**: Phase 6  
**Deliverable**: Cloth colliding with character body and environment

### 7.1 Create Collision Data Structures

**Files to Create**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothCollision.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollision.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothCollision.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollision.cpp)

**Tasks**:
- [ ] Define `FClothCollisionSphere` struct
- [ ] Define `FClothCollisionCapsule` struct
- [ ] Define `FClothCollisionBox` struct
- [ ] Add GPU buffer structures

### 7.2 Implement Collision Shader

**Files to Create**:
- [`Shaders/Cloth/ClothCollision.hlsl`](EngineSIU/Shaders/Cloth/ClothCollision.hlsl)

**Tasks**:
- [ ] Implement sphere collision detection
- [ ] Implement capsule collision detection
- [ ] Implement box collision detection
- [ ] Apply collision response (position correction)
- [ ] Add friction parameter
- [ ] Support multiple collision primitives

### 7.3 Update Cloth Solver for Collision

**Files to Modify**:
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.h`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.h)
- [`Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp`](EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSolver.cpp)

**Tasks**:
- [ ] Add `SetCollisionBodies()` method
- [ ] Create collision primitive buffer
- [ ] Upload collision data to GPU
- [ ] Add `DispatchCollision()` to simulation loop
- [ ] Run collision after constraint solving

### 7.4 Integrate with PhysX (Optional)

**Files to Modify**:
- [`Engine/Source/Runtime/Physics/PhysicsManager.h`](EngineSIU/Engine/Source/Runtime/Physics/PhysicsManager.h)
- [`Engine/Source/Runtime/Physics/PhysicsManager.cpp`](EngineSIU/Engine/Source/Runtime/Physics/PhysicsManager.cpp)

**Tasks**:
- [ ] Query PhysX scene for nearby collision shapes
- [ ] Convert PhysX primitives to cloth collision primitives
- [ ] Update each frame with dynamic bodies
- [ ] Handle moving platforms

### 7.5 Implement Self-Collision (Optional)

**Tasks**:
- [ ] Implement spatial hashing on GPU
- [ ] Detect particle-particle collisions
- [ ] Apply separation constraints
- [ ] Optimize with grid-based culling

### 7.6 Testing Phase 7

**Test Cases**:
- [ ] Add collision spheres to character skeleton
- [ ] Verify cloth doesn't penetrate body
- [ ] Test with capsule colliders on limbs
- [ ] Test cloth falling on static mesh floor
- [ ] Test moving collision objects
- [ ] Verify self-collision (if implemented)

---

## Cross-Cutting Concerns

### Performance Optimization

**Throughout all phases**:
- [ ] Profile GPU compute shader performance
- [ ] Optimize thread group sizes
- [ ] Minimize CPU-GPU sync points
- [ ] Use async compute where possible
- [ ] Implement LOD system
- [ ] Add cloth sleeping/waking

### Memory Management

**Throughout all phases**:
- [ ] Implement proper buffer cleanup
- [ ] Avoid memory leaks in GPU resources
- [ ] Pool cloth simulation resources
- [ ] Implement streaming for large cloth assets

### Error Handling

**Throughout all phases**:
- [ ] Add GPU resource creation failure handling
- [ ] Validate cloth asset data on load
- [ ] Handle missing shader files gracefully
- [ ] Add bounds checking in shaders

---

## Integration Checklist

### Before Starting Implementation

- [ ] Review DirectX 11 compute shader documentation
- [ ] Review Position-Based Dynamics paper
- [ ] Set up GPU debugging tools (PIX, RenderDoc)
- [ ] Create test scenes for each phase

### Per-Phase Integration

For each phase:
- [ ] Create feature branch in version control
- [ ] Implement all tasks for the phase
- [ ] Ensure code compiles without errors
- [ ] Run phase-specific test cases
- [ ] Profile performance
- [ ] Document any issues or limitations
- [ ] Code review
- [ ] Merge to main branch

### Final Integration

- [ ] Full system integration test
- [ ] Performance benchmark suite
- [ ] Memory leak testing
- [ ] Stress testing (many cloth instances)
- [ ] Documentation update
- [ ] Example content creation

---

## Success Metrics

### Phase 1
- ✓ 1000 particles simulating at 60fps on mid-range GPU
- ✓ Constraint solver converges in 5-10 iterations
- ✓ No visual artifacts (jittering, explosions)

### Phase 3
- ✓ Cloth renders correctly in viewport
- ✓ No visible seams or gaps
- ✓ Normal shading looks smooth

### Phase 4
- ✓ Wind affects cloth realistically
- ✓ Cloth responds to impulse forces
- ✓ Multiple force sources combine correctly

### Phase 6
- ✓ Cloth stays attached during animation
- ✓ No stretching at attachment points
- ✓ Character can run/jump with cloth following

### Phase 7
- ✓ No visible penetration with collision objects
- ✓ Collision response looks natural
- ✓ Self-collision prevents clipping (if implemented)

---

## Risk Mitigation Strategies

| Risk | Phase | Mitigation |
|------|-------|------------|
| GPU memory overflow | 1 | Implement memory budgets, streaming |
| Constraint instability | 1 | Add max correction clamping, XPBD |
| Performance bottleneck | 1,3 | Profile early, optimize hot paths |
| Rendering artifacts | 3 | Test with simple geometry first |
| Animation sync issues | 6 | Ensure correct tick order |
| Collision tunneling | 7 | Use continuous collision detection |

---

## Useful Commands & Tools

### Shader Compilation
```bash
# Compile compute shader
fxc /T cs_5_0 /E IntegrateCS /Fo ClothIntegrate.cso ClothIntegrate.hlsl

# View shader assembly
fxc /T cs_5_0 /E IntegrateCS /Fc ClothIntegrate.asm ClothIntegrate.hlsl
```

### GPU Debugging
- Use PIX for Windows to capture GPU frames
- Use RenderDoc for graphics debugging
- Enable D3D11 debug layer in development builds

### Performance Profiling
- Use GPU timestamps (similar to existing `FGPUTimingManager`)
- Profile each compute shader dispatch separately
- Target: < 1ms total for 1000 particles

---

## Optional Extensions (Post Phase 7)

### Phase 8: Tearing System (Optional)

**Tasks**:
- [ ] Detect overstretched constraints
- [ ] Remove constraints dynamically
- [ ] Duplicate vertices at tear points
- [ ] Update topology on GPU
- [ ] Add visual effects (rip sounds, particles)

### Phase 9: Advanced Features (Optional)

**Tasks**:
- [ ] Two-way cloth-character interaction
- [ ] Cloth forces affecting physics bodies
- [ ] Advanced wind (turbulence noise, vortex confinement)
- [ ] Wrinkle map generation
- [ ] Cloth durability system
- [ ] Wetness simulation

---

## Next Steps

1. **Review this roadmap** with the team
2. **Set up development environment** (GPU debugging tools)
3. **Create test content** (simple cloth meshes)
4. **Begin Phase 1** implementation
5. **Iterate based on testing** results

---

**Document Version**: 1.0  
**Last Updated**: 2026-01-07  
**Status**: Implementation Roadmap - Ready for Review
