/**
 * Cloth Normal Update CUDA Kernel
 * Computes per-vertex normals from triangle mesh data
 *
 * Process:
 * 1. Clear normals to zero
 * 2. For each triangle, compute face normal and accumulate to vertices (atomic)
 * 3. Normalize accumulated normals
 *
 * Ported from: Shaders/Cloth/ClothUpdateNormals.hlsl
 */

#include <windows.h>
#include <cstdio>
#include <cuda_runtime.h>
#include "Cloth/ClothGPUStructs.h"

/**
 * Clear normals kernel - one thread per particle
 * Initialize all normals to zero before accumulation
 */
__global__ void ClearNormalsKernel(
    FVector *Normals,
    uint32 NumParticles)
{
    uint32 idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx < NumParticles)
    {
        Normals[idx] = FVector(0, 0, 0);
    }
}

/**
 * Accumulate face normals kernel - one thread per triangle
 *
 * For each triangle:
 * 1. Load the three vertex positions
 * 2. Compute face normal via cross product
 * 3. Atomically add face normal to all three vertices
 */
__global__ void AccumulateFaceNormalsKernel(
    const FClothParticleGPU *__restrict__ Positions,
    const uint32 *__restrict__ Indices,
    FVector *__restrict__ Normals,
    uint32 NumTriangles)
{
    uint32 triangleIdx = blockIdx.x * blockDim.x + threadIdx.x;

    // Bounds check
    if (triangleIdx >= NumTriangles)
        return;

    // Load triangle indices
    uint32 i0 = Indices[triangleIdx * 3 + 0];
    uint32 i1 = Indices[triangleIdx * 3 + 1];
    uint32 i2 = Indices[triangleIdx * 3 + 2];

    // Load vertex positions
    FVector p0 = Positions[i0].Position;
    FVector p1 = Positions[i1].Position;
    FVector p2 = Positions[i2].Position;

    // Calculate triangle edges
    FVector edge1, edge2;
    edge1.X = p1.X - p0.X;
    edge1.Y = p1.Y - p0.Y;
    edge1.Z = p1.Z - p0.Z;

    edge2.X = p2.X - p0.X;
    edge2.Y = p2.Y - p0.Y;
    edge2.Z = p2.Z - p0.Z;

    // Calculate face normal via cross product: edge1 × edge2
    FVector normal;
    normal.X = edge1.Y * edge2.Z - edge1.Z * edge2.Y;
    normal.Y = edge1.Z * edge2.X - edge1.X * edge2.Z;
    normal.Z = edge1.X * edge2.Y - edge1.Y * edge2.X;

    // Accumulate face normal to all three vertices (atomic)
    // This is area-weighted by default (larger triangles contribute more)
    atomicAddFloat3(&Normals[i0], normal);
    atomicAddFloat3(&Normals[i1], normal);
    atomicAddFloat3(&Normals[i2], normal);
}

/**
 * Normalize normals kernel - one thread per particle
 * Normalize the accumulated normals to unit length
 */
__global__ void NormalizeNormalsKernel(
    FVector *Normals,
    uint32 NumParticles)
{
    uint32 idx = blockIdx.x * blockDim.x + threadIdx.x;

    // Bounds check
    if (idx >= NumParticles)
        return;

    // Load accumulated normal
    FVector normal = Normals[idx];

    // Calculate length
    float length = sqrtf(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);

    // Normalize if length is valid
    if (length > 1e-6f)
    {
        float invLength = 1.0f / length;
        Normals[idx].X = normal.X * invLength;
        Normals[idx].Y = normal.Y * invLength;
        Normals[idx].Z = normal.Z * invLength;
    }
    else
    {
        // Degenerate case - use default up normal
        Normals[idx] = FVector(0, 0, 1);
    }
}

/**
 * Host launcher function for normal update kernel
 * Executes three passes: clear, accumulate, normalize
 * Callable from C++ code
 */
extern "C" void LaunchUpdateNormalsKernel(
    const void *positions,
    const void *indices,
    void *normals,
    uint32 numParticles,
    uint32 numTriangles,
    cudaStream_t stream)
{
    const int threadsPerBlock = 256;

    // Pass 1: Clear normals to zero
    int blocks = (numParticles + threadsPerBlock - 1) / threadsPerBlock;
    ClearNormalsKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        (FVector *)normals,
        numParticles);

    // Pass 2: Accumulate face normals (atomic)
    blocks = (numTriangles + threadsPerBlock - 1) / threadsPerBlock;
    AccumulateFaceNormalsKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        (const FClothParticleGPU *)positions,
        (const uint32 *)indices,
        (FVector *)normals,
        numTriangles);

    // Pass 3: Normalize accumulated normals
    blocks = (numParticles + threadsPerBlock - 1) / threadsPerBlock;
    NormalizeNormalsKernel<<<blocks, threadsPerBlock, 0, stream>>>(
        (FVector *)normals,
        numParticles);
}
