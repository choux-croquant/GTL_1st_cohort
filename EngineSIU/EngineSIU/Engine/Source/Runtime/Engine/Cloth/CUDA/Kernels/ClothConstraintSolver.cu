/**
 * Cloth Constraint Solver CUDA Kernel
 * Solves distance constraints using Position-Based Dynamics (PBD)
 *
 * KEY IMPROVEMENT OVER DX11:
 * Uses native CUDA float atomics (atomicAdd) to accumulate constraint corrections
 * directly, eliminating the complex Jacobi accumulation workaround required in DX11.
 *
 * Ported from: Shaders/Cloth/ClothConstraintSolver.hlsl
 */

#include <windows.h>
#include <cstdio>
#include <cuda_runtime.h>
#include "Cloth/ClothGPUStructs.h"

/**
 * Helper function to atomically add a float3/FVector
 * This is the MAIN ADVANTAGE of CUDA over DX11 Compute!
 */
//__device__ void atomicAddFloat3(FVector *addr, const FVector &delta)
//{
//    atomicAdd(&addr->X, delta.X);
//    atomicAdd(&addr->Y, delta.Y);
//    atomicAdd(&addr->Z, delta.Z);
//}

/**
 * Distance constraint solver kernel - one thread per constraint
 *
 * For each distance constraint:
 * 1. Calculate current distance between particles
 * 2. Calculate constraint error (violation)
 * 3. Compute correction forces based on PBD formulation
 * 4. Atomically accumulate corrections to delta buffers
 *
 * Unlike DX11, we can use atomicAdd directly on floats!
 */
__global__ void SolveDistanceConstraintsKernel(
    const FClothParticleGPU *__restrict__ Positions,
    const FClothConstraintGPU *__restrict__ Constraints,
    FVector *__restrict__ PositionDeltas,
    float *__restrict__ PositionWeights,
    FClothSimConstants Constants)
{
    uint32 idx = blockIdx.x * blockDim.x + threadIdx.x;

    // Bounds check
    if (idx >= Constants.NumConstraints)
        return;

    // Load constraint data
    FClothConstraintGPU constraint = Constraints[idx];

    // Load particle positions
    FClothParticleGPU pA = Positions[constraint.ParticleA];
    FClothParticleGPU pB = Positions[constraint.ParticleB];

    // Calculate vector from A to B
    FVector delta;
    delta.X = pB.Position.X - pA.Position.X;
    delta.Y = pB.Position.Y - pA.Position.Y;
    delta.Z = pB.Position.Z - pA.Position.Z;

    // Calculate current distance
    float currentLength = sqrtf(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);

    // Skip if particles are at same position (avoid division by zero)
    if (currentLength < 1e-6f)
        return;

    // Calculate constraint violation
    float error = currentLength - constraint.RestLength;

    // Normalized direction vector
    FVector dir;
    float invLength = 1.0f / currentLength;
    dir.X = delta.X * invLength;
    dir.Y = delta.Y * invLength;
    dir.Z = delta.Z * invLength;

    // Get inverse masses
    float w1 = pA.InvMass;
    float w2 = pB.InvMass;
    float wSum = w1 + w2;

    // Skip if both particles are fixed
    if (wSum < 1e-6f)
        return;

    // Calculate constraint lambda (Lagrange multiplier)
    // PBD formulation: λ = -C / (∇C · M^-1 · ∇C)
    float stiffness = constraint.Stiffness * Constants.StretchStiffness;
    float lambda = -error / wSum;

    // Calculate position corrections
    // Δp = stiffness * λ * w * ∇C
    FVector correctionA, correctionB;
    correctionA.X = stiffness * lambda * w1 * (-dir.X);
    correctionA.Y = stiffness * lambda * w1 * (-dir.Y);
    correctionA.Z = stiffness * lambda * w1 * (-dir.Z);

    correctionB.X = stiffness * lambda * w2 * dir.X;
    correctionB.Y = stiffness * lambda * w2 * dir.Y;
    correctionB.Z = stiffness * lambda * w2 * dir.Z;

    // ========================================
    // ATOMIC ACCUMULATION - THIS IS WHY WE USE CUDA!
    // ========================================
    // In DX11 SM 5.0, InterlockedAdd doesn't support float, requiring complex workarounds
    // In CUDA, we have native atomicAdd for floats!

    atomicAddFloat3(&PositionDeltas[constraint.ParticleA], correctionA);
    atomicAdd(&PositionWeights[constraint.ParticleA], 1.0f);

    atomicAddFloat3(&PositionDeltas[constraint.ParticleB], correctionB);
    atomicAdd(&PositionWeights[constraint.ParticleB], 1.0f);
}

/**
 * Host launcher function for constraint solver kernel
 * Callable from C++ code
 */
extern "C" void LaunchConstraintSolverKernel(
    const void *positions,
    const void *constraints,
    void *deltas,
    void *weights,
    FClothSimConstants constants,
    cudaStream_t stream)
{
    const int threadsPerBlock = 256;
    const int blocks = (constants.NumConstraints + threadsPerBlock - 1) / threadsPerBlock;

    SolveDistanceConstraintsKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        (const FClothParticleGPU *)positions,
        (const FClothConstraintGPU *)constraints,
        (FVector *)deltas,
        (float *)weights,
        constants);
}
