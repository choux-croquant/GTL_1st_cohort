# Cloth Self-Collision Implementation Plan

## Executive Summary

This document outlines the implementation plan for adding GPU-based self-collision detection to the existing XPBD cloth simulation system using a spatial hash grid approach with point-point distance constraints.

**Target Performance**: <1ms for 5,000 particles  
**Algorithm**: Spatial Hash Grid + Point-Point Distance Constraints  
**Integration Point**: After XPBD constraint iterations, before velocity finalization

---

## 1. Architecture Analysis

### 1.1 Current Simulation Pipeline

Based on analysis of [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:917-965), the simulation loop follows this structure:

```
SimulateSubstep(SubstepDeltaTime):
  1. DispatchIntegration()           // Apply forces, predict positions
  2. DispatchCollisionSDF()          // External collision (SDF-based)
  3. DispatchEdgeCollisionSDF()      // Edge-based external collision
  4. FOR iter in NumIterations:
       - DispatchConstraintSolver()      // Distance constraints
       - DispatchBendConstraintSolver()  // Bend constraints
       - DispatchAreaConstraintSolver()  // Area constraints
       - DispatchApplyDeltas()           // Apply accumulated corrections
  5. DispatchComputeKinematicTargets()  // Apply kinematic attachments
  6. DispatchFinalize()              // Update velocities and positions
```

**Integration Point**: Insert self-collision **after step 4 (constraint iterations)** and **before step 5 (kinematic targets)**. This ensures:
- Self-collision works with constraint-corrected positions
- Kinematic particles can override self-collision if needed
- Follows the same pattern as external collision (SDF)

### 1.2 Existing Buffer Architecture

The system uses a **unified batched buffer approach** (Velvet-inspired):
- **UnifiedPositionBuffer**: Final positions (for rendering)
- **UnifiedPredictedBuffer**: Working buffer (constraint solving)
- **UnifiedVelocityBuffer**: Particle velocities
- **UnifiedInvMassBuffer**: Inverse masses (0 = kinematic)
- **UnifiedPositionDeltaBuffer**: Atomic accumulation (int3)
- **UnifiedPositionWeightBuffer**: Constraint count (int)

Self-collision will follow the same **Jacobi-style accumulation pattern** used by existing constraints.

### 1.3 Code Style Patterns

From analysis of existing shaders and C++ code:
- **Naming**: PascalCase for types, camelCase for variables
- **Shader structure**: Separate files per compute shader
- **Buffer binding**: Explicit register slots (t0, t1, u0, u1, etc.)
- **Atomic operations**: InterlockedAdd for thread-safe accumulation
- **Fixed-point scaling**: 10000.0f scale for position deltas
- **Thread group size**: 256 threads (or 64 for some passes)

---

## 2. Spatial Hash Grid Design

### 2.1 Grid Parameters

```cpp
// Auto-computed from mesh
float CellSize = AverageEdgeLength * 2.0f;  // Typical: 5-10cm for cloth

// Grid dimensions (configurable, default 32x32x32)
uint3 GridDimensions = {32, 32, 32};
uint TotalCells = 32768;  // 32^3

// Per-cell capacity (configurable, default 16)
uint MaxParticlesPerCell = 16;
```

**Rationale**:
- Cell size = 2× average edge length ensures neighboring particles are in adjacent cells
- 32³ grid provides good spatial resolution for typical cloth sizes (1-5m)
- 16 particles/cell handles dense cloth regions without overflow

### 2.2 Hash Function

```hlsl
// Spatial hash function (matches Velvet/NVIDIA approaches)
uint3 GetGridCell(float3 position, float3 gridMin, float cellSize)
{
    float3 localPos = position - gridMin;
    uint3 cell = uint3(floor(localPos / cellSize));
    return clamp(cell, uint3(0,0,0), GridDimensions - 1);
}

uint GetCellHash(uint3 cell)
{
    // Simple hash: flatten 3D to 1D
    return cell.x + cell.y * GridDimensions.x + cell.z * GridDimensions.x * GridDimensions.y;
}
```

### 2.3 GPU Buffer Layout

```cpp
// Buffer 1: Cell counters (atomic)
// Size: TotalCells × sizeof(uint) = 32768 × 4 = 128 KB
RWStructuredBuffer<uint> CellCounters;  // Per-cell particle count

// Buffer 2: Cell data (flat array)
// Size: TotalCells × MaxParticlesPerCell × sizeof(uint) = 32768 × 16 × 4 = 2 MB
RWStructuredBuffer<uint> CellData;  // Flat array: [cell0_particles...][cell1_particles...]

// Buffer 3: Grid parameters (constant buffer)
cbuffer SelfCollisionParams : register(b1)
{
    float3 GridMin;
    float CellSize;
    uint3 GridDimensions;
    uint MaxParticlesPerCell;
    float CollisionRadius;  // Particle radius (e.g., 1.0cm)
    float CollisionStiffness;  // Separation strength (0-1)
    uint bEnableSelfCollision;
    uint Padding;
};
```

