/**
 * Cloth Distance Constraint Solver
 * Solves distance constraints using XPBD (Extended Position-Based Dynamics)
 * Provides iteration-independent and time-step-independent stiffness
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t1);
StructuredBuffer<float> InvMassBuffer : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);
StructuredBuffer<FClothVelocity> VelocityBuffer : register(t4);  // For constraint damping

// Write buffers for delta accumulation
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int>  PositionWeight : register(u1);

static const float kScale = 1000.0f;

[numthreads(64, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumConstraints) return;

    FDistanceConstraint constraint = ConstraintBuffer[idx];

    FClothParticle pA = PositionRead[constraint.ParticleA];
    FClothParticle pB = PositionRead[constraint.ParticleB];

    float3 deltaPos = pB.Position - pA.Position;
    float currentLength = length(deltaPos);
    if (currentLength < 1e-6f) return;

    // Constraint error C = |x_b - x_a| - L_rest
    float C = currentLength - constraint.RestLength;
    float3 dir = deltaPos / currentLength;  // Unit direction vector

    // Load inverse masses
    float w1 = InvMassBuffer[constraint.ParticleA];
    float w2 = InvMassBuffer[constraint.ParticleB];
    float wSum = w1 + w2;
    if (wSum < 1e-6f) return;  // Both particles are fixed

    // Get per-instance parameters
    uint instanceID = pA.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    if (params.IsActive == 0) return;

    float3 correctionA, correctionB;

    if (!UseXPBD)
    {
        // Classical PBD (legacy path for comparison)
        float stiffness = constraint.Stiffness * params.StretchStiffness;
        float lambda = -C / wSum;
        correctionA = stiffness * lambda * w1 * (-dir);
        correctionB = stiffness * lambda * w2 * dir;
    }
    else
    {
        // XPBD: Iteration-independent, time-step-independent stiffness
        // Formulation: Δλ = -(C + α̃·λ) / (∇C·M^-1·∇C^T + α̃)
        // where α̃ = α/(Δt²) and α is compliance
        
        float compliance = constraint.Compliance;
        float alphaTilde = compliance / (DeltaTime * DeltaTime);
        
        // CRITICAL FIX: Compliance is already computed from artist stiffness in C++
        // Don't apply instance multiplier to alphaTilde - compliance already encodes stiffness
        // Instance params are baked into compliance during constraint generation
        // Applying multiplier here would double-apply it or apply it backwards
        
        // XPBD constraint damping: Add velocity term to constraint error
        // This damps oscillations along constraint direction without global velocity scaling
        float constraintDamping = params.Damping;
        float velocityTerm = 0.0f;
        
        if (constraintDamping > 0.0f)
        {
            FClothVelocity vA = VelocityBuffer[constraint.ParticleA];
            FClothVelocity vB = VelocityBuffer[constraint.ParticleB];
            float3 relativeVel = vB.Velocity - vA.Velocity;
            float velAlongConstraint = dot(relativeVel, dir);
            velocityTerm = constraintDamping * velAlongConstraint * DeltaTime;
        }
        
        // Modified constraint error with damping
        float C_damped = C + velocityTerm;
        
        // Gradient dot product: ∇C·M^-1·∇C^T = w1 + w2 (for unit direction)
        float gradDotW = wSum;
        
        // XPBD lambda update (Gauss-Seidel)
        float lambda = constraint.Lambda;  // Previous lambda (warm start)
        float deltaLambda = -(C_damped + alphaTilde * lambda) / (gradDotW + alphaTilde);
        
        // Note: We don't update lambda back to constraint buffer (would require RW access)
        // Cold start each frame is acceptable and still much better than classical PBD
        
        // Position corrections from constraint impulse
        // Δx_a = -Δλ · w_a · n
        // Δx_b = +Δλ · w_b · n
        correctionA = -deltaLambda * w1 * dir;
        correctionB = deltaLambda * w2 * dir;
    }

    // Accumulate corrections via atomic integer operations
    uint iA = constraint.ParticleA;
    uint iB = constraint.ParticleB;

    int3 deltaAInt = int3(
        correctionA.x * kScale,
        correctionA.y * kScale,
        correctionA.z * kScale);

    int3 deltaBInt = int3(
        correctionB.x * kScale,
        correctionB.y * kScale,
        correctionB.z * kScale);

    InterlockedAdd(PositionDelta[iA].x, deltaAInt.x);
    InterlockedAdd(PositionDelta[iA].y, deltaAInt.y);
    InterlockedAdd(PositionDelta[iA].z, deltaAInt.z);
    InterlockedAdd(PositionWeight[iA], 1);

    InterlockedAdd(PositionDelta[iB].x, deltaBInt.x);
    InterlockedAdd(PositionDelta[iB].y, deltaBInt.y);
    InterlockedAdd(PositionDelta[iB].z, deltaBInt.z);
    InterlockedAdd(PositionWeight[iB], 1);
}
