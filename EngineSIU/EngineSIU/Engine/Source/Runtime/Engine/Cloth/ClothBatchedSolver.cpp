/**
 * Cloth Batched Solver Implementation
 * GPU-based batched cloth simulation for multiple instances using unified buffers
 */

#include "ClothBatchedSolver.h"
#include "ClothCollisionManager.h"
#include "ClothGPURenderStructs.h"
#include "ClothSkinningWeightGenerator.h"
#include "ClothMeshAnalysis.h"
#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"
#include "Engine/UserInterface/Console.h"
#include "Core/Math/MathUtility.h"
#include "Core/Math/Matrix.h"

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

FClothBatchedSolver::FClothBatchedSolver()
	: Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), IntegrateCS(nullptr), ConstraintSolverCS(nullptr), BendConstraintSolverCS(nullptr), AreaConstraintSolverCS(nullptr), ApplyDeltasCS(nullptr), ComputeKinematicTargetsCS(nullptr), FinalizeCS(nullptr), UpdateNormalsCS(nullptr), ComputeTriangleNormalsCS(nullptr), NormalizeVertexNormalsCS(nullptr), CollisionSolverCS(nullptr), EdgeCollisionSolverCS(nullptr), CollisionManager(nullptr), BatchSimConstantBuffer(nullptr), AllocatedParticleCapacity(0), AllocatedConstraintCapacity(0), AllocatedBendConstraintCapacity(0), AllocatedKinematicTargetCapacity(0), AllocatedTriangleCapacity(0), AllocatedInstanceCapacity(0), AllocatedAreaConstraintCapacity(0), AllocatedEdgeCollisionCapacity(0), AllocatedRenderVertexCapacity(0), AllocatedRenderIndexCapacity(0), UsedParticleCount(0), UsedConstraintCount(0), UsedBendConstraintCount(0), UsedTriangleCount(0), UsedInstanceCount(0), UsedAttachmentCount(0), UsedAreaConstraintCount(0), UsedEdgeCollisionCount(0), UsedRenderVertexCount(0), UsedRenderIndexCount(0), bInitialized(false), AccumulatedTime(0.0f)
{
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
	UnifiedAreaConstraintBuffer = nullptr;
	UnifiedEdgeCollisionBuffer = nullptr;
	UnifiedKinematicTargetBuffer = nullptr;
	UnifiedIndexBuffer = nullptr;
	UnifiedNormalBuffer = nullptr;
	UnifiedNormalAccumulationBuffer = nullptr; 
	UnifiedPositionDeltaBuffer = nullptr;
	UnifiedPositionWeightBuffer = nullptr;

	UnifiedVelocityUAV = nullptr;
	UnifiedNormalUAV = nullptr;
	UnifiedNormalAccumulationUAV = nullptr; 
	UnifiedPositionDeltaUAV = nullptr;
	UnifiedPositionWeightUAV = nullptr;
	UnifiedConstraintUAV = nullptr;     
	UnifiedBendConstraintUAV = nullptr; 
	UnifiedAreaConstraintUAV = nullptr; 

	UnifiedVelocitySRV = nullptr;
	UnifiedInvMassSRV = nullptr;
	UnifiedConstraintSRV = nullptr;
	UnifiedBendConstraintSRV = nullptr;
	UnifiedAreaConstraintSRV = nullptr;      
	UnifiedEdgeCollisionSRV = nullptr;       
	UnifiedKinematicTargetSRV = nullptr;
	UnifiedIndexSRV = nullptr;
	UnifiedNormalSRV = nullptr;

	InstanceParameterBuffer = nullptr;
	InstanceParameterSRV = nullptr;
	
	AttachmentDataBuffer = nullptr;
	AttachmentDataSRV = nullptr;
	ComponentTransformBuffer = nullptr;
	ComponentTransformSRV = nullptr;
	
	UnifiedRenderVertexBuffer = nullptr;
	UnifiedRenderIndexBuffer = nullptr;
	UnifiedSkinningWeightBuffer = nullptr;
	UnifiedTriangleSkinningWeightBuffer = nullptr;  
	RenderNormalsBuffer = nullptr;
	RenderPositionsBuffer = nullptr;
	
	UnifiedRenderVertexSRV = nullptr;
	UnifiedRenderIndexSRV = nullptr;
	SkinningWeightsSRV = nullptr;
	TriangleSkinningWeightsSRV = nullptr;  
	RenderNormalsUAV = nullptr;
	RenderNormalsSRV = nullptr;
	RenderPositionsSRV = nullptr;
	
	// Self-collision buffers
	SelfCollisionCellCountersBuffer = nullptr;
	SelfCollisionCellDataBuffer = nullptr;
	SelfCollisionParamsBuffer = nullptr;
	SelfCollisionCellCountersUAV = nullptr;
	SelfCollisionCellDataUAV = nullptr;
	SelfCollisionCellCountersSRV = nullptr;
	SelfCollisionCellDataSRV = nullptr;
	SelfCollisionBuildGridCS = nullptr;
	SelfCollisionSolverCS = nullptr;
	AllocatedSelfCollisionCells = 0;
	bSelfCollisionInitialized = false;
	
	BoundsComputeBuffer = nullptr;
	BoundsReadbackBuffer = nullptr;
	BoundsComputeUAV = nullptr;
	ComputeBoundsPass1CS = nullptr;
	ComputeBoundsPass2CS = nullptr;
}

FClothBatchedSolver::~FClothBatchedSolver()
{
    Release();
}

void FClothBatchedSolver::Initialize(FGraphicsDevice *InGraphics,
                                     FDXDBufferManager *InBufferManager,
                                     FDXDShaderManager *InShaderManager,
                                     FClothCollisionManager *InCollisionManager)
{
    Graphics = InGraphics;
    BufferManager = InBufferManager;
    ShaderManager = InShaderManager;
    CollisionManager = InCollisionManager; // Assign shared collision manager (not owned)

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
    AreaConstraintSolverCS = nullptr;    
    ApplyDeltasCS = nullptr;
    ComputeKinematicTargetsCS = nullptr; 
    FinalizeCS = nullptr;                
    UpdateNormalsCS = nullptr;
    ComputeTriangleNormalsCS = nullptr;  
    NormalizeVertexNormalsCS = nullptr;  
    CollisionSolverCS = nullptr;
    EdgeCollisionSolverCS = nullptr;     
    SelfCollisionBuildGridCS = nullptr;  
    SelfCollisionSolverCS = nullptr;

    // Do NOT release collision manager - it's shared and owned by ClothWorld
    CollisionManager = nullptr;

    
    SAFE_RELEASE(AttachmentDataBuffer);
    SAFE_RELEASE(AttachmentDataSRV);
    SAFE_RELEASE(ComponentTransformBuffer);
    SAFE_RELEASE(ComponentTransformSRV);

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
    SAFE_RELEASE(UnifiedConstraintUAV); 
    SAFE_RELEASE(UnifiedConstraintSRV);

    SAFE_RELEASE(UnifiedBendConstraintBuffer);
    SAFE_RELEASE(UnifiedBendConstraintUAV); 
    SAFE_RELEASE(UnifiedBendConstraintSRV);

    SAFE_RELEASE(UnifiedAreaConstraintBuffer);
    SAFE_RELEASE(UnifiedAreaConstraintUAV); 
    SAFE_RELEASE(UnifiedAreaConstraintSRV);

    SAFE_RELEASE(UnifiedEdgeCollisionBuffer);
    SAFE_RELEASE(UnifiedEdgeCollisionSRV);

    SAFE_RELEASE(UnifiedKinematicTargetBuffer);
    SAFE_RELEASE(UnifiedKinematicTargetSRV);

    SAFE_RELEASE(UnifiedIndexBuffer);
    SAFE_RELEASE(UnifiedIndexSRV);

    SAFE_RELEASE(UnifiedNormalBuffer);
    SAFE_RELEASE(UnifiedNormalUAV);
    SAFE_RELEASE(UnifiedNormalSRV);

    SAFE_RELEASE(UnifiedNormalAccumulationBuffer);  
    SAFE_RELEASE(UnifiedNormalAccumulationUAV);     

    SAFE_RELEASE(UnifiedPositionDeltaBuffer);
    SAFE_RELEASE(UnifiedPositionDeltaUAV);

    SAFE_RELEASE(UnifiedPositionWeightBuffer);
    SAFE_RELEASE(UnifiedPositionWeightUAV);

    SAFE_RELEASE(InstanceParameterBuffer);
    SAFE_RELEASE(InstanceParameterSRV);

    SAFE_RELEASE(BatchSimConstantBuffer);
    
    
    SAFE_RELEASE(UnifiedRenderVertexBuffer);
    SAFE_RELEASE(UnifiedRenderIndexBuffer);
    SAFE_RELEASE(UnifiedSkinningWeightBuffer);
    SAFE_RELEASE(UnifiedTriangleSkinningWeightBuffer);  
    SAFE_RELEASE(RenderNormalsBuffer);
    SAFE_RELEASE(RenderPositionsBuffer);
    
    SAFE_RELEASE(UnifiedRenderVertexSRV);
    SAFE_RELEASE(UnifiedRenderIndexSRV);
    SAFE_RELEASE(SkinningWeightsSRV);
    SAFE_RELEASE(TriangleSkinningWeightsSRV);  
    SAFE_RELEASE(RenderNormalsUAV);
    SAFE_RELEASE(RenderNormalsSRV);
    SAFE_RELEASE(RenderPositionsSRV);
    
    // Release self-collision buffers
    SAFE_RELEASE(SelfCollisionCellCountersBuffer);
    SAFE_RELEASE(SelfCollisionCellDataBuffer);
    SAFE_RELEASE(SelfCollisionParamsBuffer);
    SAFE_RELEASE(SelfCollisionCellCountersUAV);
    SAFE_RELEASE(SelfCollisionCellDataUAV);
    SAFE_RELEASE(SelfCollisionCellCountersSRV);
    SAFE_RELEASE(SelfCollisionCellDataSRV);
    
    
    SAFE_RELEASE(BoundsComputeBuffer);
    SAFE_RELEASE(BoundsReadbackBuffer);
    SAFE_RELEASE(BoundsComputeUAV);
   
    bInitialized = false;
    bSelfCollisionInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Released resources"));
}

