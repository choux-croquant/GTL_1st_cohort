/**
 * Shader Constants for Cloth Simulation
 * C++ structures that mirror HLSL constant buffers
 * Must match ClothCommon.hlsli exactly for proper GPU upload
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Matrix.h"

/**
 * Cloth simulation constant buffer
 * Must match cbuffer ClothSimConstants in ClothCommon.hlsli exactly
 */
struct FClothSimConstants
{
    // Basic counts
    uint32 NumParticles;
    uint32 NumConstraints;
    uint32 NumBendConstraints;
    uint32 NumKinematicTargets;

    // Timing and material
    float DeltaTime;
    float Damping;
    float StretchStiffness;
    float BendStiffness;

    // Forces
    FVector Gravity;
    float AirDrag;

    FVector Wind;
    uint32 NumIterations;

    // Solver parameters
    uint32 CurrentIteration;
    uint32 UseXPBD;
    float RelaxationFactor;
    float MaxSpeed;

    float LongRangeStretchiness;
    float Padding3;

    FMatrix WorldMatrix;

    // PhysixStudio-style per-constraint-type parameters
    uint32 NumShearConstraints;
    uint32 NumAreaConstraints;
    uint32 NumLRAEntries;
    uint32 NumSubsteps;

    float ComplianceStretch; // XPBD compliance for distance constraints
    float ComplianceShear;   // XPBD compliance for shear constraints
    float ComplianceBend;    // XPBD compliance for bend constraints
    float ComplianceArea;    // XPBD compliance for area constraints

    float BetaStretch; // Velocity-level damping for stretch
    float BetaBend;    // Velocity-level damping for bend
    float Thickness;   // Collision thickness
    float Friction;    // Ground/collision friction

    // Padding to ensure 16-byte alignment
    float Padding4;
    float Padding5;
    float Padding6;
    float Padding7;
};

/**
 * Normal update constant buffer (if needed separately)
 */
struct FClothNormalUpdateConstants
{
    uint32 NumParticles;
    uint32 NumTriangles;
    float Padding0;
    float Padding1;
};
