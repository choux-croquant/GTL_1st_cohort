/**
 * Cloth Integration CUDA Kernel
 * Performs semi-implicit Euler integration for cloth particles
 * Applies external forces (gravity, wind, drag) and predicts new positions
 *
 * Ported from: Shaders/Cloth/ClothIntegrate.hlsl
 */
#include <windows.h>
#include <cstdio>
#include <cuda_runtime.h>
#include "Cloth/ClothGPUStructs.h"

/**
 * Integration kernel - one thread per particle
 *
 * Implements semi-implicit Euler integration:
 * 1. Calculate acceleration from forces
 * 2. Update velocity: v_{n+1} = v_n + a * dt
 * 3. Apply damping
 * 4. Update position: x_{n+1} = x_n + v_{n+1} * dt
 */
__global__ void IntegrateKernel(
    FClothParticleGPU *PositionRead,
    FClothParticleGPU *PositionWrite,
    FClothVelocityGPU *Velocities,
    FClothSimConstants Constants)
{
    uint32 idx = blockIdx.x * blockDim.x + threadIdx.x;

    // Bounds check
    if (idx >= Constants.NumParticles)
        return;

    // Load particle data
    FClothParticleGPU particle = PositionRead[idx];
    FClothVelocityGPU velocity = Velocities[idx];

    // Skip fixed particles (invMass == 0)
    if (particle.InvMass == 0.0f)
    {
        // Fixed particles don't move - preserve their state
        PositionWrite[idx] = particle;
        Velocities[idx] = velocity;
        return;
    }

    // Calculate total external force
    FVector force = FVector(0, 0, 0);

    // Add gravity
    force.X += Constants.Gravity.X;
    force.Y += Constants.Gravity.Y;
    force.Z += Constants.Gravity.Z;

    // Add wind with air drag
    force.X += Constants.Wind.X * Constants.AirDrag;
    force.Y += Constants.Wind.Y * Constants.AirDrag;
    force.Z += Constants.Wind.Z * Constants.AirDrag;

    // Calculate acceleration: F = ma, a = F/m = F * invMass
    FVector acceleration;
    acceleration.X = force.X * particle.InvMass;
    acceleration.Y = force.Y * particle.InvMass;
    acceleration.Z = force.Z * particle.InvMass;

    // Semi-implicit Euler: v_{n+1} = v_n + a * dt
    velocity.Velocity.X += acceleration.X * Constants.DeltaTime;
    velocity.Velocity.Y += acceleration.Y * Constants.DeltaTime;
    velocity.Velocity.Z += acceleration.Z * Constants.DeltaTime;

    // Apply velocity damping
    float dampFactor = 1.0f - Constants.Damping;
    velocity.Velocity.X *= dampFactor;
    velocity.Velocity.Y *= dampFactor;
    velocity.Velocity.Z *= dampFactor;

    // Clamp velocity to prevent instability
    const float maxVelocity = 10000.0f; // cm/s
    float velMag = sqrtf(velocity.Velocity.X * velocity.Velocity.X +
                         velocity.Velocity.Y * velocity.Velocity.Y +
                         velocity.Velocity.Z * velocity.Velocity.Z);

    if (velMag > maxVelocity)
    {
        float scale = maxVelocity / velMag;
        velocity.Velocity.X *= scale;
        velocity.Velocity.Y *= scale;
        velocity.Velocity.Z *= scale;
    }

    // Predict new position: x_{n+1} = x_n + v_{n+1} * dt
    particle.Position.X += velocity.Velocity.X * Constants.DeltaTime;
    particle.Position.Y += velocity.Velocity.Y * Constants.DeltaTime;
    particle.Position.Z += velocity.Velocity.Z * Constants.DeltaTime;

    // Write back results
    PositionWrite[idx] = particle;
    Velocities[idx] = velocity;
}

/**
 * Host launcher function for integration kernel
 * Callable from C++ code
 */
extern "C" void LaunchIntegrateKernel(
    void *posRead,
    void *posWrite,
    void *velocities,
    FClothSimConstants constants,
    cudaStream_t stream)
{
    const int threadsPerBlock = 256;
    const int blocks = (constants.NumParticles + threadsPerBlock - 1) / threadsPerBlock;

    IntegrateKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        (FClothParticleGPU *)posRead,
        (FClothParticleGPU *)posWrite,
        (FClothVelocityGPU *)velocities,
        constants);

    /*cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        char buf[512];
        snprintf(buf, sizeof(buf), "[CUDA] IntegrateKernel launch failed: %s (code=%d)\n", cudaGetErrorString(err), (int)err);
        OutputDebugStringA(buf);
        return;
    }

    err = cudaStreamSynchronize(stream);
    if (err != cudaSuccess) {
        char buf[512];
        snprintf(buf, sizeof(buf), "[CUDA] IntegrateKernel execution failed: %s (code=%d)\n", cudaGetErrorString(err), (int)err);
        OutputDebugStringA(buf);
    }*/
}
