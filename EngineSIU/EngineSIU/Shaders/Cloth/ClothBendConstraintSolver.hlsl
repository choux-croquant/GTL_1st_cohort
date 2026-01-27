/**
 * Cloth Bend Constraint Solver
 * Solves dihedral angle bending constraints using proper gradient derivation
 *
 * VELVET PATTERN (Single Working Buffer):
 * - Reads from PredictedBuffer (working buffer)
 * - Accumulates deltas to PositionDelta/PositionWeight buffers
 * - XPBD compliance support
 * - Uses same InstanceParams pattern as ClothConstraintSolver.hlsl
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Read predicted positions
StructuredBuffer<FBendConstraint> BendConstraints : register(t1);
StructuredBuffer<float> InvMass : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

// Write buffers
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-6f;

[numthreads(256, 1, 1)]
void SolveBendConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= NumBendConstraints) return;

    FBendConstraint constraint = BendConstraints[id];

    // Load particle indices (Velvet naming)
    uint idx0 = constraint.ParticleA;
    uint idx1 = constraint.ParticleB;
    uint idx2 = constraint.ParticleC;
    uint idx3 = constraint.ParticleD;
    float restAngle = constraint.RestAngle;

    // Load inverse masses
    float w0 = InvMass[idx0];
    float w1 = InvMass[idx1];
    float w2 = InvMass[idx2];
    float w3 = InvMass[idx3];

    // Load predicted positions
    float3 p0 = PredictedRead[idx0].Position;
    float3 p1 = PredictedRead[idx1].Position;
    float3 p2 = PredictedRead[idx2].Position;
    float3 p3 = PredictedRead[idx3].Position;

    // Compute shared edge e = p3 - p2 (Velvet formulation)
    float3 e = p3 - p2;
    float elen = length(e);
    if (elen < EPSILON) return;  // Degenerate edge
    
    float invElen = 1.0f / elen;

    // Compute normals (NOT normalized yet)
    // n1 = cross(p2 - p0, p3 - p0)
    // n2 = cross(p3 - p1, p2 - p1)
    float3 n1 = cross(p2 - p0, p3 - p0);
    float3 n2 = cross(p3 - p1, p2 - p1);
    
    float n1LenSq = dot(n1, n1);
    float n2LenSq = dot(n2, n2);
    
    if (n1LenSq < EPSILON || n2LenSq < EPSILON) return;  // Degenerate triangle
    
    // Normalize by squared length (Velvet's approach)
    // This is part of the gradient formulation
    n1 /= n1LenSq;
    n2 /= n2LenSq;

    // Compute gradients (Velvet formulation)
    float3 d0 = elen * n1;
    float3 d1 = elen * n2;
    float3 d2 = dot(p0 - p3, e) * invElen * n1 + dot(p1 - p3, e) * invElen * n2;
    float3 d3 = dot(p2 - p0, e) * invElen * n1 + dot(p2 - p1, e) * invElen * n2;

    // Compute current dihedral angle
    // Need normalized normals for angle calculation
    float3 n1Norm = normalize(cross(p2 - p0, p3 - p0));
    float3 n2Norm = normalize(cross(p3 - p1, p2 - p1));
    float dotProduct = clamp(dot(n1Norm, n2Norm), -1.0f, 1.0f);
    float phi = acos(dotProduct);

    // Compute lambda denominator (sum of weighted gradient magnitudes)
    float lambda_denom =
        w0 * dot(d0, d0) +
        w1 * dot(d1, d1) +
        w2 * dot(d2, d2) +
        w3 * dot(d3, d3);

    if (lambda_denom < EPSILON) return;

    // Per-instance bend stiffness (all particles in a bend constraint belong to the same instance)
    uint instanceID = PredictedRead[idx0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];

    // Combine stiffness: global (constant buffer), per-instance, per-constraint
    float combinedStiffness = clamp(BendStiffness * params.BendStiffness * constraint.Stiffness, 0.0f, 1.0f);

    // XPBD compliance (Velvet pattern)
    // If UseXPBD is enabled, use compliance; otherwise use PBD with stiffness
    float xpbd_compliance = 0.0f;
    if (UseXPBD > 0)
    {
        // XPBD mode: use constraint compliance scaled by dt^2
        float bendCompliance = constraint.Compliance;
        xpbd_compliance = bendCompliance / (DeltaTime * DeltaTime);
    }
    
    // Compute lambda (angle error divided by denominator)
    float angleError = phi - restAngle;
    float lambda = angleError / (lambda_denom + xpbd_compliance);

    // Apply stiffness scaling (for PBD mode or as additional control for XPBD)
    lambda *= combinedStiffness;

    // Determine sign based on normal orientation
    if (dot(cross(n1Norm, n2Norm), e) > 0.0f)
        lambda = -lambda;

    // Compute corrections
    /*float3 corr0 = -w0 * lambda * d0;
    float3 corr1 = -w1 * lambda * d1;
    float3 corr2 = -w2 * lambda * d2;
    float3 corr3 = -w3 * lambda * d3;*/
    float3 corr0 = -w0 * lambda * d0;
    float3 corr1 = w1 * lambda * d1;
    float3 corr2 = -w2 * lambda * d2;
    float3 corr3 = w3 * lambda * d3;

    // Atomic accumulation (scaled to int)
    int3 delta0 = int3(corr0 * kScale);
    int3 delta1 = int3(corr1 * kScale);
    int3 delta2 = int3(corr2 * kScale);
    int3 delta3 = int3(corr3 * kScale);

    InterlockedAdd(PositionDelta[idx0].x, delta0.x);
    InterlockedAdd(PositionDelta[idx0].y, delta0.y);
    InterlockedAdd(PositionDelta[idx0].z, delta0.z);
    InterlockedAdd(PositionWeight[idx0], 1);

    InterlockedAdd(PositionDelta[idx1].x, delta1.x);
    InterlockedAdd(PositionDelta[idx1].y, delta1.y);
    InterlockedAdd(PositionDelta[idx1].z, delta1.z);
    InterlockedAdd(PositionWeight[idx1], 1);

    InterlockedAdd(PositionDelta[idx2].x, delta2.x);
    InterlockedAdd(PositionDelta[idx2].y, delta2.y);
    InterlockedAdd(PositionDelta[idx2].z, delta2.z);
    InterlockedAdd(PositionWeight[idx2], 1);

    InterlockedAdd(PositionDelta[idx3].x, delta3.x);
    InterlockedAdd(PositionDelta[idx3].y, delta3.y);
    InterlockedAdd(PositionDelta[idx3].z, delta3.z);
    InterlockedAdd(PositionWeight[idx3], 1);
}
