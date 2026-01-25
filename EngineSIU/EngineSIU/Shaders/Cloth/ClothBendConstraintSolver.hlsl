/**
 * Cloth Bend Constraint Solver
 * Solves bend constraints using dihedral angle with XPBD formulation
 * Provides iteration-independent bending stiffness
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

    // Calculate current dihedral angle between two triangles
    float3 e = pB.Position - pA.Position;  // Shared edge
    float eLen = length(e);
    if (eLen < 1e-6f) return;  // Degenerate edge
    
    float3 eNorm = e / eLen;

    // Triangle normals (unnormalized for now)
    float3 n1 = cross(pC.Position - pA.Position, e);  // Normal of triangle ABC
    float3 n2 = cross(e, pD.Position - pA.Position);  // Normal of triangle ABD

    float n1Len = length(n1);
    float n2Len = length(n2);

    if (n1Len < 1e-6f || n2Len < 1e-6f) return;  // Degenerate triangle

    // Normalized normals
    float3 n1Norm = n1 / n1Len;
    float3 n2Norm = n2 / n2Len;

    // Current dihedral angle
    float cosAngle = clamp(dot(n1Norm, n2Norm), -1.0f, 1.0f);
    float currentAngle = acos(cosAngle);

    // Constraint error: C = θ_current - θ_rest
    float C = currentAngle - constraint.RestAngle;

    // Get per-instance parameters
    uint instanceID = pA.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    if (params.IsActive == 0) return;

    // Load inverse masses
    float w1 = InvMassBuffer[constraint.ParticleA];
    float w2 = InvMassBuffer[constraint.ParticleB];
    float w3 = InvMassBuffer[constraint.ParticleC];
    float w4 = InvMassBuffer[constraint.ParticleD];

    float3 corrA, corrB, corrC, corrD;

    if (!UseXPBD)
    {
        // Classical PBD (legacy path)
        float stiffness = constraint.Stiffness * params.BendStiffness;
        float wSum = w1 + w2 + w3 + w4;
        float k = -stiffness * C / (eLen * wSum + 1e-6f);

        // Approximate gradients (simplified)
        corrA = k * w1 * cross(n2Norm - n1Norm, e);
        corrB = k * w2 * cross(n1Norm - n2Norm, e);
        corrC = k * w3 * n1Norm * eLen;
        corrD = k * w4 * n2Norm * eLen;
    }
    else
    {
        // XPBD: Accurate gradients and compliance-based formulation
        // Gradients from Bergou et al. 2008 (simplified version)
        
        // ∂θ/∂x_i for dihedral angle constraint
        float3 gradA = (cross(eNorm, n2Norm) / n1Len + cross(n1Norm, eNorm) / n2Len) / eLen;
        float3 gradB = -(cross(eNorm, n2Norm) / n1Len + cross(n1Norm, eNorm) / n2Len) / eLen;
        float3 gradC = cross(eNorm, n1Norm) / n1Len / eLen;
        float3 gradD = -cross(eNorm, n2Norm) / n2Len / eLen;

        // Weighted gradient dot product: ∇C·M^-1·∇C^T
        float gradDotW = dot(gradA, gradA) * w1 + dot(gradB, gradB) * w2
                       + dot(gradC, gradC) * w3 + dot(gradD, gradD) * w4;

        // XPBD compliance (already baked with instance params in C++)
        float compliance = constraint.Compliance;
        float alphaTilde = compliance / (DeltaTime * DeltaTime);
        
        // CRITICAL FIX: Don't apply instance multiplier here - compliance already includes it
        // Instance params were baked into compliance during constraint generation

        // XPBD lambda update
        float lambda = constraint.Lambda;
        float deltaLambda = -(C + alphaTilde * lambda) / (gradDotW + alphaTilde);

        // Position corrections from constraint impulse
        corrA = deltaLambda * w1 * gradA;
        corrB = deltaLambda * w2 * gradB;
        corrC = deltaLambda * w3 * gradC;
        corrD = deltaLambda * w4 * gradD;
    }

    // Accumulate corrections via atomic integer operations
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
