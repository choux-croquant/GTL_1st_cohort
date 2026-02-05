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
float4 main(PS_INPUT_ClothMesh Input) : SV_TARGET
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
    // 2. Sample and apply normal map (with robust fallbacks for zero normals/tangents)
    float3 worldNormal = Input.WorldNormal;
    if (!Input.IsFrontFace)
    {
        worldNormal = -worldNormal;
    }
    
    // Guard against zero/NaN normals reaching PS
    if (dot(worldNormal, worldNormal) < 1e-6)
    {
        worldNormal = float3(0, 0, 1);
    }
    worldNormal = normalize(worldNormal);
    // Normal map path remains optional; enable when material flag is set.
    if (Material.TextureFlag & TEXTURE_FLAG_NORMAL)
    {
        float3 normalSample = MaterialTextures[TEXTURE_SLOT_NORMAL].Sample(SamplerLinearWrap, Input.UV).xyz;
        normalSample = normalSample * 2.0 - 1.0; // [0,1] -> [-1,1]

        float4 safeTangent = Input.WorldTangent;
        // If tangent is degenerate, rebuild a basis from the normal to keep TBN valid.
        if (dot(safeTangent.xyz, safeTangent.xyz) < 1e-6)
        {
            float3 axis = (abs(worldNormal.y) < 0.999) ? float3(0, 1, 0) : float3(1, 0, 0);
            float3 rebuiltTangent = normalize(cross(axis, worldNormal));
            safeTangent = float4(rebuiltTangent, 1.0);
        }

        //worldNormal = ApplyNormalMap(normalSample, worldNormal, safeTangent);
    }

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
    
       // Simplified directional lighting for cloth (no shadows, no tile culling)
    float3 accumulatedDiffuse = float3(0.0, 0.0, 0.0);
    float3 accumulatedSpecular = float3(0.0, 0.0, 0.0);

    // Apply directional light (index 0)
    if (DirectionalLightsCount > 0)
    {
        FDirectionalLightInfo lightInfo = Directional[0];

        // Light direction (negate because Direction points FROM light)
        float3 L = normalize(-lightInfo.Direction);

        // View direction
        float3 V = normalize(ViewWorldLocation - Input.WorldPosition);

        // Lambert diffuse
        float NdotL = saturate(dot(worldNormal, L));

        // PBR calculations
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
        float3 H = normalize(V + L);
        float NdotH = saturate(dot(worldNormal, H));
        float NdotV = saturate(dot(worldNormal, V));
        float LdotH = saturate(dot(L, H));

        // Disney Diffuse
        float roughness2 = roughness * roughness;
        float Fd90 = 0.5 + 2.0 * roughness2 * LdotH * LdotH;
        float FdL = 1.0 + (Fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
        float FdV = 1.0 + (Fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
        float3 diffuse = (baseColor * (1.0 - metallic) * FdL * FdV) / 3.14159265359;

        // Cook-Torrance Specular (GGX)
        float alpha = max(0.001, roughness2);
        float a2 = alpha * alpha;
        float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
        float D = a2 / (3.14159265359 * denom * denom);

        float k = alpha * 0.5 + 0.0001;
        float gV = NdotV / (NdotV * (1.0 - k) + k);
        float gL = NdotL / (NdotL * (1.0 - k) + k);
        float G = gV * gL;

        float3 F = F0 + (1.0 - F0) * pow(1.0 - LdotH, 5.0);

        float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 1e-5);

        // Light energy
        float3 lightEnergy = lightInfo.LightColor.rgb * lightInfo.Intensity;

        // Accumulate
        accumulatedDiffuse += diffuse * lightEnergy * NdotL;
        accumulatedSpecular += specular * lightEnergy * NdotL;
    }

    // Add ambient/IBL contribution
    float3 ambient = float3(0.03, 0.03, 0.03);
    if (AmbientLightsCount > 0)
    {
        ambient = Ambient[0].AmbientColor.rgb;
    }
    accumulatedDiffuse += baseColor * (1.0 - metallic) * ambient;

    // Combine diffuse and specular
    float4 litColor = float4(accumulatedDiffuse + accumulatedSpecular, baseAlpha);
    
    // 7. Add emissive contribution
    litColor.rgb += emissive;
    
    // 8. Two-sided lighting support
    // If backfacing, flip the normal for lighting (already handled by rasterizer state)
    // The rasterizer is set to D3D11_CULL_NONE, so both sides render
    
    return litColor;
    // Shader connection TEST Color;
    //return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
