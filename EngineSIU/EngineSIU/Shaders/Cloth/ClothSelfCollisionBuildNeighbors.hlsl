/**
 * Cloth Self-Collision Build Neighbor Lists Shader
 * Pass 5: Pre-compute neighbor lists by querying 27-cell neighborhood
 * 
 * This shader pre-computes neighbor lists for each particle by querying
 * the 27-cell neighborhood. This eliminates redundant queries in the solver
 * and provides significant performance improvement.
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothParticle> PredictedRead : register(t0);      // Predicted positions
StructuredBuffer<uint> SortedParticleIndices : register(t1);        // Sorted particle indices
StructuredBuffer<uint> CellStarts : register(t2);                   // Cell start indices
StructuredBuffer<uint> CellEnds : register(t3);                     // Cell end indices
StructuredBuffer<FClothAdjacency> AdjacencyBuffer : register(t4);  // Topology adjacency data
StructuredBuffer<FCollisionMask> CollisionMasks : register(t5);    // Collision masks per particle

// Output buffers
RWStructuredBuffer<uint> NeighborLists : register(u0);    // Neighbor particle indices [NumParticles × MaxNeighbors]
RWStructuredBuffer<uint> NeighborCounts : register(u1);   // Number of neighbors per particle

/**
 * PhysixStudio-style hash function (must match Pass 1)
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
 * Check if two particles are topologically adjacent (share an edge)
 * Uses pre-computed adjacency buffer for accurate filtering
 */
bool IsTopologicallyAdjacent(uint particleA, uint particleB, uint instanceA, uint instanceB)
{
    // Only check adjacency for same-instance particles
    if (instanceA != instanceB)
        return false;
    
    // Look up adjacency buffer for particle A
    // Note: In P0, we'll use a simple linear search. P1 can optimize with hash lookup.
    for (uint i = 0; i < NumParticles; ++i)
    {
        FClothAdjacency adj = AdjacencyBuffer[i];
        if (adj.ParticleIndex == particleA)
        {
            // Check if particleB is in the connected list
            for (uint j = 0; j < 8; ++j)
            {
                if (adj.ConnectedParticles[j] == particleB)
                    return true;
                if (adj.ConnectedParticles[j] == 0xFFFFFFFFu)
                    break;  // End of list
            }
            return false;
        }
    }
    
    return false;
}

/**
 * Build neighbor lists for each particle
 * Each thread processes one particle and queries its 27-cell neighborhood
 */
[numthreads(256, 1, 1)]
void BuildNeighborListsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    if (i >= NumParticles)
        return;
    
    float3 posA = PredictedRead[i].Position;
    uint instanceA = PredictedRead[i].InstanceID;
    int3 cellA = GetGridCell(posA);
    
    uint neighborCount = 0;
    float r2 = CollisionRadius * CollisionRadius * 4.0f;  // Detection diameter squared
    
    // Query 27-cell neighborhood
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int3 neighborCell = cellA + int3(dx, dy, dz);
                
                // Bounds check
                if (any(neighborCell < int3(0, 0, 0)) || 
                    any(neighborCell >= int3(GridDimensions)))
                    continue;
                
                uint hash = HashCoords(neighborCell);
                uint start = CellStarts[hash];
                
                // Skip empty cells
                if (start == 0xFFFFFFFFu)
                    continue;
                
                uint end = CellEnds[hash];
                
                // Check all particles in this cell
                for (uint idx = start; idx < end; ++idx)
                {
                    uint j = SortedParticleIndices[idx];
                    
                    // Skip self
                    if (j == i)
                        continue;
                    
                    // Prevent double-counting (PhysixStudio pattern)
                    // Only process pairs where i < j
                    if (j < i)
                        continue;
                    
                    uint instanceB = PredictedRead[j].InstanceID;
                    
                    // Instance filtering
                    bool sameInstance = (instanceA == instanceB);
                    if (sameInstance && !bEnableIntraInstanceCollision)
                        continue;
                    if (!sameInstance && !bEnableInterInstanceCollision)
                        continue;
                    
                    // Topology filtering (same-instance only)
                    if (sameInstance && IsTopologicallyAdjacent(i, j, instanceA, instanceB))
                        continue;
                    
                    // Distance check
                    float3 posB = PredictedRead[j].Position;
                    float dist2 = dot(posA - posB, posA - posB);
                    if (dist2 >= r2)
                        continue;
                    
                    // Add to neighbor list (if space available)
                    if (neighborCount < MaxNeighbors)
                    {
                        NeighborLists[i * MaxNeighbors + neighborCount] = j;
                        neighborCount++;
                    }
                    // Note: Overflow is silently dropped in P0
                    // P1 will add overflow handling and warnings
                }
            }
        }
    }
    
    NeighborCounts[i] = neighborCount;
}
