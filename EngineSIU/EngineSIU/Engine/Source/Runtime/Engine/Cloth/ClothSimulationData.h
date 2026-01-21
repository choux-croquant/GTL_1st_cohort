#pragma once

#include "HAL/PlatformType.h"
#include "Container/Array.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Math/Quat.h"
#include "CoreUObject/UObject/NameTypes.h"

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
    float Mass = 1.0f;
    float Damping = 0.05f;
    float Friction = 0.1f;

    // Constraint stiffness (0-1)
    float StretchStiffness = 0.9f;
    float BendStiffness = 0.9f;
    float AttachStiffness = 1.0f;

    // Solver settings
    int32 NumIterations = 5;
    float TimeStep = 0.016f; // Fixed 60fps or variable
    bool bUseXPBD = false;   // Use XPBD instead of PBD

    // Wind and drag
    float AirDrag = 1.0f;
    float WindStrength = 1.0f;

    // Collision
    float CollisionThickness = 0.01f;
    bool bEnableSelfCollision = false;
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

    // 선택: XPBD 파라미터를 직접 지정하는 생성자
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
 * Runtime simulation state data
 */
struct FClothSimulationData
{
    uint32 NumParticles = 0;
    uint32 NumConstraints = 0;
    uint32 NumBendConstraints = 0;

    // CPU-side copies (for debugging and readback)
    TArray<FVector> CurrentPositions;
    TArray<FVector> CurrentVelocities;

    // External forces
    FVector Gravity = FVector(0.0f, 0.0f, 0.0f); // cm/s^2
    FVector Wind = FVector(0.0f, 0.0f, 0.0f);
    FVector ExternalForce = FVector(0.0f, 0.0f, 0.0f);

    // Timing
    float CurrentTime = 0.0f;
    float AccumulatedTime = 0.0f;

    FClothSimulationData()
        : NumParticles(0), NumConstraints(0), NumBendConstraints(0), Gravity(0.0f, 0.0f, 0.0f), Wind(0.0f, 0.0f, 0.0f), ExternalForce(0.0f, 0.0f, 0.0f), CurrentTime(0.0f), AccumulatedTime(0.0f)
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

/**
 * Attachment data for connecting cloth to skeletal meshes or static objects
 */
struct FClothAttachmentData
{
    uint32 ClothVertexIndex;

    // Attachment type
    EClothAttachmentType Type = EClothAttachmentType::WorldPosition;

    // For skeletal mesh attachment
    FName BoneName;
    int32 BoneIndex;
    FTransform LocalOffset;

    // For world/actor attachment
    FVector WorldPosition;

    // Constraint properties
    float Stiffness = 1.0f;
    bool bIsKinematic = true;

    FClothAttachmentData()
        : ClothVertexIndex(0), Type(EClothAttachmentType::WorldPosition), BoneName(FName()), BoneIndex(-1), LocalOffset(FTransform::Identity), WorldPosition(FVector::ZeroVector), Stiffness(1.0f), bIsKinematic(true)
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
    Ar << Cfg.Mass;
    Ar << Cfg.Damping;
    Ar << Cfg.Friction;

    Ar << Cfg.StretchStiffness;
    Ar << Cfg.BendStiffness;
    Ar << Cfg.AttachStiffness;

    Ar << Cfg.NumIterations;
    Ar << Cfg.TimeStep;
    Ar << Cfg.bUseXPBD;

    Ar << Cfg.AirDrag;
    Ar << Cfg.WindStrength;

    Ar << Cfg.CollisionThickness;
    Ar << Cfg.bEnableSelfCollision;

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

inline FArchive &operator<<(FArchive &Ar, FClothVertexPaintData &V)
{
    Ar << V.MaxDistance;
    Ar << V.BackstopDistance;
    Ar << V.BackstopRadius;
    Ar << V.Stiffness;
    Ar << V.bFixed;
    return Ar;
}

inline FArchive &operator<<(FArchive &Ar, FClothSimulationData &Sim)
{
    Ar << Sim.NumParticles;
    Ar << Sim.NumConstraints;
    Ar << Sim.NumBendConstraints;

    Ar << Sim.CurrentPositions;
    Ar << Sim.CurrentVelocities;

    Ar << Sim.Gravity;
    Ar << Sim.Wind;
    Ar << Sim.ExternalForce;

    Ar << Sim.CurrentTime;
    Ar << Sim.AccumulatedTime;

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
