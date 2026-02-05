/**
 * Cloth Normal Update Compute Shader
 * Two-pass approach with integer accumulation to eliminate race conditions:
 * Pass 1: Accumulate triangle normals to vertices using atomic integer operations
 * Pass 2: Convert integers to floats, normalize, and store in final normal buffer
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PositionBuffer : register(t0);
Buffer<uint> IndexBuffer : register(t1);  // Typed buffer for index data

// Integer accumulation buffer (for atomic operations)
RWStructuredBuffer<int3> NormalAccumulationBuffer : register(u0);

// Final output buffer (float normals)
RWStructuredBuffer<float3> NormalBuffer : register(u1);

// Scale factor for float-to-int conversion (higher = more precision, but risk of overflow)
static const float NORMAL_SCALE = 10000.0f;

/**
 * PASS 1: Compute Triangle Normals and Accumulate to Vertices (Integer Atomic)
 *
 * Uses integer accumulation with InterlockedAdd to eliminate race conditions:
 * - One thread per triangle
 * - Compute face normal using cross product
 * - Scale to integer and accumulate atomically to all 3 vertices
 * - Thread-safe with no flickering/vibration
 */
[numthreads(256, 1, 1)]
void ComputeTriangleNormalsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint triIdx = DTid.x;
    
    // Early out if beyond triangle count
    if (triIdx >= NumConstraints)  // NumConstraints stores triangle count in this context
        return;
    
    // Get triangle vertex indices
    uint idx0 = IndexBuffer[triIdx * 3 + 0];
    uint idx1 = IndexBuffer[triIdx * 3 + 1];
    uint idx2 = IndexBuffer[triIdx * 3 + 2];
    
    // Bounds check
    if (idx0 >= NumParticles || idx1 >= NumParticles || idx2 >= NumParticles)
        return;
    
    // Get vertex positions
    float3 p0 = PositionBuffer[idx0].Position;
    float3 p1 = PositionBuffer[idx1].Position;
    float3 p2 = PositionBuffer[idx2].Position;
    
    // Calculate edges
    float3 edge1 = p1 - p0;
    float3 edge2 = p2 - p0;
    
    // Calculate face normal (cross product gives area-weighted normal)
    float3 faceNormal = cross(edge1, edge2);
    
    // Check for degenerate triangles
    float normalLengthSq = dot(faceNormal, faceNormal);
    if (normalLengthSq < 1e-12f)
    {
        // Degenerate triangle, skip
        return;
    }
    
    // Convert float normal to scaled integer for atomic accumulation
    int3 intNormal = int3(faceNormal * NORMAL_SCALE);
    
    // Atomic accumulation (thread-safe, no race conditions)
    InterlockedAdd(NormalAccumulationBuffer[idx0].x, intNormal.x);
    InterlockedAdd(NormalAccumulationBuffer[idx0].y, intNormal.y);
    InterlockedAdd(NormalAccumulationBuffer[idx0].z, intNormal.z);
    
    InterlockedAdd(NormalAccumulationBuffer[idx1].x, intNormal.x);
    InterlockedAdd(NormalAccumulationBuffer[idx1].y, intNormal.y);
    InterlockedAdd(NormalAccumulationBuffer[idx1].z, intNormal.z);
    
    InterlockedAdd(NormalAccumulationBuffer[idx2].x, intNormal.x);
    InterlockedAdd(NormalAccumulationBuffer[idx2].y, intNormal.y);
    InterlockedAdd(NormalAccumulationBuffer[idx2].z, intNormal.z);
}

/**
 * PASS 2: Convert Integer Accumulation to Float and Normalize
 *
 * - One thread per vertex
 * - Convert accumulated integer normal back to float
 * - Normalize and store in final normal buffer
 * - Handle zero-length normals with fallback
 */
[numthreads(256, 1, 1)]
void NormalizeVertexNormalsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint vertIdx = DTid.x;
    
    // Early out if beyond particle count
    if (vertIdx >= NumParticles)
        return;
    
    // Read accumulated integer normal and convert back to float
    int3 intNormal = NormalAccumulationBuffer[vertIdx];
    float3 normal = float3(intNormal) / NORMAL_SCALE;
    
    // Normalize
    float len = length(normal);
    
    if (len > 1e-6f)
    {
        // Valid normal - normalize it
        NormalBuffer[vertIdx] = normal / len;
    }
    else
    {
        // Zero or near-zero normal - use default up vector
        NormalBuffer[vertIdx] = float3(0, 1, 0);
    }
}

/**
 * Legacy entry point for compatibility
 * Calls Pass 1 (triangle normal accumulation)
 */
[numthreads(256, 1, 1)]
void UpdateNormalsCS(uint3 DTid : SV_DispatchThreadID)
{
    // Forward to Pass 1
    ComputeTriangleNormalsCS(DTid);
}
