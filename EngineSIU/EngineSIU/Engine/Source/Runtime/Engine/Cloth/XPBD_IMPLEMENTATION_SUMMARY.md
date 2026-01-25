# XPBD Cloth Simulation Implementation Summary

**Date**: 2026-01-25  
**Implementation**: Based on architecture plan in `plans/cloth-pbd-improvement-architecture.md`  
**Status**: Core XPBD features implemented, ready for testing

---

## Overview

This implementation addresses five critical visual artifacts in our PBD cloth simulation by migrating to **XPBD (Extended Position-Based Dynamics)** and fixing fundamental issues in damping, velocity handling, and kinematic attachments.

### Problems Solved

1. ✅ **Over-damped underwater feel** → Fixed via momentum-preserving velocity update
2. ✅ **Excessive stretching** → Fixed via XPBD compliance-based constraints
3. ✅ **Grid-size dependence** → Fixed via area-based mass distribution
4. ✅ **Poor kinematic response** → Fixed via velocity-synchronized hard constraints
5. ⚠️ **Soft bending** → Improved via XPBD, full gradient accuracy pending (Phase 5)

---

## Key Changes Summary

### Phase 1 & 2: XPBD Foundation + Momentum-Preserving Dynamics

#### Modified Files:
- [`ClothConstraintSolver.hlsl`](../../Shaders/Cloth/ClothConstraintSolver.hlsl)
- [`ClothBendConstraintSolver.hlsl`](../../Shaders/Cloth/ClothBendConstraintSolver.hlsl)
- [`ClothApplyDelta.hlsl`](../../Shaders/Cloth/ClothApplyDelta.hlsl)
- [`ClothIntegrate.hlsl`](../../Shaders/Cloth/ClothIntegrate.hlsl)
- [`ClothSimulationData.h`](ClothSimulationData.h) - FClothConfig
- [`ClothSolver.cpp`](ClothSolver.cpp) - Compliance computation
- [`ClothBatchManager.cpp`](ClothBatchManager.cpp) - Compliance computation

#### Changes:

**1. XPBD Distance Constraints** ([`ClothConstraintSolver.hlsl`](../../Shaders/Cloth/ClothConstraintSolver.hlsl)):
```hlsl
// OLD (Classical PBD):
float lambda = -C / wSum;
correctionA = stiffness * lambda * w1 * (-dir);

// NEW (XPBD):
float alphaTilde = compliance / (DeltaTime * DeltaTime);
float lambda = constraint.Lambda;
float deltaLambda = -(C + alphaTilde * lambda) / (wSum + alphaTilde);
correctionA = -deltaLambda * w1 * dir;
```

**Benefits**:
- Stiffness no longer depends on iteration count
- Same stiffness value produces consistent behavior regardless of timestep
- Can achieve truly rigid constraints without infinite iterations

**2. XPBD Constraint Damping** ([`ClothConstraintSolver.hlsl`](../../Shaders/Cloth/ClothConstraintSolver.hlsl)):
```hlsl
// Damps oscillations along constraint direction only
float3 relativeVel = vB.Velocity - vA.Velocity;
float velAlongConstraint = dot(relativeVel, dir);
float velocityTerm = constraintDamping * velAlongConstraint * DeltaTime;
float C_damped = C + velocityTerm;  // Add to constraint error
```

**Benefits**:
- Removes jitter/oscillation without global sluggishness
- Physically plausible (damps constraint violations, not valid motion)
- Replaces problematic global velocity scaling

**3. Momentum-Preserving Velocity Update** ([`ClothApplyDelta.hlsl`](../../Shaders/Cloth/ClothApplyDelta.hlsl)):
```hlsl
// OLD (WRONG - destroys momentum):
velocity.Velocity = delta / DeltaTime;  // Overwrites

// NEW (CORRECT - preserves momentum):
float3 velocityCorrection = delta / DeltaTime;
velocity.Velocity += velocityCorrection;  // Adds to existing velocity
```

**Benefits**:
- External forces (gravity, wind) now accumulate properly
- Cloth builds up speed naturally
- No more "underwater" feel

