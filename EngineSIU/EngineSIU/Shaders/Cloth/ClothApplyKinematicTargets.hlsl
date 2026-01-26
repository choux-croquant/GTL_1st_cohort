/**
 * Cloth Apply Kinematic Targets Shader
 * 
 * Applies kinematic constraints to cloth particles by snapping them to target positions.
 * This is used for attachment points (e.g., cloth pinned to a moving pole or character)
 * 
 * NEW: Long Range Attachment (LRA) Support
 * - Hard kinematic (AttachDistance = 0): Full position correction
 * - LRA (AttachDistance > 0): Unilateral distance constraint (Velvet pattern)
 * 
 * Based on: Velvet's SolveAttachment_Kernel
 * 
 * Execution: One thread per kinematic target
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FKinematicTarget> KinematicTargets : register(t0);
StructuredBuffer<FClothParticle> PositionRead : register(t1);  // NEW: Read current positions
StructuredBuffer<float> InvMassBuffer : register(t2);          // NEW: Inverse masses

// Output buffers - NOW using delta accumulation (Velvet pattern)
RWStructuredBuffer<int3> PositionDelta : register(u0);   // NEW: Accumulate deltas
RWStructuredBuffer<int> PositionWeight : register(u1);   // NEW: Accumulate weights
RWStructuredBuffer<FClothParticle> PositionWrite : register(u2);  // Direct write for hard kinematic

static const float kScale = 10000.0f;
static const float EPSILON = 1e-6f;

[numthreads(64, 1, 1)]
void ApplyKinematicTargetsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint targetIdx = DTid.x;
    
    // Bounds check
    if (targetIdx >= NumKinematicTargets)
        return;
    
    // Get kinematic target data
    FKinematicTarget target = KinematicTargets[targetIdx];
    uint particleIdx = target.ParticleIndex;
    
    // Bounds check for particle index
    if (particleIdx >= NumParticles)
        return;
    
    // Read current particle state
    FClothParticle particle = PositionRead[particleIdx];
    float invMass = InvMassBuffer[particleIdx];
    float3 currentPos = particle.Position;
    float3 targetPos = target.TargetPosition;
    float attachDistance = target.AttachDistance;
    
    // Two modes: Hard Kinematic vs Long Range Attachment (Velvet pattern)
    
    if (attachDistance <= EPSILON)
    {
        // MODE 1: HARD KINEMATIC (attachDistance = 0)
        // Direct position correction for fixed attachment points
        // Velvet check: if (invMass[pid] == 0 && targetDist > 0) return;
        
        if (invMass == 0.0f)
        {
            // Already kinematic (invMass = 0), just ensure position is correct
            particle.Position = targetPos;
            PositionWrite[particleIdx] = particle;
            return;
        }
        
        // Apply as hard constraint (full correction with stiffness)
        float3 correction = (targetPos - currentPos) * target.Stiffness;
        
        // Use delta accumulation for consistency with other constraints
        int3 deltaInt = int3(correction * kScale);
        InterlockedAdd(PositionDelta[particleIdx].x, deltaInt.x);
        InterlockedAdd(PositionDelta[particleIdx].y, deltaInt.y);
        InterlockedAdd(PositionDelta[particleIdx].z, deltaInt.z);
        InterlockedAdd(PositionWeight[particleIdx], 1);
    }
    else
    {
        // MODE 2: LONG RANGE ATTACHMENT (attachDistance > 0)
        // Unilateral distance constraint - only activates if too far
        // Velvet implementation: VtClothSolverGPU.cu::SolveAttachment_Kernel
        
        // Skip if particle is kinematic with non-zero distance (Velvet check)
        if (invMass == 0.0f)
            return;
        
        // Compute current distance to attachment point
        float3 diff = currentPos - targetPos;
        float currentDist = length(diff);
        
        // Target distance with stretchiness factor (Velvet pattern)
        float targetDist = attachDistance * LongRangeStretchiness;
        
        // Only enforce if distance exceeds target (unilateral constraint)
        if (currentDist > targetDist)
        {
            // Velvet formula: correction = -diff + diff / dist * targetDist
            // This pulls the particle back to exactly targetDist from the attachment point
            float3 correction = -diff + (diff / currentDist) * targetDist;
            
            // Accumulate delta (Velvet pattern)
            int3 deltaInt = int3(correction * kScale);
            InterlockedAdd(PositionDelta[particleIdx].x, deltaInt.x);
            InterlockedAdd(PositionDelta[particleIdx].y, deltaInt.y);
            InterlockedAdd(PositionDelta[particleIdx].z, deltaInt.z);
            InterlockedAdd(PositionWeight[particleIdx], 1);
        }
        // If currentDist <= targetDist, no correction needed (allows natural motion within range)
    }
}
