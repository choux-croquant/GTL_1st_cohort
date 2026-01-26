/**
 * Cloth Distance Constraint Solver - Graph-Colored XPBD
 * Based on PhysixStudio solve_stretch.comp
 * 
 * CRITICAL: Constraints must be graph-colored so no two constraints
 * in the same dispatch share particles. This enables DIRECT position
 * writes without atomics, maximizing parallelism and performance.
 * 
 * XPBD formulation with compliance (alpha) and optional damping (beta)
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PositionOld : register(t0);     // x (previous frame)
StructuredBuffer<FClothParticle> PositionRead : register(t1);   // xp (predicted)
StructuredBuffer<FDistanceConstraint> ConstraintBuffer : register(t2);
StructuredBuffer<float> InvMassBuffer : register(t3);
StructuredBuffer<FClothInstanceParameters> InstanceParams : register(t4);

// Output buffers
RWStructuredBuffer<FClothParticle> PositionWrite : register(u0);  // xp (corrected)
RWStructuredBuffer<FDistanceConstraint> ConstraintWrite : register(u1); // For lambda update

static const float EPSILON = 1e-7f;

bool isFinite_f(float x) { return !isnan(x) && !isinf(x); }

[numthreads(256, 1, 1)]
void SolveDistanceConstraintsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint eidx = DTid.x;
    if (eidx >= NumConstraints) return;
    
    FDistanceConstraint constraint = ConstraintBuffer[eidx];
    
    uint i = constraint.ParticleA;
    uint j = constraint.ParticleB;
    float rest = constraint.RestLength;
    float lambda_old = constraint.Lambda;
    
    float wi = InvMassBuffer[i];
    float wj = InvMassBuffer[j];
    float wsum = wi + wj;
    
    // Skip if both particles fixed
    if (wsum == 0.0f)
    {
        ConstraintWrite[eidx].Lambda = 0.0f;
        return;
    }
    
    // Load particle data
    FClothParticle pi_pred = PositionRead[i];  // Predicted position
    FClothParticle pj_pred = PositionRead[j];
    
    FClothParticle pi_old = PositionOld[i];    // Old position (for velocity damping)
    FClothParticle pj_old = PositionOld[j];
    
    // Get instance parameters
    uint instanceID = pi_pred.InstanceID;
    FClothInstanceParameters params = InstanceParams[instanceID];
    if (params.IsActive == 0) return;
    
    // Current state
    float3 xi = pi_pred.Position;
    float3 xj = pj_pred.Position;
    
    float3 d = xi - xj;
    float len = length(d);
    if (len < EPSILON) return;
    
    float3 n = d / len;  // Normalized direction
    
    // XPBD formulation (PhysixStudio solve_stretch.comp lines 34-56)
    float dt = DeltaTime;
    float alpha_tilde = ComplianceStretch / (dt * dt);
    
    // Velocity-level damping (beta parameter)
    float beta = BetaStretch;  // Default 100.0 for stretch
    float beta_tilde = dt * dt * beta;
    float gamma = (alpha_tilde * beta_tilde) / dt;
    
    float C = len - rest;  // Constraint violation
    
    // Relative velocity contribution (for damping)
    float3 pxi = pi_old.Position;
    float3 pxj = pj_old.Position;
    float3 dpi = xi - pxi;  // Position change particle i
    float3 dpj = xj - pxj;  // Position change particle j
    float rel = dot(n, dpi) + dot(-n, dpj);  // Relative velocity along constraint
    
    // Solve for lambda increment
    float denom = (1.0f + gamma) * wsum + alpha_tilde;
    if (denom < EPSILON) denom = EPSILON;
    
    float rhs = C + alpha_tilde * lambda_old + gamma * rel;
    float dlambda = -rhs / denom;
    
    // Apply stiffness multiplier (artist control)
    float newLambda = lambda_old + dlambda * params.StretchStiffness;
    
    // Safety check
    if (!isFinite_f(newLambda)) newLambda = 0.0f;
    
    // Update lambda for warm starting next iteration
    ConstraintWrite[eidx].Lambda = newLambda;
    
    // DIRECT position corrections (NO ATOMICS - graph coloring guarantees no conflicts)
    float3 corr = dlambda * n;
    
    if (wi > 0.0f)
    {
        FClothParticle newPi = pi_pred;
        newPi.Position += wi * corr;
        PositionWrite[i] = newPi;
    }
    
    if (wj > 0.0f)
    {
        FClothParticle newPj = pj_pred;
        newPj.Position -= wj * corr;
        PositionWrite[j] = newPj;
    }
}