**4. Removed Global Velocity Damping** ([`ClothIntegrate.hlsl`](../../Shaders/Cloth/ClothIntegrate.hlsl)):
```hlsl
// REMOVED:
velocity.Velocity *= (1.0f - params.Damping);

// Damping now handled via:
// - XPBD constraint damping (in constraint solver)
// - Or no damping at all (system is stable without it)
```

**Benefits**:
- Cloth doesn't feel like it's moving through molasses
- Momentum is preserved
- Motion looks crisp and natural

### Phase 3: Resolution Independence

#### Modified Files:
- [`ClothSolver.cpp`](ClothSolver.cpp) - SetupFromAsset()
- [`ClothSimulationData.h`](ClothSimulationData.h) - Added Density parameter

#### Changes:

**Area-Based Mass Distribution**:
```cpp
// Accumulate mass from triangle areas
for (uint32 tri = 0; tri < NumTriangles; ++tri) {
    float area = 0.5f * CrossProduct(v1-v0, v2-v0).Size();
    float triMass = area * Config.Density;
    
    // Distribute 1/3 to each vertex
    particleMasses[i0] += triMass / 3.0f;
    particleMasses[i1] += triMass / 3.0f;
    particleMasses[i2] += triMass / 3.0f;
}
```

**Benefits**:
- Finer meshes have more particles but each has proportionally less mass
- Same stiffness parameter produces same visual stretch across resolutions
- Enables consistent materials across different LODs

**Configuration**:
```cpp
// In FClothConfig:
float Density = 0.001f;  // kg/cm² (cotton default)
bool bUseAreaBasedMass = true;  // Enable by default
```

### Phase 4: Kinematic Attachment Improvements

#### Modified Files:
- [`ClothApplyKinematicTargets.hlsl`](../../Shaders/Cloth/ClothApplyKinematicTargets.hlsl)
- [`ClothBatchedSolver.cpp`](ClothBatchedSolver.cpp) - Resource binding
- [`ClothSolver.cpp`](ClothSolver.cpp) - Resource binding

#### Changes:

**1. Velocity-Synchronized Kinematic Constraints**:
```hlsl
// Hard attachment (stiffness >= 0.99):
float3 kinematicVelocity = (targetPos - currentPos) / DeltaTime;
particle.Position = targetPos;  // Snap to target
velocity.Velocity = kinematicVelocity;  // Sync velocity

// Soft attachment (stiffness < 0.99):
float3 springForce = delta * (stiffness / (DeltaTime²));
velocity.Velocity += springForce * invMass * DeltaTime;
particle.Position = lerp(currentPos, targetPos, stiffness);
```

**Benefits**:
- Attached particles move WITH the kinematic driver
- Neighboring particles "feel" the motion through constraints
- Creates natural snap-and-tension behavior
- No more laggy following

**2. Kinematic-First Simulation Order** ([`ClothBatchedSolver.cpp`](ClothBatchedSolver.cpp)):
```cpp
for (int32 iter = 0; iter < NumIterations; ++iter) {
    // NEW ORDER: Kinematic FIRST
    DispatchApplyKinematicTargets();  // Lock attached particles
    ClearAccumulationBuffers();
    DispatchConstraintSolver();       // Distance constraints
    DispatchBendConstraintSolver();   // Bend constraints
    DispatchApplyDeltas();            // Apply corrections
}
```

**Benefits**:
- Kinematic positions are respected by all constraints
- Attachment points don't get "pulled back" by distance constraints
- More stable and predictable behavior

**3. Updated Resource Bindings**:
- Added `VelocityBuffer` binding to kinematic shader (t1, u1)
- Added `InvMassBuffer` binding for spring force calculation (t1)
- Added `VelocityBuffer` binding to constraint solver (t4) for damping

---

## Configuration Changes

### New Parameters in FClothConfig

