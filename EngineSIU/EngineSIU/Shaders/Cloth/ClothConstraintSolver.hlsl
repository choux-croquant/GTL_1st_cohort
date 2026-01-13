/**
 * Cloth Constraint Solver Compute Shader
 * Solves distance constraints using Position-Based Dynamics (PBD) or XPBD
 * This shader implements parallel Jacobi iteration
 */

#include "ClothCommon.hlsli"

// Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionBuffer : register(u0);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u1);
//RWStructuredBuffer<FDistanceConstraint> ConstraintBufferRW : register(u2);
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
    if (idx >= NumConstraints) return;

    FDistanceConstraint constraint = ConstraintBuffer[idx];

    FClothParticle pA = PositionBuffer[constraint.ParticleA];
    FClothParticle pB = PositionBuffer[constraint.ParticleB];

    // ===== 추가: 속도 로드 =====
    FClothVelocity vA = VelocityBuffer[constraint.ParticleA];
    FClothVelocity vB = VelocityBuffer[constraint.ParticleB];

    float3 delta = pB.Position - pA.Position;
    float currentLength = length(delta);

    if (currentLength < 1e-6f) return;

    float error = currentLength - constraint.RestLength;
    float3 dir = delta / currentLength;

    float w1 = pA.InvMass;
    float w2 = pB.InvMass;
    float wSum = w1 + w2;

    if (wSum < 1e-6f) return;

    float stiffness = constraint.Stiffness * StretchStiffness;

    if (!UseXPBD)
    {
        float lambda = error * stiffness / wSum;
        float3 common = lambda * dir;

        float3 correctionA = -w1 * common;
        float3 correctionB = w2 * common;

        float effectiveStiffness = stiffness;

        if (w1 < 1e-6f)  // pA가 고정점
        {
            // 모든 보정을 pB에만 적용, 하지만 stiffness를 반반 나눔
            correctionA = float3(0, 0, 0);
            correctionB = -error * dir * effectiveStiffness * 0.5f;
        }
        else if (w2 < 1e-6f)  // pB가 고정점
        {
            correctionA = error * dir * effectiveStiffness * 0.5f;
            correctionB = float3(0, 0, 0);
        }

        float maxCorr = constraint.RestLength * 0.015f;
        correctionA = ClampFloat3(correctionA, -maxCorr, maxCorr);
        correctionB = ClampFloat3(correctionB, -maxCorr, maxCorr);

        pA.Position += correctionA;
        pB.Position += correctionB;

        // ===== 추가: 속도 업데이트 =====
        float correctionDamping = 1.0f - Damping;  // 같은 damping 적용
        vA.Velocity += correctionA / DeltaTime * correctionDamping;
        vB.Velocity += correctionB / DeltaTime * correctionDamping;
    }
    else
    {
        float alpha = constraint.Compliance;
        float lambda = constraint.Lambda;
        float alphaTilde = alpha / (DeltaTime * DeltaTime);
        float denom = wSum + alphaTilde;

        if (denom > 1e-6f)
        {
            float dLambda = (-error - alphaTilde * lambda) / denom;
            lambda += dLambda;
            constraint.Lambda = lambda;
        }

        float3 common = lambda * dir;
        float3 correctionA = -w1 * common;
        float3 correctionB = w2 * common;

        float maxCorr = constraint.RestLength * 0.1f;
        correctionA = ClampFloat3(correctionA, -maxCorr, maxCorr);
        correctionB = ClampFloat3(correctionB, -maxCorr, maxCorr);

        pA.Position += correctionA;
        pB.Position += correctionB;

        // ===== 추가: 속도 업데이트 =====
        float correctionDamping = 1.0f - Damping;
        vA.Velocity += correctionA / DeltaTime * correctionDamping;
        vB.Velocity += correctionB / DeltaTime * correctionDamping;
    }

    // Write back
    PositionBuffer[constraint.ParticleA] = pA;
    PositionBuffer[constraint.ParticleB] = pB;
    VelocityBuffer[constraint.ParticleA] = vA;  // 추가
    VelocityBuffer[constraint.ParticleB] = vB;  // 추가

    //ConstraintBufferRW[idx] = constraint;
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
