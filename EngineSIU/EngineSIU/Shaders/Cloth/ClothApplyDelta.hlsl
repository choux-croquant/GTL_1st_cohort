/**
 * Cloth Apply Constraint Deltas
 * Applies accumulated constraint corrections to particle positions
 */

#include "ClothCommon.hlsli"

// In-place modification of predicted buffer
RWStructuredBuffer<FClothParticle> PredictedBuffer : register(u0);  // In-place modification
RWStructuredBuffer<int3> PositionDelta : register(u1);
RWStructuredBuffer<int>  PositionWeight : register(u2);

static const float kScale = 10000.0f;  // Fixed-point scale for delta accumulation

[numthreads(256, 1, 1)]
void ApplyConstraintDeltasCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumParticles) return;

    int weightFixed = PositionWeight[idx];
    if (weightFixed > 0)
    {
        // Convert fixed-point delta to float
        int3 deltaInt = PositionDelta[idx];
        float3 delta = float3(deltaInt) / kScale;

        float weight = (float)weightFixed;
        if (weight > 0.0f)
        {
            // Average the accumulated deltas from all constraints
            float3 avgDelta = delta / weight;

            PredictedBuffer[idx].Position += avgDelta * RelaxationFactor;
        }
        
        // Clear for next iteration
        PositionDelta[idx] = int3(0, 0, 0);
        PositionWeight[idx] = 0;
    }
}
