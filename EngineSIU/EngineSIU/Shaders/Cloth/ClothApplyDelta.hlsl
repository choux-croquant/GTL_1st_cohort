#include "ClothCommon.hlsli"  

static const float kScale = 1000.0f;

StructuredBuffer<FClothParticle> PositionRead : register(t0);
RWStructuredBuffer<int3> PositionDelta  : register(u0);
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
    float w = float(PositionWeight[i]);
    float3 avgDelta = (float3(PositionDelta[i]) / kScale) / max(1, PositionWeight[i]);
    float3 delta = (w > 0.0f) ? avgDelta : float3(0, 0, 0);

    p.Position += delta;
    PositionWrite[i] = p;

    PositionDelta[i] = int3(0, 0, 0);
    PositionWeight[i] = 0;
}
