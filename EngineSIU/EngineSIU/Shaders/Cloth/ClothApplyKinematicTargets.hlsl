/**
 * Cloth Apply Kinematic Targets Shader
 *
 * Applies kinematic constraints to cloth particles with velocity synchronization.
 * CRITICAL FIX: Updates velocity to match kinematic motion for momentum preservation.
 * This prevents laggy attachment response and enables proper tension propagation.
 *
 * Execution: One thread per kinematic target
 */

#include "ClothCommon.hlsli"

// Input: Kinematic target data
StructuredBuffer<FKinematicTarget> KinematicTargets : register(t0);
// Note: InvMassBuffer (t1) is optional - only needed for soft attachments

// Output: Particle positions and velocities (write)
RWStructuredBuffer<FClothParticle> ParticlesWrite : register(u0);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u1);  // CRITICAL: Update velocity

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
    FClothVelocity velocity = VelocityBuffer[particleIdx];
    
    float3 currentPos = particle.Position;
    float3 targetPos = target.TargetPosition;
    float3 delta = targetPos - currentPos;
    
    // Hard kinematic constraint (stiffness >= 0.99)
    // This is the common case for attachments (flags, capes, etc.)
    if (target.Stiffness >= 0.99f)
    {
        // CRITICAL FIX: Compute kinematic velocity from position change
        // This makes neighboring particles "feel" the pull through constraints
        float3 kinematicVelocity = delta / DeltaTime;
        
        // Snap position to target exactly
        particle.Position = targetPos;
        
        // CRITICAL: Sync velocity to kinematic motion
        // Without this, attachment point moves but velocity stays old = laggy response
        velocity.Velocity = kinematicVelocity;
    }
    // Soft spring attachment (stiffness < 0.99)
    // Note: This path requires InvMassBuffer, but for hard attachments we don't need it
    else
    {
        // Simple soft interpolation (without spring force)
        // If InvMassBuffer is available, could compute proper spring force here
        particle.Position = lerp(currentPos, targetPos, target.Stiffness);
        
        // Update velocity from position change
        float3 velocityFromChange = (particle.Position - currentPos) / DeltaTime;
        velocity.Velocity += velocityFromChange;
    }
    
    // Write back both position AND velocity
    ParticlesWrite[particleIdx] = particle;
    VelocityBuffer[particleIdx] = velocity;
}
