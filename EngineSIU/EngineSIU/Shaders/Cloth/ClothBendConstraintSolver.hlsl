/**
 * Cloth Bend Constraint Solver - Dihedral Angle XPBD
 * Based on PhysixStudio solve_bend.comp
 * Uses isometric bending model with complex gradients
 * 
 * Phase 5: Upgraded to PhysixStudio's exact formulation
 */

#include "ClothCommon.hlsli"

StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<FBendConstraint> BendBuffer : register(t1);
StructuredBuffer<float> InvMassBuffer : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);
RWStructuredBuffer<FBendConstraint> BendWrite : register(u2);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-8f;

bool isFinite_f(float x) { return !isnan(x) && !isinf(x); }
float safeLen(float3 v) { float l = length(v); return (l < EPSILON) ? EPSILON : l; }

void accumulate_delta(uint i, float3 corr)
{
    int3 corrInt = int3(corr * kScale);
    InterlockedAdd(PositionDelta[i].x, corrInt.x);
    InterlockedAdd(PositionDelta[i].y, corrInt.y);
    InterlockedAdd(PositionDelta[i].z, corrInt.z);
    InterlockedAdd(PositionWeight[i], 1);
}

[numthreads(256, 1, 1)]
void SolveBendConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint gid = DTid.x;
    if (gid >= NumBendConstraints) return;

    FBendConstraint bc = BendBuffer[gid];

    uint i0 = bc.ParticleA;
    uint i1 = bc.ParticleB;
    uint i2 = bc.ParticleC;
    uint i3 = bc.ParticleD;
    float rest = bc.RestAngle;
    float lambda_old = bc.Lambda;

    float w0 = InvMassBuffer[i0];
    float w1 = InvMassBuffer[i1];
    float w2 = InvMassBuffer[i2];
    float w3 = InvMassBuffer[i3];

    if (w0 + w1 + w2 + w3 == 0.0f)
    {
        BendWrite[gid].Lambda = 0.0f;
        return;
    }

    float3 x0 = PositionRead[i0].Position;
    float3 x1 = PositionRead[i1].Position;
    float3 x2 = PositionRead[i2].Position;
    float3 x3 = PositionRead[i3].Position;

    // Get instance parameters
    uint instanceID = PositionRead[i0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    if (params.IsActive == 0) return;

    // Compute current dihedral angle (PhysixStudio solve_bend.comp lines 39-48)
    float3 e = x1 - x0;
    float el = safeLen(e);
    float3 ehat = e / el;

    float3 n1 = normalize(cross(x1 - x0, x2 - x0));
    float3 n2 = normalize(cross(x1 - x0, x3 - x0));

    float c = clamp(dot(n1, n2), -1.0f, 1.0f);
    float s = dot(ehat, cross(n1, n2));
    float phi = atan2(s, c); // Current dihedral angle

    float C = phi - rest; // Constraint violation

    // Gradients (isometric bending model - PhysixStudio lines 52-59)
    float A1 = safeLen(cross(x1 - x0, x2 - x0));
    float A2 = safeLen(cross(x1 - x0, x3 - x0));

    float3 q2 = (cross(x1 - x0, n2) + cross(n1, x1 - x0) * c) / A1;
    float3 q3 = (cross(x1 - x0, n1) + cross(n2, x1 - x0) * c) / A2;
    float3 q1 = -(cross(x2 - x0, n2) + cross(n1, x2 - x0) * c) / A1 - (cross(x3 - x0, n1) + cross(n2, x3 - x0) * c) / A2;
    float3 q0 = -q1 - q2 - q3;

    // XPBD solve
    float dt = DeltaTime;
    float alpha_tilde = ComplianceBend / (dt * dt);

    float wsum_grad = w0 * dot(q0, q0) +
                      w1 * dot(q1, q1) +
                      w2 * dot(q2, q2) +
                      w3 * dot(q3, q3);

    float denom = wsum_grad + alpha_tilde;
    if (denom < EPSILON)
        denom = EPSILON;

    float rhs = C + alpha_tilde * lambda_old;
    float dlambda = -rhs / denom;

    // Apply stiffness multiplier
    float newLambda = lambda_old + dlambda * params.BendStiffness;
    if (!isFinite_f(newLambda))
        newLambda = 0.0f;

    BendWrite[gid].Lambda = newLambda;

    // Accumulate deltas (PhysixStudio solve_bend.comp lines 81-89)
    float3 corr0 = w0 * dlambda * q0;
    float3 corr1 = w1 * dlambda * q1;
    float3 corr2 = w2 * dlambda * q2;
    float3 corr3 = w3 * dlambda * q3;

    if (w0 > 0.0f)
        accumulate_delta(i0, corr0);
    if (w1 > 0.0f)
        accumulate_delta(i1, corr1);
    if (w2 > 0.0f)
        accumulate_delta(i2, corr2);
    if (w3 > 0.0f)
        accumulate_delta(i3, corr3);
}
