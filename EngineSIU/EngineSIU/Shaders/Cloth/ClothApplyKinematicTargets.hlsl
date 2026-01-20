/**
 * Cloth Apply Kinematic Targets Compute Shader
 * Overrides particle positions for kinematically-constrained vertices
 * Used for attaching cloth to moving objects (e.g., flag on pole, cape on shoulders)
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FKinematicTarget> KinematicTargetBuffer : register(t0);

// Input/Output buffer
RWStructuredBuffer<FClothParticle> PositionBuffer : register(u0);

[numthreads(64, 1, 1)]
void ApplyKinematicTargetsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    // Bounds check
    if (idx >= NumKinematicTargets)
        return;
    
    // Load kinematic target data
    FKinematicTarget target = KinematicTargetBuffer[idx];
    
    // Get particle index
    uint particleIdx = target.ParticleIndex;
    
    // Bounds check for particle index
    if (particleIdx >= NumParticles)
        return;
    
    // Load current particle
    FClothParticle particle = PositionBuffer[particleIdx];
    
    // Apply kinematic constraint
    if (target.Stiffness >= 0.99f)
    {
        // Hard kinematic constraint - set position exactly
        particle.Position = target.TargetPosition;
        particle.InvMass = 0.0f;  // Make it fixed (ignores forces)
    }
    else
    {
        // Soft kinematic constraint - blend toward target
        float3 delta = target.TargetPosition - particle.Position;
        particle.Position += delta * target.Stiffness;
        // Keep InvMass as-is for soft constraints
    }
    
    // Write back
    PositionBuffer[particleIdx] = particle;
}
