/**
 * Cloth Batched Solver Implementation
 * GPU-based batched cloth simulation for multiple instances using unified buffers
 */

#include "ClothBatchedSolver.h"
#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"
#include "Engine/UserInterface/Console.h"
#include "Core/Math/MathUtility.h"
#include "Core/Math/Matrix.h"
#include "ShaderConstants.h"

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

FClothBatchedSolver::FClothBatchedSolver()
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), IntegrateCS(nullptr), ConstraintSolverCS(nullptr), BendConstraintSolverCS(nullptr), ApplyDeltasCS(nullptr), ApplyKinematicTargetsCS(nullptr), FinalizeCS(nullptr), ClearNormalsCS(nullptr), UpdateNormalsCS(nullptr), NormalizeNormalsCS(nullptr), BatchSimConstantBuffer(nullptr), AllocatedParticleCapacity(0), AllocatedConstraintCapacity(0), AllocatedBendConstraintCapacity(0), AllocatedKinematicTargetCapacity(0), AllocatedTriangleCapacity(0), AllocatedInstanceCapacity(0), UsedParticleCount(0), UsedConstraintCount(0), UsedBendConstraintCount(0), UsedKinematicTargetCount(0), UsedTriangleCount(0), UsedInstanceCount(0), CurrentBufferIndex(0), bInitialized(false), AccumulatedTime(0.0f)
{
    // Initialize all buffer pointers to nullptr
    for (int32 i = 0; i < 2; ++i)
    {
        UnifiedPositionBuffer[i] = nullptr;
        UnifiedPositionUAV[i] = nullptr;
        UnifiedPositionSRV[i] = nullptr;
    }

    UnifiedVelocityBuffer = nullptr;
    UnifiedInvMassBuffer = nullptr;
    UnifiedConstraintBuffer = nullptr;
    UnifiedBendConstraintBuffer = nullptr;
    UnifiedShearConstraintBuffer = nullptr; // NEW: Phase 3
    UnifiedAreaConstraintBuffer = nullptr;  // NEW: Phase 4
    UnifiedLRAIdsBuffer = nullptr;          // NEW: Phase 6
    UnifiedLRADistancesBuffer = nullptr;    // NEW: Phase 6
    UnifiedKinematicTargetBuffer = nullptr;
    UnifiedIndexBuffer = nullptr;
    UnifiedNormalBuffer = nullptr;
    UnifiedPositionDeltaBuffer = nullptr;
    UnifiedPositionWeightBuffer = nullptr;

    UnifiedVelocityUAV = nullptr;
    UnifiedNormalUAV = nullptr;
    UnifiedPositionDeltaUAV = nullptr;
    UnifiedPositionWeightUAV = nullptr;
    UnifiedConstraintUAV = nullptr; // NEW: For lambda updates

    UnifiedVelocitySRV = nullptr;
    UnifiedInvMassSRV = nullptr;
    UnifiedConstraintSRV = nullptr;
    UnifiedBendConstraintSRV = nullptr;
    UnifiedBendConstraintUAV = nullptr;  // NEW: Phase 5
    UnifiedShearConstraintSRV = nullptr; // NEW: Phase 3
    UnifiedShearConstraintUAV = nullptr; // NEW: Phase 3
    UnifiedAreaConstraintSRV = nullptr;  // NEW: Phase 4
    UnifiedAreaConstraintUAV = nullptr;  // NEW: Phase 4
    UnifiedLRAIdsSRV = nullptr;          // NEW: Phase 6
    UnifiedLRADistancesSRV = nullptr;    // NEW: Phase 6
    UnifiedKinematicTargetSRV = nullptr;
    UnifiedIndexSRV = nullptr;
    UnifiedNormalSRV = nullptr;

    InstanceParameterBuffer = nullptr;
    InstanceParameterSRV = nullptr;
}

FClothBatchedSolver::~FClothBatchedSolver()
{
    Release();
}

void FClothBatchedSolver::Initialize(FGraphicsDevice *InGraphics,
                                     FDXDBufferManager *InBufferManager,
                                     FDXDShaderManager *InShaderManager)
{
    Graphics = InGraphics;
    BufferManager = InBufferManager;
    ShaderManager = InShaderManager;

    if (!Graphics || !BufferManager || !ShaderManager)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Invalid initialization parameters"));
        bInitialized = false;
        return;
    }

    // Load compute shaders - this will compile them if not already loaded
    if (!LoadComputeShaders())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load compute shaders"));
        bInitialized = false;
        return;
    }

    // NOTE: bInitialized will be set true in AllocateBuffers() after buffers are created
    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Shaders loaded, ready for buffer allocation"));
}

