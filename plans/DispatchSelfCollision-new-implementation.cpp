// NEW IMPLEMENTATION OF DispatchSelfCollision()
// Replace the entire function at line 2531 in ClothBatchedSolver.cpp
// This implements the 7-pass improved self-collision pipeline

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
        HRESULT hr = Graphics->Device->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
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
