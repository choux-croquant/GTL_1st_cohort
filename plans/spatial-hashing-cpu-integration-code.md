# Spatial Hashing CPU Integration Code Snippets
**File:** Complete code snippets for CPU-side integration  
**Date:** 2026-02-13

---

## Part 1: Constructor Initialization

### File: ClothBatchedSolver.cpp (Constructor - add after line 100)

```cpp
// NEW: Improved self-collision buffers (P0) - Initialize to nullptr
SelfCollisionParticleHashesBuffer = nullptr;
SelfCollisionCellCountsBuffer = nullptr;
SelfCollisionCellPrefixSumBuffer = nullptr;
SelfCollisionSortedIndicesBuffer = nullptr;
SelfCollisionCellStartsBuffer = nullptr;
SelfCollisionCellEndsBuffer = nullptr;
SelfCollisionNeighborListsBuffer = nullptr;
SelfCollisionNeighborCountsBuffer = nullptr;
SelfCollisionNeighborLambdasBuffer = nullptr;
SelfCollisionAdjacencyBuffer = nullptr;
SelfCollisionCollisionMasksBuffer = nullptr;

SelfCollisionParticleHashesUAV = nullptr;
SelfCollisionParticleHashesSRV = nullptr;
SelfCollisionCellCountsUAV = nullptr;
SelfCollisionCellCountsSRV = nullptr;
SelfCollisionCellPrefixSumSRV = nullptr;
SelfCollisionSortedIndicesUAV = nullptr;
SelfCollisionSortedIndicesSRV = nullptr;
SelfCollisionCellStartsUAV = nullptr;
SelfCollisionCellStartsSRV = nullptr;
SelfCollisionCellEndsUAV = nullptr;
SelfCollisionCellEndsSRV = nullptr;
SelfCollisionNeighborListsUAV = nullptr;
SelfCollisionNeighborListsSRV = nullptr;
SelfCollisionNeighborCountsUAV = nullptr;
SelfCollisionNeighborCountsSRV = nullptr;
SelfCollisionNeighborLambdasUAV = nullptr;
SelfCollisionNeighborLambdasSRV = nullptr;
SelfCollisionAdjacencySRV = nullptr;
SelfCollisionCollisionMasksSRV = nullptr;

// NEW: Improved self-collision shaders (P0)
SelfCollisionHashCS = nullptr;
SelfCollisionCountingSortCS = nullptr;
SelfCollisionReorderCS = nullptr;
SelfCollisionBuildCellsCS = nullptr;
SelfCollisionBuildNeighborsCS = nullptr;
SelfCollisionSolverXPBDCS = nullptr;

AllocatedSelfCollisionMaxNeighbors = 0;
```

---

## Part 2: Release() Method

### File: ClothBatchedSolver.cpp (Release method - add before existing self-collision releases)

```cpp
// NEW: Release improved self-collision buffers (P0)
SAFE_RELEASE(SelfCollisionParticleHashesBuffer);
SAFE_RELEASE(SelfCollisionCellCountsBuffer);
SAFE_RELEASE(SelfCollisionCellPrefixSumBuffer);
SAFE_RELEASE(SelfCollisionSortedIndicesBuffer);
SAFE_RELEASE(SelfCollisionCellStartsBuffer);
SAFE_RELEASE(SelfCollisionCellEndsBuffer);
SAFE_RELEASE(SelfCollisionNeighborListsBuffer);
SAFE_RELEASE(SelfCollisionNeighborCountsBuffer);
SAFE_RELEASE(SelfCollisionNeighborLambdasBuffer);
SAFE_RELEASE(SelfCollisionAdjacencyBuffer);
SAFE_RELEASE(SelfCollisionCollisionMasksBuffer);

SAFE_RELEASE(SelfCollisionParticleHashesUAV);
SAFE_RELEASE(SelfCollisionParticleHashesSRV);
SAFE_RELEASE(SelfCollisionCellCountsUAV);
SAFE_RELEASE(SelfCollisionCellCountsSRV);
SAFE_RELEASE(SelfCollisionCellPrefixSumSRV);
SAFE_RELEASE(SelfCollisionSortedIndicesUAV);
SAFE_RELEASE(SelfCollisionSortedIndicesSRV);
SAFE_RELEASE(SelfCollisionCellStartsUAV);
SAFE_RELEASE(SelfCollisionCellStartsSRV);
SAFE_RELEASE(SelfCollisionCellEndsUAV);
SAFE_RELEASE(SelfCollisionCellEndsSRV);
SAFE_RELEASE(SelfCollisionNeighborListsUAV);
SAFE_RELEASE(SelfCollisionNeighborListsSRV);
SAFE_RELEASE(SelfCollisionNeighborCountsUAV);
SAFE_RELEASE(SelfCollisionNeighborCountsSRV);
SAFE_RELEASE(SelfCollisionNeighborLambdasUAV);
SAFE_RELEASE(SelfCollisionNeighborLambdasSRV);
SAFE_RELEASE(SelfCollisionAdjacencySRV);
SAFE_RELEASE(SelfCollisionCollisionMasksSRV);

SAFE_RELEASE(SelfCollisionHashCS);
SAFE_RELEASE(SelfCollisionCountingSortCS);
SAFE_RELEASE(SelfCollisionReorderCS);
SAFE_RELEASE(SelfCollisionBuildCellsCS);
SAFE_RELEASE(SelfCollisionBuildNeighborsCS);
SAFE_RELEASE(SelfCollisionSolverXPBDCS);
```

