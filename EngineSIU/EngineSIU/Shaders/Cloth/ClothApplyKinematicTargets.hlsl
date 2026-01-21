/**
 * Cloth Apply Kinematic Targets Shader
 * 
 * Applies kinematic constraints to cloth particles by snapping them to target positions.
 * This is used for attachment points (e.g., cloth pinned to a moving pole or character)
 * 
 * Execution: One thread per kinematic target
 */

#include "ClothCommon.hlsli"

// Input: Kinematic target data
StructuredBuffer<FKinematicTarget> KinematicTargets : register(t0);

// Output: Particle positions (write)
RWStructuredBuffer<FClothParticle> ParticlesWrite : register(u0);

[numthreads(64, 1, 1)]
void ApplyKinematicTargetsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint targetIdx = DTid.x;
    
    // Bounds check
    if (targetIdx >= NumKinematicTargets)
        return;
    
    // Get kinematic target data
    FKinematicTarget target = KinematicTargets[targetIdx];
    uint particleIdx = target.ParticleIndex;
    
    // Bounds check for particle index
    if (particleIdx >= NumParticles)
        return;
    
    // Read current particle state
    FClothParticle particle = ParticlesWrite[particleIdx];
    
    // Apply kinematic constraint based on stiffness
    // Stiffness = 1.0 means hard constraint (full snap to target)
    // Stiffness < 1.0 means soft constraint (spring-like behavior)
    float3 currentPos = particle.Position;
    float3 targetPos = target.TargetPosition;
    
    // Interpolate between current and target position based on stiffness
    float3 newPos = lerp(currentPos, targetPos, target.Stiffness);
    
    // Write back updated position
    particle.Position = newPos;
    ParticlesWrite[particleIdx] = particle;
}