bool FClothBatchedSolver::AllocateBuffers(uint32 MaxParticles, uint32 MaxConstraints,
                                          uint32 MaxBendConstraints, uint32 MaxKinematicTargets,
                                          uint32 MaxTriangles, uint32 MaxInstances,
                                          uint32 MaxAreaConstraints, uint32 MaxEdgeCollisions)
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
    AllocatedAreaConstraintCapacity = MaxAreaConstraints;
    AllocatedEdgeCollisionCapacity = MaxEdgeCollisions;

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

    // Create constraint buffer (needs UAV for XPBD lambda write-back)
    if (MaxConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothDistanceConstraintGPU) * MaxConstraints;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE; 
        bufferDesc.StructureByteStride = sizeof(FClothDistanceConstraintGPU);
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create constraint buffer"));
            return false;
        }

        // Create UAV for XPBD lambda write-back
        uavDesc.Buffer.NumElements = MaxConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedConstraintBuffer, &uavDesc, &UnifiedConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create constraint UAV"));
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

    // Create bend constraint buffer (needs UAV for XPBD lambda write-back)
    if (MaxBendConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothBendConstraintGPU) * MaxBendConstraints;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE; 
        bufferDesc.StructureByteStride = sizeof(FClothBendConstraintGPU);
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedBendConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bend constraint buffer"));
            return false;
        }

        // Create UAV for XPBD lambda write-back
        uavDesc.Buffer.NumElements = MaxBendConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedBendConstraintBuffer, &uavDesc, &UnifiedBendConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bend constraint UAV"));
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

    // Create area constraint buffer (needs UAV for XPBD lambda write-back)
    if (MaxAreaConstraints > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothAreaConstraintGPU) * MaxAreaConstraints;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothAreaConstraintGPU);
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedAreaConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create area constraint buffer"));
            return false;
        }

        // Create UAV for XPBD lambda write-back
        uavDesc.Buffer.NumElements = MaxAreaConstraints;
        hr = Graphics->Device->CreateUnorderedAccessView(UnifiedAreaConstraintBuffer, &uavDesc, &UnifiedAreaConstraintUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create area constraint UAV"));
            return false;
        }

        srvDesc.Buffer.NumElements = MaxAreaConstraints;
        hr = Graphics->Device->CreateShaderResourceView(UnifiedAreaConstraintBuffer, &srvDesc, &UnifiedAreaConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create area constraint SRV"));
            return false;
        }

        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Created area constraint buffer (MaxAreaConstraints: %u)"), MaxAreaConstraints);
    }

    // Create edge collision buffer
    if (MaxEdgeCollisions > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothEdgeCollisionConstraintGPU) * MaxEdgeCollisions;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothEdgeCollisionConstraintGPU);
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedEdgeCollisionBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create edge collision buffer"));
            return false;
        }

        srvDesc.Buffer.NumElements = MaxEdgeCollisions;
        hr = Graphics->Device->CreateShaderResourceView(UnifiedEdgeCollisionBuffer, &srvDesc, &UnifiedEdgeCollisionSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create edge collision SRV"));
            return false;
        }

        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Created edge collision buffer (MaxEdgeCollisions: %u)"), MaxEdgeCollisions);
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
    // This buffer is used both as:
    //   1. Index buffer for rendering (IASetIndexBuffer) - requires D3D11_BIND_INDEX_BUFFER
    //   2. Shader resource for compute shaders (normal computation) - requires D3D11_BIND_SHADER_RESOURCE
    // Cannot use D3D11_RESOURCE_MISC_BUFFER_STRUCTURED with index buffers, so use typed buffer instead
    if (MaxTriangles > 0)
    {
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint32) * MaxTriangles * 3;
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = 0;
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

    // Create normal buffer (final float3 normals)
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(float) * MaxParticles * 3;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(float) * 3;
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

    // Create integer accumulation buffer for atomic normal updates (int3 format)
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(int32) * MaxParticles * 3;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(int32) * 3;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedNormalAccumulationBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create normal accumulation buffer"));
        return false;
    }

    uavDesc.Buffer.NumElements = MaxParticles;
    hr = Graphics->Device->CreateUnorderedAccessView(UnifiedNormalAccumulationBuffer, &uavDesc, &UnifiedNormalAccumulationUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create normal accumulation UAV"));
        return false;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Created normal buffers (float3 + int3 accumulation) for %u particles"), MaxParticles);

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

    
    // These will be allocated on-demand when first render mesh is added
    AllocatedRenderVertexCapacity = 0;
    AllocatedRenderIndexCapacity = 0;
    UsedRenderVertexCount = 0;
    UsedRenderIndexCount = 0;

    // Allocate self-collision buffers if enabled
    if (!AllocateSelfCollisionBuffers())
    {
    	UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to allocate self-collision buffers, disabling self-collision"));
    	Config.bEnableSelfCollision = false;
    }
    
    
    if (!AllocateBoundsComputeBuffers())
    {
    	UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to allocate bounds compute buffers, dynamic bounds disabled"));
    	Config.bEnableDynamicBoundsUpdate = false;
    }
   
    // Mark as fully initialized now that both shaders and buffers are ready
    bInitialized = true;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Allocated buffers - Particles: %d, Constraints: %d, Instances: %d"),
           MaxParticles, MaxConstraints, MaxInstances);

    return true;
}


