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
    uint NumEdgeCollisions;        // NEW: Number of edge collision constraints
    uint EdgeSamplesPerEdge;       // NEW: Number of samples per edge (3-5 recommended)

    float4x4 WorldMatrix;
};

/**
 * Self-collision grid parameters (constant buffer b1)
 * Used for spatial hash grid-based self-collision detection
 * Must match FClothSelfCollisionParams in ClothGPUStructs.h
 */
cbuffer SelfCollisionParams : register(b1)
{
    float3 GridMin;              // AABB min
    float CellSize;              // Cell size
    
    uint3 GridDimensions;        // Grid dimensions (e.g., 32x32x32)
    uint MaxParticlesPerCell;    // Max particles per cell
    
    float CollisionRadius;       // Particle radius
    float CollisionStiffness;    // Separation strength (0-1)
    uint bEnableSelfCollision;   // Enable/disable flag
    uint SelfCollisionPadding;   // Alignment padding
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
    uint Type;
    uint ComponentIndex;   // Index into ComponentTransforms buffer (deduplication!)
    uint ParticleIndex;    // Target particle index in batch
    float Stiffness;       // Attachment strength (0-1)

    float3 TargetPosition;
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
    
    // Collision properties
    float Friction;         // Per-instance friction coefficient (0-10, default 1.0)
    
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
    
    uint EdgeCollisionOffset;   // NEW: Edge collision offset
    uint EdgeCollisionCount;    // NEW: Edge collision count
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
 * Single structure for all collider types (sphere/capsule/box/torus)
 * Must match FClothColliderGPU in ClothGPUStructs.h
 *
 * CRITICAL: HLSL packing rules require explicit padding after float3
 * to match C++ struct layout (FVector is 12 bytes, but float3 aligns to 16)
 */
struct FClothCollider
{
    uint Type;              // 4 bytes - 0=Sphere, 1=Capsule, 2=Box, 3=Torus
    float Radius;           // 4 bytes
    float HalfHeight;       // 4 bytes - Capsule half-height / Torus major radius
    float Padding0;         // 4 bytes
    // Subtotal: 16 bytes
    
    float3 Center;          // 12 bytes - World-space center
    float Padding1;         // 4 bytes - REQUIRED for C++ alignment
    // Subtotal: 16 bytes (offset 16)
    
    float3 Axis;            // 12 bytes - Capsule/Torus axis (normalized)
    float Padding2;         // 4 bytes - REQUIRED for C++ alignment
    // Subtotal: 16 bytes (offset 32)
    
    float3 Extents;         // 12 bytes - Box half-extents
    float Padding3;         // 4 bytes - REQUIRED for C++ alignment
    // Total: 64 bytes (offset 48)
};

/**
 * Edge collision constraint structure (16 bytes, aligned)
 * Used for edge-based SDF collision to prevent low-resolution cloth edges from penetrating colliders
 * Must match FClothEdgeCollisionConstraintGPU in ClothGPUStructs.h
 */
struct FEdgeCollisionConstraint
{
    uint ParticleA;    // First edge vertex
    uint ParticleB;    // Second edge vertex
    float RestLength;  // Rest length of edge (for validation/debugging)
    float Padding;     // Alignment padding
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
