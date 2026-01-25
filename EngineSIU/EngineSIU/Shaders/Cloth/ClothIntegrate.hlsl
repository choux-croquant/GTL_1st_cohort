/**
 * Cloth Integration Compute Shader
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 * Now supports batched simulation with per-instance parameters
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
    float invMass = InvMassBuffer[idx];  // NEW: Load from separate buffer

    // Skip fixed particles
    if (invMass == 0.0f)
    {
        PositionWrite[idx] = particle;
        VelocityBuffer[idx] = velocity;
        return;
    }

    // NEW: Get per-instance parameters
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

    // Semi-implicit Euler integration
    velocity.Velocity += acceleration * DeltaTime;

    // REMOVED: Global velocity damping (causes underwater feel)
    // OLD: velocity.Velocity *= (1.0f - params.Damping);
    // Damping is now handled via:
    // 1. XPBD constraint-level damping (in ClothConstraintSolver.hlsl)
    // 2. Per-particle position damping (optional, in ClothApplyDelta.hlsl)
    // This preserves momentum from external forces while still controlling oscillations

    // Clamp velocity for numerical stability
    float maxVelocity = 10000.0f;
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }

    // Predict new position
    particle.Position += velocity.Velocity * DeltaTime;

    PositionWrite[idx] = particle;
    VelocityBuffer[idx] = velocity;
}
