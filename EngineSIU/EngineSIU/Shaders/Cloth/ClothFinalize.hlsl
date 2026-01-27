/**
 * Cloth Velocity Finalization Compute Shader
 * Updates velocity from position change, clamps max velocity, applies damping
 * Based on Velvet's Finalize_Kernel pattern
 *
 * VELVET PATTERN (Single Working Buffer):
 * - Reads from PositionBuffer (old) and PredictedBuffer (new)
 * - Derives velocity from position change
 * - Writes final velocity to VelocityBuffer
 * - Writes final position to PositionBuffer (overwrite old)
 */

#include "ClothCommon.hlsli"

// Read buffers
StructuredBuffer<FClothParticle> PredictedFinal : register(t0);     // Final predicted positions
StructuredBuffer<float> InvMass : register(t1);                     // Inverse masses
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t2);  // Per-instance parameters

// Read/Write buffers (avoid SRV+UAV aliasing on Position buffer)
RWStructuredBuffer<FClothVelocity> VelocityWrite : register(u0);
RWStructuredBuffer<FClothParticle> PositionRW : register(u1);

[numthreads(256, 1, 1)]
void FinalizeVelocityCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumParticles) return;
    
    float invMass = InvMass[idx];
    
    // Skip kinematic particles
    if (invMass == 0.0f)
    {
        // Copy predicted position directly to final buffer
        FClothParticle pinned;
        pinned.Position = PredictedFinal[idx].Position;
        pinned.InstanceID = PredictedFinal[idx].InstanceID;
        PositionRW[idx] = pinned;

        FClothVelocity vel;
        vel.Velocity = float3(0, 0, 0);
        vel.Padding = 0.0f;
        VelocityWrite[idx] = vel;
        return;
    }
    
    // Get per-instance parameters
    uint instanceID = PredictedFinal[idx].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    // Check if instance is active
    if (params.IsActive == 0)
    {
        PositionRW[idx] = PredictedFinal[idx];
        FClothVelocity vel;
        vel.Velocity = float3(0, 0, 0);
        vel.Padding = 0.0f;
        VelocityWrite[idx] = vel;
        return;
    }
    
    // Read positions (matching Velvet: new_pos = predicted[id], old_pos = positions[id])
    float3 oldPos = PositionRW[idx].Position;
    float3 newPos = PredictedFinal[idx].Position;
    
    // Derive velocity from position change
    float3 rawVel = (newPos - oldPos) / DeltaTime;
    
    // Clamp velocity to max speed (matching Velvet)
    float speed = length(rawVel);
    if (speed > MaxSpeed)
    {
        rawVel = normalize(rawVel) * MaxSpeed;
        newPos = oldPos + rawVel * DeltaTime;  // Recalculate position with clamped velocity
    }
    
    // Apply damping using per-instance damping parameter (matching Velvet: velocities[id] = raw_vel * (1 - damping * dt))
    float3 dampedVel = rawVel * (1.0 - params.Damping * DeltaTime);
    
    // Write outputs (positions updated in-place on GPU)
    FClothVelocity vel;
    vel.Velocity = dampedVel;
    vel.Padding = 0.0f;
    VelocityWrite[idx] = vel;

    FClothParticle outParticle;
    outParticle.Position = newPos;
    outParticle.InstanceID = instanceID;
    PositionRW[idx] = outParticle;  // Overwrite old positions with new
}
