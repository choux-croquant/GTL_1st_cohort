/**
 * Cloth Batch Types
 * Common types and structures for batched cloth simulation
 */

#pragma once

#include "HAL/PlatformType.h"
#include "Container/Array.h"
#include "Math/Vector.h"
#include "ClothSimulationData.h"
#include "Cloth/ClothAssetGenerator.h"

// Forward declarations
class FClothBatchManager;
class UClothComponent;

/**
 * LOD Level enumeration for cloth simulation
 */
enum class EClothLODLevel : uint8
{
    LOD_0 = 0, // High detail   - Close to camera
    LOD_1 = 1, // Medium detail - Medium distance
    LOD_2 = 2, // Low detail    - Far from camera

    Max
};

/**
 * LOD selection parameters
 */
struct FClothLODSelectionParams
{
    float LOD0Distance = 1000.0f; // 0-1000cm: LOD 0
    float LOD1Distance = 3000.0f; // 1000-3000cm: LOD 1
    float LOD2Distance = 6000.0f; // 3000-6000cm: LOD 2

    bool bEnableDistanceCulling = true;
    float MaxCullDistance = 10000.0f;
};

/**
 * Per-instance parameters for batched simulation
 * Must be 64-byte aligned for constant buffer compatibility
 */
struct FClothInstanceParameters
{
    // Forces (world-space)
    FVector Gravity;         // 12 bytes - Per-instance gravity
    float GravityMultiplier; // 4 bytes

    FVector Wind;       // 12 bytes - Per-instance wind
    float WindStrength; // 4 bytes

    // Material properties
    float AirDrag;          // 4 bytes - Drag coefficient
    float Damping;          // 4 bytes - Velocity damping
    float StretchStiffness; // 4 bytes - Distance constraint multiplier
    float BendStiffness;    // 4 bytes - Bend constraint multiplier

    // Instance identification
    uint32 ParticleOffset;   // 4 bytes - First particle index
    uint32 ParticleCount;    // 4 bytes - Number of particles
    uint32 ConstraintOffset; // 4 bytes - First constraint index
    uint32 ConstraintCount;  // 4 bytes - Number of constraints

    uint32 BendConstraintOffset;  // 4 bytes
    uint32 BendConstraintCount;   // 4 bytes
    uint32 KinematicTargetOffset; // 4 bytes
    uint32 KinematicTargetCount;  // 4 bytes

    uint32 TriangleOffset;       // 4 bytes
    uint32 TriangleCount;        // 4 bytes
    uint32 AreaConstraintOffset; // 4 bytes - NEW: Area constraint offset
    uint32 AreaConstraintCount;  // 4 bytes - NEW: Area constraint count

    uint32 EdgeCollisionOffset;  // 4 bytes - NEW: Edge collision offset
    uint32 EdgeCollisionCount;   // 4 bytes - NEW: Edge collision count
    uint32 IsActive;             // 4 bytes - Enable/disable flag
    uint32 Padding;              // 4 bytes

    FClothInstanceParameters()
        : Gravity(0.0f, 0.0f, -980.0f), GravityMultiplier(1.0f), Wind(0.0f, 0.0f, 0.0f), WindStrength(1.0f), AirDrag(1.0f), Damping(0.5f), StretchStiffness(0.9f), BendStiffness(0.9f), ParticleOffset(0), ParticleCount(0), ConstraintOffset(0), ConstraintCount(0), BendConstraintOffset(0), BendConstraintCount(0), KinematicTargetOffset(0), KinematicTargetCount(0), TriangleOffset(0), TriangleCount(0), AreaConstraintOffset(0), AreaConstraintCount(0), EdgeCollisionOffset(0), EdgeCollisionCount(0), IsActive(1), Padding(0)
    {
    }
};

// Verify structure size for GPU compatibility
static_assert(sizeof(FClothInstanceParameters) == 112, "FClothInstanceParameters must be 112 bytes");

/**
 * Instance metadata for tracking buffer ranges
 */
