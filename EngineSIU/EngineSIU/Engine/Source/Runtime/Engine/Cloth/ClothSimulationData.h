#pragma once

#include "HAL/PlatformType.h"
#include "Container/Array.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Math/Quat.h"
#include "CoreUObject/UObject/NameTypes.h"
#include "Components/SceneComponent.h"

/**
 * Cloth Simulation Data Structures
 * These structures define the runtime simulation state and configuration parameters
 */

/**
 * Global simulation configuration parameters
 */
struct FClothConfig
{
    // Global simulation settings
    //FVector Gravity = {0.0f, 0.0f, -980.0f};
    FVector Gravity = { 0.0f, 0.0f, -0.0f };
    float Mass = 1.0f;
    float Damping = 5.0f;

    // Constraint stiffness (0-1)
    float StretchStiffness = 0.9f;
    float BendStiffness = 0.9f;
    float AreaStiffness = 0.001f; // NEW: Area constraint stiffness (0-1)
    float AttachStiffness = 1.0f;
    float LongRangeStretchiness = 1.2f; // NEW: LRA slack multiplier (Velvet default)

    // Solver settings
    int32 NumIterations = 5;
    float TimeStep = 0.016f; // Fixed 60fps or variable
    bool bUseXPBD = false;   // Use XPBD instead of PBD

    // NEW: Substep settings (Velvet-inspired)
    int32 NumSubsteps = 5;                  // How many substeps per frame time
    float FixedSubstepTime = 1.0f / 600.0f; // Target substep dt (120 Hz default)
    int32 MaxSubstepsPerFrame = 10;         // Safety limit to prevent death spiral
    float MaxSpeed = 300.0f;               // Velocity clamping (cm/s)
    float RelaxationFactor = 1.0f;          // Jacobi convergence control

    // Wind and drag
    float AirDrag = 1.0f;
    float WindStrength = 1.0f;

    // Collision
    float CollisionThickness = 1.3f;
    float CollisionFriction = 0.1f;
    bool bEnableSelfCollision = true;
    
    // Self-collision parameters (REFACTORED: Now use multipliers for adaptive computation)
    // MULTIPLIERS (applied to adaptive base values computed from mesh topology)
    float SelfCollisionRadiusMultiplier = 0.75f;      // × (AvgEdgeLength × 0.5)
    float SelfCollisionStiffnessMultiplier = 0.3f;   // × adaptive base stiffness
    float SelfCollisionCellSizeMultiplier = 1.0f;    // × (AvgEdgeLength × 1.5)
    
    // Grid capacity (still absolute, but computed adaptively per instance)
    uint32 SelfCollisionMaxPerCell = 32;   // Max particles per cell (safety limit)
    
    // NEW: Dynamic bounds tracking parameters
    float BoundsUpdateMotionThreshold = 0.15f;  // Trigger update when motion exceeds 15% of bounds size
    uint32 BoundsUpdateMaxFrames = 60;          // Force update every N frames regardless of motion
    bool bEnableDynamicBoundsUpdate = true;     // Enable/disable dynamic bounds tracking
    
    // DEPRECATED (kept for backwards compatibility, but ignored in favor of multipliers)
    float SelfCollisionRadius = 0.01f;      // DEPRECATED: Use SelfCollisionRadiusMultiplier instead
    float SelfCollisionStiffness = 1.0f;   // DEPRECATED: Use SelfCollisionStiffnessMultiplier instead
    uint32 SelfCollisionGridDim = 32;       // DEPRECATED: Computed adaptively from mesh bounds
    
    // Edge-based collision (NEW: Prevents edge penetration in low-resolution meshes)
    bool bEnableEdgeCollision = true;      // Enable edge-based SDF collision
    int32 EdgeSamplesPerEdge = 3;          // Number of sample points per edge (3-5 recommended)
};

/**
 * Generic constraint structure for distance and bend constraints
 */
struct FClothDistanceConstraint
{
    uint32 ParticleA;
    uint32 ParticleB;
    float RestLength; // For distance constraints
    float Stiffness;  // Per-constraint stiffness
    float Compliance; // XPBD용
    float Lambda;     // XPBD용 상태

    FClothDistanceConstraint()
        : ParticleA(0), ParticleB(0), RestLength(0.0f), Stiffness(1.0f), Compliance(0.0f), Lambda(0.0f)
    {
    }

    FClothDistanceConstraint(uint32 InA, uint32 InB, float InRestLength, float InStiffness = 1.0f)
        : ParticleA(InA), ParticleB(InB), RestLength(InRestLength), Stiffness(InStiffness), Compliance(0.0f) // 기본값: hard constraint
          ,
          Lambda(0.0f) // 누적값 초기화
    {
    }