bool FClothBatchedSolver::AllocateRenderBuffers(uint32 MaxRenderVertices, uint32 MaxRenderIndices)
{
    if (!Graphics || !Graphics->Device)
        return false;

    AllocatedRenderVertexCapacity = MaxRenderVertices;
    AllocatedRenderIndexCapacity = MaxRenderIndices;

    HRESULT hr;
    D3D11_BUFFER_DESC bufferDesc = {};
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    if (MaxRenderVertices > 0)
    {
        bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothRenderVertex) * MaxRenderVertices;
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;  // Only vertex buffer, no SRV needed for now
        bufferDesc.StructureByteStride = 0;  // Not a structured buffer
        bufferDesc.MiscFlags = 0;  // No structured buffer flag

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedRenderVertexBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create render vertex buffer (HRESULT: 0x%08X)"), hr);
            return false;
        }

        // Note: No SRV created for vertex buffer - accessed via Input Assembler stage
        UnifiedRenderVertexSRV = nullptr;
        
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Created render vertex buffer - Vertices: %u, Size: %u bytes"),
               MaxRenderVertices, bufferDesc.ByteWidth);
    }

    // Create unified render index buffer
    if (MaxRenderIndices > 0)
    {
        bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint32) * MaxRenderIndices;
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = 0; // Not a structured buffer
        bufferDesc.MiscFlags = 0;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedRenderIndexBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create render index buffer"));
            return false;
        }

        // Create SRV as typed buffer
        D3D11_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
        indexSrvDesc.Format = DXGI_FORMAT_R32_UINT;
        indexSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        indexSrvDesc.Buffer.FirstElement = 0;
        indexSrvDesc.Buffer.NumElements = MaxRenderIndices;

        hr = Graphics->Device->CreateShaderResourceView(UnifiedRenderIndexBuffer, &indexSrvDesc, &UnifiedRenderIndexSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create render index SRV"));
            return false;
        }
    }

    // Create unified legacy K-nearest neighbor skinning weight buffer
    if (MaxRenderVertices > 0)
    {
        bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothSkinningWeightGPU) * MaxRenderVertices;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothSkinningWeightGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedSkinningWeightBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create legacy skinning weight buffer"));
            return false;
        }

        srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = MaxRenderVertices;

        hr = Graphics->Device->CreateShaderResourceView(UnifiedSkinningWeightBuffer, &srvDesc, &SkinningWeightsSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create legacy skinning weight SRV"));
            return false;
        }
    }
    
    
    if (MaxRenderVertices > 0)
    {
        bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothSkinningWeightTriangleGPU) * MaxRenderVertices;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothSkinningWeightTriangleGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &UnifiedTriangleSkinningWeightBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create triangle skinning weight buffer"));
            return false;
        }

        srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = MaxRenderVertices;

        hr = Graphics->Device->CreateShaderResourceView(UnifiedTriangleSkinningWeightBuffer, &srvDesc, &TriangleSkinningWeightsSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create triangle skinning weight SRV"));
            return false;
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Allocated render buffers - Vertices: %u, Indices: %u (Legacy + Triangle skinning)"),
           MaxRenderVertices, MaxRenderIndices);

    return true;
}

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

    AllocatedSelfCollisionCells = totalCells;

    HRESULT hr;
    D3D11_BUFFER_DESC bufferDesc = {};
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    // Create cell counters buffer (atomic counters)
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(uint32) * totalCells;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(uint32);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &SelfCollisionCellCountersBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision cell counters buffer"));
        return false;
    }

    // Create UAV for cell counters
    uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = totalCells;

    hr = Graphics->Device->CreateUnorderedAccessView(SelfCollisionCellCountersBuffer, &uavDesc, &SelfCollisionCellCountersUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision cell counters UAV"));
        return false;
    }

    // Create SRV for cell counters
    srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = totalCells;

    hr = Graphics->Device->CreateShaderResourceView(SelfCollisionCellCountersBuffer, &srvDesc, &SelfCollisionCellCountersSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision cell counters SRV"));
        return false;
    }

    // Create cell data buffer (flat array of particle indices)
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(uint32) * totalCells * maxPerCell;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(uint32);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &SelfCollisionCellDataBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision cell data buffer"));
        return false;
    }

    // Create UAV for cell data
    uavDesc.Buffer.NumElements = totalCells * maxPerCell;
    hr = Graphics->Device->CreateUnorderedAccessView(SelfCollisionCellDataBuffer, &uavDesc, &SelfCollisionCellDataUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision cell data UAV"));
        return false;
    }

    // Create SRV for cell data
    srvDesc.Buffer.NumElements = totalCells * maxPerCell;
    hr = Graphics->Device->CreateShaderResourceView(SelfCollisionCellDataBuffer, &srvDesc, &SelfCollisionCellDataSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create self-collision cell data SRV"));
        return false;
    }

    // Create constant buffer for self-collision parameters
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

    bSelfCollisionInitialized = true;

    return true;
}

void FClothBatchedSolver::Simulate(float DeltaTime, TArray<FClothInstanceMetadata>& InstanceMetadata)
{
	QUICK_SCOPE_CYCLE_COUNTER(ClothSolver_Simulate);

	if (!bInitialized || !Graphics || !Graphics->DeviceContext)
		return;

	if (UsedParticleCount == 0)
		return;

	// Clamp delta time for stability
	float clampedDT = FMath::Clamp(DeltaTime, 0.0001f, 0.033f);

	// Use fixed substep time from config
	float SubstepTime = Config.TimeStep / Config.NumSubsteps;

	if (CollisionManager && CollisionManager->GetColliderCount() > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(ClothSolver_UpdateCollision);
		CollisionManager->UpdateTransforms();
		CollisionManager->UploadToGPU(Graphics->Device, Graphics->DeviceContext);
	}

	
	if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
	{
		//UpdateDynamicBounds(InstanceMetadata);
		UpdateSelfCollisionParams(InstanceMetadata);
	}

	// Update frame-constant data ONCE
	{
		QUICK_SCOPE_CYCLE_COUNTER(ClothSolver_UpdateFrameConstants);
		UpdateFrameConstants(SubstepTime);
	}

	// === Substep loop ===
	for (int substep = 0; substep < Config.NumSubsteps; substep++)
	{
		QUICK_SCOPE_CYCLE_COUNTER(ClothSolver_Substep);

		// Update ONLY iteration-varying constants (minimal overhead)
		{
			QUICK_SCOPE_CYCLE_COUNTER(ClothSolver_UpdateIterationConstants);
			UpdateIterationConstants(substep);
		}

		SimulateSubstep(SubstepTime);
	}

	// Final normal update (once per frame, not per substep)
	if (UsedTriangleCount > 0)
	{
		DispatchUpdateNormals(UsedTriangleCount);
	}
}