---

## Part 3: LoadComputeShaders() Method

### File: ClothBatchedSolver.cpp (LoadComputeShaders - add after existing self-collision shader loading)

```cpp
// Load improved self-collision shaders (P0)
SelfCollisionHashCS = ShaderManager->LoadComputeShader(L"Shaders/Cloth/ClothSelfCollisionHash.hlsl", "HashParticlesCS");
if (!SelfCollisionHashCS)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load ClothSelfCollisionHash.hlsl"));
    return false;
}

SelfCollisionCountingSortCS = ShaderManager->LoadComputeShader(L"Shaders/Cloth/ClothSelfCollisionCountingSort.hlsl", "CountingSortHistogramCS");
if (!SelfCollisionCountingSortCS)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load ClothSelfCollisionCountingSort.hlsl"));
    return false;
}

SelfCollisionReorderCS = ShaderManager->LoadComputeShader(L"Shaders/Cloth/ClothSelfCollisionReorder.hlsl", "ReorderParticlesCS");
if (!SelfCollisionReorderCS)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load ClothSelfCollisionReorder.hlsl"));
    return false;
}

SelfCollisionBuildCellsCS = ShaderManager->LoadComputeShader(L"Shaders/Cloth/ClothSelfCollisionBuildCells.hlsl", "BuildCellRangesCS");
if (!SelfCollisionBuildCellsCS)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load ClothSelfCollisionBuildCells.hlsl"));
    return false;
}

SelfCollisionBuildNeighborsCS = ShaderManager->LoadComputeShader(L"Shaders/Cloth/ClothSelfCollisionBuildNeighbors.hlsl", "BuildNeighborListsCS");
if (!SelfCollisionBuildNeighborsCS)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load ClothSelfCollisionBuildNeighbors.hlsl"));
    return false;
}

SelfCollisionSolverXPBDCS = ShaderManager->LoadComputeShader(L"Shaders/Cloth/ClothSelfCollisionSolverXPBD.hlsl", "SolveCollisionsXPBDCS");
if (!SelfCollisionSolverXPBDCS)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load ClothSelfCollisionSolverXPBD.hlsl"));
    return false;
}

UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Loaded improved self-collision shaders (P0)"));
```

---

## Part 4: Complete AllocateSelfCollisionBuffers() Replacement

### File: ClothBatchedSolver.cpp (Replace entire function at line 950)

