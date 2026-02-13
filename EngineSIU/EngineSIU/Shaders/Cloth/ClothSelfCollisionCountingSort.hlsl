/**
 * Cloth Self-Collision Counting Sort Shader
 * Pass 2: Build histogram of particles per cell (counting sort preparation)
 * 
 * This shader counts how many particles fall into each grid cell,
 * which is used to compute the prefix sum for particle reordering.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<uint> ParticleHashes : register(t0);  // Hash per particle

// Output buffers
RWStructuredBuffer<uint> CellCounts : register(u0);  // Atomic counters per cell

/**
 * Build histogram of particles per cell
 * Each thread processes one particle and atomically increments its cell counter
 */
[numthreads(256, 1, 1)]
void CountingSortHistogramCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles)
        return;
    
    uint hash = ParticleHashes[i];
    
    // Skip invalid particles (kinematic)
    if (hash == 0xFFFFFFFFu)
        return;
    
    // Atomically increment cell counter
    InterlockedAdd(CellCounts[hash], 1);
}