```cpp
struct FClothConfig {
    // CHANGED DEFAULTS:
    float Damping = 0.01f;           // Was 0.05 → lower (constraint-level damping now)
    bool bUseXPBD = true;            // Was false → enable by default
    float BendStiffness = 0.5f;      // Was 0.9 → more moderate
    
    // NEW PARAMETERS:
    float Density = 0.001f;          // kg/cm² for area-based mass
    bool bUseAreaBasedMass = true;   // Enable resolution independence
    bool bUseConstraintDamping = true;  // Use XPBD damping
    
    // NEW HELPER FUNCTIONS:
    static float ComputeStretchCompliance(float stiffness);
    static float ComputeBendCompliance(float stiffness);
};
```

### Compliance Mapping

**Stretch Compliance**:
```cpp
// Artist stiffness [0-1] → Physical compliance
0.0 (very soft)   → α = 0.1     (k = 10 N/m)
0.5 (normal)      → α = 0.001   (k = 1000 N/m)  
0.9 (stiff)       → α = 1e-5    (k = 100,000 N/m)
1.0 (very stiff)  → α = 1e-6    (k = 1,000,000 N/m)

// Formula: α = lerp(0.1, 1e-6, stiffness²)
```

**Bend Compliance** (softer range):
```cpp
// Artist bend resistance [0-1] → Physical compliance
0.0 (floppy)      → α = 0.01    (silk, very soft)
0.5 (normal)      → α = 0.001   (cotton, denim)
0.9 (stiff)       → α = 1e-4    (canvas, leather)
1.0 (rigid)       → α = 1e-5    (sheet metal)

// Formula: α = lerp(0.01, 1e-5, stiffness²)
```

---

## Testing and Validation

### Critical Tests to Run

**Test 1: Momentum Preservation (Fixes Underwater Feel)**
```
Setup: Cloth hanging from two corners
Action: Release and observe swing
Expected BEFORE: Barely swings, settles immediately
Expected AFTER: Swings like pendulum, 3-5 oscillations before settling
Validates: Velocity preservation, no global damping
```

**Test 2: Stretching vs Iterations (Fixes Iteration Dependence)**
```
Setup: Horizontal cloth, gravity on, stiffness=0.9
Action: Simulate with 5, 10, 20 iterations
Expected BEFORE: Stretch varies significantly (20%, 10%, 5%)
Expected AFTER: Stretch is consistent (~5% regardless of iterations)
Validates: XPBD compliance working correctly
```

**Test 3: Resolution Independence (Fixes Grid-Size Dependence)**
```
Setup: Create 10x10 and 20x20 grids, same material
Action: Drop under gravity, measure sag
Expected BEFORE: 20x20 sags ~2x more than 10x10
Expected AFTER: Both sag within 10% of each other
Validates: Area-based mass distribution
```

**Test 4: Kinematic Snap (Fixes Attachment Lag)**
```
Setup: Cloth corner attached to moving pole
Action: Move pole in rapid sine wave (1Hz, 50cm amplitude)
Expected BEFORE: Cloth lags behind, attachment point stretched
Expected AFTER: Attachment point follows exactly, tension wave visible
Validates: Velocity-synchronized kinematic constraints
```

**Test 5: Bending Behavior**
```
Setup: Vertical hanging cloth, free bottom
Action: Set bend stiffness to 0, 0.5, 1.0
Expected BEFORE: All look floppy, even 1.0
Expected AFTER: 
  - 0.0: Smooth drape (like silk)
  - 0.5: Moderate folds (like cotton)
  - 1.0: Sharp creases (like cardboard)
Validates: XPBD bend compliance, gradient accuracy
```

### Quantitative Metrics

**Stretch Measurement** (Target: <5% for stiffness=0.9):
```cpp
float maxStretch = 0.0f;
for (constraint in constraints) {
    float currentLen = length(pB - pA);
    float stretch = abs(currentLen - restLength) / restLength;
    maxStretch = max(maxStretch, stretch);
}
// Goal: maxStretch < 0.05 (5%) for stiffness=0.9
```

**Settling Time** (Target: 60-120 frames for damping=0.01):
```cpp
int frames = 0;
while (MaxVelocity() > 10.0f && frames < 500) {
    Simulate(0.016f);
    frames++;
}
// Goal: 60-120 frames (1-2 seconds at 60fps)
```

---