    FClothDistanceConstraint(uint32 InA, uint32 InB, float InRestLength, float InStiffness, float InCompliance)
        : ParticleA(InA), ParticleB(InB), RestLength(InRestLength), Stiffness(InStiffness), Compliance(InCompliance), Lambda(0.0f) // 누적값은 항상 0으로 시작
    {
    }
};

struct FClothBendConstraint
{
    uint32 ParticleA; // Shared edge vertex 1
    uint32 ParticleB; // Shared edge vertex 2
    uint32 ParticleC; // Triangle 1 opposite vertex
    uint32 ParticleD; // Triangle 2 opposite vertex
    float RestAngle;  // Dihedral angle at rest (radians)
    float Stiffness;  // Bend stiffness [0-1]
    float Compliance; // XPBD compliance
    float Lambda;     // XPBD lambda (warm start)

    FClothBendConstraint()
        : ParticleA(0), ParticleB(0), ParticleC(0), ParticleD(0), RestAngle(0.0f), Stiffness(1.0f), Compliance(0.0f), Lambda(0.0f)
    {
    }

    FClothBendConstraint(uint32 InA, uint32 InB, uint32 InC, uint32 InD, float InRestAngle, float InStiffness = 1.0f)
        : ParticleA(InA), ParticleB(InB), ParticleC(InC), ParticleD(InD), RestAngle(InRestAngle), Stiffness(InStiffness), Compliance(0.0f), Lambda(0.0f)
    {
    }

    // 선택: XPBD 파라미터를 직접 지정하는 생성자
    FClothBendConstraint(uint32 InA, uint32 InB, uint32 InC, uint32 InD, float InRestAngle, float InStiffness, float InCompliance)
        : ParticleA(InA), ParticleB(InB), ParticleC(InC), ParticleD(InD), RestAngle(InRestAngle), Stiffness(InStiffness), Compliance(InCompliance), Lambda(0.0f)
    {
    }
};

/**
 * Area constraint structure
 * Preserves triangle area to prevent excessive in-plane deformation
 * Complements distance constraints by resisting area change under shear/compression
 */
struct FClothAreaConstraint
{
    uint32 ParticleA;   // First triangle vertex
    uint32 ParticleB;   // Second triangle vertex
    uint32 ParticleC;   // Third triangle vertex
    float RestArea;     // Rest area of triangle
    FVector RestNormal; // Rest normal (for signed area computation)
    float Compliance;   // XPBD compliance (inverse stiffness)
    float Lambda;       // XPBD accumulated Lagrange multiplier
    float Stiffness;    // Authoring parameter (not directly used in XPBD solve)

    FClothAreaConstraint()
        : ParticleA(0), ParticleB(0), ParticleC(0), RestArea(0.0f), RestNormal(FVector::ZeroVector), Compliance(0.0f), Lambda(0.0f), Stiffness(1.0f)
    {
    }

    FClothAreaConstraint(uint32 InA, uint32 InB, uint32 InC, float InRestArea, const FVector &InRestNormal, float InCompliance = 1e-5f)
        : ParticleA(InA), ParticleB(InB), ParticleC(InC), RestArea(InRestArea), RestNormal(InRestNormal), Compliance(InCompliance), Lambda(0.0f), Stiffness(1.0f)
    {
    }

    FClothAreaConstraint(uint32 InA, uint32 InB, uint32 InC, float InRestArea, const FVector &InRestNormal, float InCompliance, float InStiffness)
        : ParticleA(InA), ParticleB(InB), ParticleC(InC), RestArea(InRestArea), RestNormal(InRestNormal), Compliance(InCompliance), Lambda(0.0f), Stiffness(InStiffness)
    {
    }
};

/**
 * Edge collision constraint structure
 * Used for edge-based SDF collision to prevent low-resolution cloth edges from penetrating colliders
 * Each edge samples multiple points along its length and checks collision for all sample points
 */
struct FClothEdgeCollisionConstraint
{
    uint32 ParticleA;   // First edge vertex
    uint32 ParticleB;   // Second edge vertex
    float RestLength;   // Rest length of edge (for validation/debugging)
    float Padding;      // Alignment padding

    FClothEdgeCollisionConstraint()
        : ParticleA(0), ParticleB(0), RestLength(0.0f), Padding(0.0f)
    {
    }

    FClothEdgeCollisionConstraint(uint32 InA, uint32 InB, float InRestLength)
        : ParticleA(InA), ParticleB(InB), RestLength(InRestLength), Padding(0.0f)
    {
    }
};