void FClothBatchedSolver::Release()
{
    if (!bInitialized)
        return;

    // Release compute shaders (just null out pointers - managed by ShaderManager)
    IntegrateCS = nullptr;
    ConstraintSolverCS = nullptr;
    BendConstraintSolverCS = nullptr;
    ShearConstraintSolverCS = nullptr; // NEW: Phase 3
    AreaConstraintSolverCS = nullptr;  // NEW: Phase 4
    LRAConstraintSolverCS = nullptr;   // NEW: Phase 6
    ApplyDeltasCS = nullptr;
    ApplyKinematicTargetsCS = nullptr;
    FinalizeCS = nullptr; // NEW
    ClearNormalsCS = nullptr;
    UpdateNormalsCS = nullptr;
    NormalizeNormalsCS = nullptr;

    // Release unified buffers
    for (int32 i = 0; i < 2; ++i)
    {
        SAFE_RELEASE(UnifiedPositionBuffer[i]);
        SAFE_RELEASE(UnifiedPositionUAV[i]);
        SAFE_RELEASE(UnifiedPositionSRV[i]);
    }

    SAFE_RELEASE(UnifiedVelocityBuffer);
    SAFE_RELEASE(UnifiedVelocityUAV);
    SAFE_RELEASE(UnifiedVelocitySRV);

    SAFE_RELEASE(UnifiedInvMassBuffer);
    SAFE_RELEASE(UnifiedInvMassSRV);

    SAFE_RELEASE(UnifiedConstraintBuffer);
    SAFE_RELEASE(UnifiedConstraintSRV);
    SAFE_RELEASE(UnifiedConstraintUAV); // NEW: Release constraint UAV

    SAFE_RELEASE(UnifiedBendConstraintBuffer);
    SAFE_RELEASE(UnifiedBendConstraintSRV);
    SAFE_RELEASE(UnifiedBendConstraintUAV); // NEW: Phase 5

    SAFE_RELEASE(UnifiedShearConstraintBuffer); // NEW: Phase 3
    SAFE_RELEASE(UnifiedShearConstraintSRV);
    SAFE_RELEASE(UnifiedShearConstraintUAV);

    SAFE_RELEASE(UnifiedAreaConstraintBuffer); // NEW: Phase 4
    SAFE_RELEASE(UnifiedAreaConstraintSRV);
    SAFE_RELEASE(UnifiedAreaConstraintUAV);

    SAFE_RELEASE(UnifiedLRAIdsBuffer); // NEW: Phase 6
    SAFE_RELEASE(UnifiedLRAIdsSRV);
    SAFE_RELEASE(UnifiedLRADistancesBuffer);
    SAFE_RELEASE(UnifiedLRADistancesSRV);

    SAFE_RELEASE(UnifiedKinematicTargetBuffer);
    SAFE_RELEASE(UnifiedKinematicTargetSRV);

    SAFE_RELEASE(UnifiedIndexBuffer);
    SAFE_RELEASE(UnifiedIndexSRV);

    SAFE_RELEASE(UnifiedNormalBuffer);
    SAFE_RELEASE(UnifiedNormalUAV);
    SAFE_RELEASE(UnifiedNormalSRV);

    SAFE_RELEASE(UnifiedPositionDeltaBuffer);
    SAFE_RELEASE(UnifiedPositionDeltaUAV);

    SAFE_RELEASE(UnifiedPositionWeightBuffer);
    SAFE_RELEASE(UnifiedPositionWeightUAV);

    SAFE_RELEASE(InstanceParameterBuffer);
    SAFE_RELEASE(InstanceParameterSRV);

    SAFE_RELEASE(BatchSimConstantBuffer);

    bInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Released resources"));
}

