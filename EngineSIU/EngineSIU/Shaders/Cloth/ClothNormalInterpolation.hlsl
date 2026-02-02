/**
 * Cloth Normal Interpolation Compute Shader
 * Interpolates simulation mesh normals to render mesh using skinning weights
 * 
 * This shader reuses the same skinning weights as position skinning,
 * ensuring consistent deformation between positions and normals.
 */

#include "ClothCommon.hlsli"

// Maximum influences per render vertex (same as skeletal mesh)
#define MAX_INFLUENCES 4

/**
 * GPU skinning weight structure
 * Maps render vertex to simulation vertices with blend weights
 */
struct FClothGPUSkinningWeight
{
    uint SimVertexIndices[MAX_INFLUENCES];  // Simulation vertex indices
    float Weights[MAX_INFLUENCES];          // Blend weights (sum = 1.0)
    uint NumInfluences;                     // Actual number of influences (1-4)
    float Padding[3];                       // Align to 32 bytes
};

// Input: Simulation mesh normals (computed by existing ClothUpdateNormals shader)
StructuredBuffer<float3> SimulationNormals : register(t0);

// Input: Skinning weights (render vertex → sim vertex mapping)
StructuredBuffer<FClothGPUSkinningWeight> SkinningWeights : register(t1);

// Output: Interpolated render mesh normals
RWStructuredBuffer<float3> RenderNormals : register(u0);

cbuffer NormalInterpolationConstants : register(b0)
{
    uint NumRenderVertices;
    uint RenderVertexOffset;    // For batched instances
    uint SimVertexOffset;       // For batched instances
    uint Padding;
};

/**
 * Interpolate normals from simulation mesh to render mesh
 * Uses weighted blend based on skinning weights (same as position skinning)
 */
[numthreads(256, 1, 1)]
void InterpolateNormalsCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint renderVertexID = DispatchThreadID.x;
    if (renderVertexID >= NumRenderVertices)
        return;
    
    // Get skinning weights for this render vertex
    uint weightIndex = RenderVertexOffset + renderVertexID;
    FClothGPUSkinningWeight weights = SkinningWeights[weightIndex];
    
    // Blend simulation normals using skinning weights
    float3 blendedNormal = float3(0, 0, 0);
    
    for (uint i = 0; i < weights.NumInfluences && i < MAX_INFLUENCES; ++i)
    {
        uint simIndex = SimVertexOffset + weights.SimVertexIndices[i];
        float weight = weights.Weights[i];
        
        // Accumulate weighted normals
        float3 simNormal = SimulationNormals[simIndex];
        blendedNormal += simNormal * weight;
    }
    
    // Normalize the blended normal
    // This is critical - interpolated normals need renormalization
    float len = length(blendedNormal);
    if (len > 1e-6f)
    {
        blendedNormal = blendedNormal / len;
    }
    else
    {
        // Fallback to up vector if invalid
        blendedNormal = float3(0, 0, 1);
    }
    
    // Write interpolated normal
    uint outputIndex = RenderVertexOffset + renderVertexID;
    RenderNormals[outputIndex] = blendedNormal;
}
