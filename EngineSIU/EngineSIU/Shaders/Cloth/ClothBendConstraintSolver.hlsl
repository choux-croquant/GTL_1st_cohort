/**
 * Cloth Bend Constraint Solver
 * Solves bend constraints using dihedral angle constraints
 * Now supports batched simulation with per-instance parameters
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<FBendConstraint> BendConstraintBuffer : register(t1);
StructuredBuffer<float> InvMassBuffer : register(t2);  // NEW: Separate inverse mass buffer
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);  // NEW: Per-instance parameters

// Write buffers
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);

static const float kScale = 1000.0f;

[numthreads(64, 1, 1)]
void SolveBendConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumBendConstraints) return;

    FBendConstraint constraint = BendConstraintBuffer[idx];

    // Load particle positions
    FClothParticle pA = PositionRead[constraint.ParticleA];
    FClothParticle pB = PositionRead[constraint.ParticleB];
    FClothParticle pC = PositionRead[constraint.ParticleC];
    FClothParticle pD = PositionRead[constraint.ParticleD];

    // Calculate current dihedral angle
    float3 e = pB.Position - pA.Position;  // Shared edge
    float3 n1 = cross(pC.Position - pA.Position, e);  // Normal of tri 1
    float3 n2 = cross(e, pD.Position - pA.Position);  // Normal of tri 2

    float n1Len = length(n1);
    float n2Len = length(n2);

    if (n1Len < 1e-6f || n2Len < 1e-6f) return;  // Degenerate triangle

    n1 /= n1Len;
    n2 /= n2Len;

    float currentAngle = acos(clamp(dot(n1, n2), -1.0f, 1.0f));

    // Calculate error
    float angleError = currentAngle - constraint.RestAngle;

    // NEW: Get per-instance bend stiffness multiplier
    uint instanceID = pA.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    // Skip if instance is inactive
    if (params.IsActive == 0) return;
    
    // Apply per-instance bend stiffness multiplier
    float stiffness = constraint.Stiffness * params.BendStiffness;

    // NEW: Load inverse masses from separate buffer
    float w1 = InvMassBuffer[constraint.ParticleA];
    float w2 = InvMassBuffer[constraint.ParticleB];
    float w3 = InvMassBuffer[constraint.ParticleC];
    float w4 = InvMassBuffer[constraint.ParticleD];

    // Gradient magnitudes (approximate, full derivation is complex)
    float edgeLen = length(e);
    float k = -stiffness * angleError / (edgeLen * (w1 + w2 + w3 + w4) + 1e-6f);

    // Compute corrections (cross products give gradient directions)
    float3 corrA = k * w1 * cross(n2 - n1, e);
    float3 corrB = k * w2 * cross(n1 - n2, e);
    float3 corrC = k * w3 * n1 * edgeLen;
    float3 corrD = k * w4 * n2 * edgeLen;

    // Atomic accumulation (scaled integer)
    int3 deltaA = int3(corrA * kScale);
    int3 deltaB = int3(corrB * kScale);
    int3 deltaC = int3(corrC * kScale);
    int3 deltaD = int3(corrD * kScale);

    InterlockedAdd(PositionDelta[constraint.ParticleA].x, deltaA.x);
    InterlockedAdd(PositionDelta[constraint.ParticleA].y, deltaA.y);
    InterlockedAdd(PositionDelta[constraint.ParticleA].z, deltaA.z);
    InterlockedAdd(PositionWeight[constraint.ParticleA], 1);

    InterlockedAdd(PositionDelta[constraint.ParticleB].x, deltaB.x);
    InterlockedAdd(PositionDelta[constraint.ParticleB].y, deltaB.y);
    InterlockedAdd(PositionDelta[constraint.ParticleB].z, deltaB.z);
    InterlockedAdd(PositionWeight[constraint.ParticleB], 1);

    InterlockedAdd(PositionDelta[constraint.ParticleC].x, deltaC.x);
    InterlockedAdd(PositionDelta[constraint.ParticleC].y, deltaC.y);
    InterlockedAdd(PositionDelta[constraint.ParticleC].z, deltaC.z);
    InterlockedAdd(PositionWeight[constraint.ParticleC], 1);

    InterlockedAdd(PositionDelta[constraint.ParticleD].x, deltaD.x);
    InterlockedAdd(PositionDelta[constraint.ParticleD].y, deltaD.y);
    InterlockedAdd(PositionDelta[constraint.ParticleD].z, deltaD.z);
    InterlockedAdd(PositionWeight[constraint.ParticleD], 1);
}