bool FClothBatchedSolver::AllocateBuffers(uint32 MaxParticles, uint32 MaxConstraints,
                                          uint32 MaxBendConstraints, uint32 MaxKinematicTargets,
                                          uint32 MaxTriangles, uint32 MaxInstances)
{
    if (!Graphics || !Graphics->Device)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Invalid graphics device"));
        return false;
    }

    // Store capacities
    AllocatedParticleCapacity = MaxParticles;
    AllocatedConstraintCapacity = MaxConstraints;
    AllocatedBendConstraintCapacity = MaxBendConstraints;
    AllocatedKinematicTargetCapacity = MaxKinematicTargets;
    AllocatedTriangleCapacity = MaxTriangles;
    AllocatedInstanceCapacity = MaxInstances;

    HRESULT hr;
    D3D11_BUFFER_DESC bufferDesc = {};

    // Create unified position buffers (ping-pong)
    for (int32 i = 0; i < 2; ++i)
    {
        bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothParticleGPU) * MaxParticles;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothParticleGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedPositionBuffer[i]);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create unified position buffer %d"), i);
            return false;
        }

        // Create UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.NumElements = MaxParticles;

        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedPositionBuffer[i], &uavDesc, &UnifiedPositionUAV[i]);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position UAV %d"), i);
            return false;
        }

        // Create SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = MaxParticles;

        hr = Graphics->Device->CreateShaderResourceView(UnifiedPositionBuffer[i], &srvDesc, &UnifiedPositionSRV[i]);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position SRV %d"), i);
            return false;
        }
    }

    // Create unified velocity buffer
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FClothVelocityGPU) * MaxParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FClothVelocityGPU);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedVelocityBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create unified velocity buffer"));
        return false;
    }

    // Create velocity UAV and SRV
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = MaxParticles;

    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedVelocityBuffer, &uavDesc, &UnifiedVelocityUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create velocity UAV"));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = MaxParticles;

    hr = Graphics->Device->CreateShaderResourceView(UnifiedVelocityBuffer, &srvDesc, &UnifiedVelocitySRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create velocity SRV"));
        return false;
    }

    // Create inverse mass buffer
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(float) * MaxParticles;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(float);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedInvMassBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create inverse mass buffer"));
        return false;
    }

    srvDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateShaderResourceView(UnifiedInvMassBuffer, &srvDesc, &UnifiedInvMassSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create inverse mass SRV"));
        return false;
    }

    // Create constraint buffer (needs both SRV and UAV for lambda updates)
    if (MaxConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothDistanceConstraintGPU) * MaxConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.StructureByteStride = sizeof(FClothDistanceConstraintGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create constraint buffer"));
            return false;
        }

        srvDesc.Buffer.NumElements = MaxConstraints;
        hr = Graphics->Device->CreateShaderResourceView(UnifiedConstraintBuffer, &srvDesc, &UnifiedConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create constraint SRV"));
            return false;
        }

        // NEW: Create UAV for lambda updates
        uavDesc.Buffer.NumElements = MaxConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedConstraintBuffer, &uavDesc, &UnifiedConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create constraint UAV"));
            return false;
        }
    }

    // Create bend constraint buffer (needs UAV for lambda updates - Phase 5)
    if (MaxBendConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothBendConstraintGPU) * MaxBendConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.StructureByteStride = sizeof(FClothBendConstraintGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedBendConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bend constraint buffer"));
            return false;
        }

        srvDesc.Buffer.NumElements = MaxBendConstraints;
        hr = Graphics->Device->CreateShaderResourceView(UnifiedBendConstraintBuffer, &srvDesc, &UnifiedBendConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bend constraint SRV"));
            return false;
        }

        // NEW: Create UAV for lambda updates (Phase 5)
        uavDesc.Buffer.NumElements = MaxBendConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedBendConstraintBuffer, &uavDesc, &UnifiedBendConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bend constraint UAV"));
            return false;
        }
    }

    // NEW: Create shear constraint buffer (Phase 3)
    // Use same capacity as bend constraints (one per shared edge, similar count)
    uint32 MaxShearConstraints = MaxBendConstraints * 2; // Conservative estimate
    if (MaxShearConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothShearConstraintGPU) * MaxShearConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.StructureByteStride = sizeof(FClothShearConstraintGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedShearConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create shear constraint buffer"));
            return false;
        }

        srvDesc.Buffer.NumElements = MaxShearConstraints;
        hr = Graphics->Device->CreateShaderResourceView(UnifiedShearConstraintBuffer, &srvDesc, &UnifiedShearConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create shear constraint SRV"));
            return false;
        }

        // Create UAV for lambda updates
        uavDesc.Buffer.NumElements = MaxShearConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedShearConstraintBuffer, &uavDesc, &UnifiedShearConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create shear constraint UAV"));
            return false;
        }

        AllocatedShearConstraintCapacity = MaxShearConstraints;
    }

    // NEW: Create area constraint buffer (Phase 4)
    // Use same capacity as shear (one per triangle)
    uint32 MaxAreaConstraints = MaxShearConstraints;
    if (MaxAreaConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothAreaConstraintGPU) * MaxAreaConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.StructureByteStride = sizeof(FClothAreaConstraintGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedAreaConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create area constraint buffer"));
            return false;
        }

        srvDesc.Buffer.NumElements = MaxAreaConstraints;
        hr = Graphics->Device->CreateShaderResourceView(UnifiedAreaConstraintBuffer, &srvDesc, &UnifiedAreaConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create area constraint SRV"));
            return false;
        }

        // Create UAV for lambda updates
        uavDesc.Buffer.NumElements = MaxAreaConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedAreaConstraintBuffer, &uavDesc, &UnifiedAreaConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create area constraint UAV"));
            return false;
        }

        AllocatedAreaConstraintCapacity = MaxAreaConstraints;
    }

    // NEW: Create LRA buffers (Phase 6) - K=2 entries per particle
    uint32 MaxLRAEntries = MaxParticles * 2; // K=2 anchors per particle
    if (MaxLRAEntries > 0)
    {
        // LRA IDs buffer (uint32)
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint32) * MaxLRAEntries;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(uint32);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedLRAIdsBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to create LRA IDs buffer"));
        }
        else
        {
            srvDesc.Buffer.NumElements = MaxLRAEntries;
            hr = Graphics->Device->CreateShaderResourceView(UnifiedLRAIdsBuffer, &srvDesc, &UnifiedLRAIdsSRV);
            if (FAILED(hr))
            {
                UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to create LRA IDs SRV"));
            }
        }

        // LRA Distances buffer (float)
        bufferDesc.ByteWidth = sizeof(float) * MaxLRAEntries;
        bufferDesc.StructureByteStride = sizeof(float);

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedLRADistancesBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to create LRA distances buffer"));
        }
        else
        {
            srvDesc.Buffer.NumElements = MaxLRAEntries;
            hr = Graphics->Device->CreateShaderResourceView(UnifiedLRADistancesBuffer, &srvDesc, &UnifiedLRADistancesSRV);
            if (FAILED(hr))
            {
                UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to create LRA distances SRV"));
            }
        }

        AllocatedLRACapacity = MaxLRAEntries;
    }

    // Create kinematic target buffer (dynamic)
    if (MaxKinematicTargets > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(FClothKinematicTargetGPU) * MaxKinematicTargets;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothKinematicTargetGPU);
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedKinematicTargetBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to create kinematic target buffer"));
        }
        else
        {
            srvDesc.Buffer.NumElements = MaxKinematicTargets;
            hr = Graphics->Device->CreateShaderResourceView(UnifiedKinematicTargetBuffer, &srvDesc, &UnifiedKinematicTargetSRV);
            if (FAILED(hr))
            {
                UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to create kinematic target SRV"));
            }
        }
    }

    // Create index buffer
    // NOTE: This buffer is used both as:
    //   1. Index buffer for rendering (IASetIndexBuffer) - requires D3D11_BIND_INDEX_BUFFER
    //   2. Shader resource for compute shaders (normal computation) - requires D3D11_BIND_SHADER_RESOURCE
    // Cannot use D3D11_RESOURCE_MISC_BUFFER_STRUCTURED with index buffers, so use typed buffer instead
    if (MaxTriangles > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint32) * MaxTriangles * 3;                    // CRITICAL: 3 indices per triangle!
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE; // Both flags for dual use
        bufferDesc.StructureByteStride = 0;                                          // Not a structured buffer
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = 0; // Remove D3D11_RESOURCE_MISC_BUFFER_STRUCTURED

        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Creating index buffer - Triangles: %u, Indices: %u, ByteWidth: %u"),
               MaxTriangles, MaxTriangles * 3, bufferDesc.ByteWidth);

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedIndexBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create index buffer (HRESULT: 0x%08X)"), hr);
            return false;
        }

        // VERIFY buffer was created with correct size
        D3D11_BUFFER_DESC verifyDesc;
        UnifiedIndexBuffer->GetDesc(&verifyDesc);
        uint32 actualIndexCapacity = verifyDesc.ByteWidth / sizeof(uint32);
        uint32 expectedIndexCapacity = MaxTriangles * 3;

        if (actualIndexCapacity != expectedIndexCapacity)
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Index buffer size mismatch! Expected: %u indices, Got: %u indices"),
                   expectedIndexCapacity, actualIndexCapacity);
            return false;
        }

        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Index buffer verified - Capacity: %u indices (%u bytes)"),
               actualIndexCapacity, verifyDesc.ByteWidth);

        // Create SRV as typed buffer (Buffer<uint> in HLSL) instead of StructuredBuffer<uint>
        D3D11_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
        indexSrvDesc.Format = DXGI_FORMAT_R32_UINT; // Typed as uint32
        indexSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        indexSrvDesc.Buffer.FirstElement = 0;
        indexSrvDesc.Buffer.NumElements = MaxTriangles * 3;

        hr = Graphics->Device->CreateShaderResourceView(UnifiedIndexBuffer, &indexSrvDesc, &UnifiedIndexSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create index SRV"));
            return false;
        }
    }

    // Create normal buffer
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FVector) * MaxParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FVector);
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedNormalBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create normal buffer"));
        return false;
    }

    uavDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedNormalBuffer, &uavDesc, &UnifiedNormalUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create normal UAV"));
        return false;
    }

    srvDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateShaderResourceView(UnifiedNormalBuffer, &srvDesc, &UnifiedNormalSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create normal SRV"));
        return false;
    }

    // Create position delta buffer (for constraint solving)
    bufferDesc.ByteWidth = sizeof(int32) * 3 * MaxParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(int32) * 3;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedPositionDeltaBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position delta buffer"));
        return false;
    }

    uavDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedPositionDeltaBuffer, &uavDesc, &UnifiedPositionDeltaUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position delta UAV"));
        return false;
    }

    // Create position weight buffer
    bufferDesc.ByteWidth = sizeof(int32) * MaxParticles;
    bufferDesc.StructureByteStride = sizeof(int32);

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedPositionWeightBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position weight buffer"));
        return false;
    }

    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedPositionWeightBuffer, &uavDesc, &UnifiedPositionWeightUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position weight UAV"));
        return false;
    }

    // Create instance parameter buffer
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(FClothInstanceParameters) * MaxInstances;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FClothInstanceParameters);
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &InstanceParameterBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create instance parameter buffer"));
        return false;
    }

    srvDesc.Buffer.NumElements = MaxInstances;
    hr = Graphics->Device->CreateShaderResourceView(InstanceParameterBuffer, &srvDesc, &InstanceParameterSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create instance parameter SRV"));
        return false;
    }

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = (sizeof(FClothSimConstants) + 0xf) & 0xfffffff0; // 16-byte aligned

    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &BatchSimConstantBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create constant buffer"));
        return false;
    }

    // Mark as fully initialized now that both shaders and buffers are ready
    bInitialized = true;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Allocated buffers - Particles: %d, Constraints: %d, Instances: %d"),
           MaxParticles, MaxConstraints, MaxInstances);

    return true;
}