struct FClothInstanceMetadata
{
    // Simulation mesh buffer ranges
    uint32 ParticleOffset;
    uint32 ParticleCount;
    uint32 ConstraintOffset;
    uint32 ConstraintCount;
    uint32 BendConstraintOffset;
    uint32 BendConstraintCount;
    uint32 KinematicTargetOffset;
    uint32 KinematicTargetCount;
    uint32 TriangleOffset;
    uint32 TriangleCount;
    uint32 AreaConstraintOffset;   // Area constraint offset
    uint32 AreaConstraintCount;    // Area constraint count
    uint32 EdgeCollisionOffset;    // Edge collision offset
    uint32 EdgeCollisionCount;     // Edge collision count

    // Render mesh buffer ranges (for production rendering)
    uint32 RenderVertexOffset;     // Offset into unified render vertex buffers
    uint32 RenderVertexCount;      // Number of render vertices
    uint32 RenderIndexOffset;      // Offset into unified render index buffer
    uint32 RenderIndexCount;       // Number of render indices (triangles * 3)

    // Instance ID in parameter buffer
    uint32 InstanceParameterIndex;

    // State
    bool bIsActive;
    EClothLODLevel CurrentLOD;

    FClothInstanceMetadata()
        : ParticleOffset(0), ParticleCount(0), ConstraintOffset(0), ConstraintCount(0), BendConstraintOffset(0), BendConstraintCount(0), KinematicTargetOffset(0), KinematicTargetCount(0), TriangleOffset(0), TriangleCount(0), AreaConstraintOffset(0), AreaConstraintCount(0), EdgeCollisionOffset(0), EdgeCollisionCount(0), RenderVertexOffset(0), RenderVertexCount(0), RenderIndexOffset(0), RenderIndexCount(0), InstanceParameterIndex(0), bIsActive(true), CurrentLOD(EClothLODLevel::LOD_0)
    {
    }
};

/**
 * Instance creation parameters
 */
struct FClothInstanceCreationParams
{
    // Simulation mesh data (in LOCAL space - will be transformed to world space during upload)
    TArray<FVector> RestPositions;
    TArray<float> InvMasses;
    TArray<uint32> Indices;
    TArray<FClothDistanceConstraint> Constraints;
    TArray<FClothBendConstraint> BendConstraints;
    TArray<FClothAreaConstraint> AreaConstraints;           // Area constraints
    TArray<FClothEdgeCollisionConstraint> EdgeCollisions;   // Edge collision constraints
    TArray<FClothAttachmentData> Attachments;

    // NEW: Production rendering - render mesh data (in LOCAL space)
    bool bUseRenderMesh = false;                            // Flag to enable production rendering
    TArray<FVector> RenderRestPositions;                    // High-res render mesh positions
    TArray<FVector> RenderNormals;                          // Render mesh normals
    TArray<FVector2D> RenderUVs;                            // Render mesh UVs
    TArray<uint32> RenderIndices;                           // Render mesh indices
    TArray<struct FClothSkinningWeight> SkinningWeights;    // Render → Sim vertex mapping

    // Configuration
    FClothConfig Config;
    FClothInstanceParameters InstanceParams;

    // Transform (For converting local-space positions to world space)
    FTransform WorldTransform;

    // Owner
    UClothComponent *OwnerComponent;

    // Initial state
    EClothLODLevel InitialLOD = EClothLODLevel::LOD_0;
    bool bStartActive = true;

    FClothInstanceCreationParams()
        : bUseRenderMesh(false), WorldTransform(FTransform::Identity), OwnerComponent(nullptr), InitialLOD(EClothLODLevel::LOD_0), bStartActive(true)
    {
    }
};

/**
 * Fixed timestep state
 */
struct FClothFixedTimestepState
{
    float FixedTimestep = 1.0f / 60.0f; // 60 Hz default
    float AccumulatedTime = 0.0f;
    int32 MaxSubsteps = 5; // Prevent death spiral
    bool bUseFixedTimestep = true;
};

/**
 * Cloth system mode
 */
enum class EClothSystemMode : uint8
{
    Batched // New batched system
};
