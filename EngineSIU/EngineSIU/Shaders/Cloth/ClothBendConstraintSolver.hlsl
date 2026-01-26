/**
 * Cloth Bend Constraint Solver
 * Solves dihedral angle bending constraints using proper gradient derivation
 * Now supports batched simulation with per-instance parameters
 * 
 * PHASE 2 COMPLETE REWRITE (Velvet-inspired):
 * - Proper gradient calculation matching Velvet's SolveBending_Kernel
 * - XPBD compliance support
 * - Accurate dihedral angle constraint solving
 * 
 * Based on: Velvet/VtClothSolverGPU.cu::SolveBending_Kernel (lines 117-189)
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<FBendConstraint> BendConstraintBuffer : register(t1);
StructuredBuffer<float> InvMassBuffer : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

// Write buffers
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-6f;

[numthreads(64, 1, 1)]
void SolveBendConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= NumBendConstraints) return;

    FBendConstraint constraint = BendConstraintBuffer[id];

    // Load particle indices (Velvet naming)
    uint idx0 = constraint.ParticleA;
    uint idx1 = constraint.ParticleB;
    uint idx2 = constraint.ParticleC;
    uint idx3 = constraint.ParticleD;
    float restAngle = constraint.RestAngle;

    // Load inverse masses
    float w0 = InvMassBuffer[idx0];
    float w1 = InvMassBuffer[idx1];
    float w2 = InvMassBuffer[idx2];
    float w3 = InvMassBuffer[idx3];

    // Load positions
    float3 p0 = PositionRead[idx0].Position;
    float3 p1 = PositionRead[idx1].Position;
    float3 p2 = PositionRead[idx2].Position;
    float3 p3 = PositionRead[idx3].Position;

    // Get instance parameters
    uint instanceID = PositionRead[idx0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    if (params.IsActive == 0) return;

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

    // XPBD compliance (Velvet pattern)
    // bendCompliance / deltaTime^2
    float bendCompliance = constraint.Compliance;
    float xpbd_bend = bendCompliance / (DeltaTime * DeltaTime);
    
    // Compute lambda
    float angleError = phi - restAngle;
    float lambda = angleError / (lambda_denom + xpbd_bend);

    // Determine sign based on normal orientation (Velvet check)
    if (dot(cross(n1Norm, n2Norm), e) > 0.0f)
        lambda = -lambda;

    // Apply per-instance stiffness (our addition for per-instance materials)
    lambda *= params.BendStiffness;

    // Compute corrections (Velvet pattern)
    float3 corr0 = -w0 * lambda * d0;
    float3 corr1 = -w1 * lambda * d1;
    float3 corr2 = -w2 * lambda * d2;
    float3 corr3 = -w3 * lambda * d3;

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