void FClothBatchedSolver::Simulate(float DeltaTime)
{
    if (!bInitialized || !Graphics || !Graphics->DeviceContext)
        return;

    if (UsedParticleCount == 0)
        return;

    // Clamp delta time for stability
    float clampedDT = FMath::Clamp(DeltaTime, 0.0001f, 0.033f);

    // NEW: Fixed timestep accumulation with substeps (Velvet-inspired)
    AccumulatedTime += clampedDT;
    int32 NumSubstepsExecuted = 0;

    // Use fixed substep time from config
    float SubstepTime = Config.FixedSubstepTime;

    while (AccumulatedTime >= SubstepTime && NumSubstepsExecuted < Config.MaxSubstepsPerFrame)
    {
        SimulateSubstep(SubstepTime);
        AccumulatedTime -= SubstepTime;
        NumSubstepsExecuted++;
    }

    // Final normal update (once per frame, not per substep)
    if (UsedTriangleCount > 0)
    {
        // DispatchClearNormals(UsedParticleCount);
        // DispatchUpdateNormals(UsedTriangleCount);
        // DispatchNormalizeNormals(UsedParticleCount);
    }
}

void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    // CRITICAL: Save the starting buffer index for velocity calculation in Finalize
    int32 substepStartBufferIndex = CurrentBufferIndex;

    // Update constant buffers with substep delta time
    UpdateConstantBuffers(SubstepDeltaTime);

    // 1. Integration Pass - Apply forces and predict positions
    DispatchIntegration(UsedParticleCount);

    // Swap ping-pong buffers
    int32 readIdx = CurrentBufferIndex;
    int32 writeIdx = 1 - CurrentBufferIndex;
    CurrentBufferIndex = writeIdx;

    // 2. Apply kinematic targets after integration
    if (UsedKinematicTargetCount > 0)
    {
        DispatchApplyKinematicTargets(UsedKinematicTargetCount);
    }

    // TODO Phase 3: Pre-stabilization collision would go here

    // 3. Constraint solver iterations
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        // Clear delta accumulation buffers
        ClearAccumulationBuffers(UsedParticleCount);

        // Solve distance constraints (graph-colored, direct write)
        if (UsedConstraintCount > 0)
        {
            DispatchConstraintSolver(UsedConstraintCount);
        }

        // Solve shear constraints (delta accumulation) - NEW: Phase 3
        if (UsedShearConstraintCount > 0)
        {
            DispatchShearConstraintSolver(UsedShearConstraintCount);
        }

        // Solve bend constraints (delta accumulation)
        if (UsedBendConstraintCount > 0)
        {
            DispatchBendConstraintSolver(UsedBendConstraintCount);
        }

        // Solve area constraints (delta accumulation) - NEW: Phase 4
        if (UsedAreaConstraintCount > 0)
        {
            DispatchAreaConstraintSolver(UsedAreaConstraintCount);
        }

        // Apply accumulated deltas (for shear, bend, and area)
        DispatchApplyDeltas(UsedParticleCount);

        // Reapply kinematic targets to enforce attachment
        if (UsedKinematicTargetCount > 0)
        {
            DispatchApplyKinematicTargets(UsedKinematicTargetCount);
        }

        // Update buffer indices for next iteration
        readIdx = writeIdx;
        writeIdx = 1 - writeIdx;
        CurrentBufferIndex = writeIdx;
    }

    // Long range attachments (once per substep, after iterations) - NEW: Phase 6
    if (UsedLRACount > 0)
    {
        //DispatchLRAConstraints(UsedParticleCount);
    }

    // TODO Phase 7: Per-substep collision would go here

    // 4. Velocity Finalization (NEW - Velvet pattern)
    // Derives velocity from position change, clamps max velocity, applies damping
    // CRITICAL: Pass substepStartBufferIndex to get correct velocity baseline
    DispatchFinalize(UsedParticleCount, substepStartBufferIndex);
}

