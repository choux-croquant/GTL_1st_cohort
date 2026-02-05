/**
 * Cloth Production Pixel Shader
 * PBR material evaluation for production cloth rendering
 * Supports full material system with textures and lighting
 */

#define LIGHTING_MODEL_PBR
#include "../ShaderRegisters.hlsl"
#include "../Light.hlsl"

// Material constant buffer (register b1)
cbuffer MaterialBuffer : register(b1)
{
    FMaterial Material;
};

/**
 * Apply normal mapping to world normal
 * Transforms tangent-space normal from texture to world space
 */
float3 ApplyNormalMap(float3 sampledNormal, float3 worldNormal, float4 worldTangent)
{
    // Reconstruct tangent space basis
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w; // Bitangent with handedness
    
    // Transform tangent-space normal to world space
    float3x3 TBN = float3x3(T, B, N);
    float3 normal = normalize(mul(sampledNormal, TBN));
    
    return normal;
}

/**
 * Main pixel shader entry point
 * Evaluates material and lighting for cloth surface
 */
float4 main(PS_INPUT_CommonMesh Input) : SV_TARGET
{
    // 1. Sample base color (albedo)
    float3 baseColor = Material.DiffuseColor;
    if (Material.TextureFlag & TEXTURE_FLAG_DIFFUSE)
    {
        float4 albedoSample = MaterialTextures[TEXTURE_SLOT_DIFFUSE].Sample(SamplerLinearWrap, Input.UV);
        baseColor = SRGBToLinear(albedoSample.rgb);
        //return float4(Input.UV.x, Input.UV.y, 0.0f, 1.0f);
        //return albedoSample;
    }
    //return float4(1.0f, 0.0f, 0.0f, 1.0f);
    // 2. Sample and apply normal map
    float3 worldNormal = normalize(Input.WorldNormal);
    //if (Material.TextureFlag & TEXTURE_FLAG_NORMAL)
    //{
    //    float3 normalSample = MaterialTextures[TEXTURE_SLOT_NORMAL].Sample(SamplerLinearWrap, Input.UV).xyz;
    //    // Convert from [0,1] to [-1,1]
    //    normalSample = normalSample * 2.0 - 1.0;
    //    worldNormal = ApplyNormalMap(normalSample, Input.WorldNormal, Input.WorldTangent);
    //}
    
    // 3. Sample metallic
    float metallic = Material.Metallic;
    if (Material.TextureFlag & TEXTURE_FLAG_METALLIC)
    {
        metallic = MaterialTextures[TEXTURE_SLOT_METALLIC].Sample(SamplerLinearWrap, Input.UV).r;
    }
    
    // 4. Sample roughness
    float roughness = Material.Roughness;
    if (Material.TextureFlag & TEXTURE_FLAG_ROUGHNESS)
    {
        roughness = MaterialTextures[TEXTURE_SLOT_ROUGHNESS].Sample(SamplerLinearWrap, Input.UV).r;
    }
    
    // 5. Sample emissive
    float3 emissive = Material.EmissiveColor;
    if (Material.TextureFlag & TEXTURE_FLAG_EMISSIVE)
    {
        float3 emissiveSample = MaterialTextures[TEXTURE_SLOT_EMISSIVE].Sample(SamplerLinearWrap, Input.UV).rgb;
        emissive = SRGBToLinear(emissiveSample);
    }
    
    // 6. Calculate lighting using PBR model
    float baseAlpha = 1.0; // Cloth is typically opaque
    
    // Use tiled lighting if available, otherwise use simple lighting
    // For now, use simple lighting (tile index would require screen-space calculation)
    float4 litColor = Lighting(
        Input.WorldPosition,
        worldNormal,
        ViewWorldLocation,
        baseColor,
        metallic,
        roughness,
        baseAlpha
    );
    
    // 7. Add emissive contribution
    litColor.rgb += emissive;
    
    // 8. Two-sided lighting support
    // If backfacing, flip the normal for lighting (already handled by rasterizer state)
    // The rasterizer is set to D3D11_CULL_NONE, so both sides render
    
    return litColor;
    // Shader connection TEST Color;
    //return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
