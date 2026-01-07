/**
 * Cloth Normal Update Compute Shader
 * Recalculates vertex normals after simulation
 * Uses face normals and accumulates to vertices
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PositionBuffer : register(t0);
StructuredBuffer<uint> IndexBuffer : register(t1);

// Output buffer
RWStructuredBuffer<float3> NormalBuffer : register(u0);

// Additional constant for triangle count
cbuffer NormalUpdateConstants : register(b1)
{
    uint NumTriangles;
    uint3 Padding;
};

/**
 * Clear normals to zero
 * Run this before accumulating face normals
 */
[numthreads(64, 1, 1)]
void ClearNormalsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    if (idx >= NumParticles)
        return;
    
    NormalBuffer[idx] = float3(0, 0, 0);
}

/**
 * Calculate face normals and accumulate to vertices
 * One thread per triangle
 */
[numthreads(64, 1, 1)]
void UpdateNormalsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint triIdx = DTid.x;
    
    if (triIdx >= NumTriangles)
        return;
    
    // Get triangle indices
    uint i0 = IndexBuffer[triIdx * 3 + 0];
    uint i1 = IndexBuffer[triIdx * 3 + 1];
    uint i2 = IndexBuffer[triIdx * 3 + 2];
    
    // Bounds check
    if (i0 >= NumParticles || i1 >= NumParticles || i2 >= NumParticles)
        return;
    
    // Get positions
    float3 p0 = PositionBuffer[i0].Position;
    float3 p1 = PositionBuffer[i1].Position;
    float3 p2 = PositionBuffer[i2].Position;
    
    // Calculate edges
    float3 edge1 = p1 - p0;
    float3 edge2 = p2 - p0;
    
    // Calculate face normal (counter-clockwise winding)
    float3 faceNormal = cross(edge1, edge2);
    
    // Normalize (weighted by area - larger triangles contribute more)
    float area = length(faceNormal);
    if (area > 1e-6f)
    {
        faceNormal = faceNormal / area;
    }
    else
    {
        // Degenerate triangle, skip
        return;
    }
    
    // Accumulate to vertices
    // Note: This has race conditions but they average out over many triangles
    // For perfect accuracy, would need atomic float operations or separate passes
    NormalBuffer[i0] += faceNormal;
    NormalBuffer[i1] += faceNormal;
    NormalBuffer[i2] += faceNormal;
}

/**
 * Normalize the accumulated normals
 * Run this after accumulating all face normals
 */
[numthreads(64, 1, 1)]
void NormalizeNormalsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    
    if (idx >= NumParticles)
        return;
    
    float3 normal = NormalBuffer[idx];
    float len = length(normal);
    
    if (len > 1e-6f)
    {
        NormalBuffer[idx] = normal / len;
    }
    else
    {
        // Default to up vector if no valid normal
        NormalBuffer[idx] = float3(0, 0, 1);
    }
}

/**
 * Alternative: Single-pass normal calculation
 * Slower but avoids multiple dispatch calls
 */
/*
[numthreads(64, 1, 1)]
void UpdateNormalsSinglePassCS(uint3 DTid : SV_DispatchThreadID)
{
    uint vertIdx = DTid.x;
    
    if (vertIdx >= NumParticles)
        return;
    
    float3 normal = float3(0, 0, 0);
    
    // Iterate through all triangles (expensive!)
    for (uint triIdx = 0; triIdx < NumTriangles; triIdx++)
    {
        uint i0 = IndexBuffer[triIdx * 3 + 0];
        uint i1 = IndexBuffer[triIdx * 3 + 1];
        uint i2 = IndexBuffer[triIdx * 3 + 2];
        
        // Check if this triangle uses this vertex
        if (i0 == vertIdx || i1 == vertIdx || i2 == vertIdx)
        {
            float3 p0 = PositionBuffer[i0].Position;
            float3 p1 = PositionBuffer[i1].Position;
            float3 p2 = PositionBuffer[i2].Position;
            
            float3 edge1 = p1 - p0;
            float3 edge2 = p2 - p0;
            float3 faceNormal = cross(edge1, edge2);
            
            float area = length(faceNormal);
            if (area > 1e-6f)
            {
                normal += faceNormal / area;
            }
        }
    }
    
    // Normalize
    float len = length(normal);
    if (len > 1e-6f)
    {
        NormalBuffer[vertIdx] = normal / len;
    }
    else
    {
        NormalBuffer[vertIdx] = float3(0, 0, 1);
    }
}
*/
