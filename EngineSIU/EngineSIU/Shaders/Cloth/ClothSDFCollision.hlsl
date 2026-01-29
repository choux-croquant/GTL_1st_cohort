/**
 * Cloth SDF Collision Solver
 * Analytic signed distance field collision detection and response
 * Operates on predicted particle positions (in-place modification)
 */

#include "ClothCommon.hlsli"

 // Input buffers
StructuredBuffer<FClothCollider> Colliders : register(t0);  // Collider data
StructuredBuffer<float> InvMass : register(t1);             // Inverse masses

// Output buffer (read-write)
RWStructuredBuffer<FClothParticle> PredictedRW : register(u0);  // Modify in-place

static const float EPSILON = 1e-6f;

/**
 * Safe normalize with fallback
 * Returns fallback vector if input is near-zero length
 */
float3 SafeNormalizeWithFallback(float3 v, float3 fallback = float3(0, 1, 0))
{
    float lenSq = dot(v, v);
    if (lenSq < EPSILON * EPSILON)
        return fallback;

    return v * rsqrt(lenSq);  // rsqrt = 1/sqrt (faster than normalize)
}

/**
 * SDF Functions - Analytic signed distance computations
 */

 // Sphere SDF
float sdSphere(float3 pos, float3 center, float radius)
{
    return length(pos - center) - radius;
}

// Capsule SDF (safe version)
float sdCapsule(float3 pos, float3 centerA, float3 centerB, float radius)
{
    float3 pa = pos - centerA;
    float3 ba = centerB - centerA;
    float baLenSq = dot(ba, ba);

    // Handle degenerate capsule (zero height) → treat as sphere
    if (baLenSq < EPSILON)
    {
        return length(pa) - radius;
    }

    float h = saturate(dot(pa, ba) / baLenSq);  // Safe: baLenSq > EPSILON
    return length(pa - ba * h) - radius;
}

// Box SDF (oriented)
float sdBox(float3 pos, float3 center, float3 extents)
{
    float3 localPos = pos - center;
    float3 q = abs(localPos) - extents;
    return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

/**
 * Query SDF for a single collider
 * Returns: signed distance (negative = inside)
 */
float QueryColliderSDF(FClothCollider collider, float3 pos, out float3 normal)
{
    float dist = 0.0f;

    if (collider.Type == 0)  // Sphere
    {
        dist = sdSphere(pos, collider.Center, collider.Radius);

        // Safe normalize: if particle is exactly at center, use upward normal
        normal = SafeNormalizeWithFallback(pos - collider.Center, float3(0, 1, 0));
    }
    else if (collider.Type == 1)  // Capsule
    {
        float3 centerA = collider.Center - collider.Axis * collider.HalfHeight;
        float3 centerB = collider.Center + collider.Axis * collider.HalfHeight;

        float3 ba = centerB - centerA;
        float baLenSq = dot(ba, ba);

        // Degenerate capsule (zero height) → treat as sphere
        if (baLenSq < EPSILON)
        {
            dist = sdSphere(pos, collider.Center, collider.Radius);
            normal = SafeNormalizeWithFallback(pos - collider.Center, float3(0, 1, 0));
            return dist;
        }

        // Normal capsule
        dist = sdCapsule(pos, centerA, centerB, collider.Radius);

        // Compute normal (safe version)
        float3 pa = pos - centerA;
        float h = saturate(dot(pa, ba) / baLenSq);  // Safe: baLenSq > EPSILON
        float3 closestPoint = centerA + ba * h;

        normal = SafeNormalizeWithFallback(pos - closestPoint, float3(0, 1, 0));
    }
    else if (collider.Type == 2)  // Box
    {
        dist = sdBox(pos, collider.Center, collider.Extents);

        // Normal = gradient of SDF (approximate)
        float3 localPos = pos - collider.Center;  // ✅ 수정!
        float3 q = abs(localPos) - collider.Extents;
        float3 gradientDir = sign(localPos) * max(q, 0.0f);

        normal = SafeNormalizeWithFallback(gradientDir, float3(0, 1, 0));

        // Handle interior case
        if (dist < 0.0f)
        {
            float3 interiorNormal = sign(localPos) * step(q.yzx, q.xyz) * step(q.zxy, q.xyz);
            normal = SafeNormalizeWithFallback(interiorNormal, float3(0, 1, 0));
        }
    }

    return dist;
}

/**
 * Collision response
 * Applies position correction and velocity damping
 */
void ApplyCollisionResponse(inout float3 position, float3 normal, float penetration,
    float friction, float invMass)
{
    if (invMass < EPSILON)
        return;  // Kinematic particle - no response

    // Clamp penetration to prevent extreme corrections
    float maxCorrection = 100.0f;  // Adjust based on your scene scale
    penetration = min(penetration, maxCorrection);

    // Position correction - push particle out by penetration distance
    float3 correction = normal * penetration;
    position += correction;

    // TODO: If you need velocity-based friction/damping:
    // This would require reading velocity buffer and applying tangential friction
    // For now, position correction is sufficient for basic collision
}

/**
 * Main collision solver kernel
 * Processes one particle per thread
 */
[numthreads(256, 1, 1)]
void SolveCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumParticles)
        return;

    float invMass = InvMass[idx];

    // Skip kinematic particles
    if (invMass < EPSILON)
        return;

    // Read current predicted position
    FClothParticle particle = PredictedRW[idx];
    float3 position = particle.Position;

    // Store original position for NaN recovery
    float3 originalPosition = position;

    bool hadCollision = false;
    float3 totalCorrection = float3(0, 0, 0);

    // Test against all colliders
    for (uint i = 0; i < NumColliders; i++)
    {
        FClothCollider collider = Colliders[i];

        float3 normal;
        float dist = QueryColliderSDF(collider, position, normal);

        // Check for penetration (dist < collision thickness)
        float penetration = CollisionThickness - dist;

        if (penetration > 0.0f)
        {
            // Clamp individual correction to prevent extreme values
            float maxSingleCorrection = max(collider.Radius * 2.0f, 50.0f);
            penetration = min(penetration, maxSingleCorrection);

            // Apply correction
            totalCorrection += normal * penetration;
            hadCollision = true;
        }
    }

    // Apply accumulated corrections
    if (hadCollision)
    {
        position += totalCorrection;

        // NaN/Inf validation and recovery
        if (any(isnan(position)) || any(isinf(position)))
        {
            // Recovery: revert to original position
            position = originalPosition;

            // Alternative recovery: push to safe position outside all colliders
            // position = collider.Center + SafeNormalizeWithFallback(originalPosition - collider.Center) 
            //            * (collider.Radius + CollisionThickness);
        }

        // Write back modified position
        particle.Position = position;
        PredictedRW[idx] = particle;
    }
}
