/**
 * Cloth GPU Structures
 *
 * Shared data structures for cloth simulation that work across C++ and HLSL.
 *
 * IMPORTANT: Structures must match the HLSL definitions in ClothCommon.hlsli exactly
 * for proper GPU buffer compatibility.
 */

#pragma once

// C++ mode - use engine vector types
#include "Core/HAL/PlatformType.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Matrix.h"

// FClothSimConstants is defined in ShaderConstants.h for C++ mode
// Forward declare it here to avoid circular include, full definition pulled in by users
struct FClothSimConstants;
struct FClothNormalUpdateConstants;

/**
 * GPU particle structure (16 bytes, aligned)
 * Must match FClothParticle in ClothCommon.hlsli
 */
struct FClothParticleGPU
{
    FVector Position;
    float InvMass;
};

/**
 * GPU velocity structure (16 bytes, aligned)
 * Must match FClothVelocity in ClothCommon.hlsli
 */
struct FClothVelocityGPU
{
    FVector Velocity;
    float Padding;
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

// Static assertions to verify structure sizes (C++ only)
static_assert(sizeof(FClothParticleGPU) == 16, "FClothParticleGPU must be 16 bytes");
static_assert(sizeof(FClothVelocityGPU) == 16, "FClothVelocityGPU must be 16 bytes");
static_assert(sizeof(FClothConstraintGPU) == 32, "FClothConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothCollisionSphereGPU) == 16, "FClothCollisionSphereGPU must be 16 bytes");
static_assert(sizeof(FClothCollisionCapsuleGPU) == 32, "FClothCollisionCapsuleGPU must be 32 bytes");

// Verify alignment
static_assert(alignof(FClothParticleGPU) == 4, "FClothParticleGPU alignment");
static_assert(alignof(FClothVelocityGPU) == 4, "FClothVelocityGPU alignment");
static_assert(alignof(FClothConstraintGPU) == 4, "FClothConstraintGPU alignment");
