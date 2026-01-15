# CUDA Cloth Simulation

This directory contains the CUDA implementation of the cloth physics simulation system for EngineSIU.

## Overview

The cloth simulation has been migrated from DirectX 11 Compute Shaders to CUDA to leverage native float atomic operations, which are unavailable in DX11 Shader Model 5.0.

## Directory Structure

```
CUDA/
├── README.md                    # This file
├── CUDATest.cu                  # Build system test
├── CUDADXInterop.h/.cpp         # CUDA-DirectX interop manager
├── ClothCUDAKernels.h           # Kernel launcher declarations
└── Kernels/
    ├── ClothIntegrate.cu        # Force integration and position prediction
    ├── ClothConstraintSolver.cu # Distance constraint solver (uses atomics!)
    ├── ClothApplyDelta.cu       # Apply constraint corrections
    └── ClothUpdateNormals.cu    # Per-vertex normal computation
```

## Key Files

### ClothGPUStructs.h (parent directory)
Shared data structures that work across C++, CUDA, and HLSL:
- `FClothParticleGPU` - Position and inverse mass (16 bytes)
- `FClothVelocityGPU` - Velocity vector (16 bytes)
- `FClothConstraintGPU` - Distance constraint (32 bytes)
- `FClothSimConstants` - Simulation parameters (128 bytes)

### CUDADXInterop
Manages zero-copy buffer sharing between CUDA and DirectX 11:
- Registers D3D11 buffers with CUDA
- Maps resources for CUDA access
- Manages CUDA stream for async operations
- Handles synchronization

### Kernels

All kernels are direct ports of the HLSL compute shaders with CUDA-specific optimizations:

**ClothIntegrate.cu**
- One thread per particle
- Semi-implicit Euler integration
- Force accumulation: gravity + wind + drag

**ClothConstraintSolver.cu** ⭐ **Main Improvement**
- One thread per constraint
- Uses `atomicAdd()` on float for delta accumulation
- Eliminates Jacobi iteration complexity
- Cleaner algorithm than DX11 version

**ClothApplyDelta.cu**
- One thread per particle
- Averages accumulated corrections
- Applies to positions
- Clears buffers for next iteration

**ClothUpdateNormals.cu**
- Three-pass: clear → accumulate → normalize
- Uses atomic float ops for face normal accumulation
- Area-weighted normals by default

## Requirements

### Hardware
- NVIDIA GPU with Compute Capability 5.0+ (Maxwell or newer)
- Examples: GTX 750+, RTX series, Quadro, Tesla

### Software
- CUDA Toolkit 12.x (or 11.0+ minimum)
- Windows 10/11
- NVIDIA Driver 452.39+ (for CUDA 11) or later

### Build Environment
- Visual Studio 2019/2022
- CUDA build customizations installed with toolkit
- `CUDA_PATH` environment variable set (automatic with CUDA installer)

## Building

The project automatically detects CUDA availability:

1. If CUDA Toolkit is installed and `$(CUDA_PATH)` is set:
   - .cu files compile with nvcc
   - CUDA libraries link automatically
   - `CUDA_ENABLED=1` preprocessor define is set

2. If CUDA is not available:
   - Build proceeds without CUDA support
   - Fallback to DX11 compute shaders
   - No build errors (graceful degradation)

## Runtime Behavior

### With CUDA Available

```
ClothSolver: CUDA-DX11 interop initialized successfully
ClothSolver: Using CUDA backend for cloth simulation
ClothSolver: CUDA device 0: NVIDIA GeForce RTX 3080 (Compute 8.6)
ClothSolver: CUDA resources initialized - 400 particles, 1140 constraints
```

Simulation runs on CUDA kernels with zero-copy D3D11 interop.

### Without CUDA (Fallback)

```
ClothSolver: Failed to initialize CUDA interop, falling back to DX11 compute
ClothSolver: Initialized successfully
```

Simulation runs on DirectX 11 compute shaders (original implementation).

## Debugging

### CUDA Errors

All CUDA API calls are checked and logged. If you see errors like:

```
CUDA Error in cudaGraphicsD3D11RegisterResource: invalid argument (1)
```