```cpp
bool FClothBatchedSolver::AllocateSelfCollisionBuffers()
{
    if (!Graphics || !Graphics->Device)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Invalid graphics device for self-collision"));
        return false;
    }

    uint32 gridDim = Config.SelfCollisionGridDim;
    uint32 totalCells = gridDim * gridDim * gridDim;
    uint32 maxPerCell = Config.SelfCollisionMaxPerCell;
    uint32 maxNeighbors = 16;  // P0 default: 16 neighbors per particle
    uint32 maxParticles = AllocatedParticleCapacity;

    AllocatedSelfCollisionCells = totalCells;
    AllocatedSelfCollisionMaxNeighbors = maxNeighbors;

    HRESULT hr;
    D3D11_BUFFER_DESC bufferDesc = {};
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    // Helper lambda for creating structured buffers
    auto CreateStructuredBuffer = [&](uint32 elementCount, uint32 elementSize, 
                                      ID3D11Buffer** outBuffer,
                                      ID3D11UnorderedAccessView** outUAV,
                                      ID3D11ShaderResourceView** outSRV,
                                      const wchar_t* name) -> bool
    {
        bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = elementSize * elementCount;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = elementSize;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, outBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create %s buffer"), name);
            return false;
        }

        if (outUAV)
        {
            uavDesc = {};
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.Buffer.NumElements = elementCount;

            hr = Graphics->Device->CreateUnorderedAccessView(*outBuffer, &uavDesc, outUAV);
            if (FAILED(hr))
            {
                UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create %s UAV"), name);
                return false;
            }
        }

        if (outSRV)
        {
            srvDesc = {};
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.NumElements = elementCount;

            hr = Graphics->Device->CreateShaderResourceView(*outBuffer, &srvDesc, outSRV);
            if (FAILED(hr))
            {
                UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create %s SRV"), name);
                return false;
            }
        }

        return true;
    };

    // Create all new buffers
    if (!CreateStructuredBuffer(maxParticles, sizeof(uint32), 
        &SelfCollisionParticleHashesBuffer, &SelfCollisionParticleHashesUAV, &SelfCollisionParticleHashesSRV, L"ParticleHashes"))
        return false;

    if (!CreateStructuredBuffer(totalCells, sizeof(uint32), 
        &SelfCollisionCellCountsBuffer, &SelfCollisionCellCountsUAV, &SelfCollisionCellCountsSRV, L"CellCounts"))
        return false;

    if (!CreateStructuredBuffer(totalCells, sizeof(uint32), 
        &SelfCollisionCellPrefixSumBuffer, nullptr, &SelfCollisionCellPrefixSumSRV, L"CellPrefixSum"))
        return false;

    if (!CreateStructuredBuffer(maxParticles, sizeof(uint32), 
        &SelfCollisionSortedIndicesBuffer, &SelfCollisionSortedIndicesUAV, &SelfCollisionSortedIndicesSRV, L"SortedIndices"))
        return false;

    if (!CreateStructuredBuffer(totalCells, sizeof(uint32), 
        &SelfCollisionCellStartsBuffer, &SelfCollisionCellStartsUAV, &SelfCollisionCellStartsSRV, L"CellStarts"))
        return false;

    if (!CreateStructuredBuffer(totalCells, sizeof(uint32), 
        &SelfCollisionCellEndsBuffer, &SelfCollisionCellEndsUAV, &SelfCollisionCellEndsSRV, L"CellEnds"))
        return false;

    if (!CreateStructuredBuffer(maxParticles * maxNeighbors, sizeof(uint32), 
        &SelfCollisionNeighborListsBuffer, &SelfCollisionNeighborListsUAV, &SelfCollisionNeighborListsSRV, L"NeighborLists"))
        return false;

    if (!CreateStructuredBuffer(maxParticles, sizeof(uint32), 
        &SelfCollisionNeighborCountsBuffer, &SelfCollisionNeighborCountsUAV, &SelfCollisionNeighborCountsSRV, L"NeighborCounts"))
        return false;

    if (!CreateStructuredBuffer(maxParticles * maxNeighbors, sizeof(float), 
        &SelfCollisionNeighborLambdasBuffer, &SelfCollisionNeighborLambdasUAV, &SelfCollisionNeighborLambdasSRV, L"NeighborLambdas"))
        return false;

    // Adjacency and collision masks buffers (will be populated later)
    if (!CreateStructuredBuffer(maxParticles, sizeof(FClothAdjacencyGPU), 
        &SelfCollisionAdjacencyBuffer, nullptr, &SelfCollisionAdjacencySRV, L"Adjacency"))
        return false;

    if (!CreateStructuredBuffer(maxParticles, sizeof(FClothCollisionMaskGPU), 
        &SelfCollisionCollisionMasksBuffer, nullptr, &SelfCollisionCollisionMasksSRV, L"CollisionMasks"))
        return false;

    // Keep existing constant buffer creation
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = (sizeof(FClothSelfCollisionParams) + 0xf) & 0xfffffff0; // 16-byte aligned

    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &SelfCollisionParamsBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision params buffer"));
        return false;
    }

    // Allocate CPU-side prefix sum buffer (P0 fallback)
    SelfCollisionCellPrefixSumCPU.SetNum(totalCells);

    bSelfCollisionInitialized = true;

    float memoryMB = (
        maxParticles * sizeof(uint32) +                    // Hashes
        totalCells * sizeof(uint32) * 4 +                  // Counts, PrefixSum, Starts, Ends
        maxParticles * sizeof(uint32) +                    // SortedIndices
        maxParticles * maxNeighbors * sizeof(uint32) +     // NeighborLists
        maxParticles * sizeof(uint32) +                    // NeighborCounts
        maxParticles * maxNeighbors * sizeof(float) +      // NeighborLambdas
        maxParticles * sizeof(FClothAdjacencyGPU) +        // Adjacency
        maxParticles * sizeof(FClothCollisionMaskGPU)      // CollisionMasks
    ) / (1024.0f * 1024.0f);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Allocated improved self-collision buffers - Grid: %ux%ux%u (%u cells), MaxNeighbors: %u, Memory: ~%.2f MB"),
           gridDim, gridDim, gridDim, totalCells, maxNeighbors, memoryMB);

    return true;
}
```