**Memory Estimate** (5000 particles, 32³ grid):
- Cell counters: 128 KB
- Cell data: 2 MB
- **Total**: ~2.1 MB (acceptable for GPU)

---

## 3. Two-Pass GPU Algorithm

### 3.1 Pass 1: Build Spatial Hash Grid

**Shader**: [`ClothSelfCollisionBuildGrid.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionBuildGrid.hlsl)

```hlsl
// Input: Predicted positions (after constraint solving)
StructuredBuffer<FClothParticle> PredictedRead : register(t0);
StructuredBuffer<float> InvMass : register(t1);

// Output: Grid data structures
RWStructuredBuffer<uint> CellCounters : register(u0);
RWStructuredBuffer<uint> CellData : register(u1);

[numthreads(256, 1, 1)]
void BuildSpatialHashGridCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    if (particleIdx >= NumParticles) return;
    
    // Skip kinematic particles
    if (InvMass[particleIdx] == 0.0f) return;
    
    // Compute grid cell
    float3 pos = PredictedRead[particleIdx].Position;
    uint3 cell = GetGridCell(pos, GridMin, CellSize);
    uint cellHash = GetCellHash(cell);
    
    // Atomically increment cell counter and get slot
    uint slot;
    InterlockedAdd(CellCounters[cellHash], 1, slot);
    
    // Write particle index to cell data (if space available)
    if (slot < MaxParticlesPerCell)
    {
        uint writeIndex = cellHash * MaxParticlesPerCell + slot;
        CellData[writeIndex] = particleIdx;
    }
    // Note: Overflow particles are silently dropped (acceptable for performance)
}
```

**Dispatch**: `(NumParticles + 255) / 256` thread groups

### 3.2 Pass 2: Solve Self-Collisions

**Shader**: [`ClothSelfCollisionSolver.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSelfCollisionSolver.hlsl)

```hlsl
// Input: Grid data + predicted positions
StructuredBuffer<FClothParticle> PredictedRead : register(t0);
StructuredBuffer<float> InvMass : register(t1);
StructuredBuffer<uint> CellCounters : register(t2);
StructuredBuffer<uint> CellData : register(t3);
StructuredBuffer<uint> Indices : register(t4);  // For topology check

// Output: Position corrections (Jacobi accumulation)
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);

static const float kScale = 10000.0f;

[numthreads(256, 1, 1)]
void SolveSelfCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    if (particleIdx >= NumParticles) return;
    
    float invMassA = InvMass[particleIdx];
    if (invMassA == 0.0f) return;  // Skip kinematic
    
    float3 posA = PredictedRead[particleIdx].Position;
    uint3 cellA = GetGridCell(posA, GridMin, CellSize);
    
    // Query 3×3×3 neighborhood (27 cells)
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int3 neighborCell = int3(cellA) + int3(dx, dy, dz);
        
        // Bounds check
        if (any(neighborCell < 0) || any(neighborCell >= int3(GridDimensions)))
            continue;
        
        uint cellHash = GetCellHash(uint3(neighborCell));
        uint cellCount = min(CellCounters[cellHash], MaxParticlesPerCell);
        
        // Check all particles in this cell
        for (uint i = 0; i < cellCount; i++)
        {
            uint particleIdxB = CellData[cellHash * MaxParticlesPerCell + i];
            
            // Skip self
            if (particleIdxB == particleIdx) continue;
            
            // Skip if topologically adjacent (share an edge)
            if (AreTopologicallyAdjacent(particleIdx, particleIdxB))
                continue;
            
            float invMassB = InvMass[particleIdxB];
            float3 posB = PredictedRead[particleIdxB].Position;
            
            // Collision detection
            float3 diff = posB - posA;
            float dist = length(diff);
            float minDist = 2.0f * CollisionRadius;
            
            if (dist < minDist && dist > 1e-6f)
            {
                // Collision response (mass-weighted separation)
                float3 normal = diff / dist;
                float penetration = minDist - dist;
                
                float wSum = invMassA + invMassB;
                if (wSum < 1e-6f) continue;
                
                // Compute corrections
                float3 correction = normal * penetration * CollisionStiffness;
                float3 corrA = -correction * (invMassA / wSum);
                float3 corrB = +correction * (invMassB / wSum);
                
                // Atomic accumulation (scaled to int)
                int3 deltaA = int3(corrA * kScale);
                int3 deltaB = int3(corrB * kScale);
                
                InterlockedAdd(PositionDelta[particleIdx].x, deltaA.x);
                InterlockedAdd(PositionDelta[particleIdx].y, deltaA.y);
                InterlockedAdd(PositionDelta[particleIdx].z, deltaA.z);
                InterlockedAdd(PositionWeight[particleIdx], 1);
                
                InterlockedAdd(PositionDelta[particleIdxB].x, deltaB.x);
                InterlockedAdd(PositionDelta[particleIdxB].y, deltaB.y);
                InterlockedAdd(PositionDelta[particleIdxB].z, deltaB.z);
                InterlockedAdd(PositionWeight[particleIdxB], 1);
            }
        }
    }
}

// Helper: Check if two particles share an edge (topology check)
bool AreTopologicallyAdjacent(uint idxA, uint idxB)
{
    // Simple approach: Check if they appear in the same triangle
    // This requires scanning the index buffer (expensive but necessary)
    // Optimization: Pre-compute adjacency list on CPU and upload as buffer
    
    // For now, use a simplified check:
    // If |idxA - idxB| == 1, they're likely adjacent (works for grid-like meshes)
    // TODO: Replace with proper adjacency buffer lookup
    return abs(int(idxA) - int(idxB)) <= 1;
}
```

