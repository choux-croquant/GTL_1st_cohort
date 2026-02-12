# Self-Collision Quality Improvement - Implementation Plan

## Overview

This document provides a step-by-step implementation plan to improve self-collision quality based on Velvet reference code analysis. The plan is divided into phases with clear, actionable tasks.

## Phase 1: Critical Fixes (Highest Priority)

### Task 1.1: Add Original Position Buffer

**Goal**: Store rest/original positions to prevent false collisions between naturally-close particles.

#### Step 1: Add Buffer Declarations

**File**: [`ClothBatchedSolver.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h)

Add to class members:
```cpp
// Original/rest positions (never modified during simulation)
ID3D11Buffer* UnifiedOriginalPositionBuffer;
ID3D11ShaderResourceView* UnifiedOriginalPositionSRV;
```

Initialize in constructor:
```cpp
UnifiedOriginalPositionBuffer = nullptr;
UnifiedOriginalPositionSRV = nullptr;
```

Release in destructor:
```cpp
SAFE_RELEASE(UnifiedOriginalPositionBuffer);
SAFE_RELEASE(UnifiedOriginalPositionSRV);
```

#### Step 2: Allocate Original Position Buffer

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

In `AllocateBuffers()` method (after creating position buffer):
```cpp
// Create original position buffer (read-only, stores rest positions)
bufferDesc = {};
bufferDesc.Usage = D3D11_USAGE_DEFAULT;
bufferDesc.ByteWidth = sizeof(FClothParticleGPU) * MaxParticles;
bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
bufferDesc.StructureByteStride = sizeof(FClothParticleGPU);
bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedOriginalPositionBuffer);
if (FAILED(hr))
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create original position buffer"));
    return false;
}

// Create SRV for original positions
srvDesc = {};
srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
srvDesc.Format = DXGI_FORMAT_UNKNOWN;
srvDesc.Buffer.NumElements = MaxParticles;

hr = Graphics->Device->CreateShaderResourceView(UnifiedOriginalPositionBuffer, &srvDesc, &UnifiedOriginalPositionSRV);
if (FAILED(hr))
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create original position SRV"));
    return false;
}

UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Created original position buffer (MaxParticles: %u)"), MaxParticles);
```

#### Step 3: Upload Original Positions

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

Modify `UploadParticleData()` to also upload to original position buffer:
```cpp
void FClothBatchedSolver::UploadParticleData(const TArray<FVector> &Positions,
                                             const TArray<float> &InvMasses,
                                             const TArray<uint32> &InstanceIDs,
                                             uint32 DestOffset)
{
    // ... existing code for position/predicted/velocity buffers ...
    
    // NEW: Upload to original position buffer (ONCE, never modified)
    if (UnifiedOriginalPositionBuffer)
    {
        D3D11_BOX destBox;
        destBox.left = DestOffset * sizeof(FClothParticleGPU);
        destBox.right = destBox.left + numParticles * sizeof(FClothParticleGPU);
        destBox.top = 0;
        destBox.bottom = 1;
        destBox.front = 0;
        destBox.back = 1;
        
        Graphics->DeviceContext->UpdateSubresource(UnifiedOriginalPositionBuffer, 0, &destBox,
                                                   particlesGPU.GetData(), 0, 0);
        
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Uploaded original positions - Count: %u (offset %u)"),
               numParticles, DestOffset);
    }
}
```

#### Step 4: Bind Original Position Buffer in Dispatch

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

In `DispatchSelfCollision()` method (around line 2398-2437):
```cpp
// PASS 2: Solve self-collisions
{
    // Bind shader
    Graphics->DeviceContext->CSSetShader(SelfCollisionSolverCS, nullptr, 0);
    
    // Bind constant buffers
    ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
    Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
    
    // Bind SRVs
    ID3D11ShaderResourceView* srvs[] = {
        UnifiedPredictedSRV,              // t0: Predicted positions
        UnifiedInvMassSRV,                // t1: Inverse masses
        SelfCollisionCellCountersSRV,    // t2: Cell counters
        SelfCollisionCellDataSRV,         // t3: Cell data
        UnifiedIndexSRV,                  // t4: Index buffer
        UnifiedOriginalPositionSRV        // t5: Original positions (NEW)
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 6, srvs);  // Changed from 5 to 6
    
    // ... rest of dispatch code ...
    
    // Unbind (update count)
    ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 6, nullSRVs);  // Changed from 5 to 6
}
```

#### Step 5: Update Shader to Use Original Positions

**File**: [`ClothSelfCollisionSolver.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionSolver.hlsl)