bool FClothBatchedSolver::LoadComputeShaders()
{
    if (!ShaderManager)
        return false;

    bool bSuccess = true;
    HRESULT hr;

    // Load or get Integration shader
    hr = ShaderManager->AddComputeShader(L"ClothIntegrateCS", L"Shaders/Cloth/ClothIntegrate.hlsl", "IntegrateCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothIntegrate shader"));
        bSuccess = false;
    }
    IntegrateCS = ShaderManager->GetComputeShaderByKey(L"ClothIntegrateCS");

    // Load or get Constraint Solver shader
    hr = ShaderManager->AddComputeShader(L"ClothConstraintSolverCS", L"Shaders/Cloth/ClothConstraintSolver.hlsl", "SolveDistanceConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothConstraintSolver shader"));
        bSuccess = false;
    }
    ConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothConstraintSolverCS");

    // Load or get Bend Constraint Solver shader
    hr = ShaderManager->AddComputeShader(L"ClothBendConstraintSolverCS", L"Shaders/Cloth/ClothBendConstraintSolver.hlsl", "SolveBendConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothBendConstraintSolver shader"));
        bSuccess = false;
    }
    BendConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothBendConstraintSolverCS");

    // NEW: Load or get Shear Constraint Solver shader (Phase 3)
    hr = ShaderManager->AddComputeShader(L"ClothSolveShearCS", L"Shaders/Cloth/ClothSolveShear.hlsl", "SolveShearConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothSolveShear shader"));
        bSuccess = false;
    }
    ShearConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothSolveShearCS");

    // NEW: Load or get Area Constraint Solver shader (Phase 4)
    hr = ShaderManager->AddComputeShader(L"ClothSolveAreaCS", L"Shaders/Cloth/ClothSolveArea.hlsl", "SolveAreaConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothSolveArea shader"));
        bSuccess = false;
    }
    AreaConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothSolveAreaCS");

    // NEW: Load or get LRA Constraint Solver shader (Phase 6)
    hr = ShaderManager->AddComputeShader(L"ClothSolveLRACS", L"Shaders/Cloth/ClothSolveLRA.hlsl", "SolveLRAConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothSolveLRA shader (optional)"));
    }
    LRAConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothSolveLRACS");

    // Load or get Apply Deltas shader
    hr = ShaderManager->AddComputeShader(L"ClothApplyConstraintDeltasCS", L"Shaders/Cloth/ClothApplyDelta.hlsl", "ApplyConstraintDeltasCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothApplyDelta shader"));
        bSuccess = false;
    }
    ApplyDeltasCS = ShaderManager->GetComputeShaderByKey(L"ClothApplyConstraintDeltasCS");

    // Load or get Apply Kinematic Targets shader
    hr = ShaderManager->AddComputeShader(L"ClothApplyKinematicTargetsCS", L"Shaders/Cloth/ClothApplyKinematicTargets.hlsl", "ApplyKinematicTargetsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothApplyKinematicTargets shader (optional)"));
        // Not critical - kinematic targets are optional
    }
    ApplyKinematicTargetsCS = ShaderManager->GetComputeShaderByKey(L"ClothApplyKinematicTargetsCS");

    // NEW: Load or get Finalize shader (Velvet-inspired velocity finalization)
    hr = ShaderManager->AddComputeShader(L"ClothFinalizeCS", L"Shaders/Cloth/ClothFinalize.hlsl", "FinalizeVelocityCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothFinalize shader"));
        bSuccess = false;
    }
    FinalizeCS = ShaderManager->GetComputeShaderByKey(L"ClothFinalizeCS");

    // Load or get Normal Update shaders
    hr = ShaderManager->AddComputeShader(L"ClothClearNormalsCS", L"Shaders/Cloth/ClothUpdateNormals.hlsl", "ClearNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothClearNormals shader"));
        bSuccess = false;
    }
    ClearNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothClearNormalsCS");

    hr = ShaderManager->AddComputeShader(L"ClothUpdateNormalsCS", L"Shaders/Cloth/ClothUpdateNormals.hlsl", "UpdateNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothUpdateNormals shader"));
        bSuccess = false;
    }
    UpdateNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothUpdateNormalsCS");

    hr = ShaderManager->AddComputeShader(L"ClothNormalizeNormalsCS", L"Shaders/Cloth/ClothUpdateNormals.hlsl", "NormalizeNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothNormalizeNormals shader"));
        bSuccess = false;
    }
    NormalizeNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothNormalizeNormalsCS");

    // Verify critical shaders loaded
    if (!IntegrateCS || !ConstraintSolverCS || !BendConstraintSolverCS || !ApplyDeltasCS || !FinalizeCS)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to load required compute shaders"));
        bSuccess = false;
    }

    if (bSuccess)
    {
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: All compute shaders loaded successfully"));
    }

    return bSuccess;
}

void FClothBatchedSolver::SetUsedCounts(uint32 Particles, uint32 Constraints, uint32 BendConstraints,
                                        uint32 ShearConstraints, // NEW: Phase 3
                                        uint32 AreaConstraints,  // NEW: Phase 4
                                        uint32 KinematicTargets, uint32 Triangles, uint32 Instances)
{
    UsedParticleCount = Particles;
    UsedConstraintCount = Constraints;
    UsedBendConstraintCount = BendConstraints;
    UsedShearConstraintCount = ShearConstraints; // NEW: Phase 3
    UsedAreaConstraintCount = AreaConstraints;   // NEW: Phase 4
    UsedKinematicTargetCount = KinematicTargets;
    UsedTriangleCount = Triangles;
    UsedInstanceCount = Instances;
}

ID3D11ShaderResourceView *FClothBatchedSolver::GetPositionBufferSRV() const
{
    return UnifiedPositionSRV[CurrentBufferIndex];
}

ID3D11ShaderResourceView *FClothBatchedSolver::GetNormalBufferSRV() const
{
    return UnifiedNormalSRV;
}

void FClothBatchedSolver::SetConfig(const FClothConfig &InConfig)
{
    Config = InConfig;
}

uint32 FClothBatchedSolver::GetDispatchCount(uint32 ElementCount, uint32 ThreadGroupSize) const
{
    return (ElementCount + ThreadGroupSize - 1) / ThreadGroupSize;
}

