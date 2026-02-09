/**
 * Cloth Self-Collision Build Grid Shader
 * Pass 1: Build spatial hash grid for self-collision detection
 * 
 * This shader hashes particles into a 3D grid structure for efficient
 * neighbor queries in the collision solver pass.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);  // Predicted positions (after constraints)
StructuredBuffer<float> InvMass : register(t1);                 // Inverse masses

// Output buffers
RWStructuredBuffer<uint> CellCounters : register(u0);  // Per-cell particle count (atomic)
RWStructuredBuffer<uint> CellData : register(u1);      // Flat array of particle indices

/**
 * Compute grid cell from world position
 */
uint3 GetGridCell(float3 position)
{
    float3 localPos = position - GridMin;
    uint3 cell = uint3(floor(localPos / CellSize));
    return clamp(cell, uint3(0, 0, 0), GridDimensions - uint3(1, 1, 1));
}

/**
 * Hash 3D cell coordinates to 1D index
 */
uint GetCellHash(uint3 cell)
{
    return cell.x + cell.y * GridDimensions.x + 
           cell.z * GridDimensions.x * GridDimensions.y;
}

/**
 * Build spatial hash grid
 * Each thread processes one particle and inserts it into the grid
 */
[numthreads(256, 1, 1)]
void BuildSpatialHashGridCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    if (particleIdx >= NumParticles)
        return;
    
    // Skip kinematic particles (they don't participate in self-collision)
    float invMass = InvMass[particleIdx];
    if (invMass == 0.0f)
        return;
    
    // Compute grid cell for this particle
    float3 pos = PredictedRead[particleIdx].Position;
    uint3 cell = GetGridCell(pos);
    uint cellHash = GetCellHash(cell);
    
    // Atomically increment cell counter and get slot index
    uint slot;
    InterlockedAdd(CellCounters[cellHash], 1, slot);
    
    // Write particle index to cell data (if space available)
    if (slot < MaxParticlesPerCell)
    {
        uint writeIndex = cellHash * MaxParticlesPerCell + slot;
        CellData[writeIndex] = particleIdx;
    }
    // Note: Overflow particles are silently dropped (acceptable trade-off for performance)
    // If overflow becomes an issue, increase MaxParticlesPerCell or CellSize
}