Add buffer binding (after line 16):
```hlsl
// Input buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Predicted positions
StructuredBuffer<float> InvMass : register(t1);                 // Inverse masses
StructuredBuffer<uint> CellCounters : register(t2);             // Per-cell particle counts
StructuredBuffer<uint> CellData : register(t3);                 // Particle indices per cell
Buffer<uint> Indices : register(t4);                            // Index buffer (for topology check)
StructuredBuffer<FClothParticle> OriginalPositions : register(t5);  // NEW: Original/rest positions
```

Modify collision detection loop (around line 72-113):
```hlsl
float3 posA = PredictedRead[particleIdx].Position;
float3 originalPosA = OriginalPositions[particleIdx].Position;  // NEW
uint3 cellA = GetGridCell(posA);

// Query 3×3×3 neighborhood (27 cells)
for (int dz = -1; dz <= 1; dz++)
{
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            // ... cell iteration code ...
            
            for (uint i = 0; i < cellCount; i++)
            {
                uint particleIdxB = CellData[cellHash * MaxParticlesPerCell + i];
                
                // Skip self
                if (particleIdxB == particleIdx)
                    continue;
                
                float invMassB = InvMass[particleIdxB];
                float3 posB = PredictedRead[particleIdxB].Position;
                float3 originalPosB = OriginalPositions[particleIdxB].Position;  // NEW
                
                // Collision detection
                float3 diff = posB - posA;
                float dist = length(diff);
                float minDist = 2.0f * CollisionRadius;
                
                // NEW: Velvet's original position check
                float3 originalDiff = originalPosB - originalPosA;
                float originalDist = length(originalDiff);
                float minOriginalDist = 2.0f * CollisionRadius;  // Particles must be far apart in rest pose
                
                // CRITICAL: Only collide if currently close AND originally far apart
                if (dist < minDist && dist > EPSILON && originalDist > minOriginalDist)
                {
                    // Collision response...
                }
            }
        }
    }
}
```

**Testing Checkpoint**: After implementing Task 1.1, test with two cloths on floor. You should see significant improvement in collision quality.

---

### Task 1.2: Fix Symmetric Accumulation (Asymmetric Pattern)

**Goal**: Prevent double-counting by only accumulating corrections for the current particle.

#### Step 1: Modify Collision Response

