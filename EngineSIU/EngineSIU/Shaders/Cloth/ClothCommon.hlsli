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
    float RelaxationFactor;        // NEW: Jacobi convergence control (Velvet-inspired)
    float MaxSpeed;                // NEW: Velocity clamping (Velvet-inspired)
    
    float LongRangeStretchiness;   // NEW: LRA slack multiplier (Velvet default: 1.2)
    uint NumColliders;             // NEW: Number of active colliders
    float CollisionThickness;      // NEW: Collision distance threshold
    float CollisionFriction;       // NEW: Friction coefficient (0-1)
    
    uint NumAreaConstraints;       // NEW: Number of area constraints
    float AreaStiffness;           // NEW: Global area constraint stiffness
    float Padding0;                // Alignment padding
    float Padding1;                // Alignment padding

    float4x4 WorldMatrix;
};

/**
 * Particle data structure
 * Stores position and instance ID for batched simulation
 * InvMass is now stored in a separate buffer for batching support
 */
struct FClothParticle
{
    float3 Position;
    uint InstanceID;  // Which instance owns this particle (for batched simulation)
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
 * Area constraint structure (48 bytes, aligned)
 * Preserves triangle area to resist in-plane stretching/compression
 *
 * Complements distance constraints by preventing triangle collapse or excessive expansion.
 * Uses XPBD formulation with compliance-based stiffness control.
 *
 * Must match FClothAreaConstraintGPU in ClothGPUStructs.h
 */
struct FAreaConstraint
{
    uint ParticleA;     // First triangle vertex
    uint ParticleB;     // Second triangle vertex
    uint ParticleC;     // Third triangle vertex
    float RestArea;     // Rest area of triangle
    
    float3 RestNormal;  // Rest normal (for signed area computation)
    float Compliance;   // XPBD compliance (inverse stiffness)
    
    float Lambda;       // XPBD accumulated Lagrange multiplier
    float Stiffness;    // Authoring parameter (not used in XPBD solve)
    float Padding0;     // Alignment padding
    float Padding1;     // Alignment padding
};

/**
 * Kinematic target structure
 * Used for pinning cloth vertices to kinematic targets (e.g., flag on pole)
 * Supports both hard kinematic attachment and Long Range Attachment (LRA)
 */
struct FKinematicTarget
{
    uint ParticleIndex;    // Which particle to constrain
    float Stiffness;       // 1.0 = hard kinematic, <1.0 = soft spring
    float AttachDistance;  // NEW: Max distance for LRA (0 = hard kinematic)
    float Padding0;
    
    float3 TargetPosition; // World-space target position
    float Padding1;
};

/**
 * Kinematic attachment structure (P1 Optimization - GPU-based computation)
 * Compact representation for GPU-based kinematic target computation
 * CPU uploads component transforms, GPU computes final positions
 * Must match FKinematicAttachmentGPU in ClothGPUStructs.h
 */
struct FKinematicAttachment
{
    uint ComponentIndex;   // Index into ComponentTransforms buffer (deduplication!)
    uint ParticleIndex;    // Target particle index in batch
    float Stiffness;       // Attachment strength (0-1)
    float AttachDistance;  // Max distance for LRA (0 = hard kinematic)
    
    float3 LocalOffset;    // Local space offset from component
    float Padding;         // Align to 32 bytes
};

/**
 * Per-instance parameters for batched simulation
 * Allows different instances to have different material properties
 * Must match FClothInstanceParameters in ClothBatchTypes.h (104 bytes)
 */
struct FClothInstanceParameters
{
    // Forces (world-space)
    float3 Gravity;
    float GravityMultiplier;
    
    float3 Wind;
    float WindStrength;
    
    // Material properties
    float AirDrag;
    float Damping;
    float StretchStiffness;
    float BendStiffness;
    
    // Instance identification
    uint ParticleOffset;
    uint ParticleCount;
    uint ConstraintOffset;
    uint ConstraintCount;
    
    uint BendConstraintOffset;
    uint BendConstraintCount;
    uint KinematicTargetOffset;
    uint KinematicTargetCount;
    
    uint TriangleOffset;
    uint TriangleCount;
    uint AreaConstraintOffset;  // NEW: Area constraint offset
    uint AreaConstraintCount;   // NEW: Area constraint count
    
    uint IsActive;
    uint Padding;
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

/**
 * Unified collider structure (HLSL)
 * Single structure for all collider types (sphere/capsule/box)
 * Must match FClothColliderGPU in ClothGPUStructs.h
 */
struct FClothCollider
{
    uint Type;              // 0=Sphere, 1=Capsule, 2=Box
    float Radius;
    float HalfHeight;       // For capsule only
    float Padding0;
    
    float3 Center;          // World-space center
    float Padding1;
    
    float3 Axis;            // Capsule axis (normalized)
    float Padding2;
    
    float3 Extents;         // Box half-extents
    float Padding3;
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
