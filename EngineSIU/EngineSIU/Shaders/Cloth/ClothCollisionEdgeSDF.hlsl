/**
 * Cloth Edge-Based SDF Collision Solver
 * Prevents low-resolution cloth edges from penetrating through colliders
 * 
 * Algorithm:
 * - For each edge, sample multiple points along the edge segment
 * - For each sample point, query all colliders using existing SDF functions
 * - If penetration detected, distribute correction force to both edge endpoints
 * - Use atomic accumulation to PositionDelta/PositionWeight buffers (Jacobi pattern)
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FEdgeCollisionConstraint> EdgeCollisions : register(t0);  // Edge collision constraints
StructuredBuffer<FClothCollider> Colliders : register(t1);                 // Collider data
StructuredBuffer<float> InvMass : register(t2);                            // Inverse masses
StructuredBuffer<FClothParticle> Predicted : register(t3);                 // Current predicted positions

// Output buffers (atomic accumulation)
RWStructuredBuffer<int3> PositionDelta : register(u0);   // Accumulated position corrections
RWStructuredBuffer<int> PositionWeight : register(u1);    // Accumulated weights

static const float EPSILON = 1e-6f;
static const float FIXED_POINT_SCALE = 10000.0f;  // Scale for int3 fixed-point math

/**
 * Safe normalize with fallback
 */
float3 SafeNormalizeWithFallback(float3 v, float3 fallback = float3(0, 1, 0))
{
    float lenSq = dot(v, v);
    if (lenSq < EPSILON * EPSILON)
        return fallback;
    return v * rsqrt(lenSq);
}

/**
 * SDF Functions - reused from vertex collision
 */
float sdSphere(float3 pos, float3 center, float radius)
{
    return length(pos - center) - radius;
}

float sdCapsule(float3 pos, float3 centerA, float3 centerB, float radius)
{
    float3 pa = pos - centerA;
    float3 ba = centerB - centerA;
    float baLenSq = dot(ba, ba);
    
    if (baLenSq < EPSILON)
        return length(pa) - radius;
    
    float h = saturate(dot(pa, ba) / baLenSq);
    return length(pa - ba * h) - radius;
}

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
        normal = SafeNormalizeWithFallback(pos - collider.Center, float3(0, 1, 0));
    }
    else if (collider.Type == 1)  // Capsule
    {
        float3 centerA = collider.Center - collider.Axis * collider.HalfHeight;
        float3 centerB = collider.Center + collider.Axis * collider.HalfHeight;
        
        float3 ba = centerB - centerA;
        float baLenSq = dot(ba, ba);
        
        if (baLenSq < EPSILON)
        {
            dist = sdSphere(pos, collider.Center, collider.Radius);
            normal = SafeNormalizeWithFallback(pos - collider.Center, float3(0, 1, 0));
            return dist;
        }
        
        dist = sdCapsule(pos, centerA, centerB, collider.Radius);
        
        float3 pa = pos - centerA;
        float h = saturate(dot(pa, ba) / baLenSq);
        float3 closestPoint = centerA + ba * h;
        
        normal = SafeNormalizeWithFallback(pos - closestPoint, float3(0, 1, 0));
    }
    else if (collider.Type == 2)  // Box
    {
        dist = sdBox(pos, collider.Center, collider.Extents);
        
        float3 localPos = pos - collider.Center;
        float3 q = abs(localPos) - collider.Extents;
        float3 gradientDir = sign(localPos) * max(q, 0.0f);
        
        normal = SafeNormalizeWithFallback(gradientDir, float3(0, 1, 0));
        
        if (dist < 0.0f)
        {
            float3 interiorNormal = sign(localPos) * step(q.yzx, q.xyz) * step(q.zxy, q.xyz);
            normal = SafeNormalizeWithFallback(interiorNormal, float3(0, 1, 0));
        }
    }
    
    return dist;
}

/**
 * Atomic accumulate position delta (fixed-point int3)
 */
void AtomicAccumulateDelta(uint particleIdx, float3 delta, float weight)
{
    if (weight < EPSILON)
        return;
    
    // Convert float delta to fixed-point int3
    int3 deltaInt = int3(delta * FIXED_POINT_SCALE);
    int weightInt = int(weight * FIXED_POINT_SCALE);
    
    // Atomic add to position delta buffer
    InterlockedAdd(PositionDelta[particleIdx].x, deltaInt.x);
    InterlockedAdd(PositionDelta[particleIdx].y, deltaInt.y);
    InterlockedAdd(PositionDelta[particleIdx].z, deltaInt.z);
    
    // Atomic add to weight buffer
    InterlockedAdd(PositionWeight[particleIdx], weightInt);
}

/**
 * Main edge collision solver kernel
 * Processes one edge per thread
 */
[numthreads(256, 1, 1)]
void SolveEdgeCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint edgeIdx = DTid.x;
    if (edgeIdx >= NumEdgeCollisions)
        return;
    
    // Get edge data
    FEdgeCollisionConstraint edge = EdgeCollisions[edgeIdx];
    uint idxA = edge.ParticleA;
    uint idxB = edge.ParticleB;
    
    // Skip if either endpoint is kinematic
    float invMassA = InvMass[idxA];
    float invMassB = InvMass[idxB];
    if (invMassA < EPSILON && invMassB < EPSILON)
        return;
    
    // Get current positions
    float3 posA = Predicted[idxA].Position;
    float3 posB = Predicted[idxB].Position;
    
    // Sample points along edge
    uint numSamples = EdgeSamplesPerEdge;
    if (numSamples < 2)
        numSamples = 3;  // Minimum 3 samples
    
    // Process each sample point
    for (uint sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx)
    {
        // Compute interpolation parameter (0 to 1 along edge)
        float t = (numSamples == 1) ? 0.5f : (float(sampleIdx) / float(numSamples - 1));
        
        // Interpolate sample position
        float3 samplePos = lerp(posA, posB, t);
        
        // Test sample against all colliders
        for (uint colliderIdx = 0; colliderIdx < NumColliders; ++colliderIdx)
        {
            FClothCollider collider = Colliders[colliderIdx];
            
            float3 normal;
            float dist = QueryColliderSDF(collider, samplePos, normal);
            
            // Check for penetration
            float penetration = CollisionThickness - dist;
            
            if (penetration > 0.0f)
            {
                // Clamp penetration to prevent extreme corrections
                float maxCorrection = max(collider.Radius * 2.0f, 50.0f);
                penetration = min(penetration, maxCorrection);
                
                // Compute correction vector
                float3 correction = normal * penetration;
                
                // Distribute correction to both endpoints based on interpolation parameter
                // Weights: (1-t) for A, t for B
                float weightA = (1.0f - t);
                float weightB = t;
                
                // Apply mass weighting (heavier particles move less)
                float totalInvMass = invMassA + invMassB;
                if (totalInvMass > EPSILON)
                {
                    float massRatioA = invMassA / totalInvMass;
                    float massRatioB = invMassB / totalInvMass;
                    
                    // Accumulate corrections atomically
                    AtomicAccumulateDelta(idxA, correction * weightA * massRatioA, weightA * massRatioA);
                    AtomicAccumulateDelta(idxB, correction * weightB * massRatioB, weightB * massRatioB);
                }
            }
        }
    }
}