**Dispatch**: `(NumParticles + 255) / 256` thread groups

**Note**: After this pass, call existing [`DispatchApplyDeltas()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:1728) to apply accumulated corrections.

---

## 4. C++ Integration

### 4.1 New Data Structures

**File**: [`ClothGPUStructs.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h)

```cpp
/**
 * Self-collision grid parameters (GPU constant buffer)
 * Must match shader constant buffer layout
 */
struct FClothSelfCollisionParams
{
    FVector GridMin;           // 12 bytes - AABB min
    float CellSize;            // 4 bytes
    
    uint32 GridDimX;           // 4 bytes
    uint32 GridDimY;           // 4 bytes
    uint32 GridDimZ;           // 4 bytes
    uint32 MaxParticlesPerCell; // 4 bytes
    
    float CollisionRadius;     // 4 bytes - Particle radius
    float CollisionStiffness;  // 4 bytes - Separation strength
    uint32 bEnableSelfCollision; // 4 bytes
    uint32 Padding;            // 4 bytes
    // Total: 48 bytes (aligned)
};
```

**File**: [`ClothSimulationData.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:20-58)

```cpp
struct FClothConfig
{
    // ... existing fields ...
    
    // Self-collision parameters (NEW)
    bool bEnableSelfCollision = false;
    float SelfCollisionRadius = 1.0f;      // Particle radius (cm)
    float SelfCollisionStiffness = 0.8f;   // Separation strength (0-1)
    uint32 SelfCollisionGridDim = 32;      // Grid dimension (32^3 default)
    uint32 SelfCollisionMaxPerCell = 16;   // Max particles per cell
};
```

### 4.2 Buffer Allocation

**File**: [`ClothBatchedSolver.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h:176-241)

Add new member variables:

```cpp
class FClothBatchedSolver
{
private:
    // ... existing buffers ...
    
    // NEW: Self-collision buffers
    ID3D11Buffer* SelfCollisionCellCountersBuffer;
    ID3D11Buffer* SelfCollisionCellDataBuffer;
    ID3D11Buffer* SelfCollisionParamsBuffer;  // Constant buffer
    
    ID3D11UnorderedAccessView* SelfCollisionCellCountersUAV;
    ID3D11UnorderedAccessView* SelfCollisionCellDataUAV;
    
    ID3D11ShaderResourceView* SelfCollisionCellCountersSRV;
    ID3D11ShaderResourceView* SelfCollisionCellDataSRV;
    
    // NEW: Self-collision compute shaders
    ID3D11ComputeShader* SelfCollisionBuildGridCS;
    ID3D11ComputeShader* SelfCollisionSolverCS;
    
    // NEW: Self-collision state
    FClothSelfCollisionParams SelfCollisionParams;
    uint32 AllocatedSelfCollisionCells;
    bool bSelfCollisionInitialized;
};
```

**File**: [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:228-753)

Add buffer creation in [`AllocateBuffers()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:228):

```cpp
bool FClothBatchedSolver::AllocateBuffers(...)
{
    // ... existing buffer allocation ...
    
    // NEW: Allocate self-collision buffers (if enabled)
    if (Config.bEnableSelfCollision)
    {
        if (!AllocateSelfCollisionBuffers())
        {
            UE_LOG(ELogLevel::Warning, TEXT("Failed to allocate self-collision buffers"));
            Config.bEnableSelfCollision = false;
        }
    }
    
    return true;
}

bool FClothBatchedSolver::AllocateSelfCollisionBuffers()
{
    uint32 gridDim = Config.SelfCollisionGridDim;
    uint32 totalCells = gridDim * gridDim * gridDim;
    uint32 maxPerCell = Config.SelfCollisionMaxPerCell;
    
    AllocatedSelfCollisionCells = totalCells;
    
    // Create cell counters buffer
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(uint32) * totalCells;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(uint32);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    
    HRESULT hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &SelfCollisionCellCountersBuffer);
    if (FAILED(hr)) return false;
    
    // Create UAV and SRV for counters...
    
    // Create cell data buffer
    bufferDesc.ByteWidth = sizeof(uint32) * totalCells * maxPerCell;
    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &SelfCollisionCellDataBuffer);
    if (FAILED(hr)) return false;
    
    // Create UAV and SRV for cell data...
    
    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = (sizeof(FClothSelfCollisionParams) + 0xf) & 0xfffffff0;
    
    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &SelfCollisionParamsBuffer);
    if (FAILED(hr)) return false;
    
    bSelfCollisionInitialized = true;
    return true;
}
```

### 4.3 Shader Loading

**File**: [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:967-1109)

Add to [`LoadComputeShaders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:967):