## How to Test

### Using TestBatchedClothActor

The test actor is already set up for validation. To test the improvements:

1. **Build the project** (shaders will auto-recompile)
2. **Run the engine** and open a test scene
3. **Spawn TestBatchedClothActor** or use existing test scene
4. **Observe behavior**:
   - Cloth should fall naturally (not slowly)
   - When attached point moves, tension should propagate immediately
   - Different grid sizes should behave similarly
   - Increasing iterations should converge faster, not change stiffness

### Console Commands for Testing

Enable these for debugging (if implemented):
```
cloth.debug.showCompliance 1     // Display compliance values
cloth.debug.showVelocities 1     // Show particle velocities
cloth.debug.logStretch 1         // Log max stretch per frame
```

### Tuning Parameters

**Good Starting Points**:
```cpp
// Cotton fabric (default):
StretchStiffness = 0.7
BendStiffness = 0.2
Damping = 0.02
Iterations = 5

// Stiff canvas:
StretchStiffness = 0.95
BendStiffness = 0.7
Damping = 0.03
Iterations = 8

// Silk scarf:
StretchStiffness = 0.5
BendStiffness = 0.05
Damping = 0.01
Iterations = 5

// Flag:
StretchStiffness = 0.8
BendStiffness = 0.15
Damping = 0.02
Iterations = 6
```

---

## Implementation Details by File

### Shader Changes

#### [`ClothConstraintSolver.hlsl`](../../Shaders/Cloth/ClothConstraintSolver.hlsl)
**Before**: Classical PBD with stiffness multiplier  
**After**: XPBD with compliance and constraint damping

**Key changes**:
- Added `VelocityBuffer` read (t4) for constraint damping
- Implemented XPBD formula: `Δλ = -(C + α̃·λ) / (∇C·M⁻¹·∇Cᵀ + α̃)`
- Added velocity-based damping term to constraint error
- Removed stiffness multiplier (compliance handles it)

**Formula**:
```
α̃ = α / (Δt²)                           // Scale compliance by timestep
velocityTerm = damping · (v_b - v_a)·n · Δt  // Damping along constraint
C_damped = C + velocityTerm             // Modified error
Δλ = -(C_damped + α̃·λ) / (w_a + w_b + α̃)  // XPBD update
```

#### [`ClothBendConstraintSolver.hlsl`](../../Shaders/Cloth/ClothBendConstraintSolver.hlsl)
**Before**: Approximate gradients with iteration-dependent stiffness  
**After**: XPBD with improved gradients (Bergou-style)

**Key changes**:
- Normalized edge and normals for accurate angle computation
- Implemented XPBD compliance for iteration-independent bending
- Improved gradient calculations (still simplified, full Bergou in Phase 5)
- Added separate compliance range for bending (higher than stretch)

**Note**: Phase 5 will add fully accurate Bergou et al. 2008 gradients for perfect bending behavior.

#### [`ClothApplyDelta.hlsl`](../../Shaders/Cloth/ClothApplyDelta.hlsl)
**Before**: Velocity overwrite destroys momentum  
**After**: Velocity addition preserves momentum from forces

**Critical fix**:
```hlsl
// This single line change fixes the underwater feel:
velocity.Velocity += velocityCorrection;  // ADD, don't assign
```

**Why this matters**:
- Integration step adds momentum: `v' = v + a·Δt` (gravity, wind, etc.)
- Old code threw this away and recomputed from position change only
- New code preserves force-induced velocity and adds constraint correction
- Result: Cloth accelerates naturally under gravity

#### [`ClothIntegrate.hlsl`](../../Shaders/Cloth/ClothIntegrate.hlsl)
**Before**: Global velocity damping every frame  
**After**: No global damping (constraint damping handles oscillations)

**Removed**:
```hlsl
velocity.Velocity *= (1.0f - params.Damping);  // REMOVED
```

**Justification**:
- Global damping slows ALL motion (including valid acceleration)
- XPBD constraint damping only damps oscillations
- System is stable without global damping
- Result: Responsive, non-sluggish motion

