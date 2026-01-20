/**
 * Cloth Integration Compute Shader
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 */

#include "ClothCommon.hlsli"

 // Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionRead  : register(u0);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u1);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u2);

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

    // Skip fixed particles
    if (particle.InvMass == 0.0f)
    {
        PositionWrite[idx] = particle;
        VelocityBuffer[idx] = velocity;
        return;
    }

    // Calculate total external force
    float3 force = float3(0, 0, 0);

    // Add gravity
    force += Gravity;

    // Add wind with air drag
    force += Wind * AirDrag;

    // Acceleration
    float3 acceleration = force * particle.InvMass;

    // Semi-implicit Euler
    velocity.Velocity += acceleration * DeltaTime;

    // Velocity damping
    velocity.Velocity *= (1.0f - Damping);

    // Clamp velocity
    float maxVelocity = 10000.0f;
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }

    particle.Position += velocity.Velocity * DeltaTime;
    //particle.Position += velocity.Velocity * DeltaTime + 0.5f * acceleration * DeltaTime * DeltaTime;

    PositionWrite[idx] = particle;
    VelocityBuffer[idx] = velocity;
}