```cpp
bool FClothBatchedSolver::LoadComputeShaders()
{
    // ... existing shader loading ...
    
    // Load self-collision shaders
    hr = ShaderManager->AddComputeShader(
        L"ClothSelfCollisionBuildGridCS",
        L"Shaders/Cloth/ClothSelfCollisionBuildGrid.hlsl",
        "BuildSpatialHashGridCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("Failed to compile self-collision build grid shader"));
    }
    SelfCollisionBuildGridCS = ShaderManager->GetComputeShaderByKey(L"ClothSelfCollisionBuildGridCS");
    
    hr = ShaderManager->AddComputeShader(
        L"ClothSelfCollisionSolverCS",
        L"Shaders/Cloth/ClothSelfCollisionSolver.hlsl",
        "SolveSelfCollisionsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("Failed to compile self-collision solver shader"));
    }
    SelfCollisionSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothSelfCollisionSolverCS");
    
    return bSuccess;
}
```

### 4.4 Simulation Loop Integration

**File**: [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:917-965)

Modify [`SimulateSubstep()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:917):

```cpp
void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    // 1. Integration
    DispatchIntegration(UsedParticleCount);
    
    // 2. External collision
    DispatchCollisionSDF(UsedParticleCount);
    DispatchEdgeCollisionSDF(UsedEdgeCollisionCount);
    
    // 3. Constraint iterations
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        if (UsedConstraintCount > 0)
            DispatchConstraintSolver(UsedConstraintCount);
        
        if (UsedBendConstraintCount > 0)
            DispatchBendConstraintSolver(UsedBendConstraintCount);
        
        if (UsedAreaConstraintCount > 0)
            DispatchAreaConstraintSolver(UsedAreaConstraintCount);
        
        DispatchApplyDeltas(UsedParticleCount);
    }
    
    // 4. NEW: Self-collision (after constraint solving)
    if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
    {
        DispatchSelfCollision(UsedParticleCount);
    }
    
    // 5. Kinematic targets
    if (ComputeKinematicTargetsCS && AttachmentDataSRV && UsedAttachmentCount > 0)
    {
        DispatchComputeKinematicTargets(UsedAttachmentCount);
    }
    
    // 6. Finalize
    DispatchFinalize(UsedParticleCount);
}
```

### 4.5 Dispatch Methods

**File**: [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

Add new dispatch methods:

```cpp
void FClothBatchedSolver::DispatchSelfCollision(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || ParticleCount == 0)
        return;
    
    if (!SelfCollisionBuildGridCS || !SelfCollisionSolverCS)
        return;
    
    // Update self-collision parameters
    UpdateSelfCollisionParams();
    
    // PASS 1: Build spatial hash grid
    {
        // Clear cell counters
        UINT clearValue[4] = {0, 0, 0, 0};
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(SelfCollisionCellCountersUAV, clearValue);
        
        // Bind shader
        Graphics->DeviceContext->CSSetShader(SelfCollisionBuildGridCS, nullptr, 0);
        
        // Bind constant buffers
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        // Bind SRVs
        ID3D11ShaderResourceView* srvs[] = {UnifiedPredictedSRV, UnifiedInvMassSRV};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
        
        // Bind UAVs
        ID3D11UnorderedAccessView* uavs[] = {SelfCollisionCellCountersUAV, SelfCollisionCellDataUAV};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        
        // Dispatch
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        // Unbind
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
    }
    
    // PASS 2: Solve self-collisions
    {
        // Bind shader
        Graphics->DeviceContext->CSSetShader(SelfCollisionSolverCS, nullptr, 0);
        
        // Bind constant buffers
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        // Bind SRVs
        ID3D11ShaderResourceView* srvs[] = {
            UnifiedPredictedSRV,
            UnifiedInvMassSRV,
            SelfCollisionCellCountersSRV,
            SelfCollisionCellDataSRV,
            UnifiedIndexSRV  // For topology check
        };
        Graphics->DeviceContext->CSSetShaderResources(0, 5, srvs);
        
        // Bind UAVs (reuse existing delta/weight buffers)
        ID3D11UnorderedAccessView* uavs[] = {
            UnifiedPositionDeltaUAV,
            UnifiedPositionWeightUAV
        };
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        
        // Dispatch
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        // Unbind
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 5, nullSRVs);
    }
    
    // PASS 3: Apply accumulated corrections (reuse existing method)
    DispatchApplyDeltas(ParticleCount);
}