// Data upload method implementations
void FClothBatchedSolver::UploadParticleData(const TArray<FVector> &Positions,
                                             const TArray<float> &InvMasses,
                                             const TArray<uint32> &InstanceIDs,
                                             uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || Positions.Num() == 0)
        return;

    if (!UnifiedPositionBuffer[0] || !UnifiedPositionBuffer[1] || !UnifiedInvMassBuffer || !UnifiedVelocityBuffer)
        return;

    uint32 numParticles = Positions.Num();

    // Prepare particle data with instance IDs
    TArray<FClothParticleGPU> particlesGPU;
    particlesGPU.SetNum(numParticles);

    for (uint32 i = 0; i < numParticles; ++i)
    {
        particlesGPU[i].Position = Positions[i];
        particlesGPU[i].InstanceID = (i < static_cast<uint32>(InstanceIDs.Num()))
                                         ? InstanceIDs[i]
                                         : 0;
    }

    // Upload to unified position buffers (both ping-pong buffers)
    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothParticleGPU);
    destBox.right = destBox.left + numParticles * sizeof(FClothParticleGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedPositionBuffer[0], 0, &destBox,
                                               particlesGPU.GetData(), 0, 0);
    Graphics->DeviceContext->UpdateSubresource(UnifiedPositionBuffer[1], 0, &destBox,
                                               particlesGPU.GetData(), 0, 0);

    // Upload inverse masses to separate buffer
    if (InvMasses.Num() > 0)
    {
        destBox.left = DestOffset * sizeof(float);
        destBox.right = destBox.left + numParticles * sizeof(float);

        Graphics->DeviceContext->UpdateSubresource(UnifiedInvMassBuffer, 0, &destBox,
                                                   InvMasses.GetData(), 0, 0);
    }

    // CRITICAL FIX: Initialize velocity buffer to zero for new particles
    // Without this, velocities contain undefined data and simulation won't start correctly
    TArray<FClothVelocityGPU> velocitiesGPU;
    velocitiesGPU.SetNum(numParticles);
    for (uint32 i = 0; i < numParticles; ++i)
    {
        velocitiesGPU[i].Velocity = FVector::ZeroVector;
        velocitiesGPU[i].Padding = 0.0f;
    }

    destBox.left = DestOffset * sizeof(FClothVelocityGPU);
    destBox.right = destBox.left + numParticles * sizeof(FClothVelocityGPU);

    Graphics->DeviceContext->UpdateSubresource(UnifiedVelocityBuffer, 0, &destBox,
                                               velocitiesGPU.GetData(), 0, 0);
}

void FClothBatchedSolver::UploadConstraintData(const TArray<FClothDistanceConstraintGPU> &Constraints,
                                               uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || Constraints.Num() == 0)
        return;

    if (!UnifiedConstraintBuffer)
        return;

    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothDistanceConstraintGPU);
    destBox.right = destBox.left + Constraints.Num() * sizeof(FClothDistanceConstraintGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedConstraintBuffer, 0, &destBox,
                                               Constraints.GetData(), 0, 0);
}

void FClothBatchedSolver::UploadBendConstraintData(const TArray<FClothBendConstraintGPU> &BendConstraints,
                                                   uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || BendConstraints.Num() == 0)
        return;

    if (!UnifiedBendConstraintBuffer)
        return;

    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothBendConstraintGPU);
    destBox.right = destBox.left + BendConstraints.Num() * sizeof(FClothBendConstraintGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedBendConstraintBuffer, 0, &destBox,
                                               BendConstraints.GetData(), 0, 0);
}

void FClothBatchedSolver::UploadShearConstraintData(const TArray<FClothShearConstraintGPU> &ShearConstraints,
                                                    uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || ShearConstraints.Num() == 0)
        return;

    if (!UnifiedShearConstraintBuffer)
        return;

    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothShearConstraintGPU);
    destBox.right = destBox.left + ShearConstraints.Num() * sizeof(FClothShearConstraintGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedShearConstraintBuffer, 0, &destBox,
                                               ShearConstraints.GetData(), 0, 0);
}

void FClothBatchedSolver::UploadAreaConstraintData(const TArray<FClothAreaConstraintGPU> &AreaConstraints,
                                                   uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || AreaConstraints.Num() == 0)
        return;

    if (!UnifiedAreaConstraintBuffer)
        return;

    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothAreaConstraintGPU);
    destBox.right = destBox.left + AreaConstraints.Num() * sizeof(FClothAreaConstraintGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedAreaConstraintBuffer, 0, &destBox,
                                               AreaConstraints.GetData(), 0, 0);
}

void FClothBatchedSolver::UploadLRAData(const TArray<uint32> &LRAIds, const TArray<float> &LRADistances,
                                        uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    if (LRAIds.Num() != LRADistances.Num() || LRAIds.Num() == 0)
        return;

    if (!UnifiedLRAIdsBuffer || !UnifiedLRADistancesBuffer)
        return;

    D3D11_BOX destBox;

    // Upload IDs
    destBox.left = DestOffset * sizeof(uint32);
    destBox.right = destBox.left + LRAIds.Num() * sizeof(uint32);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedLRAIdsBuffer, 0, &destBox,
                                               LRAIds.GetData(), 0, 0);

    // Upload Distances
    destBox.left = DestOffset * sizeof(float);
    destBox.right = destBox.left + LRADistances.Num() * sizeof(float);

    Graphics->DeviceContext->UpdateSubresource(UnifiedLRADistancesBuffer, 0, &destBox,
                                               LRADistances.GetData(), 0, 0);
}

// ClothBatchedSolver.cpp
void FClothBatchedSolver::UploadKinematicTargets(
    const TArray<FClothKinematicTargetGPU> &Targets,
    uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || Targets.Num() == 0)
        return;
    if (!UnifiedKinematicTargetBuffer)
        return;

    // Kinematic targets는 매 프레임 전체를 업데이트하므로
    // WRITE_DISCARD 사용 (DestOffset은 무시됨)
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(
        UnifiedKinematicTargetBuffer,
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &msr);

    if (SUCCEEDED(hr))
    {
        // DestOffset이 0이 아니면 경고
        if (DestOffset != 0)
        {
            UE_LOG(ELogLevel::Warning,
                   TEXT("UploadKinematicTargets: DestOffset %d ignored (WRITE_DISCARD used)"),
                   DestOffset);
        }

        // 전체 버퍼의 시작부터 복사
        uint32 bytesToCopy = Targets.Num() * sizeof(FClothKinematicTargetGPU);
        memcpy(msr.pData, Targets.GetData(), bytesToCopy);

        Graphics->DeviceContext->Unmap(UnifiedKinematicTargetBuffer, 0);
    }
    else
    {
        UE_LOG(ELogLevel::Error,
               TEXT("ClothBatchedSolver: Failed to map kinematic target buffer (HR=0x%08X)"),
               hr);
    }
}

void FClothBatchedSolver::UploadInstanceParameters(const TArray<FClothInstanceParameters> &Parameters)
{
    if (!Graphics || !Graphics->DeviceContext || !InstanceParameterBuffer)
        return;

    if (Parameters.Num() == 0 || Parameters.Num() > static_cast<int32>(AllocatedInstanceCapacity))
        return;

    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(InstanceParameterBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        uint32 bytesToCopy = sizeof(FClothInstanceParameters) * Parameters.Num();
        memcpy(msr.pData, Parameters.GetData(), bytesToCopy);
        Graphics->DeviceContext->Unmap(InstanceParameterBuffer, 0);
    }
}

