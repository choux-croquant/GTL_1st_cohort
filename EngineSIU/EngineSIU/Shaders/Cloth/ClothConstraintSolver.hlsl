#include "ClothCommon.hlsli"

// Read-only
StructuredBuffer<FClothParticle>  PositionRead        : register(t0);
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t1);

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

    float error = currentLength - constraint.RestLength;
    float3 dir = deltaPos / currentLength;

    float w1 = pA.InvMass;
    float w2 = pB.InvMass;
    float wSum = w1 + w2;
    if (wSum < 1e-6f) return;

    float stiffness = constraint.Stiffness * StretchStiffness;

    float3 correctionA, correctionB;

    if (!UseXPBD)
    {
        float lambda = -error / wSum;
        correctionA = stiffness * lambda * w1 * (-dir);
        correctionB = stiffness * lambda * w2 * dir;
    }
    else
    {
        float alpha = constraint.Compliance;
        float lambda = constraint.Lambda;
        float alphaTilde = alpha / (DeltaTime * DeltaTime);
        float denom = wSum + alphaTilde;

        if (denom > 1e-6f)
        {
            float dLambda = (-error - alphaTilde * lambda) / denom;
            lambda += dLambda;
        }

        correctionA = stiffness * lambda * w1 * (-dir);
        correctionB = stiffness * lambda * w2 * dir;
    }

    // Atomic add delta
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