void FClothBatchedSolver::UpdateSelfCollisionParams()
{
    if (!Graphics || !Graphics->DeviceContext || !SelfCollisionParamsBuffer)
        return;
    
    // Compute grid bounds from current particle positions
    // TODO: Compute AABB on CPU or GPU (for now, use fixed bounds)
    FVector gridMin = FVector(-500.0f, -500.0f, 0.0f);  // 5m × 5m × 5m
    
    // Compute cell size from average edge length
    float avgEdgeLength = 10.0f;  // TODO: Compute from mesh topology
    float cellSize = avgEdgeLength * 2.0f;
    
    // Fill params
    SelfCollisionParams.GridMin = gridMin;
    SelfCollisionParams.CellSize = cellSize;
    SelfCollisionParams.GridDimX = Config.SelfCollisionGridDim;
    SelfCollisionParams.GridDimY = Config.SelfCollisionGridDim;
    SelfCollisionParams.GridDimZ = Config.SelfCollisionGridDim;
    SelfCollisionParams.MaxParticlesPerCell = Config.SelfCollisionMaxPerCell;
    SelfCollisionParams.CollisionRadius = Config.SelfCollisionRadius;
    SelfCollisionParams.CollisionStiffness = Config.SelfCollisionStiffness;
    SelfCollisionParams.bEnableSelfCollision = Config.bEnableSelfCollision ? 1 : 0;
    
    // Upload to GPU
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(SelfCollisionParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &SelfCollisionParams, sizeof(FClothSelfCollisionParams));
        Graphics->DeviceContext->Unmap(SelfCollisionParamsBuffer, 0);
    }
}
```

---

## 5. Implementation Phases

### Phase 1: Foundation (Files & Structures)
**Estimated Complexity**: Low

1. **Add configuration parameters** to [`FClothConfig`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSimulationData.h:20)
   - `bEnableSelfCollision`
   - `SelfCollisionRadius`
   - `SelfCollisionStiffness`
   - `SelfCollisionGridDim`
   - `SelfCollisionMaxPerCell`

2. **Define GPU structures** in [`ClothGPUStructs.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h)
   - `FClothSelfCollisionParams`

3. **Add shader constant buffer** to [`ClothCommon.hlsli`](EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli)
   - Self-collision parameters in `cbuffer SelfCollisionParams : register(b1)`

### Phase 2: GPU Buffers (C++)
**Estimated Complexity**: Medium

4. **Add member variables** to [`ClothBatchedSolver.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h)
   - Buffer pointers, UAVs, SRVs
   - Compute shader pointers
   - State variables

5. **Implement buffer allocation** in [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)
   - `AllocateSelfCollisionBuffers()`
   - Call from `AllocateBuffers()`

6. **Implement buffer cleanup** in [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)
   - Add releases to `Release()` method

### Phase 3: Shader Implementation (HLSL)
**Estimated Complexity**: High

7. **Create Pass 1 shader**: `ClothSelfCollisionBuildGrid.hlsl`
   - Spatial hash computation
   - Atomic cell counter increment
   - Particle index storage

8. **Create Pass 2 shader**: `ClothSelfCollisionSolver.hlsl`
   - 3×3×3 neighborhood query
   - Distance checks
   - Topology filtering
   - Mass-weighted separation
   - Jacobi accumulation

9. **Add shader loading** to [`LoadComputeShaders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:967)

### Phase 4: Integration (C++)
**Estimated Complexity**: Medium

10. **Implement dispatch methods** in [`ClothBatchedSolver.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)
    - `DispatchSelfCollision()`
    - `UpdateSelfCollisionParams()`

11. **Integrate into simulation loop** in [`SimulateSubstep()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:917)
    - Call after constraint iterations
    - Before kinematic targets

### Phase 5: Optimization & Polish
**Estimated Complexity**: Medium

12. **Compute AABB dynamically**
    - GPU reduction pass or CPU-side computation
    - Update grid bounds per frame

13. **Compute average edge length**
    - One-time CPU computation during mesh setup
    - Store in cloth asset

14. **Implement topology adjacency buffer**
    - Pre-compute on CPU during mesh setup
    - Upload as GPU buffer for fast lookup
    - Replace simple distance check in shader

15. **Add console commands** (optional)
    - Toggle self-collision on/off
    - Adjust radius/stiffness at runtime
    - Visualize grid cells (debug)

---

## 6. Shader Pseudocode (Detailed)

### 6.1 ClothSelfCollisionBuildGrid.hlsl

