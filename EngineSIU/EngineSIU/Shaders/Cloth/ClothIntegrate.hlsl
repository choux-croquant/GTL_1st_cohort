/**
 * Cloth Integration Compute Shader
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 *
 * VELVET PATTERN (Single Working Buffer):
 * - Reads from PositionBuffer (old positions) and VelocityBuffer
 * - Writes to PredictedBuffer (working buffer for constraint solving)
 * - Velocity damping removed (now in Finalize shader)
 */

#include "ClothCommon.hlsli"

// Input buffers (read-only)
StructuredBuffer<FClothParticle> PositionRead : register(t0);  // Old positions
StructuredBuffer<FClothVelocity> VelocityRead : register(t1);  // Current velocities
StructuredBuffer<float> InvMass : register(t2);                // Inverse masses
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);  // Per-instance parameters

// Output buffer (write predicted positions)
RWStructuredBuffer<FClothParticle> PredictedWrite : register(u0);  // Predicted positions

[numthreads(256, 1, 1)]
void IntegrateCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;

    // Bounds check
    if (idx >= NumParticles)
        return;

    float invMass = InvMass[idx];
    
    // Skip kinematic particles (invMass == 0)
    if (invMass == 0.0f)
    {
        // Copy position unchanged
        PredictedWrite[idx] = PositionRead[idx];
        return;
    }

    // Get per-instance parameters
    uint instanceID = PositionRead[idx].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    // Check if instance is active
    if (params.IsActive == 0)
    {
        PredictedWrite[idx] = PositionRead[idx];
        return;
    }

    // Load particle data
    float3 position = PositionRead[idx].Position;
    float3 velocity = VelocityRead[idx].Velocity;

    // Calculate total external force using per-instance parameters
    float3 force = float3(0, 0, 0);
    
    // Add per-instance gravity
    force += params.Gravity * params.GravityMultiplier;
    
    // Add per-instance wind with air drag
    force += params.Wind * params.WindStrength * params.AirDrag;
    
    // Semi-implicit Euler integration
    velocity += force * DeltaTime;

    // Predict new position
    float3 predicted = position + velocity * DeltaTime;

    // Write to predicted buffer
    FClothParticle outParticle;
    outParticle.Position = predicted;
    outParticle.InstanceID = instanceID;
    PredictedWrite[idx] = outParticle;
}