void FClothBatchedSolver::UploadIndexData(const TArray<uint32> &Indices, uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || Indices.Num() == 0)
        return;

    if (!UnifiedIndexBuffer)
        return;

    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(uint32);
    destBox.right = destBox.left + Indices.Num() * sizeof(uint32);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedIndexBuffer, 0, &destBox,
                                               Indices.GetData(), 0, 0);
}

// Dispatch method implementations
void FClothBatchedSolver::DispatchIntegration(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !IntegrateCS)
        return;

    int32 readIdx = CurrentBufferIndex;
    int32 writeIdx = 1 - CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // CRITICAL FIX: Bind SRVs to correct slots matching shader registers
    // Integration shader expects: t2 = InvMassBuffer, t3 = InstanceParams
    Graphics->DeviceContext->CSSetShaderResources(2, 1, &UnifiedInvMassSRV);    // t2: InvMassBuffer
    Graphics->DeviceContext->CSSetShaderResources(3, 1, &InstanceParameterSRV); // t3: InstanceParams

    // Bind UAVs
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionUAV[readIdx],  // u0: ParticlesRead
        UnifiedPositionUAV[writeIdx], // u1: ParticlesWrite
        UnifiedVelocityUAV            // u2: VelocityBuffer (in-place)
    };

    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(IntegrateCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(2, 2, nullSRVs); // Unbind t2, t3
}

void FClothBatchedSolver::DispatchConstraintSolver(uint32 ConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ConstraintSolverCS || ConstraintCount == 0)
        return;

    int32 readIdx = CurrentBufferIndex;
    int32 writeIdx = 1 - CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Graph-colored XPBD solver with direct position writes (PhysixStudio pattern)
    // Bind SRVs to match shader registers
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV[1 - readIdx], // t0: PositionOld (previous frame for velocity damping)
        UnifiedPositionSRV[readIdx],     // t1: PositionRead (predicted positions)
        UnifiedConstraintSRV,            // t2: Constraint buffer
        UnifiedInvMassSRV,               // t3: Inverse masses
        InstanceParameterSRV             // t4: Instance parameters
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 5, srvs);

    // Bind UAVs for DIRECT writes (no delta accumulation)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionUAV[readIdx], // u0: PositionWrite (direct write, read-modify-write)
        UnifiedConstraintUAV         // u1: ConstraintWrite (for lambda update)
    };
    UINT initialCounts[2] = {0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ConstraintSolverCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 5, nullSRVs);
}

void FClothBatchedSolver::DispatchBendConstraintSolver(uint32 BendConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !BendConstraintSolverCS || BendConstraintCount == 0)
        return;

    int32 readIdx = CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV[readIdx], // t0
        UnifiedBendConstraintSRV,    // t1
        UnifiedInvMassSRV,           // t2
        InstanceParameterSRV         // t3
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 4, srvs);

    // Bind UAVs (delta accumulation + lambda update)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,  // u0
        UnifiedPositionWeightUAV, // u1
        UnifiedBendConstraintUAV  // u2: BendWrite (lambda updates) - NEW: Phase 5
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(BendConstraintSolverCS, nullptr, 0);

    // Dispatch (256 threads per group - PhysixStudio standard)
    uint32 dispatchCount = GetDispatchCount(BendConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchShearConstraintSolver(uint32 ShearConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ShearConstraintSolverCS || ShearConstraintCount == 0)
        return;

    int32 readIdx = CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs (matches ClothSolveShear.hlsl)
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV[1 - readIdx], // t0: PositionOld
        UnifiedPositionSRV[readIdx],     // t1: PositionRead
        UnifiedShearConstraintSRV,       // t2: ShearBuffer
        UnifiedInvMassSRV,               // t3: InvMassBuffer
        InstanceParameterSRV             // t4: InstanceParams
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 5, srvs);

    // Bind UAVs (delta accumulation + lambda update)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,  // u0: PositionDelta
        UnifiedPositionWeightUAV, // u1: PositionWeight
        UnifiedShearConstraintUAV // u2: ShearWrite (lambda updates)
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ShearConstraintSolverCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ShearConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 5, nullSRVs);
}

void FClothBatchedSolver::DispatchAreaConstraintSolver(uint32 AreaConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !AreaConstraintSolverCS || AreaConstraintCount == 0)
        return;

    int32 readIdx = CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs (matches ClothSolveArea.hlsl)
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV[1 - readIdx], // t0: PositionOld
        UnifiedPositionSRV[readIdx],     // t1: PositionRead
        UnifiedAreaConstraintSRV,        // t2: AreaBuffer
        UnifiedInvMassSRV,               // t3: InvMassBuffer
        InstanceParameterSRV             // t4: InstanceParams
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 5, srvs);

    // Bind UAVs (delta accumulation + lambda update)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,  // u0: PositionDelta
        UnifiedPositionWeightUAV, // u1: PositionWeight
        UnifiedAreaConstraintUAV  // u2: AreaWrite (lambda updates)
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(AreaConstraintSolverCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(AreaConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 5, nullSRVs);
}

void FClothBatchedSolver::DispatchApplyDeltas(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyDeltasCS)
        return;

    int32 readIdx = CurrentBufferIndex;
    int32 writeIdx = 1 - CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs to match shader registers
    // ApplyDelta shader expects: t0 = PositionRead, t2 = InvMassBuffer
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &UnifiedPositionSRV[readIdx]); // t0: PositionRead
    Graphics->DeviceContext->CSSetShaderResources(2, 1, &UnifiedInvMassSRV);           // t2: InvMassBuffer

    // Bind UAVs (REMOVED u3: VelocityUAV - velocity no longer updated here)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,     // u0
        UnifiedPositionWeightUAV,    // u1
        UnifiedPositionUAV[writeIdx] // u2: Write to next buffer
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ApplyDeltasCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 1, nullSRVs);     // Unbind t0
    Graphics->DeviceContext->CSSetShaderResources(2, 1, &nullSRVs[1]); // Unbind t2
}

