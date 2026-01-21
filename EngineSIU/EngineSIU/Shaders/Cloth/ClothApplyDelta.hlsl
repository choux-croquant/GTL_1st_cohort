#include "ClothCommon.hlsli"

StructuredBuffer<FClothParticle> PositionRead : register(t0);

RWStructuredBuffer<int3> PositionDelta  : register(u0);
RWStructuredBuffer<int>  PositionWeight : register(u1);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u2);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u3);

static const float kScale = 1000.0f;

[numthreads(64, 1, 1)]
void ApplyConstraintDeltasCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles) return;

    FClothParticle p = PositionRead[i];
    FClothVelocity velocity = VelocityBuffer[i];

    if (p.InvMass == 0.0f)
    {
        PositionWrite[i] = p;
        PositionDelta[i] = int3(0, 0, 0);
        PositionWeight[i] = 0;
        return;
    }
    int w = PositionWeight[i];
    float3 avgDelta = (float3(PositionDelta[i]) / kScale) / max(1, w);
    float3 delta = (w > 0) ? avgDelta : float3(0, 0, 0);

    p.Position += delta;
    PositionWrite[i] = p;
    
    velocity.Velocity = delta / DeltaTime;
    VelocityBuffer[i] = velocity;

    PositionDelta[i] = int3(0, 0, 0);
    PositionWeight[i] = 0;
}
