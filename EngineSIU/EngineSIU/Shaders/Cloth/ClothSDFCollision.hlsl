/**
 * Cloth SDF Collision Solver
 * Analytic signed distance field collision detection and response
 * Operates on predicted particle positions (in-place modification)
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothCollider> Colliders : register(t0);  // Collider data
StructuredBuffer<float> InvMass : register(t1);             // Inverse masses
StructuredBuffer<FClothParticle> PreviousPositions : register(t2);  // Previous frame positions (for displacement)
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);  // Per-instance parameters

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

float sdTorus(float3 pos, float3 center, float3 axis, float majorRadius, float minorRadius)
{
    // Validate inputs
    if (majorRadius <= 0.0f || minorRadius <= 0.0f)
        return 1e10f;  // Return large positive distance (no collision)
    
    // Ensure axis is normalized
    float axisLen = length(axis);
    if (axisLen < EPSILON)
        return 1e10f;  // Invalid axis
    
    float3 normalizedAxis = axis / axisLen;
    
    // Transform to torus local space
    float3 localPos = pos - center;
    
    // Project onto torus plane (perpendicular to axis)
    float axisProj = dot(localPos, normalizedAxis);
    float3 planePos = localPos - normalizedAxis * axisProj;
    
    // Distance from center in plane
    float distInPlane = length(planePos);
    
    // 2D distance in (radial, vertical) space
    float2 q = float2(distInPlane - majorRadius, axisProj);
    
    return length(q) - minorRadius;
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
    else if (collider.Type == 3)  // Torus
    {
        float majorRadius = collider.HalfHeight;  // Reuse HalfHeight field for major radius
        float minorRadius = collider.Radius;
        
        // Validate radii
        if (majorRadius <= 0.0f || minorRadius <= 0.0f)
        {
            dist = 1e10f;  // No collision
            normal = float3(0, 1, 0);
            return dist;
        }
        
        dist = sdTorus(pos, collider.Center, collider.Axis, majorRadius, minorRadius);
        
        // FIX 3: Axis singularity guard in normal computation
        float3 normalizedAxis = SafeNormalizeWithFallback(collider.Axis, float3(0, 0, 1));
        float3 localPos = pos - collider.Center;
        float axisProj = dot(localPos, normalizedAxis);
        float3 planePos = localPos - normalizedAxis * axisProj;
        float distInPlane = length(planePos);
        
        // Radial direction in torus plane (handle axis singularity)
        float3 radialDir = float3(0, 0, 0);
        if (distInPlane > EPSILON)
        {
            radialDir = planePos / distInPlane;  // Normalized radial direction
        }
        else
        {
            radialDir = float3(1, 0, 0);  // Fallback when particle is on axis
        }
        
        // Point on major circle (torus ring) closest to query point
        float3 torusRingPoint = radialDir * majorRadius;
        float3 toNormal = localPos - torusRingPoint;
        
        // If toNormal is near zero (particle on torus surface ring), fall back to axis
        normal = SafeNormalizeWithFallback(toNormal, normalizedAxis);
    }

    return dist;
}

/**
 * Displacement-based friction calculation
 * Based on Coulomb friction model with tangential displacement clamping
 *
 * @param relDisp - Relative displacement vector (current - previous)
 * @param normal - Collision normal (points away from collider)
 * @param normalForce - Normal impulse magnitude (penetration depth)
 * @param mu - Friction coefficient (0-1)
 * @return Friction correction to apply to position
 */
float3 CalculateFriction(float3 relativeDisplacement, float3 normal, float normalForce, float mu)
{
    if (mu <= 0.0f || normalForce <= 0.0f)
        return float3(0, 0, 0);
    
    // Tangential displacement
    float3 tangent = relativeDisplacement - normal * dot(relativeDisplacement, normal);
    float tangentLength = length(tangent);
    
    if (tangentLength < 1e-9f)
        return float3(0, 0, 0);
    
    float maxTangentialForce = mu * normalForce;
    
    float scale = min(1.0f, maxTangentialForce / tangentLength);
    
    return -tangent * scale;
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

    // Read current and previous positions
    FClothParticle particle = PredictedRW[idx];
    float3 position = particle.Position;
    float3 previousPosition = PreviousPositions[idx].Position;
    
    // Get instance parameters for friction
    uint instanceID = particle.InstanceID;
    FClothInstanceParameters instanceParams = InstanceParams[instanceID];
    
    // Calculate displacement since last frame
    float3 displacement = position - previousPosition;
    
    // Calculate final friction coefficient (instance × global)
    float finalFriction = instanceParams.Friction * CollisionFriction;

    // Store original position for NaN recovery
    float3 originalPosition = position;

    bool hadCollision = false;
    float3 totalCorrection = float3(0, 0, 0);
    float3 totalFriction = float3(0, 0, 0);

    // Test against all colliders
    for (uint i = 0; i < NumColliders; i++)
    {
        FClothCollider collider = Colliders[i];
        
        // Validate collider type
        if (collider.Type > 3)
        {
            continue;
        }

        // Query SDF distance
        float3 normal;
        float dist = QueryColliderSDF(collider, position, normal);

        float penetration = CollisionThickness - dist;

        if (penetration > 0.0f)
        {
            // Clamp individual correction to prevent extreme values
            float maxSingleCorrection = max(collider.Radius * 2.0f, 50.0f);
            penetration = min(penetration, maxSingleCorrection);

            // Position correction (normal force)
            float3 correction = normal * penetration;
            totalCorrection += correction;
            
            // Friction correction (tangential resistance)
            float3 frictionCorrection = CalculateFriction(
                displacement,
                normal,
                penetration,  // Use penetration as normal force proxy
                finalFriction
            );
            totalFriction += frictionCorrection;
            
            hadCollision = true;
        }
    }

    // Apply accumulated corrections
    if (hadCollision)
    {
        position += totalCorrection + totalFriction;

        // NaN/Inf validation and recovery
        if (any(isnan(position)) || any(isinf(position)))
        {
            // Recovery: revert to original position
            position = originalPosition;
        }

        // Write back modified position
        particle.Position = position;
        PredictedRW[idx] = particle;
    }
}
