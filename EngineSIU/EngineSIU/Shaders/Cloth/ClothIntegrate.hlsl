/**
 * Cloth Integration Compute Shader
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 * Now supports batched simulation with per-instance parameters
 *
 * MODIFIED (Velvet-inspired):
 * - Removed velocity damping (now in Finalize shader)
 * - Uses MaxSpeed from constants for safety clamping
 */

#include "ClothCommon.hlsli"

// Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionRead  : register(u0);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u1);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u2);

// Batched simulation buffers
StructuredBuffer<float> InvMassBuffer : register(t2);  // Separate inverse mass buffer
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);  // Per-instance parameters

[numthreads(64, 1, 1)]
void IntegrateCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;

    // Bounds check
    if (idx >= NumParticles)
        return;

    // Load particle data
    FClothParticle particle = PositionRead[idx];
    FClothVelocity velocity = VelocityBuffer[idx];
    float invMass = InvMassBuffer[idx];  // Load from separate buffer

    // Skip fixed particles
    if (invMass == 0.0f)
    {
        PositionWrite[idx] = particle;
        VelocityBuffer[idx] = velocity;
        return;
    }

    // Get per-instance parameters
    uint instanceID = particle.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    // Check if instance is active
    if (params.IsActive == 0)
    {
        PositionWrite[idx] = particle;
        VelocityBuffer[idx] = velocity;
        return;
    }

    // Calculate total external force using per-instance parameters
    float3 force = float3(0, 0, 0);

    // Add per-instance gravity
    force += params.Gravity * params.GravityMultiplier;

    // Add per-instance wind with air drag
    force += params.Wind * params.WindStrength * params.AirDrag;

    // Acceleration
    float3 acceleration = force * invMass;

    // Semi-implicit Euler (velocity first, then position)
    velocity.Velocity += acceleration * DeltaTime;

    // REMOVED: Velocity damping (now in Finalize shader after constraints)
    // velocity.Velocity *= (1.0f - params.Damping);

    // Safety clamp velocity (generous limit, real clamping in Finalize)
    // This is a safety measure to prevent initial explosions
    float maxVelocity = MaxSpeed * 2.0f;  // 2x max speed as safety margin
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }

    // Update position from velocity
    particle.Position += velocity.Velocity * DeltaTime;

    PositionWrite[idx] = particle;
    VelocityBuffer[idx] = velocity;
}