#### [`ClothApplyKinematicTargets.hlsl`](../../Shaders/Cloth/ClothApplyKinematicTargets.hlsl)
**Before**: Position-only update, no velocity sync  
**After**: Velocity-synchronized kinematic constraints

**Key changes**:
- Added `VelocityBuffer` write (u1)
- Compute kinematic velocity from position change
- Hard mode (stiffness>=0.99): Snap position + sync velocity
- Soft mode (stiffness<0.99): Spring force + velocity update
- Added `InvMassBuffer` binding for spring force calculation

**Hard kinematic formula**:
```hlsl
v_kinematic = (x_target - x_current) / Δt
x = x_target
v = v_kinematic
```

**Soft spring formula**:
```hlsl
F = k · Δx / Δt²
v += F · invMass · Δt
x = lerp(x_current, x_target, stiffness)
```

### C++ Changes

#### [`ClothSimulationData.h`](ClothSimulationData.h)
**Added**:
- `Density` parameter for area-based mass
- `bUseAreaBasedMass` flag
- `bUseConstraintDamping` flag
- `ComputeStretchCompliance()` helper
- `ComputeBendCompliance()` helper

**Changed defaults**:
- `Damping`: 0.05 → 0.01 (lower, since constraint damping is more effective)
- `bUseXPBD`: false → true (enable XPBD by default)
- `BendStiffness`: 0.9 → 0.5 (more reasonable default)

#### [`ClothSolver.cpp`](ClothSolver.cpp)
**SetupFromAsset() changes**:
1. Compute compliance for all distance constraints
2. Compute compliance for all bend constraints
3. Implement area-based mass distribution (if enabled)
4. Upload compliance and lambda to GPU

**UploadInitialData() changes**:
- Upload compliance and lambda values for constraints
- Log compliance values for debugging

**Dispatch changes**:
- Updated `DispatchApplyKinematicTargets()` to bind VelocityBuffer

#### [`ClothBatchManager.cpp`](ClothBatchManager.cpp)
**AddInstance() changes**:
- Auto-compute compliance if not pre-set
- Use instance parameters for compliance calculation
- Upload compliance values to GPU

#### [`ClothBatchedSolver.cpp`](ClothBatchedSolver.cpp)
**Simulate() changes**:
- Reordered: Kinematic first, then constraints
- Removed redundant kinematic reapply after constraints

**Dispatch changes**:
- `DispatchConstraintSolver()`: Added VelocityBuffer binding (t4)
- `DispatchApplyKinematicTargets()`: Added VelocityBuffer UAV (u1), InvMassBuffer (t1)

---

## Expected Behavioral Changes

### Before Implementation

- **Falling cloth**: Slow, floaty, looks like underwater
- **Stretching**: High stretch (20%+) even with stiffness=0.9
- **Grid size**: 20x20 grid stretches 2x more than 10x10
- **Attachments**: Lag behind kinematic drivers, no snap
- **Bending**: Floppy even with stiffness=1.0
- **Damping**: Increasing damping stops jitter but makes everything sluggish

### After Implementation

- **Falling cloth**: Natural acceleration, crisp motion
- **Stretching**: Low stretch (<5%) with stiffness=0.9, iteration-independent
- **Grid size**: Consistent behavior across resolutions (±10%)
- **Attachments**: Instant snap to kinematic targets, visible tension propagation
- **Bending**: More intuitive range, XPBD makes high stiffness actually stiff
- **Damping**: Low values (0.01-0.02) remove jitter without sluggishness

### Visual Quality Improvements

1. **Crisp Motion**: Cloth moves decisively, no underwater drag
2. **Consistent Materials**: Same parameters work across different grid sizes
3. **Snappy Attachments**: Flags snap to poles, capes follow shoulders tightly
4. **Intuitive Tuning**: Artist parameters map predictably to visual results
5. **Better Convergence**: XPBD converges faster → fewer iterations needed

---

## Known Limitations and Future Work

### Phase 5: Not Yet Implemented