```hlsl
#include "ClothCommon.hlsli"

// Self-collision parameters
cbuffer SelfCollisionParams : register(b1)
{
    float3 GridMin;
    float CellSize;
    uint3 GridDimensions;
    uint MaxParticlesPerCell;
    float CollisionRadius;
    float CollisionStiffness;
    uint bEnableSelfCollision;
    uint Padding;
};

// Input
StructuredBuffer<FClothParticle> PredictedRead : register(t0);
StructuredBuffer<float> InvMass : register(t1);

// Output
RWStructuredBuffer<uint> CellCounters : register(u0);
RWStructuredBuffer<uint> CellData : register(u1);

// Helper functions
uint3 GetGridCell(float3 position)
{
    float3 localPos = position - GridMin;
    uint3 cell = uint3(floor(localPos / CellSize));
    return clamp(cell, uint3(0,0,0), GridDimensions - uint3(1,1,1));
}

uint GetCellHash(uint3 cell)
{
    return cell.x + cell.y * GridDimensions.x + 
           cell.z * GridDimensions.x * GridDimensions.y;
}

[numthreads(256, 1, 1)]
void BuildSpatialHashGridCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    if (particleIdx >= NumParticles) return;
    
    // Skip kinematic particles
    float invMass = InvMass[particleIdx];
    if (invMass == 0.0f) return;
    
    // Compute grid cell
    float3 pos = PredictedRead[particleIdx].Position;
    uint3 cell = GetGridCell(pos);
    uint cellHash = GetCellHash(cell);
    
    // Atomically increment cell counter and get slot
    uint slot;
    InterlockedAdd(CellCounters[cellHash], 1, slot);
    
    // Write particle index to cell data (if space available)
    if (slot < MaxParticlesPerCell)
    {
        uint writeIndex = cellHash * MaxParticlesPerCell + slot;
        CellData[writeIndex] = particleIdx;
    }
    // Note: Overflow particles are silently dropped
}
```

### 6.2 ClothSelfCollisionSolver.hlsl

```hlsl
#include "ClothCommon.hlsli"

// Self-collision parameters
cbuffer SelfCollisionParams : register(b1)
{
    float3 GridMin;
    float CellSize;
    uint3 GridDimensions;
    uint MaxParticlesPerCell;
    float CollisionRadius;
    float CollisionStiffness;
    uint bEnableSelfCollision;
    uint Padding;
};

// Input
StructuredBuffer<FClothParticle> PredictedRead : register(t0);
StructuredBuffer<float> InvMass : register(t1);
StructuredBuffer<uint> CellCounters : register(t2);
StructuredBuffer<uint> CellData : register(t3);
Buffer<uint> Indices : register(t4);  // For topology check

// Output (Jacobi accumulation)
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);

static const float kScale = 10000.0f;
static const float EPSILON = 1e-6f;

// Helper functions
uint3 GetGridCell(float3 position)
{
    float3 localPos = position - GridMin;
    uint3 cell = uint3(floor(localPos / CellSize));
    return clamp(cell, uint3(0,0,0), GridDimensions - uint3(1,1,1));
}

uint GetCellHash(uint3 cell)
{
    return cell.x + cell.y * GridDimensions.x + 
           cell.z * GridDimensions.x * GridDimensions.y;
}

// Check if two particles share an edge (topology check)
bool AreTopologicallyAdjacent(uint idxA, uint idxB)
{
    // Simple heuristic: particles within 1 index are likely adjacent
    // TODO: Replace with proper adjacency buffer lookup
    return abs(int(idxA) - int(idxB)) <= 1;
}

[numthreads(256, 1, 1)]
void SolveSelfCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIdx = DTid.x;
    if (particleIdx >= NumParticles) return;
    
    float invMassA = InvMass[particleIdx];
    if (invMassA == 0.0f) return;  // Skip kinematic
    
    float3 posA = PredictedRead[particleIdx].Position;
    uint3 cellA = GetGridCell(posA);
    
    // Query 3×3×3 neighborhood (27 cells)
    for (int dz = -1; dz <= 1; dz++)
    {
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int3 neighborCell = int3(cellA) + int3(dx, dy, dz);
                
                // Bounds check
                if (any(neighborCell < int3(0,0,0)) || 
                    any(neighborCell >= int3(GridDimensions)))
                    continue;
                
                uint cellHash = GetCellHash(uint3(neighborCell));
                uint cellCount = min(CellCounters[cellHash], MaxParticlesPerCell);
                
                // Check all particles in this cell
                for (uint i = 0; i < cellCount; i++)
                {
                    uint particleIdxB = CellData[cellHash * MaxParticlesPerCell + i];
                    
                    // Skip self
                    if (particleIdxB == particleIdx) continue;
                    
                    // Skip if topologically adjacent
                    if (AreTopologicallyAdjacent(particleIdx, particleIdxB))
                        continue;
                    
                    float invMassB = InvMass[particleIdxB];
                    float3 posB = PredictedRead[particleIdxB].Position;
                    
                    // Collision detection
                    float3 diff = posB - posA;
                    float dist = length(diff);
                    float minDist = 2.0f * CollisionRadius;
                    
                    if (dist < minDist && dist > EPSILON)
                    {
                        // Collision response (mass-weighted separation)
                        float3 normal = diff / dist;
                        float penetration = minDist - dist;
                        
                        float wSum = invMassA + invMassB;
                        if (wSum < EPSILON) continue;
                        
                        // Compute corrections with stiffness
                        float3 correction = normal * penetration * CollisionStiffness;
                        float3 corrA = -correction * (invMassA / wSum);
                        float3 corrB = +correction * (invMassB / wSum);
                        
                        // Atomic accumulation (scaled to int)
                        int3 deltaA = int3(corrA * kScale);
                        int3 deltaB = int3(corrB * kScale);
                        
                        InterlockedAdd(PositionDelta[particleIdx].x, deltaA.x);
                        InterlockedAdd(PositionDelta[particleIdx].y, deltaA.y);
                        InterlockedAdd(PositionDelta[particleIdx].z, deltaA.z);
                        InterlockedAdd(PositionWeight[particleIdx], 1);
                        
                        InterlockedAdd(PositionDelta[particleIdxB].x, deltaB.x);
                        InterlockedAdd(PositionDelta[particleIdxB].y, deltaB.y);
                        InterlockedAdd(PositionDelta[particleIdxB].z, deltaB.z);
                        InterlockedAdd(PositionWeight[particleIdxB], 1);
                    }
                }
            }
        }
    }
}
```

