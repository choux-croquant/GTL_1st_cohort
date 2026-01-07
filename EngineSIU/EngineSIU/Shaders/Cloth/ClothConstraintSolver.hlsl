/**
 * Cloth Constraint Solver Compute Shader
 * Solves distance constraints using Position-Based Dynamics (PBD) or XPBD
 * This shader implements parallel Jacobi iteration
 */

#include "ClothCommon.hlsli"

// Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionBuffer : register(u0);
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t0);

/**
 * Solve distance constraints using PBD/XPBD
 * One thread per constraint
 * 
 * Note: This uses parallel Jacobi iteration which may have race conditions
 * when multiple constraints affect the same particle. This is acceptable
 * as it will converge over multiple iterations. For better quality, use
 * graph coloring to separate constraints into independent sets.
 */
[numthreads(64, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    // Bounds check
    if (idx >= NumConstraints)
        return;
    
    // Load constraint data
    FDistanceConstraint constraint = ConstraintBuffer[idx];
    
    // Load particle data
    FClothParticle pA = PositionBuffer[constraint.ParticleA];
    FClothParticle pB = PositionBuffer[constraint.ParticleB];
    
    // Calculate current distance vector
    float3 delta = pB.Position - pA.Position;
    float currentLength = length(delta);
    
    // Skip if particles are at same position (avoid division by zero)
    if (currentLength < 1e-6f)
        return;
    
    // Calculate constraint error (difference from rest length)
    float error = currentLength - constraint.RestLength;
    
    // Direction vector
    float3 dir = delta / currentLength;
    
    // Calculate correction magnitude
    float invMassSum = pA.InvMass + pB.InvMass;
    
    // Skip if both particles are fixed
    if (invMassSum < 1e-6f)
        return;
    
    // Apply stiffness
    float stiffness = constraint.Stiffness * StretchStiffness;
    
    // XPBD modification for time-step independent behavior
    if (UseXPBD)
    {
        // Calculate compliance (inverse stiffness) adjusted for time step
        float alpha = 1.0f / (stiffness * DeltaTime * DeltaTime);
        
        // Modify effective inverse mass sum
        float effectiveInvMass = invMassSum + alpha;
        
        if (effectiveInvMass > 1e-6f)
        {
            stiffness = 1.0f / effectiveInvMass;
        }
    }
    
    // Calculate position correction
    float3 correction = dir * error * stiffness;
    
    // Distribute correction based on inverse mass
    // Lighter particles (higher invMass) move more
    float3 correctionA = -correction * (pA.InvMass / invMassSum);
    float3 correctionB = correction * (pB.InvMass / invMassSum);
    
    // Clamp corrections to prevent explosions
    float maxCorrection = constraint.RestLength * 0.5f; // Max 50% of rest length per iteration
    correctionA = ClampFloat3(correctionA, -maxCorrection, maxCorrection);
    correctionB = ClampFloat3(correctionB, -maxCorrection, maxCorrection);
    
    // Apply corrections
    pA.Position += correctionA;
    pB.Position += correctionB;
    
    // Write back (Note: potential race condition with Jacobi iteration)
    // This is acceptable as convergence happens over multiple iterations
    PositionBuffer[constraint.ParticleA] = pA;
    PositionBuffer[constraint.ParticleB] = pB;
}

/**
 * Alternative: Solve with atomic operations (slower but more accurate per iteration)
 * Uncomment and use if Jacobi iteration causes too much jitter
 */
/*
[numthreads(64, 1, 1)]
void SolveDistanceConstraintsAtomicCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    if (idx >= NumConstraints)
        return;
    
    FDistanceConstraint constraint = ConstraintBuffer[idx];
    
    // Read current positions
    float3 posA = PositionBuffer[constraint.ParticleA].Position;
    float3 posB = PositionBuffer[constraint.ParticleB].Position;
    float invMassA = PositionBuffer[constraint.ParticleA].InvMass;
    float invMassB = PositionBuffer[constraint.ParticleB].InvMass;
    
    float3 delta = posB - posA;
    float currentLength = length(delta);
    
    if (currentLength < 1e-6f)
        return;
    
    float error = currentLength - constraint.RestLength;
    float3 dir = delta / currentLength;
    
    float invMassSum = invMassA + invMassB;
    if (invMassSum < 1e-6f)
        return;
    
    float stiffness = constraint.Stiffness * StretchStiffness;
    float3 correction = dir * error * stiffness;
    
    float3 correctionA = -correction * (invMassA / invMassSum);
    float3 correctionB = correction * (invMassB / invMassSum);
    
    // Use InterlockedAdd for atomic position updates
    // Note: Requires special buffer setup for atomic operations
    // Implementation depends on engine's atomic buffer support
}
*/
