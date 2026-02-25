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

struct FClothSkinningWeightTriangleGPU
{
    uint3 SimTriangleIndices;      // 12 bytes - 3 simulation vertex indices forming triangle
    uint Padding1;                 // 4 bytes - Padding for alignment
    float3 BarycentricCoords;      // 12 bytes - Barycentric coordinates (u, v, w) where u+v+w=1
    float Padding2;                // 4 bytes - Padding for alignment
    float3 TangentSpaceOffset;     // 12 bytes - Offset in triangle's local tangent frame
    float Padding3;                // 4 bytes - Padding for alignment
};
// Total: 48 bytes (matches C++ FClothSkinningWeightTriangleGPU)

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

StructuredBuffer<float4> SimPositionBuffer : register(t14);   // xyz=position, w=invMass (dynamic, from simulation)
StructuredBuffer<float3> SimNormalBuffer : register(t15);     // Simulation normals (dynamic, from simulation)
StructuredBuffer<FClothSkinningWeightGPU> SkinningWeightBuffer : register(t16);  // Legacy K-nearest neighbor skinning weights (static)
StructuredBuffer<FClothSkinningWeightTriangleGPU> TriangleSkinningWeightBuffer : register(t17);  // NEW: Triangle-based skinning weights (static)

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
VS_OUTPUT_ClothMesh main(VS_INPUT_ClothProduction Input)
{
    VS_OUTPUT_ClothMesh Output;
    
    // Fetch triangle-based skinning weight for this render vertex
    uint renderVertexIndex = Input.VertexID + ClothRenderVertexOffset;
    FClothSkinningWeightTriangleGPU skinning = TriangleSkinningWeightBuffer[renderVertexIndex];
    
    // Fetch deformed simulation triangle vertices
    float3 simPos0 = SimPositionBuffer[skinning.SimTriangleIndices.x].xyz;
    float3 simPos1 = SimPositionBuffer[skinning.SimTriangleIndices.y].xyz;
    float3 simPos2 = SimPositionBuffer[skinning.SimTriangleIndices.z].xyz;
    
    // Interpolate base position using barycentric coordinates
    float3 basePos = skinning.BarycentricCoords.x * simPos0 +
                     skinning.BarycentricCoords.y * simPos1 +
                     skinning.BarycentricCoords.z * simPos2;
    
    // Reconstruct tangent frame from deformed triangle
    float3 edge1 = simPos1 - simPos0;
    float3 edge2 = simPos2 - simPos0;
    
    float3 normal_deformed = cross(edge1, edge2);
    float normalLen = length(normal_deformed);
    if (normalLen > 1e-6)
        normal_deformed /= normalLen;
    else
        normal_deformed = float3(0, 0, 1); // Degenerate triangle fallback
    
    float3 tangent_deformed = normalize(edge1);
    float3 bitangent_deformed = cross(normal_deformed, tangent_deformed);
    
    // Rotate offset from tangent space to world space
    float3 offset_world = skinning.TangentSpaceOffset.x * tangent_deformed +
                          skinning.TangentSpaceOffset.y * bitangent_deformed +
                          skinning.TangentSpaceOffset.z * normal_deformed;
    
    // Final skinned position = base position + rotated offset
    float3 skinnedPosition = basePos + offset_world;
    
    // Compute skinned normal (interpolate simulation normals)
    float3 simNormal0 = SimNormalBuffer[skinning.SimTriangleIndices.x];
    float3 simNormal1 = SimNormalBuffer[skinning.SimTriangleIndices.y];
    float3 simNormal2 = SimNormalBuffer[skinning.SimTriangleIndices.z];
    
    float3 skinnedNormal = skinning.BarycentricCoords.x * simNormal0 +
                           skinning.BarycentricCoords.y * simNormal1 +
                           skinning.BarycentricCoords.z * simNormal2;
    
    float normalLenSq = dot(skinnedNormal, skinnedNormal);
    if (normalLenSq < 1e-6)
    {
        // Fallback to triangle normal if simulation normals are invalid
        skinnedNormal = normal_deformed;
    }
    skinnedNormal = normalize(skinnedNormal);
    
    // Transform to clip space
    float4 worldPos = mul(float4(skinnedPosition, 1.0), ClothWorldMatrix);
    Output.Position = mul(worldPos, ViewMatrix);
    Output.Position = mul(Output.Position, ProjectionMatrix);
    
    // Pass through world-space data for pixel shader
    Output.WorldPosition = worldPos.xyz;
    
    float3 worldNormal = mul(skinnedNormal, (float3x3)ClothWorldMatrix);
    float worldNormalLenSq = dot(worldNormal, worldNormal);
    if (worldNormalLenSq < 1e-6)
    {
        worldNormal = float3(0, 0, 1);
    }
    Output.WorldNormal = normalize(worldNormal);
    Output.UV = Input.UV;
    
    // Calculate tangent for normal mapping (simplified approach)
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
    
    // Default color
    Output.Color = float4(1, 1, 1, 1);
    
    return Output;
}