Common causes:
- Buffer missing `D3D11_RESOURCE_MISC_SHARED` flag
- CUDA device doesn't match D3D11 adapter
- Driver version too old

### IntelliSense Errors

**Expected:** .cu files will show IntelliSense errors in Visual Studio because they're parsed as C++ rather than CUDA. These are cosmetic only - nvcc will compile correctly.

**Ignore errors like:**
- `__global__ identifier not found`
- `__device__ identifier not found`
- `atomicAdd identifier not found`

These are all valid CUDA keywords.

### Performance Profiling

Use NVIDIA Nsight Systems to profile CUDA kernels:
```cmd
nsys profile -o cloth_simulation.qdrep EngineSIU.exe
```

Then open .qdrep file in Nsight Systems for detailed kernel timing.

## Architecture

### Simulation Loop

```
1. Map D3D11 buffers for CUDA access
2. Launch Integration kernel (forces → velocities → positions)
3. For each PBD iteration:
   a. Clear delta accumulation buffers
   b. Launch Constraint Solver kernel (atomic accumulation)
   c. Launch Apply Deltas kernel (average and apply)
4. Launch Normal Update kernel
5. Synchronize CUDA stream
6. Unmap buffers (return to D3D11 for rendering)
```

### Memory Layout

**Shared Buffers (D3D11 ↔ CUDA):**
- Position[0], Position[1] - Ping-pong buffers
- Velocity - Read/write per frame
- Normal - Written by CUDA, read by D3D11 rendering

**CUDA-Only Buffers:**
- PositionDeltas - Constraint correction accumulator
- PositionWeights - Weight accumulator for averaging
- Constraints - One-time upload, read-only during simulation
- Indices - One-time upload, read-only
- Constants - Updated per frame

## Performance

Expected performance characteristics vs DX11:

| Aspect | DX11 | CUDA | Notes |
|--------|------|------|-------|
| Constraint Solve | Baseline | 10-30% faster | Native atomics eliminate Jacobi overhead |
| API Overhead | Higher | Lower | Direct device pointers vs UAV bindings |
| CPU-GPU Transfer | None | None | Both use zero-copy (shared buffers) |
| Memory Usage | Baseline | Same | Shared buffers, similar CUDA-only overhead |

## Extending

To add new constraint types:

1. Add constraint structure to `ClothGPUStructs.h`
2. Create new kernel in `Kernels/ConstraintTypeName.cu`
3. Add launcher to `ClothCUDAKernels.h`
4. Call from `SimulateCUDA()` in ClothSolver.cpp

Example: Volume preservation constraint
```cuda
__global__ void SolveVolumeConstraintsKernel(...)
{
    // Calculate tetrahedral volume
    // Compute corrections
    // Atomically accumulate to deltas
    atomicAddFloat3(&PositionDeltas[i], correction);
}
```

## Troubleshooting

### Build Issues

**Problem:** `CUDA_PATH not found`
- **Solution:** Install CUDA Toolkit or set manually:
  ```cmd
  setx CUDA_PATH "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.0"
  ```

**Problem:** `nvcc.exe not found`
- **Solution:** Ensure CUDA Toolkit bin directory is in PATH

**Problem:** `cuda.lib not found`
- **Solution:** Verify CUDA Toolkit is fully installed, check lib/x64 directory exists

### Runtime Issues

**Problem:** `Failed to initialize CUDA interop`
- **Solution:** Check GPU is NVIDIA, update driver, verify D3D11 device created successfully

**Problem:** Cloth doesn't move with CUDA enabled
- **Solution:** Check log for CUDA errors, verify kernels are launching, check for NaN in positions

**Problem:** Visual artifacts or jittering
- **Solution:** Verify delta buffers are cleared each iteration, check constraint stiffness values, reduce time step

## Contact

For issues specific to CUDA implementation, check:
- CUDA error logs in console output
- [`cuda-cloth-implementation-summary.md`](../../../../plans/cuda-cloth-implementation-summary.md) - Full implementation details
- [`cuda-cloth-migration-plan.md`](../../../../plans/cuda-cloth-migration-plan.md) - Original migration plan
