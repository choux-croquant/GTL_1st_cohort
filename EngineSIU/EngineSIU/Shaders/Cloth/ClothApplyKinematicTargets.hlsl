/**
 * Cloth Apply Kinematic Targets Shader
 *
 * Applies kinematic constraints to cloth particles by setting them to target positions.
 * This is used for attachment points (e.g., cloth pinned to a moving pole or character)
 *
 * VELVET PATTERN (Single Working Buffer):
 * - Modifies PredictedBuffer IN-PLACE
 * - Applies hard kinematic constraints (snaps to target position)
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FKinematicTarget> KinematicTargets : register(t0);

// Output buffer (in-place modification)
RWStructuredBuffer<FClothParticle> PredictedBuffer : register(u0);

[numthreads(256, 1, 1)]
void ApplyKinematicTargetsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumKinematicTargets) return;
    
    FKinematicTarget target = KinematicTargets[idx];
    uint particleIdx = target.ParticleIndex;
    
    // Apply kinematic constraint (hard constraint)
    PredictedBuffer[particleIdx].Position = target.TargetPosition;
}
