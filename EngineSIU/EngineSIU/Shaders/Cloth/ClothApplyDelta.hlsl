/**
 * Cloth Apply Constraint Deltas
 * Applies accumulated constraint corrections to particle positions
 * CRITICAL FIX: Preserves momentum by adding velocity correction instead of overwriting
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PositionRead : register(t0);
StructuredBuffer<float> InvMassBuffer : register(t2);

// Write buffers
RWStructuredBuffer<int3> PositionDelta  : register(u0);
RWStructuredBuffer<int>  PositionWeight : register(u1);
RWStructuredBuffer<FClothParticle> PositionWrite : register(u2);
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u3);

static const float kScale = 1000.0f;

[numthreads(64, 1, 1)]
void ApplyConstraintDeltasCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles) return;

    FClothParticle p = PositionRead[i];
    FClothVelocity velocity = VelocityBuffer[i];
    
    float invMass = InvMassBuffer[i];

    // Skip fixed particles (kinematic/attached)
    if (invMass == 0.0f)
    {
        PositionWrite[i] = p;
        PositionDelta[i] = int3(0, 0, 0);
        PositionWeight[i] = 0;
        return;
    }
    
    // Compute average constraint correction
    int w = PositionWeight[i];
    float3 avgDelta = (float3(PositionDelta[i]) / kScale) / max(1, w);
    float3 delta = (w > 0) ? avgDelta : float3(0, 0, 0);

    // Apply position correction
    p.Position += delta;
    PositionWrite[i] = p;
    
    // CRITICAL FIX: Add constraint velocity correction instead of overwriting
    // OLD (WRONG): velocity.Velocity = delta / DeltaTime;  // Destroys momentum from forces
    // NEW (CORRECT): Add correction to preserve momentum from integration step
    float3 velocityCorrection = delta / DeltaTime;
    velocity.Velocity += velocityCorrection;
    
    // Optional: Clamp extreme velocities for stability
    float maxVelocity = 10000.0f;
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }
    
    VelocityBuffer[i] = velocity;

    // Clear accumulation buffers for next iteration
    PositionDelta[i] = int3(0, 0, 0);
    PositionWeight[i] = 0;
}
