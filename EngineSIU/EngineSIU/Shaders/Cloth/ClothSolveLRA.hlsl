/**
 * Long Range Attachment Solver
 * Based on PhysixStudio solve_lra.comp
 * Soft attachment to K nearest anchor particles
 */

#include "ClothCommon.hlsli"

StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<float> InvMassBuffer : register(t1);
StructuredBuffer<uint> LRAIdsBuffer : register(t2);        // [NumParticles * K]
StructuredBuffer<float> LRADistancesBuffer : register(t3); // [NumParticles * K]
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t4);

RWStructuredBuffer<FClothParticle> PositionWrite : register(u0);

static const uint K = 2; // Number of anchors per particle
static const float EPSILON = 1e-8f;

[numthreads(256, 1, 1)]
void SolveLRAConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles) return;

    float invMass = InvMassBuffer[i];
    if (invMass == 0.0f) return; // Fixed particles skip

    float3 xi = PositionRead[i].Position;

    // Get instance parameters
    uint instanceID = PositionRead[i].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    if (params.IsActive == 0) return;

    // Process each of K anchors (PhysixStudio solve_lra.comp lines 15-29)
    for (uint k = 0; k < K; ++k)
    {
        uint idx = i * K + k;
        uint a = LRAIdsBuffer[idx];

        if (a == 0xFFFFFFFF) continue; // Invalid anchor

        float3 xa = PositionRead[a].Position; // Anchor position
        float r = LRADistancesBuffer[idx];    // Max allowed distance

        float3 dvec = xi - xa;
        float d = length(dvec);

        // If beyond max distance, project toward anchor
        if (d > r && d > EPSILON)
        {
            float3 x_proj = xa + (r / d) * dvec;

            // Blend with stiffness (PhysixStudio uses stiffness parameter)
            float lraStiffness = LongRangeStretchiness; // Or pass via instance params
            xi = lerp(xi, x_proj, lraStiffness);
        }
    }

    // DIRECT write (after processing all anchors)
    FClothParticle newP = PositionRead[i];
    newP.Position = xi;
    PositionWrite[i] = newP;
}
