/**
 * Cloth CUDA Kernels - Host-side launcher declarations
 *
 * This header declares the C-linkage functions that launch CUDA kernels
 * from C++ host code. The actual kernel implementations are in the
 * Kernels/ subdirectory.
 */

#pragma once

#include "Cloth/ClothGPUStructs.h"

// Need FClothSimConstants definition - it's in ShaderConstants.h for C++ mode
#ifndef __CUDACC__
#include "Renderer/ShaderConstants.h"
#endif

#ifdef CUDA_ENABLED
#include <cuda_runtime.h>
#else
// Forward declarations when CUDA is not available
typedef struct CUstream_st *cudaStream_t;
#endif

extern "C"
{
    /**
     * Launch the integration kernel (semi-implicit Euler)
     * Applies forces and updates velocities and positions
     *
     * @param posRead - Input position buffer
     * @param posWrite - Output position buffer
     * @param velocities - Velocity buffer (read/write)
     * @param constants - Simulation constants
     * @param stream - CUDA stream for async execution
     */
    void LaunchIntegrateKernel(
        void *posRead,
        void *posWrite,
        void *velocities,
        FClothSimConstants constants,
        cudaStream_t stream);

    /**
     * Launch the constraint solver kernel
     * Solves distance constraints and accumulates corrections to delta buffers
     * using native CUDA float atomics
     *
     * @param positions - Current position buffer (read-only)
     * @param constraints - Distance constraint buffer
     * @param deltas - Position delta accumulator (write, atomic)
     * @param weights - Position weight accumulator (write, atomic)
     * @param constants - Simulation constants
     * @param stream - CUDA stream for async execution
     */
    void LaunchConstraintSolverKernel(
        const void *positions,
        const void *constraints,
        void *deltas,
        void *weights,
        FClothSimConstants constants,
        cudaStream_t stream);

    /**
     * Launch the apply deltas kernel
     * Averages and applies accumulated position corrections
     *
     * @param posRead - Input position buffer
     * @param posWrite - Output position buffer
     * @param deltas - Position delta buffer (read/write, cleared after apply)
     * @param weights - Position weight buffer (read/write, cleared after apply)
     * @param constants - Simulation constants
     * @param stream - CUDA stream for async execution
     */
    void LaunchApplyDeltasKernel(
        const void *posRead,
        void *posWrite,
        void *deltas,
        void *weights,
        FClothSimConstants constants,
        cudaStream_t stream);

    /**
     * Launch the normal update kernel
     * Computes per-vertex normals from triangle data
     * Executes in three passes: clear, accumulate, normalize
     *
     * @param positions - Position buffer (read-only)
     * @param indices - Triangle index buffer
     * @param normals - Normal buffer (write)
     * @param numParticles - Number of particles/vertices
     * @param numTriangles - Number of triangles
     * @param stream - CUDA stream for async execution
     */
    void LaunchUpdateNormalsKernel(
        const void *positions,
        const void *indices,
        void *normals,
        uint32 numParticles,
        uint32 numTriangles,
        cudaStream_t stream);

    /**
     * Test function to verify CUDA setup
     * Returns true if CUDA device is available
     */
    bool TestCUDASetup();
}
