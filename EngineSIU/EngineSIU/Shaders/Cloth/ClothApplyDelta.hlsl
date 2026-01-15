#include "ClothCommon.hlsli"  

StructuredBuffer<FClothParticle> PositionRead : register(t0);
RWStructuredBuffer<float3> PositionDelta  : register(u0);
RWStructuredBuffer<float>  PositionWeight : register(u1);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u2);

[numthreads(64, 1, 1)]
void ApplyConstraintDeltasCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles) return;

    FClothParticle p = PositionRead[i];

    if (p.InvMass == 0.0f)
    {
        PositionWrite[i] = p;
        PositionDelta[i] = float3(0, 0, 0);
        PositionWeight[i] = 0.0f;
        return;
    }

    float w = PositionWeight[i];
    float3 delta = (w > 0.0f) ? (PositionDelta[i] / w) : float3(0, 0, 0);

    p.Position += delta;
    PositionWrite[i] = p;

    PositionDelta[i] = float3(0, 0, 0);
    PositionWeight[i] = 0.0f;
}
