/**
 * Cloth Simulation Common Shader Definitions
 * Shared structures and constants for cloth compute shaders
 */

#ifndef CLOTH_COMMON_HLSLI
#define CLOTH_COMMON_HLSLI

// Constant buffer for cloth simulation parameters
cbuffer ClothSimConstants : register(b0)
{
    uint NumParticles;
    uint NumConstraints;
    uint NumBendConstraints;
    uint NumKinematicTargets;
    
    float DeltaTime;
    float Damping;
    float StretchStiffness;
    float BendStiffness;

    float3 Gravity;
    float AirDrag;

    float3 Wind;
    uint NumIterations;
    
    uint CurrentIteration;
    uint UseXPBD;
    float2 TempPadding;

    float4x4 WorldMatrix;
};

/**
 * Particle data structure
 * Stores position and inverse mass for each cloth particle
 */
struct FClothParticle
{
    float3 Position;
    float InvMass;  // 0 = fixed/kinematic particle
};

/**
 * Velocity data structure  
 * Separate from position for better cache coherency
 */
struct FClothVelocity
{
    float3 Velocity;
    float Padding;  // Align to 16 bytes
};

/**
 * Distance constraint structure
 * Connects two particles with a rest length
 */
struct FDistanceConstraint
{
    uint ParticleA;
    uint ParticleB;
    float RestLength;
    float Stiffness;

    float Compliance; // XPBD
    float Lambda;     // XPBD
    float Padding0;
    float Padding1;
};

/**
 * Bend constraint structure
 * Used for cloth bending resistance
 */
struct FBendConstraint
{
    uint ParticleA;
    uint ParticleB;
    uint ParticleC;
    uint ParticleD;

    float RestAngle;
    float Stiffness;
    float Compliance; // XPBD
    float Lambda;     // XPBD
};

/**
 * Kinematic target structure
 * Used for pinning cloth vertices to kinematic targets (e.g., flag on pole)
 */
struct FKinematicTarget
{
    uint ParticleIndex;    // Which particle to constrain
    float Stiffness;       // 1.0 = hard kinematic
    float Padding0;
    float Padding1;
    
    float3 TargetPosition; // World-space target position
    float Padding2;
};

/**
 * Collision sphere structure
 */
struct FClothCollisionSphere
{
    float3 Center;
    float Radius;
};

/**
 * Collision capsule structure
 */
struct FClothCollisionCapsule
{
    float3 Start;
    float Radius;
    float3 End;
    float Padding;
};

// Helper functions

/**
 * Safely normalize a vector, returning zero vector if length is too small
 */
float3 SafeNormalize(float3 v, float epsilon = 1e-6f)
{
    float len = length(v);
    return len > epsilon ? v / len : float3(0, 0, 0);
}

/**
 * Calculate squared distance between two points
 */
float DistanceSquared(float3 a, float3 b)
{
    float3 diff = b - a;
    return dot(diff, diff);
}

/**
 * Clamp a value between min and max
 */
float ClampFloat(float value, float minVal, float maxVal)
{
    return max(minVal, min(maxVal, value));
}

/**
 * Clamp a vector component-wise
 */
float3 ClampFloat3(float3 value, float minVal, float maxVal)
{
    return float3(
        ClampFloat(value.x, minVal, maxVal),
        ClampFloat(value.y, minVal, maxVal),
        ClampFloat(value.z, minVal, maxVal)
    );
}

#endif // CLOTH_COMMON_HLSLI