**File**: [`ClothSelfCollisionSolver.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionSolver.hlsl)

Replace symmetric accumulation (lines 114-159) with asymmetric:
```hlsl
if (dist < minDist && dist > EPSILON && originalDist > minOriginalDist)
{
    // Collision response (mass-weighted separation)
    float3 normal = diff / dist;
    float penetration = minDist - dist;
    
    // REMOVE floor bias code (lines 120-134) - will be handled differently
    
    float wSum = invMassA + invMassB;
    if (wSum < EPSILON)
        continue;
    
    // Compute correction for particle A ONLY (asymmetric)
    float3 correction = normal * penetration * CollisionStiffness;
    float3 corrA = -correction * (invMassA / wSum);
    
    // Atomic accumulation (scaled to int for thread safety)
    int3 deltaA = int3(corrA * kScale);
    
    // ONLY accumulate for particle A (current thread's particle)
    InterlockedAdd(PositionDelta[particleIdx].x, deltaA.x);
    InterlockedAdd(PositionDelta[particleIdx].y, deltaA.y);
    InterlockedAdd(PositionDelta[particleIdx].z, deltaA.z);
    InterlockedAdd(PositionWeight[particleIdx], 1);
    
    // REMOVED: Do NOT accumulate for particle B
    // (particle B's thread will handle its own correction when it processes this collision)
}
```

**Key Change**: Removed lines 155-159 that accumulated for particle B. Each particle only accumulates its own corrections.

**Testing Checkpoint**: After implementing Task 1.2, test stability. Collisions should be more stable with less jittering.

---

## Phase 2: Quality Improvements

### Task 2.1: Add Friction

**Goal**: Add tangential friction to improve realism and stability.

#### Step 1: Bind Velocity Buffer

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

In `DispatchSelfCollision()`, add velocity buffer to SRV bindings:
```cpp
// Bind SRVs
ID3D11ShaderResourceView* srvs[] = {
    UnifiedPredictedSRV,              // t0: Predicted positions
    UnifiedInvMassSRV,                // t1: Inverse masses
    SelfCollisionCellCountersSRV,    // t2: Cell counters
    SelfCollisionCellDataSRV,         // t3: Cell data
    UnifiedIndexSRV,                  // t4: Index buffer
    UnifiedOriginalPositionSRV,      // t5: Original positions
    UnifiedVelocitySRV                // t6: Velocities (NEW)
};
Graphics->DeviceContext->CSSetShaderResources(0, 7, srvs);  // Changed from 6 to 7
```

#### Step 2: Add Friction Computation to Shader

**File**: [`ClothSelfCollisionSolver.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionSolver.hlsl)

Add velocity buffer binding:
```hlsl
StructuredBuffer<FClothVelocity> Velocities : register(t6);  // NEW: Velocities for friction
```

Add friction computation in collision response:
```hlsl
if (dist < minDist && dist > EPSILON && originalDist > minOriginalDist)
{
    float3 normal = diff / dist;
    float penetration = minDist - dist;
    
    float wSum = invMassA + invMassB;
    if (wSum < EPSILON)
        continue;
    
    // Compute base correction
    float3 correction = normal * penetration * CollisionStiffness;
    float3 corrA = -correction * (invMassA / wSum);
    
    // NEW: Add friction
    float3 velA = Velocities[particleIdx].Velocity;
    float3 velB = Velocities[particleIdxB].Velocity;
    float3 relativeVelocity = velA - velB;
    
    // Tangential velocity (perpendicular to collision normal)
    float3 tangentVel = relativeVelocity - normal * dot(relativeVelocity, normal);
    float tangentLen = length(tangentVel);
    
    if (tangentLen > EPSILON)
    {
        // Coulomb friction model
        float correctionLen = length(corrA);
        float maxFriction = correctionLen * CollisionFriction;  // Use friction coefficient from params
        float3 frictionDir = tangentVel / tangentLen;
        float frictionMag = min(maxFriction, tangentLen);
        
        // Apply friction correction (weighted by inverse mass)
        float3 frictionCorrection = -frictionDir * frictionMag * (invMassA / wSum);
        corrA += frictionCorrection;
    }
    
    // Accumulate with friction
    int3 deltaA = int3(corrA * kScale);
    InterlockedAdd(PositionDelta[particleIdx].x, deltaA.x);
    InterlockedAdd(PositionDelta[particleIdx].y, deltaA.y);
    InterlockedAdd(PositionDelta[particleIdx].z, deltaA.z);
    InterlockedAdd(PositionWeight[particleIdx], 1);
}
```

**Testing Checkpoint**: After implementing Task 2.1, cloths should slide less and feel more realistic.

---

### Task 2.2: Reorder Execution (Floor Collision After Self-Collision)

**Goal**: Allow self-collision to separate cloths before floor constraint clamps them down.

#### Step 1: Modify Simulation Order

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