/**
 * Per-vertex authoring parameters for painted vertex data
 */
struct FClothVertexPaintData
{
    float MaxDistance = 1.0f;      // How far vertex can move from rest
    float BackstopDistance = 0.0f; // Collision backstop
    float BackstopRadius = 0.0f;
    float Stiffness = 1.0f; // Per-vertex stiffness multiplier
    bool bFixed = false;    // Is vertex pinned?

    FClothVertexPaintData()
        : MaxDistance(1.0f), BackstopDistance(0.0f), BackstopRadius(0.0f), Stiffness(1.0f), bFixed(false)
    {
    }
};

/**
 * LOD-specific mesh data for rendering and simulation
 */
struct FClothLODData
{
    // Simulation mesh (coarse)
    TArray<FVector> SimPositions;
    TArray<uint32> SimIndices;

    // Render mesh (fine detail)
    TArray<FVector> RenderPositions;
    TArray<FVector> RenderNormals;
    TArray<FVector2D> RenderUVs;
    TArray<uint32> RenderIndices;

    float ScreenSize = 1.0f; // LOD switch distance

    FClothLODData()
        : ScreenSize(1.0f)
    {
    }
};

/**
 * Attachment type for kinematic attachments
 */
enum class EClothAttachmentType : uint8
{
    WorldPosition, // Static world position
    SkeletalBone,  // Follow bone transform
    ActorTransform // Follow actor transform
};

// Forward declarations for driver references
class USceneComponent;
class AActor;

/**
 * ASSET-LEVEL: Attachment capability metadata
 * Defines which simulation vertices CAN be attached (not what they're attached to)
 * Stored in UClothAsset as reusable template data
 */
struct FClothAttachmentCapability
{
    uint32 SimVertexIndex;           // Which simulation vertex can be attached
    float DefaultStiffness = 1.0f;   // Default constraint stiffness
    float DefaultAttachDistance = 0.0f; // 0 = hard kinematic, >0 = LRA
    FString DebugName;               // Optional: "LeftShoulder", "RightHip", etc.
    
    FClothAttachmentCapability()
        : SimVertexIndex(0), DefaultStiffness(1.0f), DefaultAttachDistance(0.0f)
    {}
};

/**
 * INSTANCE-LEVEL: Attachment target
 * Defines what a specific instance attaches to (instance-specific)
 * Stored in UClothComponent per-instance
 */
struct FClothAttachmentTarget
{
    EClothAttachmentType Type = EClothAttachmentType::WorldPosition;
    
    // Driver references (instance-specific)
    USceneComponent* DriverComponent = nullptr;
    AActor* DriverActor = nullptr;
    
    // Skeletal mesh attachment
    FName BoneName;
    int32 BoneIndex = -1;
    FTransform LocalOffset = FTransform::Identity;
    
    // World position (cached, auto-updated from driver)
    FVector WorldPosition = FVector::ZeroVector;
    
    FClothAttachmentTarget() {}
};

/**
 * INSTANCE-LEVEL: Attachment binding
 * Combines vertex index with instance-specific target
 * Stored in UClothComponent per-instance
 */
struct FClothAttachmentBinding
{
    uint32 SimVertexIndex;              // Which vertex is attached
    FClothAttachmentTarget Target;      // What it's attached to (instance-specific)
    float Stiffness = 1.0f;             // Per-instance override
    float AttachDistance = 0.0f;        // Per-instance override
    bool bIsActive = true;              // Can disable without unbinding
    
    FClothAttachmentBinding()
        : SimVertexIndex(0), Stiffness(1.0f), AttachDistance(0.0f), bIsActive(true)
    {}
};

/**
 * DEPRECATED: Old attachment data structure (kept for backward compatibility during migration)
 * Will be removed after full migration to new system
 * Use FClothAttachmentCapability (asset) + FClothAttachmentBinding (instance) instead
 */
struct FClothAttachmentData
{
    uint32 ClothVertexIndex;

    // Attachment type
    EClothAttachmentType Type = EClothAttachmentType::WorldPosition;

    // NEW: Driver references for automatic transform resolution
    USceneComponent *DriverComponent = nullptr; // Reference to component that drives this attachment
    AActor *DriverActor = nullptr;              // Alternative: reference to actor

    // For skeletal mesh attachment
    FName BoneName;
    int32 BoneIndex;
    FTransform LocalOffset;

    // For world/actor attachment (cached, auto-updated from driver)
    FVector WorldPosition;

    // Constraint properties
    float Stiffness = 1.0f;
    bool bIsKinematic = true;
    float AttachDistance = 0.0f; // NEW: LRA support - 0 = hard kinematic, >0 = max distance

