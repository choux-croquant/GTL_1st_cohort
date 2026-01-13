/**
 * Cloth Velocity Update Compute Shader
 * Updates velocities based on position changes after constraint solving
 * This is critical for PBD stability - velocities must reflect constraint corrections
 */

#include "ClothCommon.hlsli"

// Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionBufferCurrent : register(u0);
StructuredBuffer<FClothParticle> PositionBufferPrevious : register(t0);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u1);

/**
 * Update velocity from position change
 * v_new = (p_new - p_old) / dt
 * 
 * This ensures velocities reflect the constrained motion,
 * preventing oscillations and energy accumulation.
 */
[numthreads(64, 1, 1)]
void UpdateVelocityFromPositionCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    if (idx >= NumParticles)
        return;
    
    // Load particle data
    FClothParticle currentParticle = PositionBufferCurrent[idx];
    FClothParticle previousParticle = PositionBufferPrevious[idx];
    FClothVelocity velocity = VelocityBuffer[idx];
    
    // Skip fixed particles
    if (currentParticle.InvMass == 0.0f)
    {
        velocity.Velocity = float3(0, 0, 0);
        VelocityBuffer[idx] = velocity;
        return;
    }
    
    // Calculate position change
    float3 positionDelta = currentParticle.Position - previousParticle.Position;
    
    // Update velocity from position change
    // This incorporates both integration motion and constraint corrections
    float3 newVelocity = positionDelta / DeltaTime;
    
    // Optional: Blend with old velocity for smoother motion
    // This helps reduce high-frequency oscillations
    float blendFactor = 0.1f;  // How much to keep old velocity
    velocity.Velocity = lerp(newVelocity, velocity.Velocity, blendFactor);
    
    // Apply additional damping to reduce oscillations
    velocity.Velocity *= (1.0f - Damping * 0.5f);
    
    // Clamp velocity magnitude to prevent instability
    float maxVelocity = 5000.0f; // cm/s (50 m/s)
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }
    
    // Write back
    VelocityBuffer[idx] = velocity;
}
