/**
 * Cloth Self-Collision Particle Reordering Shader
 * Pass 3: Reorder particles by hash using prefix sum (counting sort scatter)
 * 
 * This shader reorders particles spatially by scattering them into sorted positions
 * based on their hash values and the prefix sum computed from cell counts.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<uint> ParticleHashes : register(t0);      // Hash per particle
StructuredBuffer<uint> CellPrefixSum : register(t1);       // Exclusive prefix sum of cell counts

// Output buffers
RWStructuredBuffer<uint> SortedParticleIndices : register(u0);  // Sorted particle indices
RWStructuredBuffer<uint> CellWriteOffsets : register(u1);       // Atomic write offsets (reuses CellCounts)

/**
 * Reorder particles by hash using counting sort scatter
 * Each thread processes one particle and writes it to its sorted position
 */
[numthreads(256, 1, 1)]
void ReorderParticlesCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles)
        return;
    
    uint hash = ParticleHashes[i];
    
    // Skip invalid particles (kinematic)
    if (hash == 0xFFFFFFFFu)
        return;
    
    // Atomically get write offset within this cell's range
    uint writeOffset;
    InterlockedAdd(CellWriteOffsets[hash], 1, writeOffset);
    
    // Compute final sorted index using prefix sum
    uint sortedIndex = CellPrefixSum[hash] + writeOffset;
    
    // Write original particle index to sorted position
    SortedParticleIndices[sortedIndex] = i;
}
