/**
 * Cloth Self-Collision Solver Shader
 * Pass 2: Detect and resolve self-collisions using spatial hash grid
 * 
 * This shader queries the spatial hash grid built in Pass 1 to find
 * nearby particles and applies mass-weighted separation constraints.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Predicted positions
StructuredBuffer<float> InvMass : register(t1);                 // Inverse masses
StructuredBuffer<uint> CellCounters : register(t2);             // Per-cell particle counts
StructuredBuffer<uint> CellData : register(t3);                 // Particle indices per cell
Buffer<uint> Indices : register(t4);                            // Index buffer (for topology check)
StructuredBuffer<FClothParticle> PreviousPositions : register(t5);  // Previous frame positions (for displacement)
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t6);  // Per-instance parameters

// Output buffers (Jacobi accumulation pattern)
RWStructuredBuffer<int3> PositionDelta : register(u0);   // Accumulated position corrections
RWStructuredBuffer<int> PositionWeight : register(u1);   // Constraint count per particle

// Constants
static const float kScale = 10000.0f;  // Fixed-point scaling for atomic operations
static const float EPSILON = 1e-6f;

/**
 * Compute grid cell from world position
 */
uint3 GetGridCell(float3 position)
{
    float3 localPos = position - GridMin;
    uint3 cell = uint3(floor(localPos / CellSize));
    return clamp(cell, uint3(0, 0, 0), GridDimensions - uint3(1, 1, 1));
}

/**
 * Hash 3D cell coordinates to 1D index
 */
uint GetCellHash(uint3 cell)
{
    return cell.x + cell.y * GridDimensions.x + 
           cell.z * GridDimensions.x * GridDimensions.y;
}

/**
 * Check if two particles are topologically adjacent (share an edge)
 * Simple heuristic: particles within 1 index are likely adjacent
 * TODO: Replace with proper adjacency buffer lookup for accuracy
 */
bool AreTopologicallyAdjacent(uint idxA, uint idxB)
{
    // Simple heuristic for grid-like meshes
    // This prevents self-collision between directly connected vertices
    return abs(int(idxA) - int(idxB)) <= 1;
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
float3 CalculateFriction(float3 relDisp, float3 normal, float normalForce, float mu)
{
    // No friction if coefficient is zero or no normal force
    if (mu <= 0.0f || normalForce <= 0.0f)
        return float3(0, 0, 0);
    
    // Calculate tangential displacement (perpendicular to normal)
    float3 tangent = relDisp - normal * dot(relDisp, normal);
    float tangentLength = length(tangent);
    
    // Ignore negligible tangential movement
    if (tangentLength < 1e-9f)
        return float3(0, 0, 0);
    
    // Coulomb friction limit: mu * NormalForce
    float maxTangentialForce = mu * normalForce;
    
    // Calculate friction scaling factor
    // If tangent movement < limit: full static friction (stop completely)
    // If tangent movement > limit: dynamic friction (resist by limit amount)
    float scale = min(1.0f, maxTangentialForce / tangentLength);
    
    // Return friction correction (opposes tangential motion)
    return -tangent * scale;
}

/**
 * Solve self-collisions using spatial hash grid
 * Each thread processes one particle and checks for collisions with neighbors
 */
[numthreads(256, 1, 1)]
void SolveSelfCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    if (particleIdx >= NumParticles)
        return;
    
    float invMassA = InvMass[particleIdx];
    if (invMassA == 0.0f)
        return;  // Skip kinematic particles
    
    float3 posA = PredictedRead[particleIdx].Position;
    float3 prevPosA = PreviousPositions[particleIdx].Position;
    uint instanceIDA = PredictedRead[particleIdx].InstanceID;
    
    // Get instance friction parameters
    FClothInstanceParameters paramsA = InstanceParams[instanceIDA];
    float finalFriction = paramsA.Friction * CollisionFriction;
    
    // Calculate displacement for particle A
    float3 dispA = posA - prevPosA;
    
    uint3 cellA = GetGridCell(posA);
    
    // Query 3×3×3 neighborhood (27 cells)
    for (int dz = -1; dz <= 1; dz++)
    {
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int3 neighborCell = int3(cellA) + int3(dx, dy, dz);
                
                // Bounds check
                if (any(neighborCell < int3(0, 0, 0)) || 
                    any(neighborCell >= int3(GridDimensions)))
                    continue;
                
                uint cellHash = GetCellHash(uint3(neighborCell));
                uint cellCount = min(CellCounters[cellHash], MaxParticlesPerCell);
                
                // Check all particles in this cell
                for (uint i = 0; i < cellCount; i++)
                {
                    uint particleIdxB = CellData[cellHash * MaxParticlesPerCell + i];
                    
                    // Skip self
                    if (particleIdxB == particleIdx)
                        continue;
                    
                    // Skip if topologically adjacent (share an edge)
                    if (AreTopologicallyAdjacent(particleIdx, particleIdxB))
                        continue;
                    
                    // Prevent double counting: only process if particleIdx < particleIdxB
                    if (particleIdx > particleIdxB)
                        continue;
                    
                    float invMassB = InvMass[particleIdxB];
                    float3 posB = PredictedRead[particleIdxB].Position;
                    float3 prevPosB = PreviousPositions[particleIdxB].Position;
                    
                    // Calculate displacement for particle B
                    float3 dispB = posB - prevPosB;
                    
                    // Collision detection
                    float3 diff = posB - posA;
                    float dist = length(diff);
                    float minDist = 2.0f * CollisionRadius;
                    
                    if (dist < minDist && dist > EPSILON)
                    {
                        // Collision response (mass-weighted separation)
                        float3 normal = diff / dist;
                        float penetration = minDist - dist;
                        
                        float wSum = invMassA + invMassB;
                        if (wSum < EPSILON)
                            continue;
                        
                        // Position correction (normal separation)
                        float3 correction = normal * penetration * CollisionStiffness;
                        float3 corrA = -correction * (invMassA / wSum);
                        float3 corrB = +correction * (invMassB / wSum);
                        
                        // Friction correction (tangential resistance)
                        // Calculate relative displacement
                        float3 relDisp = dispA - dispB;
                        
                        // Calculate friction in relative space
                        float3 frictionRel = CalculateFriction(
                            relDisp,
                            normal,
                            penetration,  // Use penetration as normal force proxy
                            finalFriction
                        );
                        
                        // Distribute friction based on mass ratios
                        float3 fricA = (invMassA / wSum) * frictionRel;
                        float3 fricB = -(invMassB / wSum) * frictionRel;
                        
                        // Combine position and friction corrections
                        float3 outA = corrA + fricA;
                        float3 outB = corrB + fricB;
                        
                        // Atomic accumulation (scaled to int for thread safety)
                        int3 deltaA = int3(outA * kScale);
                        int3 deltaB = int3(outB * kScale);
                        
                        // Accumulate corrections for particle A
                        InterlockedAdd(PositionDelta[particleIdx].x, deltaA.x);
                        InterlockedAdd(PositionDelta[particleIdx].y, deltaA.y);
                        InterlockedAdd(PositionDelta[particleIdx].z, deltaA.z);
                        InterlockedAdd(PositionWeight[particleIdx], 1);
                        
                        // Accumulate corrections for particle B
                        InterlockedAdd(PositionDelta[particleIdxB].x, deltaB.x);
                        InterlockedAdd(PositionDelta[particleIdxB].y, deltaB.y);
                        InterlockedAdd(PositionDelta[particleIdxB].z, deltaB.z);
                        InterlockedAdd(PositionWeight[particleIdxB], 1);
                    }
                }
            }
        }
    }
}