    FClothAttachmentData()
        : ClothVertexIndex(0), Type(EClothAttachmentType::WorldPosition), DriverComponent(nullptr), DriverActor(nullptr), BoneName(FName()), BoneIndex(-1), LocalOffset(FTransform::Identity), WorldPosition(FVector::ZeroVector), Stiffness(1.0f), bIsKinematic(true), AttachDistance(0.0f)
    {
    }
};

/**
 * Collision primitive structures for cloth collision
 */
struct FClothCollisionSphere
{
    FVector Center;
    float Radius;

    FClothCollisionSphere()
        : Center(FVector::ZeroVector), Radius(1.0f)
    {
    }

    FClothCollisionSphere(const FVector &InCenter, float InRadius)
        : Center(InCenter), Radius(InRadius)
    {
    }
};

struct FClothCollisionCapsule
{
    FVector Start;
    FVector End;
    float Radius;

    FClothCollisionCapsule()
        : Start(FVector::ZeroVector), End(FVector::ZeroVector), Radius(1.0f)
    {
    }

    FClothCollisionCapsule(const FVector &InStart, const FVector &InEnd, float InRadius)
        : Start(InStart), End(InEnd), Radius(InRadius)
    {
    }
};

struct FClothCollisionBox
{
    FVector Center;
    FVector Extent;
    FQuat Rotation;

    FClothCollisionBox()
        : Center(FVector::ZeroVector), Extent(FVector::OneVector), Rotation(FQuat::Identity)
    {
    }

    FClothCollisionBox(const FVector &InCenter, const FVector &InExtent, const FQuat &InRotation)
        : Center(InCenter), Extent(InExtent), Rotation(InRotation)
    {
    }
};

/**
 * Combined collision primitive type
 */
enum class EClothCollisionPrimitiveType : uint8
{
    Sphere,
    Capsule,
    Box
};

struct FClothCollisionPrimitive
{
    EClothCollisionPrimitiveType Type;

    union
    {
        FClothCollisionSphere Sphere;
        FClothCollisionCapsule Capsule;
        FClothCollisionBox Box;
    };

    FClothCollisionPrimitive()
        : Type(EClothCollisionPrimitiveType::Sphere), Sphere()
    {
    }
};

inline FArchive &operator<<(FArchive &Ar, FClothConfig &Cfg)
{
    Ar << Cfg.Gravity;
    Ar << Cfg.Mass;
    Ar << Cfg.Damping;

    Ar << Cfg.StretchStiffness;
    Ar << Cfg.BendStiffness;
    Ar << Cfg.AttachStiffness;
    Ar << Cfg.LongRangeStretchiness;

    Ar << Cfg.NumIterations;
    Ar << Cfg.TimeStep;
    Ar << Cfg.bUseXPBD;

    // NEW: Substep parameters
    Ar << Cfg.NumSubsteps;
    Ar << Cfg.FixedSubstepTime;
    Ar << Cfg.MaxSubstepsPerFrame;
    Ar << Cfg.MaxSpeed;
    Ar << Cfg.RelaxationFactor;

    Ar << Cfg.AirDrag;
    Ar << Cfg.WindStrength;

    Ar << Cfg.CollisionThickness;
    Ar << Cfg.CollisionFriction;
    Ar << Cfg.bEnableSelfCollision;
    
    // Self-collision parameters (NEW: Multipliers for adaptive computation)
    Ar << Cfg.SelfCollisionRadiusMultiplier;
    Ar << Cfg.SelfCollisionStiffnessMultiplier;
    Ar << Cfg.SelfCollisionCellSizeMultiplier;
    Ar << Cfg.SelfCollisionMaxPerCell;
    
    // Dynamic bounds tracking parameters
    Ar << Cfg.BoundsUpdateMotionThreshold;
    Ar << Cfg.BoundsUpdateMaxFrames;
    Ar << Cfg.bEnableDynamicBoundsUpdate;
    
    // DEPRECATED: Old absolute values (kept for backwards compatibility)
    Ar << Cfg.SelfCollisionRadius;
    Ar << Cfg.SelfCollisionStiffness;
    Ar << Cfg.SelfCollisionGridDim;
    
    // NEW: Edge collision parameters
    Ar << Cfg.bEnableEdgeCollision;
    Ar << Cfg.EdgeSamplesPerEdge;

    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothDistanceConstraint &C)
{
    Ar << C.ParticleA;
    Ar << C.ParticleB;
    Ar << C.RestLength;
    Ar << C.Stiffness;
    Ar << C.Compliance;
    Ar << C.Lambda;

    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothBendConstraint &C)
{
    Ar << C.ParticleA;
    Ar << C.ParticleB;
    Ar << C.ParticleC;
    Ar << C.ParticleD;

    Ar << C.RestAngle;
    Ar << C.Stiffness;
    Ar << C.Compliance;
    Ar << C.Lambda;

    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothAreaConstraint &C)
{
    Ar << C.ParticleA;
    Ar << C.ParticleB;
    Ar << C.ParticleC;
    Ar << C.RestArea;
    Ar << C.RestNormal;
    Ar << C.Compliance;
    Ar << C.Lambda;
    Ar << C.Stiffness;

    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothEdgeCollisionConstraint &C)
{
    Ar << C.ParticleA;
    Ar << C.ParticleB;
    Ar << C.RestLength;
    Ar << C.Padding;
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothVertexPaintData &V)
{
    Ar << V.MaxDistance;
    Ar << V.BackstopDistance;
    Ar << V.BackstopRadius;
    Ar << V.Stiffness;
    Ar << V.bFixed;
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothLODData &LOD)
{
    Ar << LOD.SimPositions;
    Ar << LOD.SimIndices;

    Ar << LOD.RenderPositions;
    Ar << LOD.RenderNormals;
    Ar << LOD.RenderUVs;
    Ar << LOD.RenderIndices;

    Ar << LOD.ScreenSize;
    return Ar;
}

