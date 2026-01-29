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
 * GPU distance constraint structure (32 bytes, aligned)
 * Must match FDistanceConstraint in ClothCommon.hlsli
 *
 * Supports both PBD and XPBD formulations:
 *
 * PBD (legacy):
 *   - Uses Stiffness directly to scale constraint violation
 *   - Simple but time-step and iteration-count dependent
 *
 * XPBD (recommended):
 *   - Uses Compliance for time-step independent stiffness
 *   - Uses Lambda to accumulate constraint force (warm starting)
 *   - Much more stable and predictable across varying time steps
 *
 * Compliance Calculation:
 *   - Lower compliance = stiffer (0.0 = perfectly rigid)
 *   - Higher compliance = softer (allows more violation under force)
 *   - Typical range: 1e-8 to 0.1 for cloth
 *   - Computed from: compliance = (1 - stiffness^4) * scale
 *
 * Lambda (Lagrange Multiplier):
 *   - Accumulated constraint force from XPBD solver
 *   - Provides warm starting between iterations and frames
 *   - Reset to 0 on cloth initialization or optionally per frame
 *   - Updated by GPU solver (requires writable buffer)
 */
struct FClothDistanceConstraintGPU
{
    uint32 ParticleA; // 4 bytes - First particle index
    uint32 ParticleB; // 4 bytes - Second particle index
    float RestLength; // 4 bytes - Rest distance between particles
    float Stiffness;  // 4 bytes - PBD stiffness (0-1) or XPBD authoring parameter

    float Compliance; // 4 bytes - XPBD compliance (inverse stiffness in force space)
    float Lambda;     // 4 bytes - XPBD accumulated Lagrange multiplier
    float Padding0;   // 4 bytes - Alignment padding
    float Padding1;   // 4 bytes - Alignment padding
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
 * GPU attachment data structure (32 bytes, aligned)
 * Compact representation for GPU-based kinematic target computation
 * CPU uploads component transforms, GPU computes final positions
 */
struct FKinematicAttachmentGPU
{
    uint32 ComponentIndex; // 4 bytes - Index into ComponentTransforms buffer
    uint32 ParticleIndex;  // 4 bytes - Target particle index in batch
    float Stiffness;       // 4 bytes - Attachment strength (0-1)
    float AttachDistance;  // 4 bytes - Max distance for LRA (0 = hard kinematic)

    FVector LocalOffset; // 12 bytes - Local space offset from component
    float Padding;       // 4 bytes - Align to 32 bytes
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

/**
 * Unified cloth collider structure (64 bytes, aligned)
 * Single structure for all collider types (sphere/capsule/box)
 * Must match FClothCollider in ClothCommon.hlsli
 */
struct FClothColliderGPU
{
    uint32 Type;      // 4 bytes - EClothColliderType: 0=Sphere, 1=Capsule, 2=Box
    float Radius;     // 4 bytes - Sphere/Capsule radius
    float HalfHeight; // 4 bytes - Capsule half-height (0 for others)
    float Padding0;   // 4 bytes

    FVector Center; // 12 bytes - World-space center
    float Padding1; // 4 bytes

    FVector Axis;   // 12 bytes - Capsule axis (normalized), Box orientation
    float Padding2; // 4 bytes

    FVector Extents; // 12 bytes - Box half-extents (0 for sphere/capsule)
    float Padding3;  // 4 bytes
                     // Total: 64 bytes
};

// Static assertions to verify structure sizes (C++ only)
static_assert(sizeof(FClothParticleGPU) == 16, "FClothParticleGPU must be 16 bytes");
static_assert(sizeof(FClothVelocityGPU) == 16, "FClothVelocityGPU must be 16 bytes");
static_assert(sizeof(FClothDistanceConstraintGPU) == 32, "FClothConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothBendConstraintGPU) == 32, "FClothBendConstraintGPU must be 32 bytes");
static_assert(sizeof(FClothKinematicTargetGPU) == 32, "FClothKinematicTargetGPU must be 32 bytes");
static_assert(sizeof(FKinematicAttachmentGPU) == 32, "FKinematicAttachmentGPU must be 32 bytes");
static_assert(sizeof(FClothCollisionSphereGPU) == 16, "FClothCollisionSphereGPU must be 16 bytes");
static_assert(sizeof(FClothCollisionCapsuleGPU) == 32, "FClothCollisionCapsuleGPU must be 32 bytes");
static_assert(sizeof(FClothColliderGPU) == 64, "FClothColliderGPU must be 64 bytes");

// Verify alignment
static_assert(alignof(FClothParticleGPU) == 4, "FClothParticleGPU alignment");
static_assert(alignof(FClothVelocityGPU) == 4, "FClothVelocityGPU alignment");
static_assert(alignof(FClothDistanceConstraintGPU) == 4, "FClothConstraintGPU alignment");
static_assert(alignof(FClothBendConstraintGPU) == 4, "FClothBendConstraintGPU alignment");
static_assert(alignof(FClothKinematicTargetGPU) == 4, "FClothKinematicTargetGPU alignment");
static_assert(alignof(FKinematicAttachmentGPU) == 4, "FKinematicAttachmentGPU alignment");