---

## Part 5: Complete DispatchSelfCollision() Replacement

### File: ClothBatchedSolver.cpp (Replace entire function at line 2353)

```cpp
void FClothBatchedSolver::DispatchSelfCollision(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || ParticleCount == 0)
        return;
    
    if (!bSelfCollisionInitialized)
        return;
    
    // Check if we have the new shaders loaded
    if (!SelfCollisionHashCS || !SelfCollisionCountingSortCS || !SelfCollisionReorderCS ||
        !SelfCollisionBuildCellsCS || !SelfCollisionBuildNeighborsCS || !SelfCollisionSolverXPBDCS)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Improved self-collision shaders not loaded, skipping"));
        return;
    }
    
    // NOTE: UpdateSelfCollisionParams() called once per frame in Simulate()
    
    // PASS 1: Hash Computation
    {
        Graphics->DeviceContext->CSSetShader(SelfCollisionHashCS, nullptr, 0);
        
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        ID3D11ShaderResourceView* srvs[] = {UnifiedPredictedSRV, UnifiedInvMassSRV};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
        
        ID3D11UnorderedAccessView* uavs[] = {SelfCollisionParticleHashesUAV};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
    }
    
    // PASS 2: Counting Sort Histogram
    {
        UINT clearValue[4] = {0, 0, 0, 0};
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(SelfCollisionCellCountsUAV, clearValue);
        
        Graphics->DeviceContext->CSSetShader(SelfCollisionCountingSortCS, nullptr, 0);
        
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        ID3D11ShaderResourceView* srvs[] = {SelfCollisionParticleHashesSRV};
        Graphics->DeviceContext->CSSetShaderResources(0, 1, srvs);
        
        ID3D11UnorderedAccessView* uavs[] = {SelfCollisionCellCountsUAV};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    }
    
    // PASS 2b: Prefix Sum (CPU fallback for P0)
    {
        // Create staging buffer for readback
        D3D11_BUFFER_DESC stagingDesc = {};
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.ByteWidth = sizeof(uint32) * AllocatedSelfCollisionCells;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        
        ID3D11Buffer* stagingBuffer = nullptr;
        hr = Graphics->Device->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
        if (SUCCEEDED(hr))
        {
            // Copy cell counts to staging buffer
            Graphics->DeviceContext->CopyResource(stagingBuffer, SelfCollisionCellCountsBuffer);
            
            // Read back cell counts
            D3D11_MAPPED_SUBRESOURCE mapped;
            hr = Graphics->DeviceContext->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(hr))
            {
                uint32* cellCounts = (uint32*)mapped.pData;
                
                // Compute exclusive prefix sum on CPU
                uint32 sum = 0;
                for (uint32 i = 0; i < AllocatedSelfCollisionCells; ++i)
                {
                    SelfCollisionCellPrefixSumCPU[i] = sum;
                    sum += cellCounts[i];
                }
                
                Graphics->DeviceContext->Unmap(stagingBuffer, 0);
                
                // Upload prefix sum back to GPU
                Graphics->DeviceContext->UpdateSubresource(SelfCollisionCellPrefixSumBuffer, 0, nullptr, 
                    SelfCollisionCellPrefixSumCPU.GetData(), 0, 0);
            }
            
            stagingBuffer->Release();
        }
    }
    
    // PASS 3: Particle Reordering
    {
        // Reset cell write offsets (reuse CellCounts buffer)
        UINT clearValue[4] = {0, 0, 0, 0};
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(SelfCollisionCellCountsUAV, clearValue);
        
        Graphics->DeviceContext->CSSetShader(SelfCollisionReorderCS, nullptr, 0);
        
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        ID3D11ShaderResourceView* srvs[] = {SelfCollisionParticleHashesSRV, SelfCollisionCellPrefixSumSRV};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
        
        ID3D11UnorderedAccessView* uavs[] = {SelfCollisionSortedIndicesUAV, SelfCollisionCellCountsUAV};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
    }
    
    // PASS 4: Build Cell Ranges
    {
        // Initialize cell starts to 0xFFFFFFFF
        UINT clearValue[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(SelfCollisionCellStartsUAV, clearValue);
        
        Graphics->DeviceContext->CSSetShader(SelfCollisionBuildCellsCS, nullptr, 0);
        
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        ID3D11ShaderResourceView* srvs[] = {SelfCollisionParticleHashesSRV, SelfCollisionSortedIndicesSRV};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
        
        ID3D11UnorderedAccessView* uavs[] = {SelfCollisionCellStartsUAV, SelfCollisionCellEndsUAV};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
    }
    
    // PASS 5: Build Neighbor Lists
    {
        Graphics->DeviceContext->CSSetShader(SelfCollisionBuildNeighborsCS, nullptr, 0);
        
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        ID3D11ShaderResourceView* srvs[] = {
            UnifiedPredictedSRV,
            SelfCollisionSortedIndicesSRV,
            SelfCollisionCellStartsSRV,
            SelfCollisionCellEndsSRV,
            SelfCollisionAdjacencySRV,
            SelfCollisionCollisionMasksSRV
        };
        Graphics->DeviceContext->CSSetShaderResources(0, 6, srvs);
        
        ID3D11UnorderedAccessView* uavs[] = {SelfCollisionNeighborListsUAV, SelfCollisionNeighborCountsUAV};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 6, nullSRVs);
    }
    
    // PASS 6: XPBD Collision Solver
    {
        Graphics->DeviceContext->CSSetShader(SelfCollisionSolverXPBDCS, nullptr, 0);
        
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        ID3D11ShaderResourceView* srvs[] = {
            UnifiedPredictedSRV,
            UnifiedPositionSRV,  // Previous positions for friction
            UnifiedInvMassSRV,
            SelfCollisionNeighborListsSRV,
            SelfCollisionNeighborCountsSRV
        };
        Graphics->DeviceContext->CSSetShaderResources(0, 5, srvs);
        
        ID3D11UnorderedAccessView* uavs[] = {
            UnifiedPositionDeltaUAV,
            UnifiedPositionWeightUAV,
            SelfCollisionNeighborLambdasUAV
        };
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
        
        uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
        
        ID3D11UnorderedAccessView* nullUAVs[] = {nullptr, nullptr, nullptr};
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 5, nullSRVs);
    }
    
    // PASS 7: Apply Deltas (called separately in main simulation loop)
}
```