In `SimulateSubstep()` method (around line 2126-2187), reorder operations:

**Current Order**:
```cpp
void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    // 1. Integration
    DispatchIntegration(UsedParticleCount);
    
    // 2. Floor collision (BEFORE iteration loop)
    DispatchCollisionSDF(UsedParticleCount);
    
    // 3. Edge collision
    if (UsedEdgeCollisionCount > 0)
        DispatchEdgeCollisionSDF(UsedEdgeCollisionCount);
    
    // 4. Constraint solving loop
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        if (UsedConstraintCount > 0)
            DispatchConstraintSolver(UsedConstraintCount);
        
        if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
            DispatchSelfCollision(UsedParticleCount);
        
        DispatchApplyDeltas(UsedParticleCount);
    }
    
    // 5. Kinematic targets
    // 6. Finalize
}
```

**New Order** (Velvet-style):
```cpp
void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    // 1. Integration
    DispatchIntegration(UsedParticleCount);
    
    // 2. Constraint solving loop (MOVED BEFORE floor collision)
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        if (UsedConstraintCount > 0)
            DispatchConstraintSolver(UsedConstraintCount);
        
        if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
            DispatchSelfCollision(UsedParticleCount);
        
        DispatchApplyDeltas(UsedParticleCount);
    }
    
    // 3. Floor collision (MOVED AFTER iteration loop)
    DispatchCollisionSDF(UsedParticleCount);
    
    // 4. Edge collision
    if (UsedEdgeCollisionCount > 0)
        DispatchEdgeCollisionSDF(UsedEdgeCollisionCount);
    
    // 5. Kinematic targets
    if (ComputeKinematicTargetsCS && AttachmentDataSRV && UsedAttachmentCount > 0)
        DispatchComputeKinematicTargets(UsedAttachmentCount);
    
    // 6. Finalize
    DispatchFinalize(UsedParticleCount);
}
```

**Rationale**: 
- Self-collision can now separate cloths vertically
- Floor collision only affects particles that are actually below floor
- Reduces Z-axis fighting between constraints

**Testing Checkpoint**: After implementing Task 2.2, Z-axis separation should work much better.

---

## Phase 3: Optional Optimizations

### Task 3.1: Neighbor Caching (Performance)

**Goal**: Pre-cache neighbors to reduce memory accesses during collision detection.

**Complexity**: HIGH - Requires significant refactoring
**Benefit**: 20-30% performance improvement
**Recommendation**: Implement only if performance is critical

**Overview**:
1. Add neighbor cache buffer (flat array of neighbor indices)
2. Add caching pass after grid build
3. Modify collision solver to read from cache instead of querying grid

**Reference**: See Velvet's [`SpatialHashGPU.cu:79-130`](C:/Users/hgchoi11/source/repos/Velvet/Velvet/SpatialHashGPU.cu:79-130)

---

### Task 3.2: Proper Topology Check (Quality)

**Goal**: Replace index-based heuristic with proper adjacency buffer.

**Complexity**: MEDIUM
**Benefit**: Better handling of arbitrary meshes
**Recommendation**: Implement if working with non-grid meshes

**Overview**:
1. Build adjacency buffer during mesh preprocessing
2. Upload adjacency data to GPU
3. Use adjacency lookup instead of index difference check

---

## Testing Strategy

### Test Suite

#### Test 1: Two Cloths on Floor (Primary Issue)
- **Setup**: Two cloth instances on floor (z=0), overlapping
- **Baseline**: Z-fighting, poor separation
- **After Phase 1**: Proper separation in all directions
- **After Phase 2**: Smooth, stable separation with friction

#### Test 2: Cloth Folding on Itself
- **Setup**: Single cloth instance folding back on itself
- **Baseline**: False collisions, jittering
- **After Phase 1**: Smooth folding, no false collisions

