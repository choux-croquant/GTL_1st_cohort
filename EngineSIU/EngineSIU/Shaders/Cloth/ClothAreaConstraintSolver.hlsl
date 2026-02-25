/**
 * Cloth Area Constraint Solver - XPBD Edition
 * 
 * Preserves triangle area to resist in-plane stretching/compression.
 * Complements distance constraints by preventing triangle collapse or excessive expansion.
 * 
 * CONSTRAINT FORMULATION:
 * - Constraint: C = current_area - rest_area
 * - current_area = 0.5 * dot(cross(e1, e2), rest_normal)
 * - e1 = p1 - p0, e2 = p2 - p0
 * 
 * GRADIENTS:
 * - grad_0 = -grad_1 - grad_2
 * - grad_1 = 0.5 * cross(rest_normal, e2)
 * - grad_2 = 0.5 * cross(e1, rest_normal)
 * 
 * Based on:
 * - Macklin et al. "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
 * - Reference implementation from PhysixStudio solve_area.comp
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Read predicted positions
StructuredBuffer<float> InvMass : register(t1);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t2);

// Read-write buffers
RWStructuredBuffer<FAreaConstraint> AreaConstraints : register(u0);  // Read-write for XPBD lambda
RWStructuredBuffer<int3> PositionDelta : register(u1);
RWStructuredBuffer<int> PositionWeight : register(u2);

static const float kScale = 10000.0f;  // Fixed-point scale for position deltas
static const float EPSILON = 1e-6f;

[numthreads(256, 1, 1)]
void SolveAreaConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumAreaConstraints) return;

    // Load constraint (will write back lambda if XPBD)
    FAreaConstraint constraint = AreaConstraints[idx];

    // Load particle indices
    uint i0 = constraint.ParticleA;
    uint i1 = constraint.ParticleB;
    uint i2 = constraint.ParticleC;

    // Load inverse masses
    float w0 = InvMass[i0];
    float w1 = InvMass[i1];
    float w2 = InvMass[i2];

    // Early out if all particles are fixed
    float wSum = w0 + w1 + w2;
    if (wSum < EPSILON) return;

    // Read predicted positions
    float3 p0 = PredictedRead[i0].Position;
    float3 p1 = PredictedRead[i1].Position;
    float3 p2 = PredictedRead[i2].Position;

    float3 e1 = p1 - p0;
    float3 e2 = p2 - p0;

    float3 crossProduct = cross(e1, e2);
    float currentArea = 0.5f * dot(crossProduct, constraint.RestNormal);

    float C = currentArea - constraint.RestArea;

    // Early out if constraint is satisfied
    if (abs(C) < EPSILON) return;

    float3 grad1 = 0.5f * cross(constraint.RestNormal, e2);
    float3 grad2 = 0.5f * cross(e1, constraint.RestNormal);
    float3 grad0 = -grad1 - grad2;

    float denom = w0 * dot(grad0, grad0) +
                  w1 * dot(grad1, grad1) +
                  w2 * dot(grad2, grad2);

    // Compliance term: alpha_tilde = compliance / dt^2
    float alphaTilde = constraint.Compliance / (DeltaTime * DeltaTime + EPSILON);
    denom += alphaTilde;

    if (denom < EPSILON) return;

    // deltaLambda = -(C + alpha_tilde * lambda_old) / denom
    float lambdaOld = constraint.Lambda;
    float deltaLambda = -(C + alphaTilde * lambdaOld) / denom;

    // Apply global area stiffness multiplier
    deltaLambda *= AreaStiffness;

    // Update lambda (warm start for next iteration/frame)
    float lambdaNew = lambdaOld + deltaLambda;
    AreaConstraints[idx].Lambda = lambdaNew;

    // deltap_i = w_i * deltaLambda * grad_i
    float3 corr0 = w0 * deltaLambda * grad0;
    float3 corr1 = w1 * deltaLambda * grad1;
    float3 corr2 = w2 * deltaLambda * grad2;

    int3 delta0Int = int3(corr0 * kScale);
    int3 delta1Int = int3(corr1 * kScale);
    int3 delta2Int = int3(corr2 * kScale);

    InterlockedAdd(PositionDelta[i0].x, delta0Int.x);
    InterlockedAdd(PositionDelta[i0].y, delta0Int.y);
    InterlockedAdd(PositionDelta[i0].z, delta0Int.z);
    InterlockedAdd(PositionWeight[i0], 1);

    InterlockedAdd(PositionDelta[i1].x, delta1Int.x);
    InterlockedAdd(PositionDelta[i1].y, delta1Int.y);
    InterlockedAdd(PositionDelta[i1].z, delta1Int.z);
    InterlockedAdd(PositionWeight[i1], 1);

    InterlockedAdd(PositionDelta[i2].x, delta2Int.x);
    InterlockedAdd(PositionDelta[i2].y, delta2Int.y);
    InterlockedAdd(PositionDelta[i2].z, delta2Int.z);
    InterlockedAdd(PositionWeight[i2], 1);
}
