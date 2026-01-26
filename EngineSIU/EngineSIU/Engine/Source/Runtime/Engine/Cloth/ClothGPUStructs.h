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
 * Note: InvMass is now stored in a separate buffer for batched simulation
 */
struct FClothParticleGPU
{
    FVector Position;
    uint32 InstanceID; // Which instance owns this particle (for batched simulation)
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
struct FClothDistanceConstraintGPU
{
    uint32 ParticleA; // 4 bytes
    uint32 ParticleB; // 4 bytes
    float RestLength; // 4 bytes
    float Stiffness;  // 4 bytes

    float Compliance;  // 4 bytes (XPBD)
    float Lambda;      // 4 bytes (XPBD state)
    uint32 ColorGroup; // 4 bytes - Graph coloring group
    float Padding0;    // 4 bytes
    // Total: 32 bytes
};

struct FClothBendConstraintGPU
{
    uint32 ParticleA; // Shared edge vertex 1
    uint32 ParticleB; // Shared edge vertex 2
    uint32 ParticleC; // Triangle 1 opposite vertex
    uint32 ParticleD; // Triangle 2 opposite vertex
    float RestAngle;  // Dihedral angle at rest (radians)
    float Stiffness;  // Bend stiffness [0-1]
    float Compliance; // XPBD compliance
    float Lambda;     // XPBD lambda (warm start)
    // Total: 32bytes
};

/**
 * GPU shear constraint (32 bytes, aligned)
 * Must match FShearConstraint in ClothCommon.hlsli exactly
 */
struct FClothShearConstraintGPU
{
    uint32 ParticleA, ParticleB, ParticleC; // 12 bytes
    float RestDot;                          // 4 bytes
    float Compliance;                       // 4 bytes
    float Lambda;                           // 4 bytes
    float Padding0, Padding1;               // 8 bytes
    // Total: 32 bytes
};

/**
 * GPU area constraint (32 bytes, aligned)
 * Must match FAreaConstraint in ClothCommon.hlsli exactly
 */
struct FClothAreaConstraintGPU
{
    uint32 ParticleA, ParticleB, ParticleC; // 12 bytes
    float RestArea;                         // 4 bytes
    FVector RestNormal;                     // 12 bytes
    float Lambda;                           // 4 bytes
    // Total: 32 bytes
};

/**
 * Kinematic target structure (32 bytes, aligned)
 * Used for pinning cloth vertices to kinematic targets (e.g., flag on pole, cape on shoulders)
 * Supports both hard kinematic attachment and Long Range Attachment (LRA)
 */
struct FClothKinematicTargetGPU
{
    uint32 ParticleIndex; // 4 bytes - Which particle to constrain
    float Stiffness;      // 4 bytes - 1.0 = hard kinematic, <1.0 = soft spring
    float AttachDistance; // 4 bytes - NEW: Max distance for LRA (0 = hard kinematic)
    float Padding0;       // 4 bytes

    FVector TargetPosition; // 12 bytes - World-space target position
    float Padding1;         // 4 bytes
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
static_assert(sizeof(FClothDistanceConstraintGPU) == 32, "FClothConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothBendConstraintGPU) == 32, "FClothBendConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothShearConstraintGPU) == 32, "FClothShearConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothAreaConstraintGPU) == 32, "FClothAreaConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothKinematicTargetGPU) == 32, "FClothKinematicTargetGPU must be 32 bytes");
static_assert(sizeof(FClothCollisionSphereGPU) == 16, "FClothCollisionSphereGPU must be 16 bytes");
static_assert(sizeof(FClothCollisionCapsuleGPU) == 32, "FClothCollisionCapsuleGPU must be 32 bytes");

// Verify alignment
static_assert(alignof(FClothParticleGPU) == 4, "FClothParticleGPU alignment");
static_assert(alignof(FClothVelocityGPU) == 4, "FClothVelocityGPU alignment");
static_assert(alignof(FClothDistanceConstraintGPU) == 4, "FClothConstraintGPU alignment");
static_assert(alignof(FClothBendConstraintGPU) == 4, "FClothBendConstraintGPU alignment");
static_assert(alignof(FClothShearConstraintGPU) == 4, "FClothShearConstraintGPU alignment");
static_assert(alignof(FClothAreaConstraintGPU) == 4, "FClothAreaConstraintGPU alignment");
static_assert(alignof(FClothKinematicTargetGPU) == 4, "FClothKinematicTargetGPU alignment");
