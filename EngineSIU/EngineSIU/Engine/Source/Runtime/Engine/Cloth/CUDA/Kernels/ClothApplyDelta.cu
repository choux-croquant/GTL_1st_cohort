/**
 * Cloth Apply Delta CUDA Kernel
 * Applies accumulated constraint corrections to particle positions
 */

#include <windows.h>
#include <cstdio>
#include <cuda_runtime.h>
#include "Cloth/ClothGPUStructs.h"


__global__ void ApplyConstraintDeltasKernel(
    const FClothParticleGPU *__restrict__ PositionRead,
    FClothParticleGPU *__restrict__ PositionWrite,
    FVector *__restrict__ PositionDeltas,
    float *__restrict__ PositionWeights,
    FClothSimConstants Constants)
{
    uint32 idx = blockIdx.x * blockDim.x + threadIdx.x;

    // Bounds check
    if (idx >= Constants.NumParticles) return;

    // Load particle
    FClothParticleGPU particle = PositionRead[idx];

    // Skip fixed particles
    if (particle.InvMass == 0.0f)
    {
        // Fixed particles don't move
        PositionWrite[idx] = particle;
        return;
    }

    // Load accumulated delta and weight
    float weight = PositionWeights[idx];

    // Apply averaged correction if any constraints affected this particle
    if (weight > 0.0f)
    {
        // Average the accumulated deltas
        float invWeight = 1.0f / weight;

        particle.Position.X += PositionDeltas[idx].X * invWeight;
        particle.Position.Y += PositionDeltas[idx].Y * invWeight;
        particle.Position.Z += PositionDeltas[idx].Z * invWeight;

        // Clear for next iteration
        PositionDeltas[idx] = FVector(0, 0, 0);
        PositionWeights[idx] = 0.0f;
    }

    // Write updated position
    PositionWrite[idx] = particle;
}

/**
 * Host launcher function for apply deltas kernel
 * Callable from C++ code
 */
extern "C" void LaunchApplyDeltasKernel(
    const void *posRead,
    void *posWrite,
    void *deltas,
    void *weights,
    FClothSimConstants constants,
    cudaStream_t stream)
{
    const int threadsPerBlock = 256;
    const int blocks = (constants.NumParticles + threadsPerBlock - 1) / threadsPerBlock;

    ApplyConstraintDeltasKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        (const FClothParticleGPU *)posRead,
        (FClothParticleGPU *)posWrite,
        (FVector *)deltas,
        (float *)weights,
        constants);
}
