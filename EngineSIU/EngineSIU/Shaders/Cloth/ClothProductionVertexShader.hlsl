/**
 * Cloth Production Vertex Shader
 * GPU skinning for high-resolution render mesh driven by low-resolution simulation mesh
 * Uses Linear Blend Skinning (LBS) with up to 4 influences per render vertex
 */

#include "../ShaderRegisters.hlsl"

// GPU skinning weight structure (must match C++ FClothSkinningWeightGPU)
struct FClothSkinningWeightGPU
{
    uint4 SimVertexIndices;  // 4 simulation vertex indices (global indices in unified buffer)
    float4 Weights;          // 4 weights (normalized, sum to 1.0)
};

// Per-instance constant buffer for cloth rendering
cbuffer ClothInstanceConstants : register(b10)
{
    row_major matrix ClothWorldMatrix;      // 64 bytes - World transform (usually identity for batched)
    
    uint ClothRenderVertexOffset;           // Offset into unified render vertex buffers
    uint ClothRenderIndexOffset;            // Offset into unified render index buffer
    uint ClothSimParticleOffset;            // Offset into unified sim particle buffers
    uint ClothNumRenderVertices;            // Number of render vertices
    
    uint ClothNumSimParticles;              // Number of sim particles
    uint3 ClothPadding;                     // Padding to 96 bytes
};

// Shader resources for GPU skinning
// CRITICAL FIX: Use t14-t16 to avoid conflict with light buffers (t10-t13)
StructuredBuffer<float4> SimPositionBuffer : register(t14);   // xyz=position, w=invMass (dynamic, from simulation)
StructuredBuffer<float3> SimNormalBuffer : register(t15);     // Simulation normals (dynamic, from simulation)
StructuredBuffer<FClothSkinningWeightGPU> SkinningWeightBuffer : register(t16);  // Skinning weights (static)

// Vertex shader input (render mesh data)
struct VS_INPUT_ClothProduction
{
    float3 Position : POSITION;  // Render mesh rest position
    float3 Normal : NORMAL;      // Render mesh rest normal
    float2 UV : TEXCOORD;        // Render mesh UV
    uint VertexID : SV_VertexID; // Vertex index for skinning weight lookup
};

/**
 * Main vertex shader entry point
 * Performs GPU skinning to deform render mesh based on simulation mesh
 */
PS_INPUT_CommonMesh main(VS_INPUT_ClothProduction Input)
{
    PS_INPUT_CommonMesh Output;
    
    // 1. Fetch skinning weights for this render vertex
    uint renderVertexIndex = Input.VertexID + ClothRenderVertexOffset;
    FClothSkinningWeightGPU skinning = SkinningWeightBuffer[renderVertexIndex];
    
    // 2. Perform Linear Blend Skinning (up to 4 influences)
    float3 skinnedPosition = float3(0, 0, 0);
    float3 skinnedNormal = float3(0, 0, 0);
    
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float weight = skinning.Weights[i];
        if (weight > 0.0)
        {
            uint simVertexIndex = skinning.SimVertexIndices[i];
            
            // Sample simulation position/normal (already in global/world space for batched mode)
            float3 simPos = SimPositionBuffer[simVertexIndex].xyz;
            float3 simNormal = SimNormalBuffer[simVertexIndex];
            
            // Accumulate weighted contribution
            skinnedPosition += weight * simPos;
            skinnedNormal += weight * simNormal;
        }
    }
    
    // 3. Normalize blended normal
    skinnedNormal = normalize(skinnedNormal);
    
    // 4. Transform to clip space
    // Note: For batched mode, positions are already in world space, so ClothWorldMatrix is usually identity
    float4 worldPos = mul(float4(skinnedPosition, 1.0), ClothWorldMatrix);
    Output.Position = mul(worldPos, ViewMatrix);
    Output.Position = mul(Output.Position, ProjectionMatrix);
    
    // 5. Pass through world-space data for pixel shader
    Output.WorldPosition = worldPos.xyz;
    
    // NORMAL TRANSFORMATION FIX:
    // For batched cloth, ClothWorldMatrix is ALWAYS Identity (particles already in world space)
    // When matrix is identity, normal transformation simplifies to just the normal itself
    // No inverse-transpose needed because:
    // - Identity matrix: inverse-transpose(I) = I
    // - Uniform scale: inverse-transpose preserves direction
    // - Non-uniform scale: NOT APPLICABLE (matrix is identity for batched mode)
    //
    // If future non-batched mode uses non-identity transforms with non-uniform scaling,
    // inverse-transpose would be required: normalize(mul(skinnedNormal, (float3x3)InverseTransposeMatrix))
    Output.WorldNormal = normalize(mul(skinnedNormal, (float3x3)ClothWorldMatrix));
    Output.UV = Input.UV;
    
    // 6. Calculate tangent for normal mapping (simplified approach)
    // Generate tangent perpendicular to normal
    float3 worldTangent;
    if (abs(Output.WorldNormal.y) < 0.999)
    {
        // Use up vector to generate tangent
        worldTangent = normalize(cross(float3(0, 1, 0), Output.WorldNormal));
    }
    else
    {
        // Normal is nearly vertical, use right vector instead
        worldTangent = normalize(cross(float3(1, 0, 0), Output.WorldNormal));
    }
    Output.WorldTangent = float4(worldTangent, 1.0);
    
    // 7. Default color (white - will be modulated by material)
    Output.Color = float4(1, 1, 1, 1);
    
    return Output;
}