void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    // Apply Force & Position prediction
    DispatchIntegration(UsedParticleCount);

     // Self-collision (after constraint solving, before kinematic targets)
    /*if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
    {
        DispatchSelfCollision(UsedParticleCount);
    }*/
    // Collision (collision data updated once per frame in Simulate, not here)
    DispatchCollisionSDF(UsedParticleCount);
    
    // Edge-based collision (NEW: Prevents edge penetration in low-resolution meshes)
    /*if (UsedEdgeCollisionCount > 0)
    {
        DispatchEdgeCollisionSDF(UsedEdgeCollisionCount);
    }*/

    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {

        if (UsedConstraintCount > 0)
        {
            DispatchConstraintSolver(UsedConstraintCount);
        }

        if (UsedBendConstraintCount > 0)
        {
            //DispatchBendConstraintSolver(UsedBendConstraintCount);
        }

        if (UsedAreaConstraintCount > 0)
        {
            //DispatchAreaConstraintSolver(UsedAreaConstraintCount);
        }

        if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
        {
            DispatchSelfCollision(UsedParticleCount);
        }
        DispatchApplyDeltas(UsedParticleCount);
    }
    
  /*  if (Config.bEnableSelfCollision && bSelfCollisionInitialized)
    {
        DispatchSelfCollision(UsedParticleCount);
    }*/
    // Apply kinematic constraints (NEW: GPU-based computation - P1 optimization)
    // Use GPU-based kinematic target computation if available
    if (ComputeKinematicTargetsCS && AttachmentDataSRV && UsedAttachmentCount > 0)
    {
        // GPU-based method - use attachment count from BuildKinematicAttachmentData
        DispatchComputeKinematicTargets(UsedAttachmentCount);
    }

    // Update Final Position and Velocity of particles
    DispatchFinalize(UsedParticleCount);
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

    // Load or get Area Constraint Solver shader
    hr = ShaderManager->AddComputeShader(L"ClothAreaConstraintSolverCS", L"Shaders/Cloth/ClothAreaConstraintSolver.hlsl", "SolveAreaConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothAreaConstraintSolver shader"));
        bSuccess = false;
    }
    AreaConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothAreaConstraintSolverCS");

    // Load or get Apply Deltas shader
    hr = ShaderManager->AddComputeShader(L"ClothApplyConstraintDeltasCS", L"Shaders/Cloth/ClothApplyDelta.hlsl", "ApplyConstraintDeltasCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothApplyDelta shader"));
        bSuccess = false;
    }
    ApplyDeltasCS = ShaderManager->GetComputeShaderByKey(L"ClothApplyConstraintDeltasCS");

    // Load or get Kinematic Targets  compute shader
    hr = ShaderManager->AddComputeShader(L"ComputeKinematicTargetsCS", L"Shaders/Cloth/ClothComputeKinematicTargets.hlsl", "ComputeKinematicTargetsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothApplyKinematicTargets shader (optional)"));
        // Not critical - kinematic targets are optional
    }
    ComputeKinematicTargetsCS = ShaderManager->GetComputeShaderByKey(L"ComputeKinematicTargetsCS");

    
    hr = ShaderManager->AddComputeShader(L"ClothFinalizeCS", L"Shaders/Cloth/ClothFinalize.hlsl", "FinalizeVelocityCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothFinalize shader"));
        bSuccess = false;
    }
    FinalizeCS = ShaderManager->GetComputeShaderByKey(L"ClothFinalizeCS");

    hr = ShaderManager->AddComputeShader(L"ClothUpdateNormalsCS", L"Shaders/Cloth/ClothUpdateNormals.hlsl", "UpdateNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothUpdateNormals shader (legacy)"));
    }
    UpdateNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothUpdateNormalsCS");
    
    
    hr = ShaderManager->AddComputeShader(L"ClothComputeTriangleNormalsCS", L"Shaders/Cloth/ClothUpdateNormals.hlsl", "ComputeTriangleNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothComputeTriangleNormals shader"));
        bSuccess = false;
    }
    ComputeTriangleNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothComputeTriangleNormalsCS");
    
    
    hr = ShaderManager->AddComputeShader(L"ClothNormalizeVertexNormalsCS", L"Shaders/Cloth/ClothUpdateNormals.hlsl", "NormalizeVertexNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to compile ClothNormalizeVertexNormals shader"));
        bSuccess = false;
    }
    NormalizeVertexNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothNormalizeVertexNormalsCS");

    hr = ShaderManager->AddComputeShader(L"ClothCollisionSDFCS",
                                         L"Shaders/Cloth/ClothSDFCollision.hlsl",
                                         "SolveCollisionsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothSDFCollision shader (optional)"));
    }
    CollisionSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothCollisionSDFCS");

    hr = ShaderManager->AddComputeShader(L"ClothEdgeCollisionSDFCS",
                                         L"Shaders/Cloth/ClothCollisionEdgeSDF.hlsl",
                                         "SolveEdgeCollisionsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothCollisionEdgeSDF shader (optional)"));
    }
    EdgeCollisionSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothEdgeCollisionSDFCS");

    hr = ShaderManager->AddComputeShader(L"ClothComputeKinematicTargetsCS",
                                         L"Shaders/Cloth/ClothComputeKinematicTargets.hlsl",
                                         "ComputeKinematicTargetsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothComputeKinematicTargets shader (optional)"));
    }
    ComputeKinematicTargetsCS = ShaderManager->GetComputeShaderByKey(L"ClothComputeKinematicTargetsCS");

    hr = ShaderManager->AddComputeShader(L"ClothSelfCollisionBuildGridCS",
    									 L"Shaders/Cloth/ClothSelfCollisionBuildGrid.hlsl",
    									 "BuildSpatialHashGridCS");
    if (FAILED(hr))
    {
    	UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothSelfCollisionBuildGrid shader (optional)"));
    }
    SelfCollisionBuildGridCS = ShaderManager->GetComputeShaderByKey(L"ClothSelfCollisionBuildGridCS");
   
    hr = ShaderManager->AddComputeShader(L"ClothSelfCollisionSolverCS",
    									 L"Shaders/Cloth/ClothSelfCollisionSolver.hlsl",
    									 "SolveSelfCollisionsCS");
    if (FAILED(hr))
    {
    	UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothSelfCollisionSolver shader (optional)"));
    }
    SelfCollisionSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothSelfCollisionSolverCS");
    
    
    hr = ShaderManager->AddComputeShader(L"ClothComputeBoundsPass1CS",
    									 L"Shaders/Cloth/ClothComputeBounds.hlsl",
    									 "ComputeBoundsPass1CS");
    if (FAILED(hr))
    {
    	UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothComputeBoundsPass1 shader (optional)"));
    }
    ComputeBoundsPass1CS = ShaderManager->GetComputeShaderByKey(L"ClothComputeBoundsPass1CS");
    
    hr = ShaderManager->AddComputeShader(L"ClothComputeBoundsPass2CS",
    									 L"Shaders/Cloth/ClothComputeBounds.hlsl",
    									 "ComputeBoundsPass2CS");
    if (FAILED(hr))
    {
    	UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothComputeBoundsPass2 shader (optional)"));
    }
    ComputeBoundsPass2CS = ShaderManager->GetComputeShaderByKey(L"ClothComputeBoundsPass2CS");

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
    uint32 Attachments, uint32 Triangles, uint32 Instances, uint32 AreaConstraints, uint32 EdgeCollisions)
{
    UsedParticleCount = Particles;
    UsedConstraintCount = Constraints;
    UsedBendConstraintCount = BendConstraints;
    UsedAttachmentCount = Attachments;
    UsedTriangleCount = Triangles;
    UsedInstanceCount = Instances;
    UsedAreaConstraintCount = AreaConstraints;
    UsedEdgeCollisionCount = EdgeCollisions;
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

void FClothBatchedSolver::UploadEdgeCollisionData(const TArray<FClothEdgeCollisionConstraintGPU> &EdgeCollisions,
                                                   uint32 DestOffset)
{
    if (!Graphics || !Graphics->DeviceContext || EdgeCollisions.Num() == 0)
        return;

    if (!UnifiedEdgeCollisionBuffer)
        return;

    D3D11_BOX destBox;
    destBox.left = DestOffset * sizeof(FClothEdgeCollisionConstraintGPU);
    destBox.right = destBox.left + EdgeCollisions.Num() * sizeof(FClothEdgeCollisionConstraintGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedEdgeCollisionBuffer, 0, &destBox,
                                               EdgeCollisions.GetData(), 0, 0);
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


void FClothBatchedSolver::UploadAttachmentData(const TArray<FKinematicAttachmentGPU> &Attachments)
{
    if (!Graphics || !Graphics->Device || Attachments.Num() == 0)
        return;

    // Create or recreate buffer if needed
    SAFE_RELEASE(AttachmentDataBuffer);
    SAFE_RELEASE(AttachmentDataSRV);

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FKinematicAttachmentGPU) * Attachments.Num();
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FKinematicAttachmentGPU);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = Attachments.GetData();

    HRESULT hr = Graphics->Device->CreateBuffer(&bufferDesc, &initData, &AttachmentDataBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create attachment data buffer"));
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = Attachments.Num();

    hr = Graphics->Device->CreateShaderResourceView(AttachmentDataBuffer, &srvDesc, &AttachmentDataSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create attachment data SRV"));
    }
}

void FClothBatchedSolver::UploadComponentTransforms(const TArray<FMatrix> &Transforms)
{
    if (!Graphics || !Graphics->DeviceContext || Transforms.Num() == 0)
        return;

    // Create buffer on first use
    if (!ComponentTransformBuffer)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(FMatrix) * 512; // Max 512 unique components
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.StructureByteStride = sizeof(FMatrix);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        HRESULT hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &ComponentTransformBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create component transform buffer"));
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = 512;

        hr = Graphics->Device->CreateShaderResourceView(ComponentTransformBuffer, &srvDesc, &ComponentTransformSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create component transform SRV"));
            return;
        }
    }

    // Upload transforms
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(ComponentTransformBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        uint32 bytesToCopy = sizeof(FMatrix) * Transforms.Num();
        memcpy(msr.pData, Transforms.GetData(), bytesToCopy);
        Graphics->DeviceContext->Unmap(ComponentTransformBuffer, 0);
    }
}


