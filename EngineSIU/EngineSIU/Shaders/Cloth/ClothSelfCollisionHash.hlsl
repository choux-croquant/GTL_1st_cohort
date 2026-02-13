/**
 * Cloth Self-Collision Hash Computation Shader
 * Pass 1: Compute spatial hash for each particle using PhysixStudio's hash function
 * 
 * This shader computes a spatial hash for each particle based on its position
 * in the grid. Uses prime number multipliers for excellent hash distribution.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Predicted positions
StructuredBuffer<float> InvMass : register(t1);                 // Inverse masses

// Output buffers
RWStructuredBuffer<uint> ParticleHashes : register(u0);  // Hash per particle

/**
 * PhysixStudio-style hash function with prime multipliers
 * Provides excellent distribution and minimal collisions
 */
uint HashCoords(int3 cell)
{
    uint h = uint(cell.x) * 92837111u ^
             uint(cell.y) * 689287499u ^
             uint(cell.z) * 283923481u;
    
    uint numCells = GridDimensions.x * GridDimensions.y * GridDimensions.z;
    return h % numCells;
}

/**
 * Compute grid cell from world position
 */
int3 GetGridCell(float3 position)
{
    float3 localPos = position - GridMin;
    int3 cell = int3(floor(localPos / CellSize));
    return clamp(cell, int3(0, 0, 0), int3(GridDimensions) - int3(1, 1, 1));
}

/**
 * Hash particles into spatial grid
 * Each thread processes one particle
 */
[numthreads(256, 1, 1)]
void HashParticlesCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles)
        return;
    
    // Skip kinematic particles (they don't participate in self-collision)
    if (InvMass[i] == 0.0f)
    {
        ParticleHashes[i] = 0xFFFFFFFFu;  // Mark as invalid
        return;
    }
    
    // Compute grid cell for this particle
    float3 pos = PredictedRead[i].Position;
    int3 cell = GetGridCell(pos);
    
    // Compute hash using PhysixStudio's prime multiplier method
    uint hash = HashCoords(cell);
    ParticleHashes[i] = hash;
}