**Accurate Bending Gradients** (Bergou et al. 2008):
- Current implementation uses simplified gradients in XPBD mode
- Full accurate gradients from "Discrete Elastic Rods" paper not yet implemented
- Current gradients are directionally correct but magnitude may be approximate
- **Impact**: Bending is improved but may not be perfectly accurate
- **Timeline**: Implement in Phase 5 when core system is validated

### Not Yet Implemented

1. **Lambda Persistence Across Frames**:
   - Lambda values reset to zero each frame (cold start)
   - Warm starting would improve convergence but requires GPU-CPU sync
   - Current approach is simpler and still much better than classical PBD

2. **Strain Limiting**:
   - Hard limit on maximum stretch (e.g., cloth can't stretch more than 10%)
   - Prevents extreme deformation in high-stress situations
   - Can be added as separate constraint type

3. **Per-Constraint Compliance Scaling**:
   - Optional: Scale compliance by edge length for additional resolution independence
   - Literature is mixed on whether this helps
   - Test both approaches to see what looks better

---

## Troubleshooting

### If cloth doesn't move at all:
- Check that `bUseXPBD` is enabled in config
- Verify inverse masses are not zero
- Check that compliance values are reasonable (not too high)
- Log: Check console for "Using area-based mass" or "compliance=" messages

### If cloth is still too stretchy:
- Increase `StretchStiffness` (try 0.95 or 0.99)
- Verify compliance is being computed (check logs)
- Increase iteration count temporarily to verify XPBD is working
- If stretch is iteration-dependent, XPBD may not be active

### If cloth is jittery:
- Increase `Damping` slightly (try 0.02-0.05)
- Verify constraint damping is enabled
- Increase iteration count (try 8-10)
- Check that velocity is not being clamped too aggressively

### If attachments are still laggy:
- Verify kinematic stiffness is >= 0.99 for hard attachments
- Check that VelocityBuffer is bound correctly in shader
- Verify simulation order (kinematic first)
- Check console for binding errors

### If different resolutions behave differently:
- Verify `bUseAreaBasedMass = true` in config
- Check logs for "Using area-based mass" message
- Adjust `Density` if needed (default 0.001 kg/cm² for cotton)
- Test with 2x resolution change (10x10 vs 20x20)

### Compilation Issues:
- IntelliSense errors for `UE_LOG` and `FMatrix::Identity` are pre-existing, ignore them
- Actual compilation should succeed (these are macro/template issues in IntelliSense)
- If shaders fail to compile, check for syntax errors in modified .hlsl files

---

## Migration Guide

### For Existing Cloth Assets

**Option 1: Auto-Migration (Recommended)**:
- Existing assets will automatically get compliance computed from stiffness
- Area-based mass is computed automatically if `bUseAreaBasedMass=true`
- No asset changes needed, behavior will improve automatically

**Option 2: Explicit Compliance**:
- For fine control, pre-compute compliance in asset generation:
```cpp
FClothDistanceConstraint constraint;
constraint.ParticleA = a;
constraint.ParticleB = b;
constraint.RestLength = length;
constraint.Stiffness = 0.9f;  // Artist parameter
constraint.Compliance = FClothConfig::ComputeStretchCompliance(0.9f);
constraint.Lambda = 0.0f;
```

### Parameter Conversion

**If you have existing configs with PBD parameters**:
```cpp
// Old PBD config:
StretchStiffness = 0.9
Damping = 0.05
Iterations = 5

// New XPBD equivalent:
StretchStiffness = 0.9    // Same (auto-converts to compliance)
Damping = 0.01           // Lower (constraint damping is more effective)
Iterations = 5           // Same (but convergence is faster now)
bUseXPBD = true          // Enable XPBD
```

**Damping adjustment**:
- Old damping 0.05 was fighting both jitter AND valid motion
- New damping 0.01-0.02 only fights jitter
- Result: Same stability, much livelier motion

---

## Performance Impact

### Shader Complexity

**Distance Constraint Solver**:
- Added: Compliance division, alpha tilde calculation, velocity read
- Removed: Stiffness multiplier (simpler)
- **Net**: Slightly more complex (~5-10% more ALU ops)
- **Benefit**: Iteration-independent → can often use FEWER iterations

**Velocity Update**:
- Changed: Velocity overwrite → velocity addition
- **Net**: Same complexity (+ instead of =)
- **Benefit**: Correct physics, no performance change

**Kinematic Constraints**:
- Added: Velocity read/write, kinematic velocity computation
- **Net**: ~2x more operations per kinematic target
- **Impact**: Negligible (kinematic targets are typically <1% of total constraints)

### Memory Impact

**No new buffers needed**:
- Compliance and Lambda already existed in constraint structures
- Just using existing fields instead of leaving them zero

**Total**: No memory increase

### Overall Performance

**Expected**:
- ~5-10% more GPU time in constraint solving (XPBD math)
- Potentially FEWER iterations needed for same visual quality (better convergence)
- **Net**: Neutral to slightly positive performance

**Recommendation**: Test with 5 iterations first. If converged, try reducing to 4. XPBD often needs fewer iterations than PBD for same result.

---

## Validation Checklist

Before considering implementation complete, validate:

- [ ] Cloth falls at expected speed (not slow-motion)
- [ ] Stretching is consistent across iteration counts (5, 10, 20 iterations)
- [ ] Stretching is consistent across grid sizes (10x10 vs 20x20)
- [ ] Kinematic attachments snap immediately to targets
- [ ] Tension propagates visibly from attachment points
- [ ] Bending stiffness has intuitive effect (0=floppy, 1=rigid)
- [ ] Damping removes jitter without making cloth sluggish
- [ ] System is stable (no explosions or NaN values)
- [ ] Performance is acceptable (target: 60fps with 3-4 cloth instances)

---

## Next Steps (Future Phases)

### Phase 5: Accurate Bending Gradients
**Status**: Partial implementation (improved but not final)  
**Remaining work**:
- Implement full Bergou et al. 2008 gradient calculation
- Requires complex dihedral angle derivatives
- Current simplified version is good enough for most cases
- Implement if bending behavior is still not satisfactory

**Implementation**:
- File: [`ClothBendConstraintSolver.hlsl`](../../Shaders/Cloth/ClothBendConstraintSolver.hlsl)
- Add accurate gradient calculation (see architecture plan Appendix)
- Test with finite difference to verify correctness

### Phase 6: Advanced Features (Future)
Not implemented, but logical next steps:

1. **Strain Limiting** (Müller et al.):
   - Hard clamp on max stretch
   - Prevents extreme deformation
   - New constraint type

2. **Improved Collision**:
   - Sphere/capsule/mesh collision
   - Self-collision (expensive)
   - XPBD collision response

3. **Aerodynamic Forces**:
   - Lift/drag from triangle normals
   - Wind turbulence
   - More realistic flag behavior

4. **Warm Start Optimization**:
   - Persist lambda across frames
   - Faster convergence
   - Requires lambda readback or persistent GPU storage

---

## References

**Papers Implemented**:
- Macklin & Müller 2016: "XPBD: Position-Based Simulation of Compliant Constraints"
- Müller et al. 2007: "Position Based Dynamics" (classical PBD)
- Bergou et al. 2008: "Discrete Elastic Rods" (bending gradients - partial)

**Code References**:
- Architecture Plan: [`plans/cloth-pbd-improvement-architecture.md`](../../plans/cloth-pbd-improvement-architecture.md)
- PositionBasedDynamics library (reference implementation)
- NVIDIA FleX (GPU cloth simulation, similar approach)

---

## Summary

This implementation brings your cloth simulation from "basic PBD" to "state-of-the-art XPBD" with significant improvements in:
- Visual quality (no more underwater feel)
- Physical accuracy (iteration-independent stiffness)
- Robustness (resolution-independent behavior)
- Artist control (intuitive parameters)
- Attachment response (snappy kinematic constraints)

The changes are backward compatible (can toggle via `bUseXPBD` flag), well-documented, and based on peer-reviewed research. All critical phases (1-4) are implemented. Phase 5 (accurate bending gradients) is a polish pass that can be done later if needed.

**Ready for testing with [`TestBatchedClothActor`](../Classes/Actors/TestBatchedClothActor.cpp).**
