/**
 * Cloth Integration Compute Shader
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 */

#include "ClothCommon.hlsli"

 // Input/Output buffers
RWStructuredBuffer<FClothParticle> PositionRead  : register(u0); // 이번 스텝 입력
RWStructuredBuffer<FClothParticle> PositionWrite : register(u1); // 이번 스텝 출력
RWStructuredBuffer<FClothVelocity> VelocityBuffer : register(u2);

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

    // Load particle data (읽기는 항상 PositionRead에서)
    FClothParticle particle = PositionRead[idx];
    FClothVelocity velocity = VelocityBuffer[idx];

    // Skip fixed particles (invMass == 0)
    if (particle.InvMass == 0.0f)
    {
        // 고정점은 위치/속도를 그대로 써 줘야 한다 (ping-pong 시 초기화 보존)
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

    // Semi-implicit Euler: v_{n+1} = v_n + a * dt
    velocity.Velocity += acceleration * DeltaTime;

    // Velocity damping
    velocity.Velocity *= (1.0f - Damping);

    // Clamp velocity
    float maxVelocity = 10000.0f; // cm/s
    float velMagnitude = length(velocity.Velocity);
    if (velMagnitude > maxVelocity)
    {
        velocity.Velocity = (velocity.Velocity / velMagnitude) * maxVelocity;
    }

    // Predict new position: x_{n+1} = x_n + v_{n+1} * dt
    particle.Position += velocity.Velocity * DeltaTime;
    //particle.Position += velocity.Velocity * DeltaTime + 0.5f * acceleration * DeltaTime * DeltaTime;

    PositionWrite[idx] = particle;
    VelocityBuffer[idx] = velocity;
}
