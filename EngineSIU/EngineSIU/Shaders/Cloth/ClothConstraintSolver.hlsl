/**
 * Cloth Distance Constraint Solver
 * Solves distance constraints using Position-Based Dynamics (PBD/XPBD)
 * Now supports batched simulation with per-instance parameters
 *
 * PHASE 2 UPDATE (Velvet-inspired):
 * - Corrected formulation to exactly match Velvet's SolveStretch_Kernel
 * - Clearer variable naming matching Velvet
 * - Stiffness handled correctly as per-instance multiplier
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t1);
StructuredBuffer<float> InvMassBuffer : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

// Write buffers for delta accumulation
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int>  PositionWeight : register(u1);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-6f;

[numthreads(64, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumConstraints) return;

    FDistanceConstraint constraint = ConstraintBuffer[idx];

    // Load particles (Velvet naming: idx1, idx2)
    uint idx1 = constraint.ParticleA;
    uint idx2 = constraint.ParticleB;
    
    FClothParticle p1 = PositionRead[idx1];
    FClothParticle p2 = PositionRead[idx2];

    // Velvet formulation: diff = predicted[idx1] - predicted[idx2]
    float3 diff = p1.Position - p2.Position;
    float distance = length(diff);
    float expectedDistance = constraint.RestLength;
    
    // Early out for degenerate constraints
    if (distance < EPSILON) return;

    // Load inverse masses
    float w1 = InvMassBuffer[idx1];
    float w2 = InvMassBuffer[idx2];
    float denom = w1 + w2;
    if (denom < EPSILON) return;

    // Get per-instance stiffness multiplier
    uint instanceID = p1.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    // Skip if instance is inactive
    if (params.IsActive == 0) return;

    // Check if constraint should be enforced (Velvet check)
    if (distance != expectedDistance && denom > 0)
    {
        // Gradient (normalized direction from p2 to p1)
        float3 gradient = diff / (distance + EPSILON);
        
        // Compute lambda (Velvet formulation)
        // For PBD with compliance=0: lambda = C / (w1 + w2)
        // Where C = distance - expectedDistance (constraint violation)
        float lambda = (distance - expectedDistance) / denom;
        float3 common = lambda * gradient;
        
        // Compute corrections (Velvet pattern)
        float3 correction1 = -w1 * common;
        float3 correction2 =  w2 * common;
        
        // Apply per-instance stiffness as multiplier (not in Velvet's kernel, but in ours)
        // This allows per-instance material properties
        float stiffness = constraint.Stiffness * params.StretchStiffness;
        correction1 *= stiffness;
        correction2 *= stiffness;
        
        // Atomic accumulation (scaled to int for InterlockedAdd)
        int3 delta1Int = int3(correction1 * kScale);
        int3 delta2Int = int3(correction2 * kScale);

        InterlockedAdd(PositionDelta[idx1].x, delta1Int.x);
        InterlockedAdd(PositionDelta[idx1].y, delta1Int.y);
        InterlockedAdd(PositionDelta[idx1].z, delta1Int.z);
        InterlockedAdd(PositionWeight[idx1], 1);

        InterlockedAdd(PositionDelta[idx2].x, delta2Int.x);
        InterlockedAdd(PositionDelta[idx2].y, delta2Int.y);
        InterlockedAdd(PositionDelta[idx2].z, delta2Int.z);
        InterlockedAdd(PositionWeight[idx2], 1);
    }
}