void FClothBatchedSolver::UploadRenderMeshData(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& RenderNormals,
    const TArray<FVector2D>& RenderUVs,
    const TArray<uint32>& RenderIndices,
    uint32 RenderVertexOffset,
    uint32 RenderIndexOffset)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    if (RenderPositions.Num() == 0 || RenderIndices.Num() == 0)
        return;

    uint32 numRenderVertices = RenderPositions.Num();
    uint32 numRenderIndices = RenderIndices.Num();

    // CRITICAL FIX: Check if buffers need allocation or reallocation
    uint32 requiredVertexCapacity = RenderVertexOffset + numRenderVertices;
    uint32 requiredIndexCapacity = RenderIndexOffset + numRenderIndices;
    
    bool needsAllocation = !UnifiedRenderVertexBuffer || !UnifiedRenderIndexBuffer;
    bool needsReallocation = requiredVertexCapacity > AllocatedRenderVertexCapacity ||
                             requiredIndexCapacity > AllocatedRenderIndexCapacity;
    
    if (needsAllocation || needsReallocation)
    {
        ID3D11Buffer* oldVertexBuffer = nullptr;
        ID3D11Buffer* oldIndexBuffer = nullptr;
        ID3D11Buffer* oldSkinningBuffer = nullptr;
        ID3D11Buffer* oldTriangleSkinningBuffer = nullptr;
        uint32 oldVertexCapacity = 0;
        uint32 oldIndexCapacity = 0;
        
        if (needsReallocation)
        {
            oldVertexBuffer = UnifiedRenderVertexBuffer;
            oldIndexBuffer = UnifiedRenderIndexBuffer;
            oldSkinningBuffer = UnifiedSkinningWeightBuffer;
            oldTriangleSkinningBuffer = UnifiedTriangleSkinningWeightBuffer;
            oldVertexCapacity = AllocatedRenderVertexCapacity;
            oldIndexCapacity = AllocatedRenderIndexCapacity;
            
            SAFE_RELEASE(UnifiedRenderVertexSRV);
            SAFE_RELEASE(UnifiedRenderIndexSRV);
            SAFE_RELEASE(SkinningWeightsSRV);
            SAFE_RELEASE(TriangleSkinningWeightsSRV);
            
            UnifiedRenderVertexBuffer = nullptr;
            UnifiedRenderIndexBuffer = nullptr;
            UnifiedSkinningWeightBuffer = nullptr;
            UnifiedTriangleSkinningWeightBuffer = nullptr;
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Reallocating render buffers - Old: V=%u I=%u, Required: V=%u I=%u"),
                   oldVertexCapacity, oldIndexCapacity, requiredVertexCapacity, requiredIndexCapacity);
        }
        
        uint32 newVertexCapacity = FMath::Max(requiredVertexCapacity, AllocatedRenderVertexCapacity) * 2;
        uint32 newIndexCapacity = FMath::Max(requiredIndexCapacity, AllocatedRenderIndexCapacity) * 2;
        
        if (!AllocateRenderBuffers(newVertexCapacity, newIndexCapacity))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to allocate render buffers"));
            
            // Restore old buffers on failure
            if (needsReallocation)
            {
                UnifiedRenderVertexBuffer = oldVertexBuffer;
                UnifiedRenderIndexBuffer = oldIndexBuffer;
                UnifiedSkinningWeightBuffer = oldSkinningBuffer;
                UnifiedTriangleSkinningWeightBuffer = oldTriangleSkinningBuffer;
                AllocatedRenderVertexCapacity = oldVertexCapacity;
                AllocatedRenderIndexCapacity = oldIndexCapacity;
            }
            return;
        }
        
        if (needsReallocation && oldVertexBuffer && oldIndexBuffer)
        {
            // Copy vertex buffer data
            if (oldVertexCapacity > 0)
            {
                D3D11_BOX srcBox;
                srcBox.left = 0;
                srcBox.right = oldVertexCapacity * sizeof(FClothRenderVertex);
                srcBox.top = 0;
                srcBox.bottom = 1;
                srcBox.front = 0;
                srcBox.back = 1;
                
                Graphics->DeviceContext->CopySubresourceRegion(
                    UnifiedRenderVertexBuffer,  // Destination (new buffer)
                    0,                          // Dest subresource
                    0, 0, 0,                    // Dest X, Y, Z
                    oldVertexBuffer,            // Source (old buffer)
                    0,                          // Source subresource
                    &srcBox                     // Source box
                );
                
                UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u render vertices from old buffer"),
                       oldVertexCapacity);
            }
            
            // Copy index buffer data
            if (oldIndexCapacity > 0)
            {
                D3D11_BOX srcBox;
                srcBox.left = 0;
                srcBox.right = oldIndexCapacity * sizeof(uint32);
                srcBox.top = 0;
                srcBox.bottom = 1;
                srcBox.front = 0;
                srcBox.back = 1;
                
                Graphics->DeviceContext->CopySubresourceRegion(
                    UnifiedRenderIndexBuffer,   // Destination (new buffer)
                    0,                          // Dest subresource
                    0, 0, 0,                    // Dest X, Y, Z
                    oldIndexBuffer,             // Source (old buffer)
                    0,                          // Source subresource
                    &srcBox                     // Source box
                );
                
                UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u render indices from old buffer"),
                       oldIndexCapacity);
            }
            
            if (oldSkinningBuffer && oldVertexCapacity > 0)
            {
                D3D11_BOX srcBox;
                srcBox.left = 0;
                srcBox.right = oldVertexCapacity * sizeof(FClothSkinningWeightGPU);
                srcBox.top = 0;
                srcBox.bottom = 1;
                srcBox.front = 0;
                srcBox.back = 1;
                
                Graphics->DeviceContext->CopySubresourceRegion(
                    UnifiedSkinningWeightBuffer,
                    0,
                    0, 0, 0,
                    oldSkinningBuffer,
                    0,
                    &srcBox
                );
                
                UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u legacy skinning weights from old buffer"),
                       oldVertexCapacity);
            }
            
            // Copy triangle skinning weight buffer data
            if (oldTriangleSkinningBuffer && oldVertexCapacity > 0)
            {
                D3D11_BOX srcBox;
                srcBox.left = 0;
                srcBox.right = oldVertexCapacity * sizeof(FClothSkinningWeightTriangleGPU);
                srcBox.top = 0;
                srcBox.bottom = 1;
                srcBox.front = 0;
                srcBox.back = 1;
                
                Graphics->DeviceContext->CopySubresourceRegion(
                    UnifiedTriangleSkinningWeightBuffer,
                    0,
                    0, 0, 0,
                    oldTriangleSkinningBuffer,
                    0,
                    &srcBox
                );
                
                UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Copied %u triangle skinning weights from old buffer"),
                       oldVertexCapacity);
            }
            
            SAFE_RELEASE(oldVertexBuffer);
            SAFE_RELEASE(oldIndexBuffer);
            SAFE_RELEASE(oldSkinningBuffer);
            SAFE_RELEASE(oldTriangleSkinningBuffer);
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Buffer reallocation complete - New capacity: V=%u I=%u"),
                   newVertexCapacity, newIndexCapacity);
        }
    }

    // Pack render vertex data
    TArray<FClothRenderVertex> renderVertices;
    renderVertices.SetNum(numRenderVertices);

    for (uint32 i = 0; i < numRenderVertices; ++i)
    {
        renderVertices[i].Position = (i < static_cast<uint32>(RenderPositions.Num())) ? RenderPositions[i] : FVector::ZeroVector;
        renderVertices[i].Normal = (i < static_cast<uint32>(RenderNormals.Num())) ? RenderNormals[i] : FVector(0, 0, 1);
        renderVertices[i].UV = (i < static_cast<uint32>(RenderUVs.Num())) ? RenderUVs[i] : FVector2D::ZeroVector;
    }

    // Upload render vertices
    D3D11_BOX destBox;
    destBox.left = RenderVertexOffset * sizeof(FClothRenderVertex);
    destBox.right = destBox.left + numRenderVertices * sizeof(FClothRenderVertex);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedRenderVertexBuffer, 0, &destBox,
                                               renderVertices.GetData(), 0, 0);

    // Upload render indices
    destBox.left = RenderIndexOffset * sizeof(uint32);
    destBox.right = destBox.left + numRenderIndices * sizeof(uint32);

    Graphics->DeviceContext->UpdateSubresource(UnifiedRenderIndexBuffer, 0, &destBox,
                                               RenderIndices.GetData(), 0, 0);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Uploaded render mesh - Vertices: %u (offset %u), Indices: %u (offset %u)"),
           numRenderVertices, RenderVertexOffset, numRenderIndices, RenderIndexOffset);
}


void FClothBatchedSolver::UploadSkinningWeights(
    const TArray<FClothSkinningWeight>& Weights,
    uint32 RenderVertexOffset)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    if (Weights.Num() == 0)
        return;

    if (!UnifiedSkinningWeightBuffer)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Skinning weight buffer not allocated"));
        return;
    }

    uint32 numWeights = Weights.Num();

    // Convert CPU skinning weights to GPU format
    TArray<FClothSkinningWeightGPU> gpuWeights;
    gpuWeights.SetNum(numWeights);

    for (uint32 i = 0; i < numWeights; ++i)
    {
        const FClothSkinningWeight& cpuWeight = Weights[i];
        FClothSkinningWeightGPU& gpuWeight = gpuWeights[i];

        // Copy indices and weights directly (indices are already global in batched mode)
        for (int j = 0; j < 4; ++j)
        {
            gpuWeight.SimVertexIndices[j] = cpuWeight.SimVertexIndices[j];
            gpuWeight.Weights[j] = cpuWeight.Weights[j];
        }
    }

    // Upload to GPU
    D3D11_BOX destBox;
    destBox.left = RenderVertexOffset * sizeof(FClothSkinningWeightGPU);
    destBox.right = destBox.left + numWeights * sizeof(FClothSkinningWeightGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedSkinningWeightBuffer, 0, &destBox,
                                               gpuWeights.GetData(), 0, 0);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Uploaded legacy skinning weights - Count: %u (offset %u)"),
           numWeights, RenderVertexOffset);
}