void FClothBatchedSolver::DispatchApplyKinematicTargets(uint32 TargetCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyKinematicTargetsCS || TargetCount == 0)
        return;

    int32 readIdx = CurrentBufferIndex;
    int32 writeIdx = 1 - CurrentBufferIndex;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // NEW: Bind additional SRVs for LRA support (matches shader registers)
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedKinematicTargetSRV,   // t0: Kinematic targets
        UnifiedPositionSRV[readIdx], // t1: Current positions (for LRA distance check)
        UnifiedInvMassSRV            // t2: Inverse masses (for LRA)
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 3, srvs);

    // NEW: Bind UAVs for delta accumulation (LRA mode) and direct write (hard kinematic mode)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,     // u0: Delta accumulation
        UnifiedPositionWeightUAV,    // u1: Weight accumulation
        UnifiedPositionUAV[writeIdx] // u2: Direct write for hard kinematic
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Set shader
    Graphics->DeviceContext->CSSetShader(ApplyKinematicTargetsCS, nullptr, 0);

    // Dispatch (one thread per kinematic target)
    uint32 dispatchCount = GetDispatchCount(TargetCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, nullSRVs);
}

void FClothBatchedSolver::DispatchFinalize(uint32 ParticleCount, int32 OldPositionBufferIndex)
{
    if (!Graphics || !Graphics->DeviceContext || !FinalizeCS)
        return;

    // CRITICAL FIX: Use the position from START of substep, not just before constraints
    // This preserves the velocity from integration step correctly
    int32 oldIdx = OldPositionBufferIndex; // Position at START of substep (before integration)
    int32 newIdx = CurrentBufferIndex;     // Position after constraints

    // Bind constant buffer (contains DeltaTime, MaxSpeed)
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // TODO: Bind additional FinalizeConstants buffer (SubstepDeltaTime, MaxSpeed)
    // For now, we'll use values from main constant buffer

    // Bind SRVs
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV[oldIdx], // t0: Old position (before substep)
        UnifiedPositionSRV[newIdx], // t1: New position (after constraints)
        UnifiedInvMassSRV,          // t2: Inverse masses
        InstanceParameterSRV        // t3: Instance parameters
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 4, srvs);

    // Bind UAVs
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionUAV[newIdx], // u0: Write position (may clamp if velocity exceeded)
        UnifiedVelocityUAV          // u1: Write velocity
    };
    UINT initialCounts[2] = {0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(FinalizeCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchClearNormals(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ClearNormalsCS)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind normal buffer UAV
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UnifiedNormalUAV, nullptr);

    // Set shader
    Graphics->DeviceContext->CSSetShader(ClearNormalsCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAV = nullptr;
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void FClothBatchedSolver::DispatchUpdateNormals(uint32 TriangleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !UpdateNormalsCS || TriangleCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV[CurrentBufferIndex], // t0
        UnifiedIndexSRV                         // t1
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);

    // Bind normal buffer UAV
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UnifiedNormalUAV, nullptr);

    // Set shader
    Graphics->DeviceContext->CSSetShader(UpdateNormalsCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(TriangleCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAV = nullptr;
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
}

void FClothBatchedSolver::DispatchNormalizeNormals(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !NormalizeNormalsCS)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind normal buffer UAV
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UnifiedNormalUAV, nullptr);

    // Set shader
    Graphics->DeviceContext->CSSetShader(NormalizeNormalsCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAV = nullptr;
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}

void FClothBatchedSolver::ClearAccumulationBuffers(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    UINT clearValues[4] = {0, 0, 0, 0};
    if (UnifiedPositionDeltaUAV)
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(UnifiedPositionDeltaUAV, clearValues);
    if (UnifiedPositionWeightUAV)
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(UnifiedPositionWeightUAV, clearValues);
}

void FClothBatchedSolver::UpdateConstantBuffers(float DeltaTime)
{
    if (!Graphics || !Graphics->DeviceContext || !BatchSimConstantBuffer)
        return;

    FClothSimConstants constants = {};
    constants.NumParticles = UsedParticleCount;
    constants.NumConstraints = UsedConstraintCount;
    constants.NumBendConstraints = UsedBendConstraintCount;
    constants.NumKinematicTargets = UsedKinematicTargetCount;
    constants.DeltaTime = DeltaTime;
    constants.Damping = Config.Damping;
    constants.Gravity = FVector(0.0f, 0.0f, -9.80f); // Default gravity
    constants.StretchStiffness = Config.StretchStiffness;
    constants.Wind = FVector::ZeroVector;
    constants.BendStiffness = Config.BendStiffness;
    constants.AirDrag = Config.AirDrag;
    constants.NumIterations = Config.NumIterations;
    constants.CurrentIteration = 0;
    constants.UseXPBD = Config.bUseXPBD ? 1 : 0;

    // Velvet-inspired parameters
    constants.RelaxationFactor = Config.RelaxationFactor;
    constants.MaxSpeed = Config.MaxSpeed;
    constants.LongRangeStretchiness = Config.LongRangeStretchiness;

    constants.WorldMatrix = FMatrix::Identity;

    // NEW: PhysixStudio XPBD parameters
    constants.NumShearConstraints = 0; // Will be set in Phase 3
    constants.NumAreaConstraints = 0;  // Will be set in Phase 4
    constants.NumLRAEntries = 0;       // Will be set in Phase 6
    constants.NumSubsteps = Config.NumSubsteps;

    constants.ComplianceStretch = 1e-6f; // PhysixStudio default
    constants.ComplianceShear = 1e-6f;   // PhysixStudio default
    constants.ComplianceBend = 500.0f;   // PhysixStudio default (soft bending)
    constants.ComplianceArea = 1e-2f;    // PhysixStudio default

    constants.BetaStretch = 100.0f; // Velocity damping for stretch
    constants.BetaBend = 0.0f;      // No damping for bend
    constants.Thickness = Config.CollisionThickness;
    constants.Friction = Config.Friction;

    constants.Padding4 = 0.0f;
    constants.Padding5 = 0.0f;
    constants.Padding6 = 0.0f;
    constants.Padding7 = 0.0f;

    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(BatchSimConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &constants, sizeof(FClothSimConstants));
        Graphics->DeviceContext->Unmap(BatchSimConstantBuffer, 0);
    }
}

bool FClothBatchedSolver::CreateGPUResources()
{
    // Handled by AllocateBuffers
    return true;
}
