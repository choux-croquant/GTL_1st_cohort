/**
 * Cloth Shear Constraint Solver
 * Based on PhysixStudio solve_shear.comp
 * 
 * Prevents triangle shearing/skewing by constraining dot product of edges
 * Uses delta accumulation pattern with atomic operations
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PositionOld : register(t0);
StructuredBuffer<FClothParticle> PositionRead : register(t1);
StructuredBuffer<FShearConstraint> ShearBuffer : register(t2);
StructuredBuffer<float> InvMassBuffer : register(t3);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t4);

// Output buffers (delta accumulation)
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);
RWStructuredBuffer<FShearConstraint> ShearWrite : register(u2);  // For lambda update

static const float kScale = 10000.0f;
static const float EPSILON = 1e-8f;

// Helper: atomic delta accumulation
void accumulate_delta(uint i, float3 corr)
{
    int3 corrInt = int3(corr * kScale);
    InterlockedAdd(PositionDelta[i].x, corrInt.x);
    InterlockedAdd(PositionDelta[i].y, corrInt.y);
    InterlockedAdd(PositionDelta[i].z, corrInt.z);
    InterlockedAdd(PositionWeight[i], 1);
}

[numthreads(256, 1, 1)]
void SolveShearConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint gid = DTid.x;
    if (gid >= NumShearConstraints) return;
    
    FShearConstraint s = ShearBuffer[gid];
    
    uint i0 = s.ParticleA;
    uint i1 = s.ParticleB;
    uint i2 = s.ParticleC;
    
    float w0 = InvMassBuffer[i0];
    float w1 = InvMassBuffer[i1];
    float w2 = InvMassBuffer[i2];
    
    if (w0 + w1 + w2 == 0.0f) return;  // All fixed
    
    // Load predicted positions
    float3 x0 = PositionRead[i0].Position;
    float3 x1 = PositionRead[i1].Position;
    float3 x2 = PositionRead[i2].Position;
    
    // Get instance parameters
    uint instanceID = PositionRead[i0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    if (params.IsActive == 0) return;
    
    // Current edge vectors
    float3 e1 = x1 - x0;
    float3 e2 = x2 - x0;
    
    // Current dot product
    float dotNow = dot(e1, e2);
    
    // Constraint violation (PhysixStudio solve_shear.comp line 44)
    float C = dotNow - s.RestDot;
    
    // Gradients (PhysixStudio solve_shear.comp lines 47-49)
    float3 grad0 = -(e1 + e2);
    float3 grad1 = e2;
    float3 grad2 = e1;
    
    // XPBD formulation
    float dt = DeltaTime;
    float alpha_tilde = ComplianceShear / (dt * dt);
    
    float wsum_grad = w0 * dot(grad0, grad0) +
                      w1 * dot(grad1, grad1) +
                      w2 * dot(grad2, grad2);
    
    float denom = wsum_grad + alpha_tilde;
    if (denom < EPSILON) return;
    
    float lambda_old = s.Lambda;
    float rhs = C + alpha_tilde * lambda_old;
    
    float dLambda = -rhs / denom;
    
    // Apply stiffness multiplier (use global or per-instance)
    float stiffness = 1.0f;  // PhysixStudio uses stiffness multiplier
    float lambda_new = lambda_old + dLambda * stiffness;
    
    // Update lambda
    ShearWrite[gid].Lambda = lambda_new;
    
    // Accumulate position deltas (PhysixStudio solve_shear.comp lines 71-77)
    float3 corr0 = w0 * dLambda * grad0;
    float3 corr1 = w1 * dLambda * grad1;
    float3 corr2 = w2 * dLambda * grad2;
    
    accumulate_delta(i0, corr0);
    accumulate_delta(i1, corr1);
    accumulate_delta(i2, corr2);
}
