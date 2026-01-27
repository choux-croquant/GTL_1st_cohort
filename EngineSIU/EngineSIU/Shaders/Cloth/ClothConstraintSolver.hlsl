/**
 * Cloth Distance Constraint Solver
 * Solves distance constraints using Position-Based Dynamics (PBD)
 *
 * VELVET PATTERN (Single Working Buffer):
 * - Reads from PredictedBuffer (working buffer)
 * - Accumulates deltas to PositionDelta/PositionWeight buffers
 * - PredictedBuffer modified in-place by ApplyDeltas shader
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Read predicted positions
StructuredBuffer<FDistanceConstraint> Constraints : register(t1);
StructuredBuffer<float> InvMass : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

// Write buffers for delta accumulation
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int>  PositionWeight : register(u1);

static const float kScale = 10000.0f;  // Fixed-point scale for position deltas
static const float EPSILON = 1e-6f;

[numthreads(256, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumConstraints) return;

    FDistanceConstraint constraint = Constraints[idx];

    // Load particles
    uint i0 = constraint.ParticleA;
    uint i1 = constraint.ParticleB;
    
    // Read predicted positions
    float3 p0 = PredictedRead[i0].Position;
    float3 p1 = PredictedRead[i1].Position;

    // Compute constraint violation
    float3 diff = p1 - p0;
    float dist = length(diff);
    float C = dist - constraint.RestLength;
    
    // Early out for degenerate or satisfied constraints
    if (abs(C) < EPSILON || dist < EPSILON) return;

    float3 dir = diff / dist;
    float w0 = InvMass[i0];
    float w1 = InvMass[i1];
    float wSum = w0 + w1;
    
    if (wSum < EPSILON) return;

    // Per-instance stretch stiffness (both particles belong to the same instance)
    uint instanceID = PredictedRead[i0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];

    // Combine stiffness: global (constant buffer), per-instance, per-constraint
    float stiffness = clamp(StretchStiffness * params.StretchStiffness * constraint.Stiffness, 0.0f, 1.0f);

    // Compute corrections (PBD formulation)
    // CRITICAL FIX: Remove negative sign - when C > 0 (stretched), corrections should pull particles together
    float3 corr = (C * stiffness) * dir / wSum;
    float3 corr0 = corr * w0;
    float3 corr1 = -corr * w1;
    
    // Atomic accumulation (scaled to int for InterlockedAdd)
    int3 delta0Int = int3(corr0 * kScale);
    int3 delta1Int = int3(corr1 * kScale);

    InterlockedAdd(PositionDelta[i0].x, delta0Int.x);
    InterlockedAdd(PositionDelta[i0].y, delta0Int.y);
    InterlockedAdd(PositionDelta[i0].z, delta0Int.z);
    InterlockedAdd(PositionWeight[i0], 1);  // Count constraints, not invMass sum

    InterlockedAdd(PositionDelta[i1].x, delta1Int.x);
    InterlockedAdd(PositionDelta[i1].y, delta1Int.y);
    InterlockedAdd(PositionDelta[i1].z, delta1Int.z);
    InterlockedAdd(PositionWeight[i1], 1);  // Count constraints, not invMass sum
}
