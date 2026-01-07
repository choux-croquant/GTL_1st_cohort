/**
 * Cloth Integration Compute Shader
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 */

#include "ClothCommon.hlsli"

// Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionBuffer : register(u0);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u1);

/**
 * Main integration kernel
 * One thread per particle
 */
[numthreads(64, 1, 1)]
void IntegrateCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    // Bounds check
    if (idx >= NumParticles)
        return;
    
    // Load particle data
    FClothParticle particle = PositionBuffer[idx];
    FClothVelocity velocity = VelocityBuffer[idx];
    
    // Skip fixed particles (invMass == 0)
    if (particle.InvMass == 0.0f)
        return;
    
    // Calculate total external force
    float3 force = float3(0, 0, 0);
    
    // Add gravity
    force += Gravity;
    
    // Add wind with air drag
    // Wind force is proportional to air drag coefficient
    force += Wind * AirDrag;
    
    // Calculate acceleration (F = ma, a = F * invMass)
    float3 acceleration = force * particle.InvMass;
    
    // Semi-implicit Euler integration
    // Update velocity first
    velocity.Velocity += acceleration * DeltaTime;
    
    // Apply velocity damping
    velocity.Velocity *= (1.0f - Damping);
    
    // Clamp velocity to prevent instability
    float maxVelocity = 10000.0f; // cm/s
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }
    
    // Predict new position
    particle.Position += velocity.Velocity * DeltaTime;
    
    // Write back results
    PositionBuffer[idx] = particle;
    VelocityBuffer[idx] = velocity;
}
