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
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), IntegrateCS(nullptr), ConstraintSolverCS(nullptr), BendConstraintSolverCS(nullptr), ApplyDeltasCS(nullptr), ApplyKinematicTargetsCS(nullptr), FinalizeCS(nullptr), ClearNormalsCS(nullptr), UpdateNormalsCS(nullptr), NormalizeNormalsCS(nullptr), BatchSimConstantBuffer(nullptr), AllocatedParticleCapacity(0), AllocatedConstraintCapacity(0), AllocatedBendConstraintCapacity(0), AllocatedKinematicTargetCapacity(0), AllocatedTriangleCapacity(0), AllocatedInstanceCapacity(0), UsedParticleCount(0), UsedConstraintCount(0), UsedBendConstraintCount(0), UsedKinematicTargetCount(0), UsedTriangleCount(0), UsedInstanceCount(0), bInitialized(false), AccumulatedTime(0.0f)
{
    // Initialize all buffer pointers to nullptr (Velvet pattern - single working buffer)
    UnifiedPositionBuffer = nullptr;
    UnifiedPositionUAV = nullptr;
    UnifiedPositionSRV = nullptr;

    UnifiedPredictedBuffer = nullptr;
    UnifiedPredictedUAV = nullptr;
    UnifiedPredictedSRV = nullptr;

    UnifiedVelocityBuffer = nullptr;
    UnifiedInvMassBuffer = nullptr;
    UnifiedConstraintBuffer = nullptr;
    UnifiedBendConstraintBuffer = nullptr;
    UnifiedKinematicTargetBuffer = nullptr;
    UnifiedIndexBuffer = nullptr;
    UnifiedNormalBuffer = nullptr;
    UnifiedPositionDeltaBuffer = nullptr;
    UnifiedPositionWeightBuffer = nullptr;

    UnifiedVelocityUAV = nullptr;
    UnifiedNormalUAV = nullptr;
    UnifiedPositionDeltaUAV = nullptr;
    UnifiedPositionWeightUAV = nullptr;

    UnifiedVelocitySRV = nullptr;
    UnifiedInvMassSRV = nullptr;
    UnifiedConstraintSRV = nullptr;
    UnifiedBendConstraintSRV = nullptr;
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
    ApplyDeltasCS = nullptr;
    ApplyKinematicTargetsCS = nullptr;
    FinalizeCS = nullptr; // NEW
    ClearNormalsCS = nullptr;
    UpdateNormalsCS = nullptr;
    NormalizeNormalsCS = nullptr;

    // Release unified buffers (Velvet pattern - single working buffer)
    SAFE_RELEASE(UnifiedPositionBuffer);
    SAFE_RELEASE(UnifiedPositionUAV);
    SAFE_RELEASE(UnifiedPositionSRV);

    SAFE_RELEASE(UnifiedPredictedBuffer);
    SAFE_RELEASE(UnifiedPredictedUAV);
    SAFE_RELEASE(UnifiedPredictedSRV);

    SAFE_RELEASE(UnifiedVelocityBuffer);
    SAFE_RELEASE(UnifiedVelocityUAV);
    SAFE_RELEASE(UnifiedVelocitySRV);

    SAFE_RELEASE(UnifiedInvMassBuffer);
    SAFE_RELEASE(UnifiedInvMassSRV);

    SAFE_RELEASE(UnifiedConstraintBuffer);
    SAFE_RELEASE(UnifiedConstraintSRV);

    SAFE_RELEASE(UnifiedBendConstraintBuffer);
    SAFE_RELEASE(UnifiedBendConstraintSRV);

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
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    // Create position buffer (final result for rendering)
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FClothParticleGPU) * MaxParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FClothParticleGPU);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedPositionBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position buffer"));
        return false;
    }

    // Create position UAV
    uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = MaxParticles;

    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedPositionBuffer, &uavDesc, &UnifiedPositionUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position UAV"));
        return false;
    }

    // Create position SRV
    srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = MaxParticles;

    hr = Graphics->Device->CreateShaderResourceView(UnifiedPositionBuffer, &srvDesc, &UnifiedPositionSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create position SRV"));
        return false;
    }

    // Create predicted buffer (working buffer for constraint solving)
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FClothParticleGPU) * MaxParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FClothParticleGPU);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedPredictedBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create predicted buffer"));
        return false;
    }

    // Create predicted UAV
    uavDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedPredictedBuffer, &uavDesc, &UnifiedPredictedUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create predicted UAV"));
        return false;
    }

    // Create predicted SRV
    srvDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateShaderResourceView(UnifiedPredictedBuffer, &srvDesc, &UnifiedPredictedSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create predicted SRV"));
        return false;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Created position and predicted buffers (MaxParticles: %u)"), MaxParticles);

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
    uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = MaxParticles;

    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedVelocityBuffer, &uavDesc, &UnifiedVelocityUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create velocity UAV"));
        return false;
    }

    srvDesc = {};
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

    // Create constraint buffer
    if (MaxConstraints > 0)
    {
        bufferDesc.ByteWidth = sizeof(FClothDistanceConstraintGPU) * MaxConstraints;
        bufferDesc.StructureByteStride = sizeof(FClothDistanceConstraintGPU);

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
    }

    // Create bend constraint buffer
    if (MaxBendConstraints > 0)
    {
        bufferDesc.ByteWidth = sizeof(FClothBendConstraintGPU) * MaxBendConstraints;
        bufferDesc.StructureByteStride = sizeof(FClothBendConstraintGPU);

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
    float SubstepTime = Config.TimeStep / Config.NumSubsteps;

    for (int substep = 0; substep < Config.NumSubsteps; substep++)
    {
        SimulateSubstep(SubstepTime);
    }
    /*while (AccumulatedTime >= SubstepTime && NumSubstepsExecuted < Config.MaxSubstepsPerFrame)
    {
        SimulateSubstep(SubstepTime);
        AccumulatedTime -= SubstepTime;
        NumSubstepsExecuted++;
    }*/

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
    // Update constant buffers with substep delta time
    UpdateConstantBuffers(SubstepDeltaTime);

    // Step 1: Predict positions (Integration)
    // Reads: PositionBuffer (old positions), VelocityBuffer
    // Writes: PredictedBuffer
    DispatchIntegration(UsedParticleCount);

    // Step 2: Pre-stabilization collision (optional)
    // TODO: DispatchCollisionSDF(PredictedBuffer);

    // Step 3: Constraint solver iterations
    // All iterations work on PredictedBuffer IN-PLACE
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        // Clear delta accumulation buffers
        ClearAccumulationBuffers(UsedParticleCount);

        // Solve distance constraints
        if (UsedConstraintCount > 0)
        {
            DispatchConstraintSolver(UsedConstraintCount);
        }

        // Solve bend constraints
        /*if (UsedBendConstraintCount > 0)
        {
            DispatchBendConstraintSolver(UsedBendConstraintCount);
        }*/

        // Apply accumulated deltas to PredictedBuffer IN-PLACE
        // Reads: PredictedBuffer, DeltaBuffer, WeightBuffer
        // Writes: PredictedBuffer (modified in-place)
        DispatchApplyDeltas(UsedParticleCount);

        // NO BUFFER SWAPS - predicted buffer stays consistent
    }

    // Step 4: Apply kinematic targets (once after all iterations)
    if (UsedKinematicTargetCount > 0)
    {
        DispatchApplyKinematicTargets(UsedKinematicTargetCount);
    }

    // Step 5: Finalize - derive velocity and write final positions (in-place)
    // Reads: PositionBuffer (old) via UAV, PredictedBuffer (new) via SRV
    // Writes: VelocityBuffer, PositionBuffer (overwrite with new positions)
    DispatchFinalize(UsedParticleCount);

    // NO CurrentBufferIndex - always render from PositionBuffer
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
                                        uint32 KinematicTargets, uint32 Triangles, uint32 Instances)
{
    UsedParticleCount = Particles;
    UsedConstraintCount = Constraints;
    UsedBendConstraintCount = BendConstraints;
    UsedKinematicTargetCount = KinematicTargets;
    UsedTriangleCount = Triangles;
    UsedInstanceCount = Instances;
}

