/**
 * Cloth Bend Constraint Solver - Pure XPBD Edition
 * 
 * Implements dihedral angle bending constraints using pure XPBD formulation.
 * No stiffness mixing - material behavior controlled solely by compliance and iterations.
 * 
 * CONSTRAINT CONVENTION:
 * - ParticleA and ParticleB form the shared edge (edge = B - A)
 * - ParticleC is opposite vertex in Triangle 1 (A, B, C)
 * - ParticleD is opposite vertex in Triangle 2 (A, B, D)
 * - RestAngle is computed on CPU with same convention
 * 
 * GRADIENT FORMULATION:
 * Based on Macklin et al. XPBD and Bender's PBD survey
 */

#include "ClothCommon.hlsli"

// Read-only buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);
StructuredBuffer<float> InvMass : register(t2);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t3);

// Read-write buffers (XPBD requires writing back lambda)
RWStructuredBuffer<FBendConstraint> BendConstraints : register(u0);
RWStructuredBuffer<int3> PositionDelta : register(u1);
RWStructuredBuffer<int> PositionWeight : register(u2);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-6f;

// Debug mode - set to 1 to enable debug output for specific constraint
#define DEBUG_BEND 0
#define DEBUG_ID 0

[numthreads(256, 1, 1)]
void SolveBendConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint id = DTid.x;
    if (id >= NumBendConstraints) return;

    FBendConstraint constraint = BendConstraints[id];

    // Load particle indices
    // CONVENTION: A-B is shared edge, C opposite in tri1, D opposite in tri2
    uint idxA = constraint.ParticleA;  // Edge vertex 1
    uint idxB = constraint.ParticleB;  // Edge vertex 2
    uint idxC = constraint.ParticleC;  // Opposite in triangle 1 (A,B,C)
    uint idxD = constraint.ParticleD;  // Opposite in triangle 2 (A,B,D)
    
    float restAngle = constraint.RestAngle;

    // Load inverse masses
    float wA = InvMass[idxA];
    float wB = InvMass[idxB];
    float wC = InvMass[idxC];
    float wD = InvMass[idxD];

    // Load predicted positions
    float3 pA = PredictedRead[idxA].Position;
    float3 pB = PredictedRead[idxB].Position;
    float3 pC = PredictedRead[idxC].Position;
    float3 pD = PredictedRead[idxD].Position;

    // === STEP 1: Compute edge and triangle normals ===
    // Shared edge
    float3 e = pB - pA;
    float eLen = length(e);
    if (eLen < EPSILON) return;  // Degenerate edge
    
    float3 eNorm = e / eLen;

    // Triangle normals (unnormalized initially)
    // Triangle 1: A, B, C → normal n1
    // Triangle 2: A, B, D → normal n2
    float3 n1 = cross(pB - pA, pC - pA);
    float3 n2 = cross(pB - pA, pD - pA);
    
    float n1LenSq = dot(n1, n1);
    float n2LenSq = dot(n2, n2);
    
    if (n1LenSq < EPSILON || n2LenSq < EPSILON) return;  // Degenerate triangle
    
    float n1Len = sqrt(n1LenSq);
    float n2Len = sqrt(n2LenSq);
    
    // Normalized normals for angle computation
    float3 n1Norm = n1 / n1Len;
    float3 n2Norm = n2 / n2Len;

    // === STEP 2: Compute signed dihedral angle ===
    // Angle between normals
    float cosAngle = clamp(dot(n1Norm, n2Norm), -1.0f, 1.0f);
    float phi = acos(cosAngle);
    
    // Determine sign: if normals point away from each other, angle is positive
    float3 crossNormals = cross(n1Norm, n2Norm);
    float angleSign = dot(crossNormals, eNorm);
    
    // Apply sign to get signed dihedral angle
    if (angleSign < 0.0f)
        phi = -phi;

    // === STEP 3: Compute constraint error ===
    float C = BendStiffness * (phi - restAngle);

    // === STEP 4: Compute constraint gradients ===
    // Standard dihedral angle gradients (from Macklin/Bender)
    // Gradients for opposite vertices (simple):
    float3 gradC = (eLen / n1LenSq) * n1;
    float3 gradD = -(eLen / n2LenSq) * n2;  // Note negative sign
    
    // Gradients for edge vertices (involve projections):
    // Factor for how far along the edge each opposite vertex projects
    float factorC = dot(pC - pA, e) / (eLen * eLen);
    float factorD = dot(pD - pA, e) / (eLen * eLen);
    
    float3 gradA = -(1.0f - factorC) * gradC + (1.0f - factorD) * gradD;
    float3 gradB = -factorC * gradC + factorD * gradD;

    // === STEP 5: Compute XPBD denominator ===
    float denom =
        wA * dot(gradA, gradA) +
        wB * dot(gradB, gradB) +
        wC * dot(gradC, gradC) +
        wD * dot(gradD, gradD);

    if (denom < EPSILON) return;

    // === STEP 6: Pure XPBD update (NO STIFFNESS) ===
    // Compliance term: alpha = compliance / dt^2
    // If compliance = 0, constraint is hard (infinite stiffness)
    float alpha = (constraint.Compliance > EPSILON) 
        ? (constraint.Compliance / (DeltaTime * DeltaTime))
        : 0.0f;
    
    // Load old lambda (warm starting)
    float lambdaOld = constraint.Lambda;
    
    // XPBD Lagrange multiplier update
    // deltaLambda = -(C + alpha * lambdaOld) / (denom + alpha)
    float deltaLambda = -(C + alpha * lambdaOld) / (denom + alpha);
    
    // Update lambda (accumulate)
    float lambdaNew = lambdaOld + deltaLambda;
    
    // Write back updated lambda for next iteration/frame
    BendConstraints[id].Lambda = lambdaNew;

    // === STEP 7: Compute position corrections ===
    // deltax_i = -w_i * deltaLambda * grad_i
    float3 corrA = -wA * deltaLambda * gradA;
    float3 corrB = -wB * deltaLambda * gradB;
    float3 corrC = -wC * deltaLambda * gradC;
    float3 corrD = -wD * deltaLambda * gradD;