#### Test 3: Multiple Cloths Stacked
- **Setup**: 3+ cloth instances stacked vertically
- **Baseline**: Unstable, penetration
- **After Phase 1**: Stable stacking
- **After Phase 2**: Very stable with friction

#### Test 4: Falling Cloth
- **Setup**: Cloth falling onto another cloth
- **Baseline**: Penetration, bouncing
- **After Phase 1**: Proper landing
- **After Phase 2**: Realistic landing with friction

### Performance Benchmarks

Measure frame time for each phase:
- **Baseline**: Current implementation
- **Phase 1**: With original position check + asymmetric accumulation
- **Phase 2**: With friction + reordered execution

Expected performance impact:
- Phase 1: +5-10% (additional buffer read)
- Phase 2: +10-15% (friction computation)
- Total: +15-25% overhead for significantly better quality

---

## Implementation Checklist

### Phase 1: Critical Fixes
- [ ] Task 1.1: Add Original Position Buffer
  - [ ] Add buffer declarations to header
  - [ ] Allocate buffer in AllocateBuffers()
  - [ ] Upload original positions in UploadParticleData()
  - [ ] Bind buffer in DispatchSelfCollision()
  - [ ] Update shader to use original positions
  - [ ] Test: Two cloths on floor

- [ ] Task 1.2: Fix Symmetric Accumulation
  - [ ] Modify collision response to asymmetric pattern
  - [ ] Remove particle B accumulation
  - [ ] Test: Stability and jittering

### Phase 2: Quality Improvements
- [ ] Task 2.1: Add Friction
  - [ ] Bind velocity buffer in dispatch
  - [ ] Add friction computation to shader
  - [ ] Test: Realism and sliding

- [ ] Task 2.2: Reorder Execution
  - [ ] Move floor collision after iteration loop
  - [ ] Test: Z-axis separation

### Phase 3: Optional Optimizations
- [ ] Task 3.1: Neighbor Caching (if needed)
- [ ] Task 3.2: Proper Topology Check (if needed)

---

## Expected Results

### Quality Metrics

| Metric | Baseline | After Phase 1 | After Phase 2 |
|--------|----------|---------------|---------------|
| False Collisions | High | **Low** | **Low** |
| Separation Quality | Poor | **Good** | **Excellent** |
| Stability | Poor | **Good** | **Excellent** |
| Realism | Poor | Good | **Excellent** |
| Z-Axis Separation | Fails | Works | **Works Well** |
| Performance | 100% | 110% | 125% |

### Success Criteria

**Phase 1 Complete When**:
- Two cloths on floor separate properly in all directions
- No false collisions during cloth folding
- Stable simulation without jittering

**Phase 2 Complete When**:
- Cloths feel realistic with friction
- Z-axis separation works smoothly
- Multiple cloths stack stably

---

## Rollback Plan

If issues arise during implementation:

1. **Phase 1 Issues**: 
   - Verify original position buffer is uploaded correctly
   - Check originalDist threshold (try 1.5x or 2.5x CollisionRadius)
   - Ensure asymmetric accumulation doesn't skip collisions

2. **Phase 2 Issues**:
   - Friction too strong: Reduce CollisionFriction coefficient
   - Floor penetration: Revert execution order, keep floor collision first
   - Performance issues: Disable friction temporarily

3. **Complete Rollback**:
   - Keep original position check (most important)
   - Revert to symmetric accumulation if asymmetric causes issues
   - Skip friction and execution reorder

---

## Conclusion

This implementation plan provides a clear path to Velvet-quality self-collision. The most critical fix is the original position check (Task 1.1), which will eliminate false collisions and dramatically improve quality. The other fixes build on this foundation to achieve production-ready collision handling.

**Estimated Implementation Time**:
- Phase 1: 2-4 hours
- Phase 2: 1-2 hours
- Testing: 2-3 hours
- **Total**: 5-9 hours

**Priority**: Implement Phase 1 first, test thoroughly, then proceed to Phase 2 if needed.
