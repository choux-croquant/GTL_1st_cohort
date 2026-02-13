/**
 * Cloth Self-Collision Build Cell Ranges Shader
 * Pass 4: Build cell start/end arrays (PhysixStudio-style)
 * 
 * This shader builds the cell start and end index arrays by detecting
 * boundaries in the sorted particle list where the hash value changes.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<uint> ParticleHashes : register(t0);           // Hash per particle (unsorted)
StructuredBuffer<uint> SortedParticleIndices : register(t1);    // Sorted particle indices

// Output buffers
RWStructuredBuffer<uint> CellStarts : register(u0);  // Cell start index (0xFFFFFFFF if empty)
RWStructuredBuffer<uint> CellEnds : register(u1);    // Cell end index

/**
 * Build cell start/end arrays from sorted particles
 * Each thread processes one particle and detects cell boundaries
 */
[numthreads(256, 1, 1)]
void BuildCellRangesCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles)
        return;
    
    // Get current particle's hash
    uint particleIdx = SortedParticleIndices[i];
    uint currHash = ParticleHashes[particleIdx];
    
    // Skip invalid particles
    if (currHash == 0xFFFFFFFFu)
        return;
    
    // Get previous particle's hash (if exists)
    uint prevHash = 0xFFFFFFFFu;
    if (i > 0)
    {
        uint prevParticleIdx = SortedParticleIndices[i - 1];
        prevHash = ParticleHashes[prevParticleIdx];
    }
    
    // Detect cell boundaries
    if (i == 0 || currHash != prevHash)
    {
        // Start of a new cell
        CellStarts[currHash] = i;
        
        // End of previous cell (if not first particle)
        if (i > 0 && prevHash != 0xFFFFFFFFu)
        {
            CellEnds[prevHash] = i;
        }
    }
    
    // Handle last particle
    if (i == NumParticles - 1)
    {
        CellEnds[currHash] = NumParticles;
    }
}
