/**
 * Cloth Apply Constraint Deltas
 * Applies accumulated constraint corrections to particle positions
 * Now supports batched simulation with separate InvMass buffer
 *
 * MODIFIED (Velvet-inspired):
 * - Removed velocity update (now handled by ClothFinalize shader)
 * - Added RelaxationFactor for Jacobi convergence control
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<float> InvMassBuffer : register(t2);  // NEW: Separate inverse mass buffer

// Write buffers
RWStructuredBuffer<int3> PositionDelta  : register(u0);
RWStructuredBuffer<int>  PositionWeight : register(u1);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u2);
// NOTE: VelocityBuffer removed - velocity is now updated in Finalize shader

static const float kScale = 10000.0f;

[numthreads(64, 1, 1)]
void ApplyConstraintDeltasCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles) return;

    FClothParticle p = PositionRead[i];
    float invMass = InvMassBuffer[i];

    // Skip fixed particles
    if (invMass == 0.0f)
    {
        PositionWrite[i] = p;
        PositionDelta[i] = int3(0, 0, 0);
        PositionWeight[i] = 0;
        return;
    }
    
    // Average accumulated deltas by weight (Jacobi-style)
    int w = PositionWeight[i];
    float3 avgDelta = (float3(PositionDelta[i]) / kScale) / max(1, w);
    float3 delta = (w > 0) ? avgDelta : float3(0, 0, 0);

    // Apply delta with relaxation factor (Velvet pattern)
    // RelaxationFactor controls convergence rate (default 1.0)
    p.Position += delta * RelaxationFactor;
    
    PositionWrite[i] = p;
    
    // REMOVED: Velocity update (now in Finalize shader)
    // This was incorrect - it overwrote the integrated velocity
    // Velocity is now derived from final position change in ClothFinalize.hlsl

    // Clear accumulators
    PositionDelta[i] = int3(0, 0, 0);
    PositionWeight[i] = 0;
}
