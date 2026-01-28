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
 * SDF Functions - Analytic signed distance computations
 */

// Sphere SDF
float sdSphere(float3 pos, float3 center, float radius)
{
    return length(pos - center) - radius;
}

// Capsule SDF
float sdCapsule(float3 pos, float3 centerA, float3 centerB, float radius)
{
    float3 pa = pos - centerA;
    float3 ba = centerB - centerA;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
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
        normal = normalize(pos - collider.Center);
    }
    else if (collider.Type == 1)  // Capsule
    {
        float3 centerA = collider.Center - collider.Axis * collider.HalfHeight;
        float3 centerB = collider.Center + collider.Axis * collider.HalfHeight;
        dist = sdCapsule(pos, centerA, centerB, collider.Radius);
        
        // Normal = gradient of SDF (point to closest point on capsule axis)
        float3 pa = pos - centerA;
        float3 ba = centerB - centerA;
        float h = saturate(dot(pa, ba) / dot(ba, ba));
        float3 closestPoint = centerA + ba * h;
        normal = normalize(pos - closestPoint);
    }
    else if (collider.Type == 2)  // Box
    {
        dist = sdBox(pos, collider.Center, collider.Extents);
        
        // Normal = gradient of SDF (approximate)
        float3 localPos = pos - collider.Center;
        float3 q = abs(localPos) - collider.Extents;
        normal = normalize(sign(localPos) * max(q, 0.0f));
        
        // Handle interior case
        if (dist < 0.0f)
        {
            normal = sign(localPos) * step(q.yzx, q.xyz) * step(q.zxy, q.xyz);
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
            // Apply correction
            totalCorrection += normal * penetration;
            hadCollision = true;
        }
    }
    
    // Apply accumulated corrections
    if (hadCollision)
    {
        position += totalCorrection;
        
        // Write back modified position
        particle.Position = position;
        PredictedRW[idx] = particle;
    }
}
