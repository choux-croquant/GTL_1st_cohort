/**
 * Cloth GPU Structures
 *
 * Shared data structures for cloth simulation that work across C++, CUDA, and HLSL.
 * This header can be included from both host (C++) and device (CUDA) code.
 *
 * IMPORTANT: Structures must match the HLSL definitions in ClothCommon.hlsli exactly
 * for proper GPU buffer compatibility.
 */

#pragma once

#ifndef __CUDACC__
// C++ mode - use engine vector types
#include "Core/HAL/PlatformType.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Matrix.h"

// FClothSimConstants is defined in ShaderConstants.h for C++ mode
// Forward declare it here to avoid circular include, full definition pulled in by users
struct FClothSimConstants;
struct FClothNormalUpdateConstants;

#define CUDA_CALLABLE
#define CUDA_DEVICE
#define CUDA_HOST
#define CUDA_GLOBAL
#else
// CUDA mode - define minimal vector types
#define CUDA_CALLABLE __host__ __device__
#define CUDA_DEVICE __device__
#define CUDA_HOST __host__
#define CUDA_GLOBAL __global__

// CUDA-compatible FVector (matches engine's FVector layout)
struct FVector
{
    float X, Y, Z;

    CUDA_CALLABLE FVector() : X(0), Y(0), Z(0) {}
    CUDA_CALLABLE FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

    CUDA_CALLABLE FVector operator+(const FVector &other) const
    {
        return FVector(X + other.X, Y + other.Y, Z + other.Z);
    }

    CUDA_CALLABLE FVector operator-(const FVector &other) const
    {
        return FVector(X - other.X, Y - other.Y, Z - other.Z);
    }

    CUDA_CALLABLE FVector operator*(float scalar) const
    {
        return FVector(X * scalar, Y * scalar, Z * scalar);
    }

    CUDA_CALLABLE float Dot(const FVector &other) const
    {
        return X * other.X + Y * other.Y + Z * other.Z;
    }

    CUDA_CALLABLE float Length() const
    {
        return sqrtf(X * X + Y * Y + Z * Z);
    }
};

// CUDA-compatible FMatrix (minimal definition for constant buffer)
struct FMatrix
{
    float M[4][4];

    CUDA_CALLABLE FMatrix()
    {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                M[i][j] = (i == j) ? 1.0f : 0.0f;
    }
};

// Type aliases for compatibility
typedef unsigned int uint32;
typedef int int32;
#endif

/**
 * GPU particle structure (16 bytes, aligned)
 * Must match FClothParticle in ClothCommon.hlsli
 */
struct FClothParticleGPU
{
    FVector Position; // 12 bytes
    float InvMass;    // 4 bytes (0 = fixed particle)
    // Total: 16 bytes
};

/**
 * GPU velocity structure (16 bytes, aligned)
 * Must match FClothVelocity in ClothCommon.hlsli
 */
struct FClothVelocityGPU
{
    FVector Velocity; // 12 bytes
    float Padding;    // 4 bytes (alignment)
    // Total: 16 bytes
};

/**
 * GPU constraint structure (32 bytes, aligned)
 * Must match FDistanceConstraint in ClothCommon.hlsli
 */
struct FClothConstraintGPU
{
    uint32 ParticleA; // 4 bytes
    uint32 ParticleB; // 4 bytes
    float RestLength; // 4 bytes
    float Stiffness;  // 4 bytes

    float Compliance; // 4 bytes (XPBD)
    float Lambda;     // 4 bytes (XPBD state)
    float Padding0;   // 4 bytes
    float Padding1;   // 4 bytes
    // Total: 32 bytes
};

// Note: FClothSimConstants and FClothNormalUpdateConstants are defined in
// Renderer/ShaderConstants.h for C++ mode. For CUDA mode, we define compatible versions below.

#ifdef __CUDACC__
/**
 * CUDA-compatible simulation constants
 * Must match FClothSimConstants in ShaderConstants.h
 */
struct FClothSimConstants
{
    uint32 NumParticles;
    uint32 NumConstraints;
    float DeltaTime;
    float Damping;

    FVector Gravity;
    float StretchStiffness;

    FVector Wind;
    float BendStiffness;

    float AirDrag;
    uint32 NumIterations;
    uint32 CurrentIteration;
    uint32 UseXPBD;

    FMatrix WorldMatrix;
};

struct FClothNormalUpdateConstants
{
    uint32 NumParticles;
    uint32 NumTriangles;
    float Padding0;
    float Padding1;
};
#endif

/**
 * Cloth collision sphere (16 bytes, aligned)
 */
struct FClothCollisionSphereGPU
{
    FVector Center; // 12 bytes
    float Radius;   // 4 bytes
    // Total: 16 bytes
};

/**
 * Cloth collision capsule (32 bytes, aligned)
 */
struct FClothCollisionCapsuleGPU
{
    FVector Start; // 12 bytes
    float Radius;  // 4 bytes
    FVector End;   // 12 bytes
    float Padding; // 4 bytes
    // Total: 32 bytes
};

// Utility macros for CUDA kernels

#ifdef __CUDACC__
// Atomic add for float3/FVector (CUDA only)
CUDA_DEVICE inline void atomicAddFloat3(FVector *addr, const FVector &delta)
{
    atomicAdd(&addr->X, delta.X);
    atomicAdd(&addr->Y, delta.Y);
    atomicAdd(&addr->Z, delta.Z);
}

// Safe normalize for CUDA
CUDA_DEVICE inline FVector SafeNormalize(const FVector &v, float epsilon = 1e-6f)
{
    float len = v.Length();
    if (len > epsilon)
    {
        float invLen = 1.0f / len;
        return FVector(v.X * invLen, v.Y * invLen, v.Z * invLen);
    }
    return FVector(0, 0, 0);
}

// Distance squared for CUDA
CUDA_DEVICE inline float DistanceSquared(const FVector &a, const FVector &b)
{
    FVector diff = b - a;
    return diff.Dot(diff);
}
#endif

// Static assertions to verify structure sizes (C++ only)
#ifndef __CUDACC__
static_assert(sizeof(FClothParticleGPU) == 16, "FClothParticleGPU must be 16 bytes");
static_assert(sizeof(FClothVelocityGPU) == 16, "FClothVelocityGPU must be 16 bytes");
static_assert(sizeof(FClothConstraintGPU) == 32, "FClothConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothCollisionSphereGPU) == 16, "FClothCollisionSphereGPU must be 16 bytes");
static_assert(sizeof(FClothCollisionCapsuleGPU) == 32, "FClothCollisionCapsuleGPU must be 32 bytes");

// Verify alignment
static_assert(alignof(FClothParticleGPU) == 4, "FClothParticleGPU alignment");
static_assert(alignof(FClothVelocityGPU) == 4, "FClothVelocityGPU alignment");
static_assert(alignof(FClothConstraintGPU) == 4, "FClothConstraintGPU alignment");
#endif
