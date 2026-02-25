/**
 * Cloth Distance Constraint Solver
 * Solves distance constraints using Position-Based Dynamics (PBD) or XPBD
 *
 * XPBD (Extended Position-Based Dynamics):
 * - Uses compliance parameter for stiffness-independent behavior
 * - Accumulates Lagrange multipliers (lambda) for temporal coherence
 * - Time-step and iteration-count independent stiffness
 *
 * PBD (legacy path):
 * - Direct stiffness scaling of constraint violation
 * - Simple but sensitive to time step and iteration count
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Read predicted positions
StructuredBuffer<float> InvMass : register(t1);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t2);

// Write buffers
RWStructuredBuffer<FDistanceConstraint> Constraints : register(u0);  // NEW: Read-write for XPBD lambda
RWStructuredBuffer<int3> PositionDelta : register(u1);
RWStructuredBuffer<int>  PositionWeight : register(u2);

static const float kScale = 10000.0f;  // Fixed-point scale for position deltas
static const float EPSILON = 1e-6f;

[numthreads(256, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumConstraints) return;

    // Load constraint (will write back lambda if XPBD)
    FDistanceConstraint constraint = Constraints[idx];

    // Load particles
    uint i0 = constraint.ParticleA;
    uint i1 = constraint.ParticleB;
    
    // Read predicted positions
    float3 p0 = PredictedRead[i0].Position;
    float3 p1 = PredictedRead[i1].Position;

    // Compute constraint violation: C = ||p1 - p0|| - restLength
    float3 diff = p1 - p0;
    float dist = length(diff);
    float C = dist - constraint.RestLength;
    
    // Early out for degenerate or satisfied constraints
    if (abs(C) < EPSILON || dist < EPSILON) return;

    // Constraint gradient direction (normalized)
    float3 gradC = diff / dist;
    
    // Inverse masses
    float w0 = InvMass[i0];
    float w1 = InvMass[i1];
    float wSum = w0 + w1;
    
    if (wSum < EPSILON) return;

    // Per-instance stretch stiffness (both particles belong to the same instance)
    uint instanceID = PredictedRead[i0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];

    float3 corr0, corr1;
    
    if (UseXPBD != 0)
    {
        // === XPBD PATH ===
        // Uses compliance-based formulation for time-step independent stiffness
        
        // Compute alpha_tilde = compliance / (dt^2)
        // Lower compliance = stiffer constraint
        float alphaTilde = constraint.Compliance / (DeltaTime * DeltaTime + EPSILON);
        
        // XPBD update: Δλ = -(C + α_tilde * λ) / (wSum + α_tilde)
        float deltaLambda = -(C + alphaTilde * constraint.Lambda) / (wSum + alphaTilde);
        
        // Update lambda (warm start for next iteration/frame)
        constraint.Lambda += deltaLambda;
        
        // Write back lambda to constraint buffer
        Constraints[idx].Lambda = constraint.Lambda;
        
        // Compute position corrections: Δp = w * Δλ * ∇C
        corr0 = -w0 * deltaLambda * gradC;
        corr1 = +w1 * deltaLambda * gradC;
    }
    else
    {
        // === PBD PATH ===
        // Direct stiffness scaling - simple but time-step dependent
        
        // Combine stiffness: global (constant buffer), per-instance, per-constraint
        float stiffness = clamp(StretchStiffness * params.StretchStiffness * constraint.Stiffness, 0.0f, 1.0f);

        // Compute corrections: Δp = (C * stiffness / wSum) * ∇C
        float3 corr = (C * stiffness / wSum) * gradC;
        corr0 = corr * w0;
        corr1 = -corr * w1;
    }
    
    int3 delta0Int = int3(corr0 * kScale);
    int3 delta1Int = int3(corr1 * kScale);

    InterlockedAdd(PositionDelta[i0].x, delta0Int.x);
    InterlockedAdd(PositionDelta[i0].y, delta0Int.y);
    InterlockedAdd(PositionDelta[i0].z, delta0Int.z);
    InterlockedAdd(PositionWeight[i0], 1);

    InterlockedAdd(PositionDelta[i1].x, delta1Int.x);
    InterlockedAdd(PositionDelta[i1].y, delta1Int.y);
    InterlockedAdd(PositionDelta[i1].z, delta1Int.z);
    InterlockedAdd(PositionWeight[i1], 1);
}
