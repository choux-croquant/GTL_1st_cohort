/**
 * Cloth Vertex Shader
 * Reads dynamic positions from GPU simulation buffer
 */

#include "ShaderRegisters.hlsl"

// Cloth dynamic position and normal buffers from simulation
StructuredBuffer<float4> ClothPositionBuffer : register(t9);  // xyz = position, w = invMass
StructuredBuffer<float3> ClothNormalBuffer : register(t10);

// Cloth mesh constant buffer
cbuffer ClothMeshConstants : register(b10)
{
    row_major matrix ClothWorldMatrix;
    uint ClothNumVertices;
    uint ClothParticleOffset;  // NEW: For batched mode - offset into unified buffer
    uint ClothIndexOffset;     // NEW: For batched mode - index offset
    uint ClothPadding;
};

struct VS_INPUT_Cloth
{
    uint VertexID : SV_VertexID;
    float2 UV : TEXCOORD;
};

PS_INPUT_CommonMesh main(VS_INPUT_Cloth Input)
{
    PS_INPUT_CommonMesh Output;
    
    // Batched mode: Apply particle offset to access this instance's data in unified buffer
    // Each instance has a ParticleOffset that points to its data in the shared buffers
    uint particleIndex = Input.VertexID + ClothParticleOffset;
    
    // Read dynamic position from simulation buffer
    // - Batched mode: Unified buffer containing all instances at different offsets
    // - Legacy mode: Per-instance buffer (offset = 0)
    float4 particleData = ClothPositionBuffer[particleIndex];
    float3 position = particleData.xyz;
    
    // Read dynamic normal from simulation buffer
    float3 normal = ClothNormalBuffer[particleIndex];
    
    // Transform to world space
    // - Batched mode: ClothWorldMatrix = Identity (particles already in world space)
    // - Legacy mode: ClothWorldMatrix = component transform (particles in local space)
    float4 worldPos = mul(float4(position, 1.0), ClothWorldMatrix);
    Output.WorldPosition = worldPos.xyz;
    
    // Transform to clip space
    Output.Position = mul(worldPos, ViewMatrix);
    Output.Position = mul(Output.Position, ProjectionMatrix);
    
    // Transform normal to world space
    Output.WorldNormal = normalize(mul(normal, (float3x3)ClothWorldMatrix));
    
    // Calculate tangent (simplified - using normal cross up vector)
    float3 worldTangent;
    if (abs(Output.WorldNormal.y) < 0.999)
    {
        worldTangent = normalize(cross(float3(0, 1, 0), Output.WorldNormal));
    }
    else
    {
        worldTangent = normalize(cross(float3(1, 0, 0), Output.WorldNormal));
    }
    Output.WorldTangent = float4(worldTangent, 1.0);
    
    // Pass through UV coordinates
    Output.UV = Input.UV;
    
    // Default color (can be overridden by pixel shader)
    Output.Color = float4(1, 0, 0, 1);
    
    return Output;
}
