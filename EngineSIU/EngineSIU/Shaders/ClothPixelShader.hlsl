/**
 * Cloth Pixel Shader
 * Standard PBR lighting for cloth rendering
 */

#include "ShaderRegisters.hlsl"

cbuffer MaterialConstants : register(b1)
{
    FMaterial Material;
}

#include "Light.hlsl"

float4 mainPS(PS_INPUT_CommonMesh Input) : SV_TARGET
{
    // Sample diffuse texture
    float3 diffuseColor = Material.DiffuseColor;
    if (Material.TextureFlag & TEXTURE_FLAG_DIFFUSE)
    {
        diffuseColor = MaterialTextures[TEXTURE_SLOT_DIFFUSE].Sample(SamplerLinearWrap, Input.UV).rgb;
        diffuseColor = SRGBToLinear(diffuseColor);
    }
    
    // Sample normal map
    float3 normal = normalize(Input.WorldNormal);
    if (Material.TextureFlag & TEXTURE_FLAG_NORMAL)
    {
        float3 tangentNormal = MaterialTextures[TEXTURE_SLOT_NORMAL].Sample(SamplerLinearWrap, Input.UV).xyz;
        tangentNormal = tangentNormal * 2.0 - 1.0;
        
        // Transform tangent space normal to world space
        float3 N = normal;
        float3 T = normalize(Input.WorldTangent.xyz);
        float3 B = cross(N, T) * Input.WorldTangent.w;
        float3x3 TBN = float3x3(T, B, N);
        
        normal = normalize(mul(tangentNormal, TBN));
    }
    
    // Sample metallic
    float metallic = Material.Metallic;
    if (Material.TextureFlag & TEXTURE_FLAG_METALLIC)
    {
        metallic = MaterialTextures[TEXTURE_SLOT_METALLIC].Sample(SamplerLinearWrap, Input.UV).r;
    }
    
    // Sample roughness
    float roughness = Material.Roughness;
    if (Material.TextureFlag & TEXTURE_FLAG_ROUGHNESS)
    {
        roughness = MaterialTextures[TEXTURE_SLOT_ROUGHNESS].Sample(SamplerLinearWrap, Input.UV).r;
    }
    
    // Calculate lighting
    float3 finalColor = Lighting(
        Input.WorldPosition,
        normal,
        ViewWorldLocation,
        diffuseColor,
        Material.SpecularColor,
        Material.Shininess,
        1.0  // Opacity
    );
    
    // Add emissive
    if (Material.TextureFlag & TEXTURE_FLAG_EMISSIVE)
    {
        float3 emissive = MaterialTextures[TEXTURE_SLOT_EMISSIVE].Sample(SamplerLinearWrap, Input.UV).rgb;
        finalColor += emissive * Material.EmissiveColor;
    }
    else
    {
        finalColor += Material.EmissiveColor;
    }
    
    // Convert to sRGB for output
    finalColor = LinearToSRGB(finalColor);
    
    return float4(finalColor, Material.Transparency);
}