---

## 7. Performance Considerations

### 7.1 Expected Performance

**Target**: <1ms for 5,000 particles

**Breakdown**:
- Pass 1 (Build Grid): ~0.2ms (5000 particles, simple hash + atomic)
- Pass 2 (Solve Collisions): ~0.6ms (5000 particles × 27 cells × ~2 particles/cell)
- Pass 3 (Apply Deltas): ~0.1ms (reused from existing system)
- **Total**: ~0.9ms ✓

### 7.2 Optimization Strategies

1. **Grid Sizing**
   - Cell size = 2× average edge length (optimal for cloth)
   - 32³ grid balances memory vs. spatial resolution
   - Adjust grid dimensions based on cloth size

2. **Cell Capacity**
   - 16 particles/cell handles dense regions
   - Overflow particles are dropped (acceptable trade-off)
   - Monitor overflow rate, increase if >5%

3. **Topology Filtering**
   - Pre-compute adjacency list on CPU
   - Upload as compact buffer (e.g., bitset or index list)
   - Fast GPU lookup avoids false collisions

4. **Early Outs**
   - Skip kinematic particles (invMass == 0)
   - Skip empty cells (cellCount == 0)
   - Skip distant particles (AABB culling)

5. **Thread Occupancy**
   - 256 threads/group maximizes GPU utilization
   - Minimize register pressure (keep local variables low)
   - Coalesce memory access (sequential particle IDs)

### 7.3 Memory Usage

**Per-Instance** (5000 particles, 32³ grid):
- Cell counters: 128 KB
- Cell data: 2 MB
- Constant buffer: <1 KB
- **Total**: ~2.1 MB

**Scalability**:
- 10,000 particles: ~2.1 MB (same grid)
- 20,000 particles: ~2.1 MB (same grid)
- Memory cost is **grid-dependent**, not particle-dependent

---

## 8. Testing & Validation

### 8.1 Unit Tests

1. **Grid Hash Function**
   - Verify cell computation for known positions
   - Test boundary conditions (grid edges)
   - Validate hash uniqueness

2. **Collision Detection**
   - Test particle pairs at various distances
   - Verify mass-weighted separation
   - Check kinematic particle skipping

3. **Topology Filtering**
   - Verify adjacent particles are skipped
   - Test with grid-like and irregular meshes

### 8.2 Integration Tests

1. **Single Cloth Instance**
   - Drop cloth on itself (should not penetrate)
   - Twist cloth (should resist self-intersection)
   - Compare with/without self-collision

2. **Multiple Instances**
   - Verify per-instance isolation (no cross-instance collisions)
   - Test batched simulation with mixed settings

3. **Performance Profiling**
   - Measure GPU time for each pass
   - Verify <1ms target for 5000 particles
   - Profile with varying particle counts (1K, 5K, 10K)

### 8.3 Visual Validation

1. **Debug Visualization** (optional)
   - Render grid cells as wireframe boxes
   - Color particles by cell occupancy
   - Highlight collision pairs

2. **Stress Tests**
   - High-density cloth (10K+ particles)
   - Rapid motion (high velocities)
   - Complex folding scenarios

---

## 9. Future Enhancements

### 9.1 Short-Term (Post-MVP)

1. **Dynamic AABB Computation**
   - GPU reduction pass to compute bounds
   - Update grid origin/size per frame
   - Improves spatial efficiency

2. **Proper Topology Adjacency**
   - Pre-compute adjacency list on CPU
   - Upload as compact GPU buffer
   - Replace heuristic check in shader

3. **Adaptive Grid Sizing**
   - Adjust cell size based on cloth deformation
   - Use multiple grid resolutions (hierarchical)

### 9.2 Long-Term (Advanced Features)

1. **Friction Support**
   - Tangential velocity damping
   - Static/dynamic friction coefficients
   - Requires velocity buffer access

2. **Thickness Variation**
   - Per-particle radius (vertex painting)
   - Adaptive collision radius based on curvature

3. **Continuous Collision Detection (CCD)**
   - Sweep-based collision for fast-moving particles
   - Prevents tunneling at high velocities

