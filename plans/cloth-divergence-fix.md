# Cloth Simulation Divergence Fix

## Problem
After re-applying recent constraint fixes, the cloth simulation became unstable and diverged (exploded) in the YZ plane. Previously it was only following gravity without constraints applied, but with the fixes it became unstable.

## Root Causes Identified

### 1. **Double Application of Stiffness** (CRITICAL)
**Location**: [`ClothApplyDelta.hlsl:48`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothApplyDelta.hlsl:48)

**Problem**: 
- Stiffness was applied in the constraint solver when computing corrections
- Then applied AGAIN in ApplyDeltas when applying corrections to positions
- This created exponential amplification of constraint corrections

**Before**:
```hlsl
// In ConstraintSolver: correction includes stiffness
float3 corr = (-C * stiffness) * dir / wSum;

// In ApplyDeltas: stiffness applied AGAIN
PredictedBuffer[idx].Position += avgDelta * RelaxationFactor * params.StretchStiffness;
```

**After**:
```hlsl
// In ConstraintSolver: correction includes stiffness (unchanged)
float3 corr = (-C * stiffness) * dir / wSum;

// In ApplyDeltas: only relaxation factor applied
PredictedBuffer[idx].Position += avgDelta * RelaxationFactor;
```

### 2. **Incorrect Weight Accumulation** (CRITICAL)
**Location**: [`ClothConstraintSolver.hlsl:77-82`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothConstraintSolver.hlsl:77)

**Problem**:
- Distance constraints accumulated invMass values into PositionWeight
- Should accumulate constraint COUNT instead (like bend constraints do)
- The delta already includes invMass weighting in its magnitude
- Dividing by accumulated invMass was incorrect

**Before**:
```hlsl
InterlockedAdd(PositionWeight[i0], (int)(w0 * kWeightScale));
InterlockedAdd(PositionWeight[i1], (int)(w1 * kWeightScale));
```

**After**:
```hlsl
InterlockedAdd(PositionWeight[i0], 1);  // Count constraints, not invMass sum
InterlockedAdd(PositionWeight[i1], 1);  // Count constraints, not invMass sum
```

### 3. **Wrong Division Formula** (CRITICAL)
**Location**: [`ClothApplyDelta.hlsl:38-48`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothApplyDelta.hlsl:38)

**Problem**:
- Dividing accumulated delta by invMass sum (wrong denominator)
- Should divide by constraint count to get average correction
- Then multiplying by stiffness again (see issue #1)

**Before**:
```hlsl
float weight = (float)weightFixed / kWeightScale;  // invMass sum
float3 avgDelta = delta / weight;  // Wrong: dividing by invMass
PredictedBuffer[idx].Position += avgDelta * RelaxationFactor * params.StretchStiffness;
```

**After**:
```hlsl
float weight = (float)weightFixed;  // Constraint count
float3 avgDelta = delta / weight;   // Correct: average of constraint corrections
PredictedBuffer[idx].Position += avgDelta * RelaxationFactor;  // No double stiffness
```

## Technical Explanation

### Constraint Solving Flow
The correct PBD constraint solving flow is:

1. **Constraint Solver** (ConstraintSolver.hlsl):
   - Computes correction: `corr = (-C * stiffness) * dir / (w0 + w1)`
   - This correction ALREADY includes stiffness and invMass weighting
   - Accumulates correction into delta buffer
   - Increments constraint count

2. **Apply Deltas** (ApplyDelta.hlsl):
   - Averages accumulated corrections: `avgDelta = totalDelta / constraintCount`
   - Applies with relaxation: `position += avgDelta * relaxationFactor`
   - No additional stiffness multiplication needed

### Why This Matters

**Stiffness Amplification**:
- If stiffness = 0.5 and we apply it twice: `0.5 * 0.5 = 0.25` (seems okay)
- But over multiple iterations: `(0.5)^(2*N)` instead of `(0.5)^N`
- With N=5 iterations: `(0.5)^10 = 0.001` vs `(0.5)^5 = 0.03` (30x difference!)
- Higher stiffness values amplify exponentially causing divergence

**Weight Division Error**:
- Dividing by invMass sum instead of constraint count
- For particle with invMass=1.0 and 4 constraints:
  - Wrong: divide by 4.0 (invMass sum)
  - Correct: divide by 4 (constraint count)
- Looks similar but breaks when particles have different masses

## Changes Made

### File: EngineSIU/EngineSIU/Shaders/Cloth/ClothConstraintSolver.hlsl
- Changed weight accumulation from `(int)(w0 * kWeightScale)` to `1`
- Changed weight accumulation from `(int)(w1 * kWeightScale)` to `1`
- Removed unused `kWeightScale` constant

### File: EngineSIU/EngineSIU/Shaders/Cloth/ClothApplyDelta.hlsl
- Changed weight interpretation from invMass sum to constraint count
- Removed division by `kWeightScale` (now just using integer count)
- Removed multiplication by `params.StretchStiffness` (prevents double application)
- Removed unused `kWeightScale` constant
- Removed unused `InstanceParams` buffer binding

## Verification Points

After applying these fixes, verify:

1. **Stability**: Cloth should not explode or diverge
2. **Gravity Response**: Cloth should fall naturally under gravity
3. **Constraint Satisfaction**: Distance constraints should maintain rest length
4. **No Excessive Stiffness**: Cloth should be flexible, not rigid
5. **Damping Works**: Cloth should settle after perturbation

## Related Files

- [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp) - Simulation loop orchestration
- [`ClothIntegrate.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothIntegrate.hlsl) - Integration step
- [`ClothConstraintSolver.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothConstraintSolver.hlsl) - Distance constraint solving
- [`ClothApplyDelta.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothApplyDelta.hlsl) - Delta application
- [`ClothFinalize.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothFinalize.hlsl) - Velocity finalization
- [`ClothBendConstraintSolver.hlsl`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothBendConstraintSolver.hlsl) - Bend constraint solving (correct pattern)

## Note on Bend Constraints

The bend constraint solver already used the correct pattern (accumulating constraint count, not invMass). The distance constraint solver was inconsistent with this pattern, which is what caused the divergence.