---

## Part 6: Update UpdateSelfCollisionParams()

### File: ClothBatchedSolver.cpp (Find UpdateSelfCollisionParams and add new fields)

**Add these lines when populating the params structure:**

```cpp
params.CollisionFriction = 0.3f;  // Default friction coefficient (can be made configurable)
params.MaxNeighbors = AllocatedSelfCollisionMaxNeighbors;
params.Compliance = 0.0001f;  // XPBD compliance (can be made configurable)
params.CurrentIteration = 0;  // Updated per iteration if needed
params.bEnableInterInstanceCollision = 1;  // Enable by default
params.bEnableIntraInstanceCollision = 1;  // Enable by default
params.Padding0 = 0;
params.Padding1 = 0;
params.Padding2 = 0;
```

---

## Summary of Changes

### Files to Modify:
1. ✅ `ClothBatchedSolver.h` - Already updated with new declarations
2. ⏳ `ClothBatchedSolver.cpp` - Need to update:
   - Constructor initialization
   - Release() method
   - LoadComputeShaders() method
   - AllocateSelfCollisionBuffers() method
   - DispatchSelfCollision() method
   - UpdateSelfCollisionParams() method

### Integration Steps:
1. Add constructor initializations (Part 1)
2. Add Release() cleanup (Part 2)
3. Add shader loading (Part 3)
4. Replace AllocateSelfCollisionBuffers() (Part 4)
5. Replace DispatchSelfCollision() (Part 5)
6. Update UpdateSelfCollisionParams() (Part 6)

### Testing After Integration:
1. Compile and verify no errors
2. Run with self-collision disabled - should work as before
3. Enable self-collision - verify no crashes
4. Visual test with cloth dropping on sphere
5. Performance test - measure GPU time

---

**Note:** The CPU prefix sum in Pass 2b is a temporary P0 solution. It adds ~0.5ms overhead but avoids implementing GPU parallel scan. Phase 1 will replace this with a GPU-based scan for better performance.
