/**
 * Cloth Area Constraint Solver  
 * Based on PhysixStudio solve_area.comp
 * Preserves triangle area to prevent volume loss
 * Uses delta accumulation pattern
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PositionOld : register(t0);
StructuredBuffer<FClothParticle> PositionRead : register(t1);
StructuredBuffer<FAreaConstraint> AreaBuffer : register(t2);
StructuredBuffer<float> InvMassBuffer : register(t3);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t4);

// Output buffers (delta accumulation)
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);
RWStructuredBuffer<FAreaConstraint> AreaWrite : register(u2);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-8f;

void accumulate_delta(uint i, float3 corr)
{
    int3 corrInt = int3(corr * kScale);
    InterlockedAdd(PositionDelta[i].x, corrInt.x);
    InterlockedAdd(PositionDelta[i].y, corrInt.y);
    InterlockedAdd(PositionDelta[i].z, corrInt.z);
    InterlockedAdd(PositionWeight[i], 1);
}

[numthreads(256, 1, 1)]
void SolveAreaConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint gid = DTid.x;
    if (gid >= NumAreaConstraints) return;
    
    FAreaConstraint area = AreaBuffer[gid];
    
    uint i0 = area.ParticleA;
    uint i1 = area.ParticleB;
    uint i2 = area.ParticleC;
    
    float w0 = InvMassBuffer[i0];
    float w1 = InvMassBuffer[i1];
    float w2 = InvMassBuffer[i2];
    
    if (w0 + w1 + w2 == 0.0f) return;
    
    // Load predicted positions
    float3 x0 = PositionRead[i0].Position;
    float3 x1 = PositionRead[i1].Position;
    float3 x2 = PositionRead[i2].Position;
    
    // Get instance parameters
    uint instanceID = PositionRead[i0].InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    if (params.IsActive == 0) return;
    
    // Current edges
    float3 e1 = x1 - x0;
    float3 e2 = x2 - x0;
    
    // Current area projected onto rest normal (PhysixStudio solve_area.comp line 44)
    float a = 0.5f * dot(cross(e1, e2), area.RestNormal);
    float C = a - area.RestArea;  // Constraint violation
    
    // Gradients (PhysixStudio solve_area.comp lines 47-49)
    float3 g1 = 0.5f * cross(area.RestNormal, e2);
    float3 g2 = 0.5f * cross(e1, area.RestNormal);
    float3 g0 = -g1 - g2;
    
    // XPBD solve
    float dt = DeltaTime;
    float alpha_tilde = ComplianceArea / (dt * dt);
    
    float wsum_grad = w0 * dot(g0, g0) +
                      w1 * dot(g1, g1) +
                      w2 * dot(g2, g2);
    
    float denom = wsum_grad + alpha_tilde;
    if (denom < EPSILON) return;
    
    float lambda_old = area.Lambda;
    float rhs = C + alpha_tilde * lambda_old;
    
    float dLambda = -rhs / denom;
    
    // Apply stiffness
    float stiffness = 1.0f;
    float lambda_new = lambda_old + dLambda * stiffness;
    
    // Update lambda
    AreaWrite[gid].Lambda = lambda_new;
    
    // Accumulate deltas (PhysixStudio solve_area.comp lines 69-75)
    float3 corr0 = w0 * dLambda * g0;
    float3 corr1 = w1 * dLambda * g1;
    float3 corr2 = w2 * dLambda * g2;
    
    accumulate_delta(i0, corr0);
    accumulate_delta(i1, corr1);
    accumulate_delta(i2, corr2);
}