void FClothBatchedSolver::UploadTriangleSkinningWeights(
    const TArray<FClothSkinningWeightTriangle>& Weights,
    uint32 RenderVertexOffset)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    if (Weights.Num() == 0)
        return;

    if (!UnifiedTriangleSkinningWeightBuffer)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Triangle skinning weight buffer not allocated"));
        return;
    }

    uint32 numWeights = Weights.Num();

    // Convert CPU triangle skinning weights to GPU format
    TArray<FClothSkinningWeightTriangleGPU> gpuWeights;
    gpuWeights.SetNum(numWeights);

    for (uint32 i = 0; i < numWeights; ++i)
    {
        const FClothSkinningWeightTriangle& cpuWeight = Weights[i];
        FClothSkinningWeightTriangleGPU& gpuWeight = gpuWeights[i];

        // Copy triangle indices
        gpuWeight.SimTriangleIndices[0] = cpuWeight.SimTriangleIndices[0];
        gpuWeight.SimTriangleIndices[1] = cpuWeight.SimTriangleIndices[1];
        gpuWeight.SimTriangleIndices[2] = cpuWeight.SimTriangleIndices[2];
        
        // Copy barycentric coordinates
        gpuWeight.BarycentricCoords[0] = cpuWeight.BarycentricCoords[0];
        gpuWeight.BarycentricCoords[1] = cpuWeight.BarycentricCoords[1];
        gpuWeight.BarycentricCoords[2] = cpuWeight.BarycentricCoords[2];
        
        // Copy tangent-space offset
        gpuWeight.TangentSpaceOffset = cpuWeight.TangentSpaceOffset;
    }

    // Upload to GPU
    D3D11_BOX destBox;
    destBox.left = RenderVertexOffset * sizeof(FClothSkinningWeightTriangleGPU);
    destBox.right = destBox.left + numWeights * sizeof(FClothSkinningWeightTriangleGPU);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    Graphics->DeviceContext->UpdateSubresource(UnifiedTriangleSkinningWeightBuffer, 0, &destBox,
                                               gpuWeights.GetData(), 0, 0);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Uploaded triangle skinning weights - Count: %u (offset %u)"),
           numWeights, RenderVertexOffset);
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

    ID3D11ShaderResourceView *srvs[3] = {
        UnifiedPredictedSRV,
        UnifiedInvMassSRV,
        InstanceParameterSRV};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, srvs);

    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedConstraintUAV,
        UnifiedPositionDeltaUAV,
        UnifiedPositionWeightUAV
    };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ConstraintSolverCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(ConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, nullSRVs);
}

void FClothBatchedSolver::DispatchBendConstraintSolver(uint32 BendConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !BendConstraintSolverCS || BendConstraintCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    ID3D11ShaderResourceView *srvs[4] = {
        UnifiedPredictedSRV,   // t0
        nullptr,               // t1 (unused - constraint is now UAV)
        UnifiedInvMassSRV,     // t2
        InstanceParameterSRV}; // t3
    Graphics->DeviceContext->CSSetShaderResources(0, 4, srvs);

    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedBendConstraintUAV,  // u0: NEW - for lambda write-back
        UnifiedPositionDeltaUAV,   // u1
        UnifiedPositionWeightUAV}; // u2
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(BendConstraintSolverCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(BendConstraintCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchAreaConstraintSolver(uint32 AreaConstraintCount)
{
    if (!Graphics || !Graphics->DeviceContext || !AreaConstraintSolverCS || AreaConstraintCount == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    ID3D11ShaderResourceView *srvs[3] = {
        UnifiedPredictedSRV,
        UnifiedInvMassSRV,
        InstanceParameterSRV};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, srvs);

    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedAreaConstraintUAV,
        UnifiedPositionDeltaUAV,
        UnifiedPositionWeightUAV};
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

void FClothBatchedSolver::DispatchComputeKinematicTargets(uint32 AttachmentCount)
{
    if (!Graphics || !Graphics->DeviceContext || !ComputeKinematicTargetsCS || AttachmentCount == 0)
        return;

    if (!AttachmentDataSRV)
        return;

    // Set shader
    Graphics->DeviceContext->CSSetShader(ComputeKinematicTargetsCS, nullptr, 0);

    // Bind SRVs
    ID3D11ShaderResourceView *srvs[3] = {
        AttachmentDataSRV,     // t0: Attachment data
        ComponentTransformSRV, // t1: Component transforms
        UnifiedInvMassSRV      // t2: Inv mass
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 3, srvs);

    // Bind UAV (modify particles in-place)
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UnifiedPredictedUAV, nullptr);

    // Dispatch
    uint32 NumThreadGroups = (AttachmentCount + 255) / 256;
    Graphics->DeviceContext->Dispatch(NumThreadGroups, 1, 1);

    // Unbind
    ID3D11ShaderResourceView *nullSRVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 3, nullSRVs);
    ID3D11UnorderedAccessView *nullUAVs[] = {nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
}

void FClothBatchedSolver::DispatchFinalize(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !FinalizeCS || ParticleCount == 0)
        return;

    // Bind constant buffer (contains DeltaTime, MaxSpeed, Damping)
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

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

void FClothBatchedSolver::DispatchUpdateNormals(uint32 TriangleCount)
{
    if (!Graphics || !Graphics->DeviceContext || TriangleCount == 0)
        return;
    
    // Use two-pass approach with integer accumulation if shaders are available
    if (ComputeTriangleNormalsCS && NormalizeVertexNormalsCS)
    {
        UINT clearValue[4] = { 0, 0, 0, 0 };
        Graphics->DeviceContext->ClearUnorderedAccessViewUint(UnifiedNormalAccumulationUAV, clearValue);
        
        {
            // Bind constant buffer (contains NumParticles, NumConstraints stores triangle count)
            Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);
            
            // Bind SRVs - use position buffer (final positions after finalize)
            ID3D11ShaderResourceView *srvs[] = {
                UnifiedPositionSRV, // t0: Final positions
                UnifiedIndexSRV     // t1: Index buffer
            };
            Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
            
            // Bind UAVs: u0 = integer accumulation buffer, u1 = final normal buffer (not used in pass 1)
            ID3D11UnorderedAccessView *uavs[] = {
                UnifiedNormalAccumulationUAV,  // u0: Integer accumulation (write with InterlockedAdd)
                UnifiedNormalUAV               // u1: Final normals (not used in pass 1, but bound for consistency)
            };
            Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
            
            // Set Pass 1 shader
            Graphics->DeviceContext->CSSetShader(ComputeTriangleNormalsCS, nullptr, 0);
            
            // Dispatch (one thread per triangle)
            uint32 dispatchCount = GetDispatchCount(TriangleCount, 256);
            Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
            
            // Unbind UAVs before next pass (required for proper synchronization)
            ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
            Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
            ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
            Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
        }
        
        {
            // Bind constant buffer (contains NumParticles)
            Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);
            
            // Bind UAVs: u0 = integer accumulation (read), u1 = final normals (write)
            ID3D11UnorderedAccessView *uavs[] = {
                UnifiedNormalAccumulationUAV,  // u0: Read accumulated integers
                UnifiedNormalUAV               // u1: Write final normalized float3 normals
            };
            Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
            
            // Set Pass 2 shader
            Graphics->DeviceContext->CSSetShader(NormalizeVertexNormalsCS, nullptr, 0);
            
            // Dispatch (one thread per vertex)
            uint32 dispatchCount = GetDispatchCount(UsedParticleCount, 256);
            Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
            
            // Unbind
            ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
            Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
        }
    }
    else if (UpdateNormalsCS)
    {
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
}

