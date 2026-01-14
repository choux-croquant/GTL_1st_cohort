#include "ClothCommon.hlsli"

// Read-only
StructuredBuffer<FClothParticle>  PositionRead        : register(t0);
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t1);

RWStructuredBuffer<float3> PositionDelta : register(u0);
RWStructuredBuffer<float>  PositionWeight : register(u1);

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

    /*InterlockedAdd(PositionDelta[iA].x, correctionA.x);
    InterlockedAdd(PositionDelta[iA].y, correctionA.y);
    InterlockedAdd(PositionDelta[iA].z, correctionA.z);
    InterlockedAdd(PositionWeight[iA], 1.0f);

    InterlockedAdd(PositionDelta[iB].x, correctionB.x);
    InterlockedAdd(PositionDelta[iB].y, correctionB.y);
    InterlockedAdd(PositionDelta[iB].z, correctionB.z);
    InterlockedAdd(PositionWeight[iB], 1.0f);*/
}