4. **Multi-Layer Cloth**
   - Separate collision layers (e.g., shirt vs. pants)
   - Layer-specific collision rules

---

## 10. File Structure Summary

### New Files to Create

```
EngineSIU/EngineSIU/Shaders/Cloth/
├── ClothSelfCollisionBuildGrid.hlsl    (NEW - Pass 1 shader)
└── ClothSelfCollisionSolver.hlsl       (NEW - Pass 2 shader)
```

### Files to Modify

```
EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/
├── ClothSimulationData.h               (Add config parameters)
├── ClothGPUStructs.h                   (Add FClothSelfCollisionParams)
├── ClothBatchedSolver.h                (Add buffers, shaders, methods)
└── ClothBatchedSolver.cpp              (Implement allocation, dispatch, integration)

EngineSIU/EngineSIU/Shaders/Cloth/
└── ClothCommon.hlsli                   (Add self-collision constant buffer)
```

---

## 11. Implementation Checklist

### Phase 1: Foundation
- [ ] Add `bEnableSelfCollision` to `FClothConfig`
- [ ] Add `SelfCollisionRadius` to `FClothConfig`
- [ ] Add `SelfCollisionStiffness` to `FClothConfig`
- [ ] Add `SelfCollisionGridDim` to `FClothConfig`
- [ ] Add `SelfCollisionMaxPerCell` to `FClothConfig`
- [ ] Define `FClothSelfCollisionParams` in `ClothGPUStructs.h`
- [ ] Add self-collision constant buffer to `ClothCommon.hlsli`

### Phase 2: GPU Buffers
- [ ] Add buffer member variables to `ClothBatchedSolver.h`
- [ ] Add shader member variables to `ClothBatchedSolver.h`
- [ ] Implement `AllocateSelfCollisionBuffers()` in `ClothBatchedSolver.cpp`
- [ ] Call `AllocateSelfCollisionBuffers()` from `AllocateBuffers()`
- [ ] Add buffer releases to `Release()` method

### Phase 3: Shaders
- [ ] Create `ClothSelfCollisionBuildGrid.hlsl`
- [ ] Implement spatial hash computation
- [ ] Implement atomic cell counter increment
- [ ] Create `ClothSelfCollisionSolver.hlsl`
- [ ] Implement 3×3×3 neighborhood query
- [ ] Implement collision detection and response
- [ ] Implement topology filtering
- [ ] Add shader loading to `LoadComputeShaders()`

### Phase 4: Integration
- [ ] Implement `DispatchSelfCollision()` in `ClothBatchedSolver.cpp`
- [ ] Implement `UpdateSelfCollisionParams()` in `ClothBatchedSolver.cpp`
- [ ] Integrate into `SimulateSubstep()` after constraint iterations
- [ ] Test with single cloth instance
- [ ] Test with multiple cloth instances

### Phase 5: Optimization
- [ ] Implement dynamic AABB computation
- [ ] Compute average edge length from mesh
- [ ] Implement topology adjacency buffer
- [ ] Add console commands for runtime control
- [ ] Profile and optimize performance

---

## 12. Risk Assessment

### High Risk
- **Performance**: May exceed 1ms target for dense cloth
  - **Mitigation**: Profile early, optimize grid parameters, add LOD system

### Medium Risk
- **False Positives**: Topology filtering may miss some adjacent pairs
  - **Mitigation**: Implement proper adjacency buffer lookup

- **Grid Overflow**: Cells may exceed capacity in dense regions
  - **Mitigation**: Monitor overflow rate, increase capacity or cell size

### Low Risk
- **Integration Issues**: May conflict with existing constraints
  - **Mitigation**: Follow existing Jacobi accumulation pattern

- **Memory Usage**: 2MB per instance may be excessive for many instances
  - **Mitigation**: Share grid across instances (requires careful synchronization)

---

## 13. Success Criteria

### Functional Requirements
✓ No visible cloth self-penetration  
✓ Works with existing XPBD constraints  
✓ Enable/disable toggle functional  
✓ Stable across varying time steps  

### Performance Requirements
✓ <1ms GPU time for 5,000 particles  
✓ Scales linearly with particle count  
✓ No frame drops in typical scenarios  

### Code Quality Requirements
✓ Follows existing codebase patterns  
✓ Properly documented (comments + this plan)  
✓ No memory leaks or resource leaks  
✓ Passes integration tests  

---

## Conclusion

This implementation plan provides a comprehensive roadmap for adding GPU-based self-collision detection to the cloth simulation system. The spatial hash grid approach balances performance and accuracy, targeting <1ms for 5,000 particles while preventing visible interpenetration.

The design follows existing codebase patterns (Jacobi accumulation, unified buffers, two-pass shaders) and integrates cleanly into the simulation loop. The phased implementation approach allows for incremental development and testing.

**Next Steps**: Begin with Phase 1 (Foundation) to establish data structures and configuration parameters, then proceed through shader implementation and integration.