void FClothBatchedSolver::DispatchCollisionSDF(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !CollisionSolverCS || ParticleCount == 0)
        return;

    if (!CollisionManager || CollisionManager->GetColliderCount() == 0)
        return;

    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    ID3D11ShaderResourceView *srvs[4] = {
        CollisionManager->GetColliderBufferSRV(), // t0
        UnifiedInvMassSRV,                        // t1
        UnifiedPositionSRV,                       // t2 - Previous frame positions
        InstanceParameterSRV                      // t3 - Instance parameters
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 4, srvs);

    ID3D11UnorderedAccessView *uavs[] = {UnifiedPredictedUAV};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(CollisionSolverCS, nullptr, 0);

    // Dispatch (one thread per particle)
    uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[] = {nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchEdgeCollisionSDF(uint32 EdgeCollisionCount)
{
    if (!Graphics || !Graphics->DeviceContext || !EdgeCollisionSolverCS || EdgeCollisionCount == 0)
        return;

    if (!CollisionManager || CollisionManager->GetColliderCount() == 0)
        return;

    if (!Config.bEnableEdgeCollision)
        return;

    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);

    ID3D11ShaderResourceView *srvs[4] = {
        UnifiedEdgeCollisionSRV,              // t0
        CollisionManager->GetColliderBufferSRV(), // t1
        UnifiedInvMassSRV,                    // t2
        UnifiedPredictedSRV                   // t3
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 4, srvs);

    ID3D11UnorderedAccessView *uavs[] = {
        UnifiedPositionDeltaUAV,
        UnifiedPositionWeightUAV
    };
    UINT initialCounts[2] = {0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(EdgeCollisionSolverCS, nullptr, 0);

    // Dispatch (one thread per edge)
    uint32 dispatchCount = GetDispatchCount(EdgeCollisionCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
}

void FClothBatchedSolver::DispatchSelfCollision(uint32 ParticleCount)
{
	if (!Graphics || !Graphics->DeviceContext || ParticleCount == 0)
		return;
	
	if (!SelfCollisionBuildGridCS || !SelfCollisionSolverCS)
		return;
	
	if (!bSelfCollisionInitialized)
		return;
	
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
    
    {
        // Bind shader
        Graphics->DeviceContext->CSSetShader(SelfCollisionSolverCS, nullptr, 0);
        
        // Bind constant buffers
        ID3D11Buffer* cbs[] = {BatchSimConstantBuffer, SelfCollisionParamsBuffer};
        Graphics->DeviceContext->CSSetConstantBuffers(0, 2, cbs);
        
        // Bind SRVs
        ID3D11ShaderResourceView* srvs[] = {
            UnifiedPredictedSRV,              // t0: Predicted positions
            UnifiedInvMassSRV,                // t1: Inverse masses
            SelfCollisionCellCountersSRV,    // t2: Cell counters
            SelfCollisionCellDataSRV,         // t3: Cell data
            UnifiedIndexSRV,                  // t4: Indices (for topology check)
            UnifiedPositionSRV,               // t5: Previous positions (for displacement)
            InstanceParameterSRV              // t6: Instance parameters (for friction)
        };
        Graphics->DeviceContext->CSSetShaderResources(0, 7, srvs);
        
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
        ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 7, nullSRVs);
    }
    
    //DispatchApplyDeltas(ParticleCount);
}

void FClothBatchedSolver::UpdateSelfCollisionParams(const TArray<FClothInstanceMetadata>& InstanceMetadata)
{
	if (!Graphics || !Graphics->DeviceContext || !SelfCollisionParamsBuffer)
		return;
	
	if (InstanceMetadata.Num() == 0)
		return;
	
	TArray<float> avgEdgeLengths;
	TArray<float> cellSizes;
	TArray<float> collisionRadii;
	FVector globalMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector globalMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	
	// Collect parameters from all active instances
	for (const FClothInstanceMetadata& meta : InstanceMetadata)
	{
		if (!meta.bIsActive || meta.AvgEdgeLength == 0.0f)
			continue;
		
		avgEdgeLengths.Add(meta.AvgEdgeLength);
		cellSizes.Add(meta.AdaptiveCellSize);
		collisionRadii.Add(meta.AdaptiveCollisionRadius);
		
		// Expand global bounds
		globalMin.X = FMath::Min(globalMin.X, meta.MeshBoundsMin.X);
		globalMin.Y = FMath::Min(globalMin.Y, meta.MeshBoundsMin.Y);
		globalMin.Z = FMath::Min(globalMin.Z, meta.MeshBoundsMin.Z);
		
		globalMax.X = FMath::Max(globalMax.X, meta.MeshBoundsMax.X);
		globalMax.Y = FMath::Max(globalMax.Y, meta.MeshBoundsMax.Y);
		globalMax.Z = FMath::Max(globalMax.Z, meta.MeshBoundsMax.Z);
	}
	
	if (avgEdgeLengths.Num() == 0)
	{
		UE_LOG(ELogLevel::Warning, TEXT("Self-Collision: No active cloth instances with valid parameters"));
		return;
	}
	
	avgEdgeLengths.Sort();
	cellSizes.Sort();
	collisionRadii.Sort();
	
	int32 medianIdx = avgEdgeLengths.Num() / 2;
	float medianCellSize = cellSizes[medianIdx];
	float medianCollisionRadius = collisionRadii[medianIdx];
	
	// Apply multipliers from config (artist control)
	float finalCellSize = medianCellSize * Config.SelfCollisionCellSizeMultiplier;
	float finalCollisionRadius = medianCollisionRadius * Config.SelfCollisionRadiusMultiplier;
	
	// Add margin to global bounds
	float margin = finalCellSize * 2.0f;
	FVector gridMin = globalMin - FVector(margin, margin, margin);
	FVector gridMax = globalMax + FVector(margin, margin, margin);
	
	// Compute grid dimensions
	FVector extent = gridMax - gridMin;
	uint32 gridDimX = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.X / finalCellSize)));
	uint32 gridDimY = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Y / finalCellSize)));
	uint32 gridDimZ = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Z / finalCellSize)));
	
	// Clamp to reasonable limits
	const uint32 MaxGridDim = 128;
	gridDimX = FMath::Min(gridDimX, MaxGridDim);
	gridDimY = FMath::Min(gridDimY, MaxGridDim);
	gridDimZ = FMath::Min(gridDimZ, MaxGridDim);
	
	// Fill params
	SelfCollisionParams.GridMin = gridMin;
	SelfCollisionParams.CellSize = finalCellSize;
	SelfCollisionParams.GridDimX = gridDimX;
	SelfCollisionParams.GridDimY = gridDimY;
	SelfCollisionParams.GridDimZ = gridDimZ;
	SelfCollisionParams.MaxParticlesPerCell = Config.SelfCollisionMaxPerCell;
	SelfCollisionParams.CollisionRadius = finalCollisionRadius;
	SelfCollisionParams.CollisionStiffness = Config.SelfCollisionStiffness * Config.SelfCollisionStiffnessMultiplier;
	SelfCollisionParams.bEnableSelfCollision = Config.bEnableSelfCollision ? 1 : 0;
	SelfCollisionParams.Padding = 0;
	
	// Upload to GPU
	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hr = Graphics->DeviceContext->Map(SelfCollisionParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if (SUCCEEDED(hr))
	{
		memcpy(msr.pData, &SelfCollisionParams, sizeof(FClothSelfCollisionParams));
		Graphics->DeviceContext->Unmap(SelfCollisionParamsBuffer, 0);
	}
	
	bool bValid = FClothMeshAnalysis::ValidateSelfCollisionSetup(
		SelfCollisionParams.CellSize,
		SelfCollisionParams.CollisionRadius,
		SelfCollisionParams.GridMin,
		gridMax,
		SelfCollisionParams.GridDimX,
		SelfCollisionParams.GridDimY,
		SelfCollisionParams.GridDimZ,
		SelfCollisionParams.MaxParticlesPerCell,
		UsedParticleCount);
	
	if (!bValid)
	{
		UE_LOG(ELogLevel::Warning,
			TEXT("Self-Collision: Setup has issues. Consider adjusting multipliers."));
	}
}

void FClothBatchedSolver::UpdateFrameConstants(float DeltaTime)
{
    // Build and cache constants that DON'T change between substeps
    if (!Graphics || !Graphics->DeviceContext || !BatchSimConstantBuffer)
        return;

    CachedConstants.NumParticles = UsedParticleCount;
    CachedConstants.NumConstraints = UsedConstraintCount;
    CachedConstants.NumBendConstraints = UsedBendConstraintCount;
    CachedConstants.NumKinematicTargets = UsedAttachmentCount;
    CachedConstants.DeltaTime = DeltaTime;
    CachedConstants.Damping = Config.Damping;
    CachedConstants.Gravity = Config.Gravity;
    CachedConstants.StretchStiffness = Config.StretchStiffness;
    CachedConstants.Wind = FVector::ZeroVector;
    CachedConstants.BendStiffness = Config.BendStiffness;
    CachedConstants.AirDrag = Config.AirDrag;
    CachedConstants.NumIterations = Config.NumIterations;
    CachedConstants.UseXPBD = Config.bUseXPBD ? 1 : 0;
    CachedConstants.RelaxationFactor = Config.RelaxationFactor;
    CachedConstants.MaxSpeed = Config.MaxSpeed;
    CachedConstants.LongRangeStretchiness = Config.LongRangeStretchiness;
    CachedConstants.NumColliders = CollisionManager ? CollisionManager->GetColliderCount() : 0;
    CachedConstants.CollisionThickness = Config.CollisionThickness;
    CachedConstants.CollisionFriction = Config.CollisionFriction;
    CachedConstants.NumAreaConstraints = UsedAreaConstraintCount;
    CachedConstants.AreaStiffness = 1.0f; // Global area stiffness multiplier
    CachedConstants.NumEdgeCollisions = UsedEdgeCollisionCount;
    CachedConstants.EdgeSamplesPerEdge = static_cast<uint32>(Config.EdgeSamplesPerEdge);
    CachedConstants.WorldMatrix = FMatrix::Identity;
    CachedConstants.CurrentIteration = 0; // Will be updated per iteration

    // Upload to GPU
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(BatchSimConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &CachedConstants, sizeof(FClothSimConstants));
        Graphics->DeviceContext->Unmap(BatchSimConstantBuffer, 0);
    }
}

