/**
 * Cloth Compute Kinematic Targets Shader
 * GPU-based kinematic target computation
 */

#include "ClothCommon.hlsli"

StructuredBuffer<FKinematicAttachment> Attachments : register(t0);
StructuredBuffer<float4x4> ComponentTransforms : register(t1);
StructuredBuffer<float> InvMass : register(t2);

RWStructuredBuffer<FClothParticle> PredictedRW : register(u0);

[numthreads(256, 1, 1)]
void ComputeKinematicTargetsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint attachIdx = DTid.x;
    if (attachIdx >= NumKinematicTargets)
        return;
    
    FKinematicAttachment attachment = Attachments[attachIdx];
    uint particleIdx = attachment.ParticleIndex;
    uint componentIdx = attachment.ComponentIndex;
    
    float4x4 componentTransform = ComponentTransforms[componentIdx];
    
    float4 localPosHomogeneous = float4(attachment.LocalOffset, 1.0f);
    float3 worldPos = mul(componentTransform, localPosHomogeneous).xyz;
    
    FClothParticle particle = PredictedRW[particleIdx];
    
    // Strong attachment (stiffness > 0.99): Pin position
    if (attachment.Stiffness > 0.99f)
    {
        particle.Position = worldPos;
    }
    // Weak attachment - Pull toward target (soft constraint)
    else if (attachment.Stiffness > 0.0f)
    {
        float3 delta = worldPos - particle.Position;
        /*float effectiveStiffness = attachment.Stiffness * GlobalStiffness;*/
        float effectiveStiffness = attachment.Stiffness;
        particle.Position += delta * effectiveStiffness;
    }
    
    // Long Range Attachment (LRA)
    if (attachment.AttachDistance > 0.0f)
    {
        float3 delta = worldPos - particle.Position;
        float distance = length(delta);
        
        if (distance > attachment.AttachDistance)
        {
            float3 direction = delta / distance;
            float overshoot = distance - attachment.AttachDistance;
            particle.Position += direction * overshoot * attachment.Stiffness;
        }
    }
    
    PredictedRW[particleIdx] = particle;
}
