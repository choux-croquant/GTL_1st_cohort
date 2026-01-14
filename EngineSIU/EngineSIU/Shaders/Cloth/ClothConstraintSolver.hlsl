/**
 * Cloth Constraint Solver Compute Shader
 * Solves distance constraints using Position-Based Dynamics (PBD) or XPBD
 * This shader implements parallel Jacobi iteration
 */

#include "ClothCommon.hlsli"

// Input/Output buffers
//RWStructuredBuffer<FDistanceConstraint> ConstraintBufferRW : register(u2);
StructuredBuffer<FClothParticle>  PositionRead  : register(t0); // read-only
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t1);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u0);

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
    if (idx >= NumConstraints) return;

    FDistanceConstraint constraint = ConstraintBuffer[idx];

    // Load particle data
    FClothParticle pA = PositionRead[constraint.ParticleA];
    FClothParticle pB = PositionRead[constraint.ParticleB];

    // Calculate constraint error
    float3 delta = pB.Position - pA.Position;
    float currentLength = length(delta);

    // Skip if particles are too close (avoid division by zero)
    if (currentLength < 1e-6f) return;

    float error = currentLength - constraint.RestLength;
    float3 dir = delta / currentLength;

    // Get inverse masses
    float w1 = pA.InvMass;
    float w2 = pB.InvMass;
    float wSum = w1 + w2;

    // Skip if both particles are fixed
    if (wSum < 1e-6f) return;

    // Get effective stiffness
    float stiffness = constraint.Stiffness * StretchStiffness;

    // Standard PBD distance constraint solving
    float3 correctionA, correctionB;

    if (!UseXPBD)
    {
        // ====== CORRECTED PBD FORMULA ======
        // Standard PBD: Δp = -s * λ * w * ∇C
        // where λ = -C / (w_A + w_B)
        //       ∇C_A = -dir, ∇C_B = dir
        
        float lambda = -error / wSum;
        
        // Position corrections with proper inverse mass weighting
        correctionA = stiffness * lambda * w1 * (-dir);
        correctionB = stiffness * lambda * w2 * dir;
        
        // Optional: Limit correction magnitude for stability
        // Removed overly restrictive clamping - let PBD converge naturally
        // If needed for very high velocities, use reasonable limits:
        // float maxCorr = constraint.RestLength * 0.5f; // 50% per iteration
        // correctionA = ClampFloat3(correctionA, -maxCorr, maxCorr);
        // correctionB = ClampFloat3(correctionB, -maxCorr, maxCorr);
    }
    else
    {
        // XPBD (Extended Position-Based Dynamics)
        // Uses compliance and Lagrange multipliers
        float alpha = constraint.Compliance;
        float lambda = constraint.Lambda;
        float alphaTilde = alpha / (DeltaTime * DeltaTime);
        float denom = wSum + alphaTilde;

        if (denom > 1e-6f)
        {
            float dLambda = (-error - alphaTilde * lambda) / denom;
            lambda += dLambda;
            // Note: constraint.Lambda update won't persist unless using RW buffer
        }

        // Apply corrections with stiffness
        correctionA = stiffness * lambda * w1 * (-dir);
        correctionB = stiffness * lambda * w2 * dir;
    }

    // Update positions
    pA.Position += correctionA;
    pB.Position += correctionB;

    PositionWrite[constraint.ParticleA] = pA;
    PositionWrite[constraint.ParticleB] = pB;
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