void FClothBatchedSolver::UpdateIterationConstants(int32 CurrentIteration)
{
    // Update cached constants and re-upload (constant buffers don't support partial updates)
    if (!Graphics || !Graphics->DeviceContext || !BatchSimConstantBuffer)
        return;

    // Update only the iteration field in cached structure
    CachedConstants.CurrentIteration = static_cast<uint32>(CurrentIteration);

    // Re-upload full buffer (required for constant buffers)
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(BatchSimConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &CachedConstants, sizeof(FClothSimConstants));
        Graphics->DeviceContext->Unmap(BatchSimConstantBuffer, 0);
    }
}

bool FClothBatchedSolver::CreateGPUResources()
{
	// Handled by AllocateBuffers
	return true;
}


bool FClothBatchedSolver::AllocateBoundsComputeBuffers()
{
	if (!Graphics || !Graphics->Device)
		return false;
	
	// Calculate number of thread groups needed (256 threads per group)
	uint32 maxThreadGroups = (AllocatedParticleCapacity + 255) / 256;
	
	HRESULT hr;
	D3D11_BUFFER_DESC bufferDesc = {};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	
	// Create intermediate bounds buffer (one bound per thread group)
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(FClothBoundsGPU) * maxThreadGroups;
	bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	bufferDesc.StructureByteStride = sizeof(FClothBoundsGPU);
	bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	
	hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &BoundsComputeBuffer);
	if (FAILED(hr))
	{
		UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bounds compute buffer"));
		return false;
	}
	
	// Create UAV for bounds buffer
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.NumElements = maxThreadGroups;
	
	hr = Graphics->Device->CreateUnorderedAccessView(BoundsComputeBuffer, &uavDesc, &BoundsComputeUAV);
	if (FAILED(hr))
	{
		UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bounds compute UAV"));
		return false;
	}
	
	// Create staging buffer for CPU readback
	bufferDesc.Usage = D3D11_USAGE_STAGING;
	bufferDesc.ByteWidth = sizeof(FClothBoundsGPU);
	bufferDesc.BindFlags = 0;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	bufferDesc.MiscFlags = 0;
	
	hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &BoundsReadbackBuffer);
	if (FAILED(hr))
	{
		UE_LOG(ELogLevel::Error, TEXT("ClothBatchedSolver: Failed to create bounds readback buffer"));
		return false;
	}
	
	UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Allocated GPU bounds compute buffers - ThreadGroups: %u"),
		   maxThreadGroups);
	
	return true;
}


void FClothBatchedSolver::DispatchComputeBounds(uint32 ParticleCount)
{
	if (!Graphics || !Graphics->DeviceContext || ParticleCount == 0)
		return;
	
	if (!ComputeBoundsPass1CS || !ComputeBoundsPass2CS)
		return;
	
	{
		Graphics->DeviceContext->CSSetShader(ComputeBoundsPass1CS, nullptr, 0);
		Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);
		
		ID3D11ShaderResourceView* srvs[] = {UnifiedPositionSRV, UnifiedInvMassSRV};
		Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
		
		Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &BoundsComputeUAV, nullptr);
		
		uint32 numGroups = (ParticleCount + 255) / 256;
		Graphics->DeviceContext->Dispatch(numGroups, 1, 1);
		
		// Unbind
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
		ID3D11ShaderResourceView* nullSRVs[] = {nullptr, nullptr};
		Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
	}
	
	{
		Graphics->DeviceContext->CSSetShader(ComputeBoundsPass2CS, nullptr, 0);
		Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);
		
		Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &BoundsComputeUAV, nullptr);
		
		Graphics->DeviceContext->Dispatch(1, 1, 1);
		
		// Unbind
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}
}


void FClothBatchedSolver::ReadbackBounds(FVector& OutMin, FVector& OutMax)
{
	if (!Graphics || !Graphics->DeviceContext || !BoundsComputeBuffer || !BoundsReadbackBuffer)
		return;
	
	// Copy GPU bounds to staging buffer
	Graphics->DeviceContext->CopyResource(BoundsReadbackBuffer, BoundsComputeBuffer);
	
	// Map and read
	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hr = Graphics->DeviceContext->Map(BoundsReadbackBuffer, 0, D3D11_MAP_READ, 0, &msr);
	if (SUCCEEDED(hr))
	{
		FClothBoundsGPU* bounds = static_cast<FClothBoundsGPU*>(msr.pData);
		OutMin = bounds->BoundsMin;
		OutMax = bounds->BoundsMax;
		Graphics->DeviceContext->Unmap(BoundsReadbackBuffer, 0);
	}
}

void FClothBatchedSolver::UpdateDynamicBounds(TArray<FClothInstanceMetadata>& InstanceMetadata)
{
	if (!Config.bEnableDynamicBoundsUpdate)
		return;
	
	if (!ComputeBoundsPass1CS || !ComputeBoundsPass2CS || !BoundsComputeBuffer)
		return;
	
	bool bAnyInstanceNeedsUpdate = false;
	
	// Check all instances for update conditions
	for (FClothInstanceMetadata& meta : InstanceMetadata)
	{
		if (!meta.bIsActive)
			continue;
		
		meta.FramesSinceLastBoundsUpdate++;
		
		// Check if forced update needed (periodic)
		if (meta.FramesSinceLastBoundsUpdate >= Config.BoundsUpdateMaxFrames)
		{
			meta.bNeedsBoundsUpdate = true;
			bAnyInstanceNeedsUpdate = true;
		}
	}
	
	// If any instance needs update, compute new global bounds via GPU
	if (bAnyInstanceNeedsUpdate)
	{
		// Dispatch GPU bounds computation (all particles)
		DispatchComputeBounds(UsedParticleCount);
		
		// Readback computed bounds from GPU
		FVector newGlobalMin, newGlobalMax;
		ReadbackBounds(newGlobalMin, newGlobalMax);
		
		// Update all active instances with new bounds
		// NOTE: Using global bounds for all instances (conservative approach)
		// Future: Compute per-instance bounds for more accurate tracking
		for (FClothInstanceMetadata& meta : InstanceMetadata)
		{
			if (!meta.bIsActive)
				continue;
			
			if (meta.bNeedsBoundsUpdate)
			{
				// Calculate motion as percentage of current bounds size
				FVector currentExtent = meta.MeshBoundsMax - meta.MeshBoundsMin;
				FVector displacement = FVector::GetAbs((newGlobalMin - meta.MeshBoundsMin)) +
									   FVector::GetAbs((newGlobalMax - meta.MeshBoundsMax));
				
				float maxDisplacement = FMath::Max3(
					displacement.X / FMath::Max(currentExtent.X, 0.01f),
					displacement.Y / FMath::Max(currentExtent.Y, 0.01f),
					displacement.Z / FMath::Max(currentExtent.Z, 0.01f));
				
				meta.AccumulatedMotion = maxDisplacement;
				
				// Update bounds
				meta.PrevBoundsMin = meta.MeshBoundsMin;
				meta.PrevBoundsMax = meta.MeshBoundsMax;
				meta.MeshBoundsMin = newGlobalMin;
				meta.MeshBoundsMax = newGlobalMax;
				
				// Recompute grid parameters with new bounds
				FVector gridMin, gridMax;
				FClothMeshAnalysis::ComputeAdaptiveSpatialHashParams(
					meta.AvgEdgeLength,
					meta.MeshBoundsMin,
					meta.MeshBoundsMax,
					meta.ParticleCount,
					meta.AdaptiveCellSize,
					meta.AdaptiveCollisionRadius,
					gridMin,
					gridMax,
					meta.AdaptiveGridDimX,
					meta.AdaptiveGridDimY,
					meta.AdaptiveGridDimZ,
					meta.AdaptiveMaxPerCell);
				
				// Reset tracking state
				meta.FramesSinceLastBoundsUpdate = 0;
				meta.AccumulatedMotion = 0.0f;
				meta.bNeedsBoundsUpdate = false;
				
				UE_LOG(ELogLevel::Display,
					TEXT("Cloth Instance: Bounds updated (Motion=%.2f%%, Periodic update after %u frames)"),
					maxDisplacement * 100.0f, Config.BoundsUpdateMaxFrames);
			}
		}
	}
}