ID3D11ShaderResourceView *FClothBatchedSolver::GetPositionBufferSRV() const
{
    // Always return the single position buffer (no index needed)
    return UnifiedPositionSRV;
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

    if (!UnifiedPositionBuffer || !UnifiedPredictedBuffer || !UnifiedInvMassBuffer || !UnifiedVelocityBuffer)
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

    // Upload to position buffer only (predicted buffer will be initialized by first integration pass)
    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothParticleGPU);
    destBox.right = destBox.left + numParticles * sizeof(FClothParticleGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedPositionBuffer, 0, &destBox,
                                               particlesGPU.GetData(), 0, 0);

    // CRITICAL: Initialize predicted buffer with same data as position buffer
    // Without this, predicted buffer contains garbage/zeros and particles won't move
    Graphics->DeviceContext->UpdateSubresource(UnifiedPredictedBuffer, 0, &destBox,
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
    if (!Graphics || !Graphics->DeviceContext || !IntegrateCS || ParticleCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &UnifiedPositionSRV);   // t0: Old positions
    Graphics->DeviceContext->CSSetShaderResources(1, 1, &UnifiedVelocitySRV);   // t1: Velocities
    Graphics->DeviceContext->CSSetShaderResources(2, 1, &UnifiedInvMassSRV);    // t2: Inverse mass
    Graphics->DeviceContext->CSSetShaderResources(3, 1, &InstanceParameterSRV); // t3: Instance parameters

    // Bind predicted buffer as UAV (write predicted positions)
    ID3D11UnorderedAccessView *uavs[] = {UnifiedPredictedUAV}; // u0: Predicted (write)
    UINT initialCounts[] = {0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(IntegrateCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[] = {nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchConstraintSolver(uint32 ConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ConstraintSolverCS || ConstraintCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs (match ClothConstraintSolver.hlsl):
    // t0: Predicted positions (read)
    // t1: Constraints
    // t2: InvMass
    // t3: Instance parameters
    ID3D11ShaderResourceView *srvs[4] = {
        UnifiedPredictedSRV,
        UnifiedConstraintSRV,
        UnifiedInvMassSRV,
        InstanceParameterSRV};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, srvs);

    // Bind delta accumulation buffers as UAV
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV, // u0: Delta accumulation
        UnifiedPositionWeightUAV // u1: Weight accumulation
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
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchBendConstraintSolver(uint32 BendConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !BendConstraintSolverCS || BendConstraintCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind predicted buffer as SRV
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &UnifiedPredictedSRV);      // t0
    Graphics->DeviceContext->CSSetShaderResources(1, 1, &UnifiedBendConstraintSRV); // t1
    Graphics->DeviceContext->CSSetShaderResources(2, 1, &UnifiedInvMassSRV);        // t2

    // Bind delta accumulation UAVs
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,
        UnifiedPositionWeightUAV};
    UINT initialCounts[2] = {0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(BendConstraintSolverCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(BendConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, nullSRVs);
}

void FClothBatchedSolver::DispatchApplyDeltas(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyDeltasCS || ParticleCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind per-instance params (t0) for relaxation scaling in shader
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &InstanceParameterSRV);

    // Bind predicted buffer as UAV (read-modify-write IN-PLACE)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPredictedUAV,     // u0: Predicted buffer (in-place modification)
        UnifiedPositionDeltaUAV, // u1: Delta buffer (cleared after use)
        UnifiedPositionWeightUAV // u2: Weight buffer (cleared after use)
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ApplyDeltasCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);

    ID3D11ShaderResourceView *nullSRVs[1] = {nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
}

void FClothBatchedSolver::DispatchApplyKinematicTargets(uint32 TargetCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyKinematicTargetsCS || TargetCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind kinematic target data as SRV
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &UnifiedKinematicTargetSRV);

    // Bind predicted buffer as UAV (modify in-place)
    ID3D11UnorderedAccessView *uavs[] = {UnifiedPredictedUAV};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

    // Set shader
    Graphics->DeviceContext->CSSetShader(ApplyKinematicTargetsCS, nullptr, 0);

    // Dispatch (one thread per kinematic target)
    uint32 dispatchCount = GetDispatchCount(TargetCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[] = {nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[] = {nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
}

void FClothBatchedSolver::DispatchFinalize(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !FinalizeCS || ParticleCount == 0)
        return;

    // Bind constant buffer (contains DeltaTime, MaxSpeed, Damping)
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    // Bind SRVs (match ClothFinalize.hlsl):
    // t0: Predicted positions (read-only)
    // t1: InvMass
    // t2: Instance parameters
    ID3D11ShaderResourceView *srvs[3] = {
        UnifiedPredictedSRV, // t0
        UnifiedInvMassSRV,   // t1
        InstanceParameterSRV // t2
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 3, srvs);

    // Bind velocity and position buffers as UAV (read/write)
    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedVelocityUAV, // u0: Velocity write
        UnifiedPositionUAV  // u1: Position write (overwrite with new positions)
    };
    UINT initialCounts[2] = {0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(FinalizeCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, nullSRVs);
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

    // Bind SRVs - use position buffer (final positions after finalize)
    ID3D11ShaderResourceView *srvs[] = {
        UnifiedPositionSRV, // t0: Final positions
        UnifiedIndexSRV     // t1: Index buffer
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);

    // Bind normal buffer UAV
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UnifiedNormalUAV, nullptr);

    // Set shader
    Graphics->DeviceContext->CSSetShader(UpdateNormalsCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(TriangleCount, 256);
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

    // NEW: Velvet-inspired parameters
    constants.RelaxationFactor = Config.RelaxationFactor;
    constants.MaxSpeed = Config.MaxSpeed;
    constants.LongRangeStretchiness = Config.LongRangeStretchiness;

    constants.WorldMatrix = FMatrix::Identity;

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