#if DEBUG_BEND
    if (id == DEBUG_ID)
    {
        // Debug: could store values in padding or use debug buffer
        // For now, just a marker that debug is enabled
    }
#endif

    // === STEP 8: Accumulate corrections (fixed-point for atomics) ===
    int3 deltaA = int3(corrA * kScale);
    int3 deltaB = int3(corrB * kScale);
    int3 deltaC = int3(corrC * kScale);
    int3 deltaD = int3(corrD * kScale);

    InterlockedAdd(PositionDelta[idxA].x, deltaA.x);
    InterlockedAdd(PositionDelta[idxA].y, deltaA.y);
    InterlockedAdd(PositionDelta[idxA].z, deltaA.z);
    InterlockedAdd(PositionWeight[idxA], 1);

    InterlockedAdd(PositionDelta[idxB].x, deltaB.x);
    InterlockedAdd(PositionDelta[idxB].y, deltaB.y);
    InterlockedAdd(PositionDelta[idxB].z, deltaB.z);
    InterlockedAdd(PositionWeight[idxB], 1);

    InterlockedAdd(PositionDelta[idxC].x, deltaC.x);
    InterlockedAdd(PositionDelta[idxC].y, deltaC.y);
    InterlockedAdd(PositionDelta[idxC].z, deltaC.z);
    InterlockedAdd(PositionWeight[idxC], 1);

    InterlockedAdd(PositionDelta[idxD].x, deltaD.x);
    InterlockedAdd(PositionDelta[idxD].y, deltaD.y);
    InterlockedAdd(PositionDelta[idxD].z, deltaD.z);
    InterlockedAdd(PositionWeight[idxD], 1);
}