// NEW: Serialization for FClothAttachmentCapability (asset-level)
inline FArchive &operator<<(FArchive &Ar, FClothAttachmentCapability &C)
{
    Ar << C.SimVertexIndex;
    Ar << C.DefaultStiffness;
    Ar << C.DefaultAttachDistance;
    Ar << C.DebugName;
    return Ar;
}

// NEW: Serialization for FClothAttachmentTarget (instance-level)
inline FArchive &operator<<(FArchive &Ar, FClothAttachmentTarget &T)
{
    // Serialize enum as uint8
    uint8 TypeAsByte = static_cast<uint8>(T.Type);
    Ar << TypeAsByte;
    if (Ar.IsLoading())
    {
        T.Type = static_cast<EClothAttachmentType>(TypeAsByte);
    }

    // Note: DriverComponent and DriverActor are NOT serialized (runtime references only)
    Ar << T.BoneName;
    Ar << T.BoneIndex;
    Ar << T.LocalOffset;
    Ar << T.WorldPosition;
    return Ar;
}

// NEW: Serialization for FClothAttachmentBinding (instance-level)
inline FArchive &operator<<(FArchive &Ar, FClothAttachmentBinding &B)
{
    Ar << B.SimVertexIndex;
    Ar << B.Target;
    Ar << B.Stiffness;
    Ar << B.AttachDistance;
    Ar << B.bIsActive;
    return Ar;
}

// DEPRECATED: Old attachment data serialization (kept for backward compatibility)
inline FArchive &operator<<(FArchive &Ar, FClothAttachmentData &A)
{
    Ar << A.ClothVertexIndex;

    // Serialize enum as uint8
    uint8 TypeAsByte = static_cast<uint8>(A.Type);
    Ar << TypeAsByte;
    if (Ar.IsLoading())
    {
        A.Type = static_cast<EClothAttachmentType>(TypeAsByte);
    }

    Ar << A.BoneName;
    Ar << A.BoneIndex;
    Ar << A.LocalOffset;
    Ar << A.WorldPosition;
    Ar << A.Stiffness;
    Ar << A.bIsKinematic;
    Ar << A.AttachDistance; // NEW: LRA support
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothCollisionSphere &S)
{
    Ar << S.Center;
    Ar << S.Radius;
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothCollisionCapsule &C)
{
    Ar << C.Start;
    Ar << C.End;
    Ar << C.Radius;
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothCollisionBox &B)
{
    Ar << B.Center;
    Ar << B.Extent;
    Ar << B.Rotation;
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothCollisionPrimitive &P)
{
    // enum class → uint8로 직렬화
    uint8 TypeAsByte = static_cast<uint8>(P.Type);
    Ar << TypeAsByte; // 여기서 더 이상 에러 안 남

    if (Ar.IsLoading())
    {
        P.Type = static_cast<EClothCollisionPrimitiveType>(TypeAsByte);
    }

    switch (P.Type)
    {
    case EClothCollisionPrimitiveType::Sphere:
        Ar << P.Sphere;
        break;
    case EClothCollisionPrimitiveType::Capsule:
        Ar << P.Capsule;
        break;
    case EClothCollisionPrimitiveType::Box:
        Ar << P.Box;
        break;
    }

    return Ar;
}
