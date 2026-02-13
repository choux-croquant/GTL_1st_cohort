/**
 * Cloth Self-Collision XPBD Solver Shader
 * Pass 6: Solve collisions using XPBD with friction (PhysixStudio-quality)
 * 
 * This shader solves self-collisions using the XPBD formulation with friction,
 * providing high stability and realistic contact behavior. Uses pre-computed
 * neighbor lists for efficiency.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);      // Current predicted positions
StructuredBuffer<FClothParticle> PreviousPositions : register(t1);  // Previous positions (for friction)
StructuredBuffer<float> InvMass : register(t2);                     // Inverse masses
StructuredBuffer<uint> NeighborLists : register(t3);                // Pre-computed neighbor lists
StructuredBuffer<uint> NeighborCounts : register(t4);               // Neighbor counts per particle

// Output buffers
RWStructuredBuffer<int3> PositionDelta : register(u0);       // Accumulated position corrections (scaled int3)
RWStructuredBuffer<int> PositionWeight : register(u1);       // Constraint count per particle
RWStructuredBuffer<float> NeighborLambdas : register(u2);    // XPBD lambda per neighbor pair (persistent)

// Constants
static const float kScale = 10000.0f;  // Fixed-point scaling for atomic operations
static const float EPSILON = 1e-6f;

/**
 * Friction clamping function (from PhysixStudio)
 * Applies Coulomb friction model to tangential displacement
 */
float3 FrictionClamp(float3 relDisp, float3 n, float dn, float mu)
{
    if (mu <= 0.0 || dn <= 0.0)
        return float3(0, 0, 0);
    
    // Compute tangential component
    float3 t = relDisp - n * dot(relDisp, n);
    float tl = length(t);
    
    if (tl < EPSILON)
        return float3(0, 0, 0);
    
    // Coulomb friction limit
    float maxT = mu * dn;
    float s = min(1.0, maxT / tl);
    
    return -t * s;
}

/**
 * Solve self-collisions using XPBD with friction
 * Each thread processes one particle and its pre-computed neighbor list
 */
[numthreads(256, 1, 1)]
void SolveCollisionsXPBDCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles)
        return;
    
    float wi = InvMass[i];
    if (wi == 0.0)
        return;  // Skip kinematic particles
    
    float3 xi = PredictedRead[i].Position;
    float3 pxi = PreviousPositions[i].Position;
    
    uint neighborCount = NeighborCounts[i];
    float r = CollisionRadius * 2.0f;  // Detection diameter
    
    float3 sumCorr = float3(0, 0, 0);
    uint sumCount = 0;
    
    // Iterate through pre-computed neighbor list
    for (uint k = 0; k < neighborCount; ++k)
    {
        uint j = NeighborLists[i * MaxNeighbors + k];
        
        float wj = InvMass[j];
        if (wj == 0.0)
            continue;  // Skip kinematic particles
        
        float3 xj = PredictedRead[j].Position;
        float3 pxj = PreviousPositions[j].Position;
        
        // Collision detection
        float3 d = xi - xj;
        float dist2 = dot(d, d);
        
        if (dist2 < EPSILON || dist2 >= r * r)
            continue;
        
        float dist = sqrt(dist2);
        float3 n = d / dist;
        
        // Constraint: C = dist - r (negative = penetration)
        float C = dist - r;
        if (C >= 0.0)
            continue;
        
        // XPBD solver
        float wsum = wi + wj;
        if (wsum <= EPSILON)
            continue;
        
        // Compute compliance-based correction
        float alphaTilde = Compliance / (DeltaTime * DeltaTime);
        float lamOld = NeighborLambdas[i * MaxNeighbors + k];
        float denom = wsum + alphaTilde;
        float rhs = C + alphaTilde * lamOld;
        float dLam = -rhs / denom;
        
        // Apply stiffness and inequality constraint (lambda >= 0)
        float lamNew = lamOld + dLam * CollisionStiffness;
        if (lamNew < 0.0)
            lamNew = 0.0;
        
        dLam = lamNew - lamOld;
        NeighborLambdas[i * MaxNeighbors + k] = lamNew;
        
        // Position corrections
        float3 corrI = (wi * dLam) * n;
        float3 corrJ = -(wj * dLam) * n;
        
        // Friction computation
        float3 dispI = xi - pxi;
        float3 dispJ = xj - pxj;
        float3 relDisp = dispI - dispJ;
        float dn = abs(dLam);
        
        float3 fricRel = FrictionClamp(relDisp, n, dn, CollisionFriction);
        float3 fricI = (wi / wsum) * fricRel;
        float3 fricJ = -(wj / wsum) * fricRel;
        
        // Accumulate corrections for particle i (local)
        sumCorr += corrI + fricI;
        sumCount++;
        
        // Apply corrections to particle j immediately (prevents double-counting)
        int3 deltaJ = int3((corrJ + fricJ) * kScale);
        InterlockedAdd(PositionDelta[j].x, deltaJ.x);
        InterlockedAdd(PositionDelta[j].y, deltaJ.y);
        InterlockedAdd(PositionDelta[j].z, deltaJ.z);
        InterlockedAdd(PositionWeight[j], 1);
    }
    
    // Apply accumulated corrections for particle i
    if (sumCount > 0)
    {
        int3 deltaI = int3(sumCorr * kScale);
        InterlockedAdd(PositionDelta[i].x, deltaI.x);
        InterlockedAdd(PositionDelta[i].y, deltaI.y);
        InterlockedAdd(PositionDelta[i].z, deltaI.z);
        InterlockedAdd(PositionWeight[i], sumCount);
    }
}
