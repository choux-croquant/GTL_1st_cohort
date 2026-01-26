/**
 * Cloth Velocity Finalization Compute Shader
 * Updates velocity from position change, clamps max velocity, applies damping
 * Based on Velvet's Finalize_Kernel pattern
 * 
 * This shader implements the final step of the XPBD substep:
 * 1. Derive velocity from position change (implicit velocity update)
 * 2. Clamp velocity magnitude to prevent explosions
 * 3. Apply velocity damping
 * 
 * This replaces the incorrect velocity update in ApplyDelta shader
 */

#include "ClothCommon.hlsli"

// Read buffers
StructuredBuffer<FClothParticle> OldPositionRead : register(t0);  // Position before substep
StructuredBuffer<FClothParticle> NewPositionRead : register(t1);  // Position after constraints
StructuredBuffer<float> InvMassBuffer : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

// Write buffers
RWStructuredBuffer<FClothParticle> PositionWrite : register(u0);
RWStructuredBuffer<FClothVelocity> VelocityWrite : register(u1);

// Additional constants specific to finalization
cbuffer FinalizeConstants : register(b1)
{
    float SubstepDeltaTime;  // Substep dt for velocity calculation
    float MaxSpeed;          // Maximum velocity magnitude (cm/s)
    float Padding0;
    float Padding1;
};

[numthreads(64, 1, 1)]
void FinalizeVelocityCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumParticles) return;
    
    // Load old and new positions
    FClothParticle oldPos = OldPositionRead[idx];
    FClothParticle newPos = NewPositionRead[idx];
    float invMass = InvMassBuffer[idx];
    
    // Skip fixed particles (invMass == 0)
    if (invMass == 0.0f)
    {
        PositionWrite[idx] = newPos;
        // Velocity stays zero for fixed particles
        FClothVelocity vel;
        vel.Velocity = float3(0, 0, 0);
        vel.Padding = 0.0f;
        VelocityWrite[idx] = vel;
        return;
    }
    
    // Get per-instance parameters
    uint instanceID = newPos.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    
    // Check if instance is active
    if (params.IsActive == 0)
    {
        PositionWrite[idx] = newPos;
        FClothVelocity vel;
        vel.Velocity = float3(0, 0, 0);
        vel.Padding = 0.0f;
        VelocityWrite[idx] = vel;
        return;
    }
    
    // Calculate raw velocity from position change (Velvet pattern)
    // v = (newPos - oldPos) / dt
    float3 positionDelta = newPos.Position - oldPos.Position;
    float3 rawVelocity = positionDelta / SubstepDeltaTime;
    
    // Clamp velocity magnitude to prevent explosions (Velvet pattern)
    float velMagnitude = length(rawVelocity);
    float maxVel = MaxSpeed;
    
    if (velMagnitude > maxVel)
    {
        // Clamp velocity
        rawVelocity = (rawVelocity / velMagnitude) * maxVel;
        
        // Adjust position to match clamped velocity (Velvet approach)
        newPos.Position = oldPos.Position + rawVelocity * SubstepDeltaTime;
    }
    
    // Apply velocity damping (Velvet pattern)
    // damping is applied per substep: v_final = v_raw * (1 - damping * dt)
    float dampingFactor = 1.0f - params.Damping * SubstepDeltaTime;
    float3 finalVelocity = rawVelocity * dampingFactor;
    
    // Write outputs
    PositionWrite[idx] = newPos;
    
    FClothVelocity vel;
    vel.Velocity = finalVelocity;
    vel.Padding = 0.0f;
    VelocityWrite[idx] = vel;
}
