/**
 * Cloth Solver Implementation
 * GPU-based cloth simulation using Position-Based Dynamics
 */

#include "ClothSolver.h"

#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"
#include "Engine/UserInterface/Console.h"
#include "Core/Math/MathUtility.h"
#include "ShaderConstants.h"
#include "Classes/Engine/ClothAsset.h"

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

FClothSolver::FClothSolver()
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), IntegrateCS(nullptr), ConstraintSolverCS(nullptr), UpdateNormalsCS(nullptr), ClearNormalsCS(nullptr), NormalizeNormalsCS(nullptr), ClothSimConstantBuffer(nullptr), NormalUpdateConstantBuffer(nullptr), CurrentBufferIndex(0), NumParticles(0), NumConstraints(0), NumTriangles(0), bInitialized(false), ExternalForceAccum(FVector::ZeroVector)
{
    // Initialize all buffer pointers to nullptr
    for (int32 i = 0; i < 2; ++i)
    {
        PositionBuffer[i] = nullptr;
        PositionUAV[i] = nullptr;
        PositionSRV[i] = nullptr;
    }

    VelocityBuffer = nullptr;
    InvMassBuffer = nullptr;
    ConstraintBuffer = nullptr;
    IndexBuffer = nullptr;
    NormalBuffer = nullptr;

    VelocityUAV = nullptr;
    NormalUAV = nullptr;

    VelocitySRV = nullptr;
    ConstraintSRV = nullptr;
    IndexSRV = nullptr;
    NormalSRV = nullptr;

#ifdef CUDA_ENABLED
    // CUDA members
    CudaInterop = nullptr;

    PositionCudaResource[0] = nullptr;
    PositionCudaResource[1] = nullptr;
    VelocityCudaResource = nullptr;
    NormalCudaResource = nullptr;

    PositionDevicePtr[0] = nullptr;
    PositionDevicePtr[1] = nullptr;
    VelocityDevicePtr = nullptr;
    NormalDevicePtr = nullptr;

    PositionDeltasDevice = nullptr;
    PositionWeightsDevice = nullptr;
    ConstraintDevicePtr = nullptr;
    IndicesDevicePtr = nullptr;
    ConstantsDevicePtr = nullptr;

    bUseCUDA = false;
#endif
}

FClothSolver::~FClothSolver()
{
    Release();
}

void FClothSolver::Initialize(FGraphicsDevice *InGraphics, FDXDBufferManager *InBufferManager, FDXDShaderManager *InShaderManager)
{
    Graphics = InGraphics;
    BufferManager = InBufferManager;
    ShaderManager = InShaderManager;

    if (!Graphics || !BufferManager || !ShaderManager)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Invalid initialization parameters"));
        return;
    }

#ifdef CUDA_ENABLED
    CudaInterop = new FCUDADXInterop();
    if (CudaInterop->Initialize(Graphics->Device))
    {
        bUseCUDA = true;
        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: CUDA-DX11 interop initialized successfully"));
        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Using CUDA backend for cloth simulation"));
    }
    else
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothSolver: Failed to initialize CUDA interop, falling back to DX11 compute"));
        delete CudaInterop;
        CudaInterop = nullptr;
        bUseCUDA = false;
    }
#endif

    // Load compute shaders (DX11 fallback path)
    if (!LoadComputeShaders())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to load compute shaders"));
#ifdef CUDA_ENABLED
        if (!bUseCUDA)
        {
            // No fallback available
            return;
        }
#else
        return;
#endif
    }
    UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Initialized successfully"));
}

bool FClothSolver::SetupFromAsset(UClothAsset *InAsset, const FClothConfig &InConfig)
{
    if (!InAsset)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Invalid cloth asset"));
        return false;
    }

    // Release existing resources
    Release();

    Config = InConfig;

    if (!InAsset->IsValid())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Cloth asset is not valid"));
        return false;
    }

    // ===== TODO 영역 채움: Asset 데이터 복사 =====
    // 기본 LOD (필요하다면 LOD 선택 로직을 나중에 추가)
    const TArray<FVector> &AssetRestPositions = InAsset->GetRestPositions();
    const TArray<uint32> &AssetIndices = InAsset->GetIndices();
    const TArray<float> &AssetInvMasses = InAsset->GetInvMasses();
    const TArray<FClothConstraint> &AssetDistConstraints = InAsset->GetDistanceConstraints();
    const TArray<FClothConstraint> &AssetBendConstraints = InAsset->GetBendConstraints();

    RestPositions = AssetRestPositions;
    Indices = AssetIndices;
    InvMasses = AssetInvMasses;

    Constraints.Empty();
    Constraints.Append(AssetDistConstraints);
    Constraints.Append(AssetBendConstraints);

    // TODO : Attachment, VertexPaint
    // AttachmentIndices = InAsset->GetAttachmentIndices();
    // VertexPaintData = InAsset->GetVertexPaintData();

    NumParticles = RestPositions.Num();
    NumConstraints = AssetDistConstraints.Num() + AssetBendConstraints.Num();
    NumTriangles = Indices.Num() / 3;

    if (NumParticles == 0)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: No particles in cloth asset"));
        return false;
    }

    SimData.NumParticles = NumParticles;
    SimData.NumConstraints = NumConstraints;

    SimData.CurrentPositions.SetNum(NumParticles);
    SimData.CurrentVelocities.SetNum(NumParticles);

    for (int32 i = 0; i < NumParticles; ++i)
    {
        SimData.CurrentPositions[i] = RestPositions[i];
        SimData.CurrentVelocities[i] = FVector::ZeroVector;
    }

    // Create GPU resources
    if (!CreateGPUResources())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to create GPU resources"));
        Release();
        return false;
    }

    // Upload initial data
    if (!UploadInitialData())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to upload initial data"));
        Release();
        return false;
    }

#ifdef CUDA_ENABLED
    // Setup CUDA resources
    if (bUseCUDA && CudaInterop)
    {
        // Register D3D11 buffers with CUDA
        if (!CudaInterop->RegisterD3DBuffer(PositionBuffer[0], &PositionCudaResource[0]))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to register position buffer 0 with CUDA"));
            bUseCUDA = false;
        }
        if (!CudaInterop->RegisterD3DBuffer(PositionBuffer[1], &PositionCudaResource[1]))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to register position buffer 1 with CUDA"));
            bUseCUDA = false;
        }
        if (!CudaInterop->RegisterD3DBuffer(VelocityBuffer, &VelocityCudaResource))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to register velocity buffer with CUDA"));
            bUseCUDA = false;
        }
        if (!CudaInterop->RegisterD3DBuffer(NormalBuffer, &NormalCudaResource))
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to register normal buffer with CUDA"));
            bUseCUDA = false;
        }

        if (bUseCUDA)
        {
            // Allocate CUDA-only buffers (not shared with D3D11)
            cudaMalloc(&PositionDeltasDevice, sizeof(FVector) * NumParticles);
            cudaMalloc(&PositionWeightsDevice, sizeof(float) * NumParticles);
            cudaMalloc(&ConstraintDevicePtr, sizeof(FClothConstraintGPU) * NumConstraints);
            cudaMalloc(&IndicesDevicePtr, sizeof(uint32) * NumTriangles * 3);
            cudaMalloc(&ConstantsDevicePtr, sizeof(FClothSimConstants));

            // Initialize delta buffers to zero
            cudaMemset(PositionDeltasDevice, 0, sizeof(FVector) * NumParticles);
            cudaMemset(PositionWeightsDevice, 0, sizeof(float) * NumParticles);

            // Upload constraints to CUDA device memory
            TArray<FClothConstraintGPU> constraintsGPU;
            constraintsGPU.SetNum(Constraints.Num());
            for (int32 i = 0; i < Constraints.Num(); ++i)
            {
                constraintsGPU[i].ParticleA = Constraints[i].ParticleA;
                constraintsGPU[i].ParticleB = Constraints[i].ParticleB;
                constraintsGPU[i].RestLength = Constraints[i].RestLength;
                constraintsGPU[i].Stiffness = Constraints[i].Stiffness;
                constraintsGPU[i].Compliance = Constraints[i].Compliance;
                constraintsGPU[i].Lambda = Constraints[i].Lambda;
                constraintsGPU[i].Padding0 = 0.0f;
                constraintsGPU[i].Padding1 = 0.0f;
            }
            cudaMemcpy(ConstraintDevicePtr, constraintsGPU.GetData(), sizeof(FClothConstraintGPU) * NumConstraints, cudaMemcpyHostToDevice);
            // Upload triangle indices to CUDA device memory
            cudaMemcpy(IndicesDevicePtr, Indices.GetData(), sizeof(uint32) * NumTriangles * 3, cudaMemcpyHostToDevice);

            UE_LOG(ELogLevel::Display, TEXT("ClothSolver: CUDA resources initialized - %d particles, %d constraints"), NumParticles, NumConstraints);
        }
    }
#endif

    bInitialized = true;

    return true;
}

void FClothSolver::Release()
{
    if (!bInitialized) return;

#ifdef CUDA_ENABLED
    // Release CUDA resources first (before D3D11 buffers)
    if (bUseCUDA && CudaInterop)
    {
        // Unregister CUDA graphics resources
        if (PositionCudaResource[0])
            CudaInterop->UnregisterResource(PositionCudaResource[0]);
        if (PositionCudaResource[1])
            CudaInterop->UnregisterResource(PositionCudaResource[1]);
        if (VelocityCudaResource)
            CudaInterop->UnregisterResource(VelocityCudaResource);
        if (NormalCudaResource)
            CudaInterop->UnregisterResource(NormalCudaResource);

        PositionCudaResource[0] = nullptr;
        PositionCudaResource[1] = nullptr;
        VelocityCudaResource = nullptr;
        NormalCudaResource = nullptr;

        // Free CUDA-only device buffers
        if (PositionDeltasDevice)
        {
            cudaFree(PositionDeltasDevice);
            PositionDeltasDevice = nullptr;
        }
        if (PositionWeightsDevice)
        {
            cudaFree(PositionWeightsDevice);
            PositionWeightsDevice = nullptr;
        }
        if (ConstraintDevicePtr)
        {
            cudaFree(ConstraintDevicePtr);
            ConstraintDevicePtr = nullptr;
        }
        if (IndicesDevicePtr)
        {
            cudaFree(IndicesDevicePtr);
            IndicesDevicePtr = nullptr;
        }
        if (ConstantsDevicePtr)
        {
            cudaFree(ConstantsDevicePtr);
            ConstantsDevicePtr = nullptr;
        }

        // Release interop manager
        CudaInterop->Release();
        delete CudaInterop;
        CudaInterop = nullptr;

        bUseCUDA = false;

        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Released CUDA resources"));
    }
#endif

    // Release compute shaders
    // We just null out our pointers
    IntegrateCS = nullptr;
    ConstraintSolverCS = nullptr;
    UpdateNormalsCS = nullptr;
    ClearNormalsCS = nullptr;
    NormalizeNormalsCS = nullptr;

    // Release buffers
    for (int32 i = 0; i < 2; ++i)
    {
        SAFE_RELEASE(PositionBuffer[i]);
        SAFE_RELEASE(PositionUAV[i]);
        SAFE_RELEASE(PositionSRV[i]);
    }

    SAFE_RELEASE(VelocityBuffer);
    SAFE_RELEASE(VelocityUAV);
    SAFE_RELEASE(VelocitySRV);

    SAFE_RELEASE(InvMassBuffer);

    SAFE_RELEASE(ConstraintBuffer);
    SAFE_RELEASE(ConstraintSRV);

    SAFE_RELEASE(IndexBuffer);
    SAFE_RELEASE(IndexSRV);

    SAFE_RELEASE(NormalBuffer);
    SAFE_RELEASE(NormalUAV);
    SAFE_RELEASE(NormalSRV);

    SAFE_RELEASE(ClothSimConstantBuffer);
    SAFE_RELEASE(NormalUpdateConstantBuffer);

    SAFE_RELEASE(PositionDeltaBuffer);
    SAFE_RELEASE(PositionDeltaUAV);
    SAFE_RELEASE(PositionWeightBuffer);
    SAFE_RELEASE(PositionWeightUAV);

    bInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Released resources"));
}

void FClothSolver::Simulate(float InDeltaTime)
{
    if (!bInitialized) return;

    // Clamp delta time
    float DeltaTime = FMath::Clamp(InDeltaTime, 0.0001f, 0.033f);
    // Test for runtime wind change
    //Config.AirDrag = sin(SimData.CurrentTime) * 10.f;

#ifdef CUDA_ENABLED
    // Use CUDA path if available, otherwise fall back to DX11
    if (bUseCUDA && CudaInterop)
    {
        SimulateCUDA(DeltaTime);
    }
    else
    {
        SimulateDX11(DeltaTime);
    }
#else
    // CUDA not enabled - use DX11 path
    SimulateDX11(DeltaTime);
#endif

    // Update simulation time
    SimData.CurrentTime += DeltaTime;

    // Reset external force accumulator
    ExternalForceAccum = FVector::ZeroVector;
}

void FClothSolver::SimulateDX11(float DeltaTime)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    // Swap ping-pong buffers BEFORE simulation
    CurrentBufferIndex = 1 - CurrentBufferIndex;

    // Update constant buffer
    UpdateConstantBuffers();

    // 1. Integration step - apply forces and predict positions
    DispatchIntegration(DeltaTime);

    // 2. Constraint solving iterations
    for (int32 i = 0; i < Config.NumIterations; ++i)
    {
        DispatchConstraintSolver(i);
        DispatchApplyConstraintDeltas();
    }

    // 3. Update normals for rendering
    DispatchNormalUpdate();
}

void FClothSolver::ResetSimulation()
{
    if (!bInitialized)
        return;

    // Reset to initial positions and zero velocities
    if (RestPositions.Num() > 0)
    {
        // Copy rest positions to current positions
        SimData.CurrentPositions = RestPositions;

        // Zero out velocities
        for (int32 i = 0; i < SimData.CurrentVelocities.Num(); ++i)
        {
            SimData.CurrentVelocities[i] = FVector::ZeroVector;
        }

        // Re-upload data to GPU
        UploadInitialData();
    }

    SimData.CurrentTime = 0.0f;
    ExternalForceAccum = FVector::ZeroVector;

    UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Simulation reset"));
}

void FClothSolver::SetGravity(const FVector &InGravity)
{
    SimData.Gravity = InGravity;
}

void FClothSolver::SetWind(const FVector &InWind)
{
    SimData.Wind = InWind;
}

void FClothSolver::AddExternalForce(const FVector &InForce)
{
    ExternalForceAccum += InForce;
}

void FClothSolver::SetConfig(const FClothConfig &InConfig)
{
    Config = InConfig;
}

void FClothSolver::UpdateAttachmentConstraints(const TArray<FClothAttachmentData> &Attachments)
{
    // TODO: Implement attachment constraint updates
    // This would involve updating particle positions for attached vertices
}

void FClothSolver::SetCollisionBodies(const TArray<FClothCollisionPrimitive> &Primitives)
{
    // TODO: Implement collision body updates
    // This would involve creating/updating collision primitive buffers
}

bool FClothSolver::CreateGPUResources()
{
    if (!Graphics || !Graphics->Device)
        return false;

    // Create buffers
    if (!CreateBuffers())
        return false;

    // Create views
    if (!CreateViews())
        return false;

    return true;
}

bool FClothSolver::LoadComputeShaders()
{
    if (!ShaderManager)
        return false;

    bool bSuccess = true;

    // Load integration shader
    HRESULT hr = ShaderManager->AddComputeShader(
        L"ClothIntegrateCS",
        L"Shaders/Cloth/ClothIntegrate.hlsl",
        "IntegrateCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothIntegrate shader"));
        bSuccess = false;
    }
    else
    {
        IntegrateCS = ShaderManager->GetComputeShaderByKey(L"ClothIntegrateCS");
    }

    // Load constraint solver shader
    hr = ShaderManager->AddComputeShader(
        L"ClothConstraintSolverCS",
        L"Shaders/Cloth/ClothConstraintSolver.hlsl",
        "SolveDistanceConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothConstraintSolver shader"));
        bSuccess = false;
    }
    else
    {
        ConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothConstraintSolverCS");
    }

    // Load apply delta shaders
    hr = ShaderManager->AddComputeShader(
        L"ClothApplyConstraintDeltasCS",
        L"Shaders/Cloth/ClothApplyDelta.hlsl",
        "ApplyConstraintDeltasCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothApplyConstraintDeltas shader"));
        bSuccess = false;
    }
    else
    {
        ApplyConstraintDeltasCS = ShaderManager->GetComputeShaderByKey(L"ClothApplyConstraintDeltasCS");
    }

    // Load normal update shaders
    hr = ShaderManager->AddComputeShader(
        L"ClothClearNormalsCS",
        L"Shaders/Cloth/ClothUpdateNormals.hlsl",
        "ClearNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothClearNormals shader"));
        bSuccess = false;
    }
    else
    {
        ClearNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothClearNormalsCS");
    }

    hr = ShaderManager->AddComputeShader(
        L"ClothUpdateNormalsCS",
        L"Shaders/Cloth/ClothUpdateNormals.hlsl",
        "UpdateNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothUpdateNormals shader"));
        bSuccess = false;
    }
    else
    {
        UpdateNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothUpdateNormalsCS");
    }

    hr = ShaderManager->AddComputeShader(
        L"ClothNormalizeNormalsCS",
        L"Shaders/Cloth/ClothUpdateNormals.hlsl",
        "NormalizeNormalsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothNormalizeNormals shader"));
        bSuccess = false;
    }
    else
    {
        NormalizeNormalsCS = ShaderManager->GetComputeShaderByKey(L"ClothNormalizeNormalsCS");
    }

    return bSuccess;
}

bool FClothSolver::CreateBuffers()
{
    if (!Graphics || !Graphics->Device || NumParticles == 0)
        return false;

    HRESULT hr;

    // Create position buffers (ping-pong)
    // Add SHARED flag for CUDA interop
    for (int32 i = 0; i < 2; ++i)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(FClothParticleGPU) * NumParticles;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothParticleGPU);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_SHARED;

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &PositionBuffer[i]);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create position buffer %d"), i);
            return false;
        }
    }

    // Create velocity buffer
    // Add SHARED flag for CUDA interop
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FClothVelocityGPU) * NumParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FClothVelocityGPU);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_SHARED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &VelocityBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create velocity buffer"));
        return false;
    }

    // Create constraint buffer (if we have constraints)
    if (NumConstraints > 0)
    {
        bufferDesc.ByteWidth = sizeof(FClothConstraintGPU) * NumConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothConstraintGPU);

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &ConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create constraint buffer"));
            return false;
        }
    }

    // Create index buffer (if we have triangles)
    if (NumTriangles > 0)
    {
        bufferDesc.ByteWidth = sizeof(uint32) * NumTriangles * 3;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(uint32);

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &IndexBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create index buffer"));
            return false;
        }
    }

    // Create normal buffer
    // Add SHARED flag for CUDA interop
    bufferDesc.ByteWidth = sizeof(FVector) * NumParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FVector);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_SHARED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &NormalBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create normal buffer"));
        return false;
    }

    // Create constant buffers
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = (sizeof(FClothSimConstants) + 0xf) & 0xfffffff0; // 16-byte aligned

    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &ClothSimConstantBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create cloth sim constant buffer"));
        return false;
    }

    cbDesc.ByteWidth = (sizeof(FClothNormalUpdateConstants) + 0xf) & 0xfffffff0;
    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &NormalUpdateConstantBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create normal update constant buffer"));
        return false;
    }

    // Create PositionDelta buffer (float3 per particle)
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(FVector) * NumParticles; // float3
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(FVector);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &PositionDeltaBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create PositionDelta buffer"));
        return false;
    }

    // Create PositionWeight buffer (float per particle)
    bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(float) * NumParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(float);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &PositionWeightBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create PositionWeight buffer"));
        return false;
    }

    return true;
}

bool FClothSolver::CreateViews()
{
    if (!Graphics || !Graphics->Device)
        return false;

    HRESULT hr;

    // Create UAVs and SRVs for position buffers
    for (int32 i = 0; i < 2; ++i)
    {
        if (!PositionBuffer[i])
            continue;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = NumParticles;

        hr = Graphics->Device->CreateUnorderedAccessView(PositionBuffer[i], &uavDesc, &PositionUAV[i]);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create position UAV %d"), i);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = NumParticles;

        hr = Graphics->Device->CreateShaderResourceView(PositionBuffer[i], &srvDesc, &PositionSRV[i]);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create position SRV %d"), i);
            return false;
        }
    }

    // Create UAV and SRV for velocity buffer
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = NumParticles;

    hr = Graphics->Device->CreateUnorderedAccessView(VelocityBuffer, &uavDesc, &VelocityUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create velocity UAV"));
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = NumParticles;

    hr = Graphics->Device->CreateShaderResourceView(VelocityBuffer, &srvDesc, &VelocitySRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create velocity SRV"));
        return false;
    }

    // Create SRV for constraint buffer
    if (ConstraintBuffer && NumConstraints > 0)
    {
        srvDesc.Buffer.NumElements = NumConstraints;
        hr = Graphics->Device->CreateShaderResourceView(ConstraintBuffer, &srvDesc, &ConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create constraint SRV"));
            return false;
        }
    }

    // Create SRV for index buffer
    if (IndexBuffer && NumTriangles > 0)
    {
        srvDesc.Buffer.NumElements = NumTriangles * 3;
        hr = Graphics->Device->CreateShaderResourceView(IndexBuffer, &srvDesc, &IndexSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create index SRV"));
            return false;
        }
    }

    // Create UAV and SRV for normal buffer
    hr = Graphics->Device->CreateUnorderedAccessView(NormalBuffer, &uavDesc, &NormalUAV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create normal UAV"));
        return false;
    }

    srvDesc.Buffer.NumElements = NumParticles;
    hr = Graphics->Device->CreateShaderResourceView(NormalBuffer, &srvDesc, &NormalSRV);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create normal SRV"));
        return false;
    }

    // PositionDelta UAV
    if (PositionDeltaBuffer)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = NumParticles;

        hr = Graphics->Device->CreateUnorderedAccessView(PositionDeltaBuffer, &uavDesc, &PositionDeltaUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create PositionDelta UAV"));
            return false;
        }
    }

    // PositionWeight UAV
    if (PositionWeightBuffer)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = NumParticles;

        hr = Graphics->Device->CreateUnorderedAccessView(PositionWeightBuffer, &uavDesc, &PositionWeightUAV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create PositionWeight UAV"));
            return false;
        }
    }

    return true;
}

bool FClothSolver::UploadInitialData()
{
    if (!Graphics || !Graphics->DeviceContext)
        return false;

    // Upload position data (convert to GPU particle format)
    if (PositionBuffer[0] && PositionBuffer[1] && RestPositions.Num() == NumParticles)
    {
        TArray<FClothParticleGPU> particlesGPU;
        particlesGPU.SetNum(NumParticles);

        for (uint32 i = 0; i < NumParticles; ++i)
        {
            particlesGPU[i].Position = RestPositions[i];
            particlesGPU[i].InvMass = (i < static_cast<uint32>(InvMasses.Num())) ? InvMasses[i] : 1.0f;
        }

        // Upload to both ping-pong buffers
        Graphics->DeviceContext->UpdateSubresource(PositionBuffer[0], 0, nullptr, particlesGPU.GetData(), 0, 0);
        Graphics->DeviceContext->UpdateSubresource(PositionBuffer[1], 0, nullptr, particlesGPU.GetData(), 0, 0);

        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Uploaded %d positions (first pos: %f, %f, %f)"),
               NumParticles, RestPositions[0].X, RestPositions[0].Y, RestPositions[0].Z);
    }

    // Upload velocity data (all zeros initially)
    if (VelocityBuffer && SimData.CurrentVelocities.Num() == NumParticles)
    {
        TArray<FClothVelocityGPU> velocitiesGPU;
        velocitiesGPU.SetNum(NumParticles);

        for (uint32 i = 0; i < NumParticles; ++i)
        {
            velocitiesGPU[i].Velocity = SimData.CurrentVelocities[i];
            velocitiesGPU[i].Padding = 0.0f;
        }

        Graphics->DeviceContext->UpdateSubresource(VelocityBuffer, 0, nullptr, velocitiesGPU.GetData(), 0, 0);
    }

    // Upload constraint data
    if (ConstraintBuffer && Constraints.Num() > 0)
    {
        TArray<FClothConstraintGPU> constraintsGPU;
        constraintsGPU.SetNum(Constraints.Num());

        for (int32 i = 0; i < Constraints.Num(); ++i)
        {
            constraintsGPU[i].ParticleA = Constraints[i].ParticleA;
            constraintsGPU[i].ParticleB = Constraints[i].ParticleB;
            constraintsGPU[i].RestLength = Constraints[i].RestLength;
            constraintsGPU[i].Stiffness = Constraints[i].Stiffness;
        }

        Graphics->DeviceContext->UpdateSubresource(ConstraintBuffer, 0, nullptr, constraintsGPU.GetData(), 0, 0);

        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Uploaded %d constraints"), Constraints.Num());
    }

    // Upload index data
    if (IndexBuffer && Indices.Num() > 0)
    {
        Graphics->DeviceContext->UpdateSubresource(IndexBuffer, 0, nullptr, Indices.GetData(), 0, 0);

        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Uploaded %d indices"), Indices.Num());
    }

    // Initialize normal buffer (will be computed by simulation)
    if (NormalBuffer)
    {
        TArray<FVector> initialNormals;
        initialNormals.SetNum(NumParticles);

        for (uint32 i = 0; i < NumParticles; ++i)
        {
            initialNormals[i] = FVector(0.0f, 0.0f, 1.0f); // Default up normal
        }

        Graphics->DeviceContext->UpdateSubresource(NormalBuffer, 0, nullptr, initialNormals.GetData(), 0, 0);
    }

    return true;
}

void FClothSolver::UpdateConstantBuffers()
{
    if (!Graphics || !Graphics->DeviceContext || !ClothSimConstantBuffer)
        return;

    // Update cloth simulation constant buffer
    FClothSimConstants constants = {};
    constants.NumParticles = NumParticles;
    constants.NumConstraints = NumConstraints;
    constants.DeltaTime = Config.TimeStep;
    constants.Damping = Config.Damping;
    constants.Gravity = SimData.Gravity + ExternalForceAccum;
    constants.StretchStiffness = Config.StretchStiffness;
    constants.Wind = SimData.Wind;
    constants.BendStiffness = Config.BendStiffness;
    constants.AirDrag = Config.AirDrag;
    constants.NumIterations = Config.NumIterations;
    constants.CurrentIteration = 0;
    constants.UseXPBD = Config.bUseXPBD ? 1 : 0;
    constants.WorldMatrix = FMatrix::Identity;

    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(ClothSimConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &constants, sizeof(FClothSimConstants));
        Graphics->DeviceContext->Unmap(ClothSimConstantBuffer, 0);
    }
}

void FClothSolver::DispatchIntegration(float DeltaTime)
{
    if (!Graphics || !Graphics->DeviceContext || !IntegrateCS)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // Bind UAVs
    ID3D11UnorderedAccessView *uavs[] =
        {
            PositionUAV[CurrentBufferIndex],      // u0: ParticlesRead
            PositionUAV[1u - CurrentBufferIndex], // u1: ParticlesWrite
            VelocityUAV                           // u2: VelocityBuffer (in-place)
        };

    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(IntegrateCS, nullptr, 0);

    // Dispatch
    uint32 dispatchCount = GetDispatchCount(NumParticles);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
}

void FClothSolver::DispatchConstraintSolver(int32 Iteration)
{
    if (!Graphics || !Graphics->DeviceContext || !ConstraintSolverCS || NumConstraints == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // t0: ParticlesRead (현재 step의 읽기 버퍼)
    ID3D11ShaderResourceView *srvs[] =
        {
            PositionSRV[CurrentBufferIndex], // t0
            ConstraintSRV                    // t1
        };
    Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);

    // u0: PositionDelta, u1: PositionWeight
    ID3D11UnorderedAccessView *uavs[] =
        {
            PositionDeltaUAV, // u0
            PositionWeightUAV // u1
        };
    UINT initialCounts[2] = {0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initialCounts);

    // Bind shader (SolveDistanceConstraintsCS: delta 누적용으로 수정된 버전)
    Graphics->DeviceContext->CSSetShader(ConstraintSolverCS, nullptr, 0);

    // Dispatch (제약 개수 기준)
    uint32 dispatchCount = GetDispatchCount(NumConstraints);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
}

void FClothSolver::DispatchApplyConstraintDeltas()
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyConstraintDeltasCS)
        return;

    // t0: PositionRead = 현재 읽기 버퍼
    ID3D11ShaderResourceView *srvs[] =
        {
            PositionSRV[1u - CurrentBufferIndex] // t0
        };
    Graphics->DeviceContext->CSSetShaderResources(0, 1, srvs);

    // u0: PositionDelta, u1: PositionWeight, u2: PositionWrite
    ID3D11UnorderedAccessView *uavs[] =
        {
            PositionDeltaUAV,               // u0
            PositionWeightUAV,              // u1
            PositionUAV[CurrentBufferIndex] // u2: 다음 버퍼에 결과 기록
        };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, uavs, initialCounts);

    // Bind constants
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ApplyConstraintDeltasCS, nullptr, 0);

    // Dispatch (파티클 개수 기준)
    uint32 dispatchCount = GetDispatchCount(NumParticles);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[3] = {nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRV = nullptr;
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &nullSRV);

    // ping-pong 스왑: 이제 write 버퍼가 새 Current가 됨
    CurrentBufferIndex = 1u - CurrentBufferIndex;
}

void FClothSolver::DispatchNormalUpdate()
{
    if (!Graphics || !Graphics->DeviceContext || NumTriangles == 0)
        return;

    // Step 1: Clear normals
    if (ClearNormalsCS)
    {
        Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &NormalUAV, nullptr);
        Graphics->DeviceContext->CSSetShader(ClearNormalsCS, nullptr, 0);

        uint32 dispatchCount = GetDispatchCount(NumParticles);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
    }

    // Step 2: Accumulate face normals
    if (UpdateNormalsCS)
    {
        // Bind SRVs
        ID3D11ShaderResourceView *srvs[] = {PositionSRV[CurrentBufferIndex], IndexSRV};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);

        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &NormalUAV, nullptr);
        Graphics->DeviceContext->CSSetShader(UpdateNormalsCS, nullptr, 0);

        uint32 dispatchCount = GetDispatchCount(NumTriangles);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

        // Unbind SRVs
        ID3D11ShaderResourceView *nullSRVs[] = {nullptr, nullptr};
        Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
    }

    // Step 3: Normalize normals
    if (NormalizeNormalsCS)
    {
        Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);
        Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &NormalUAV, nullptr);
        Graphics->DeviceContext->CSSetShader(NormalizeNormalsCS, nullptr, 0);

        uint32 dispatchCount = GetDispatchCount(NumParticles);
        Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
    }

    // Unbind
    ID3D11UnorderedAccessView *nullUAV = nullptr;
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11Buffer *nullCB = nullptr;
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &nullCB);
}

#ifdef CUDA_ENABLED
void FClothSolver::SimulateCUDA(float DeltaTime)
{
    if (!CudaInterop || !bUseCUDA) return;

    cudaStream_t stream = CudaInterop->GetStream();

    // ping-pong buffer index
    int readIdx = CurrentBufferIndex;
    int writeIdx = 1- CurrentBufferIndex;

    // Prepare simulation constants
    FClothSimConstants constants = {};
    constants.NumParticles = NumParticles;
    constants.NumConstraints = NumConstraints;
    constants.DeltaTime = DeltaTime;
    constants.Damping = Config.Damping;
    constants.Gravity = SimData.Gravity + ExternalForceAccum;
    constants.StretchStiffness = Config.StretchStiffness;
    constants.Wind = SimData.Wind;
    constants.BendStiffness = Config.BendStiffness;
    constants.AirDrag = Config.AirDrag;
    constants.NumIterations = Config.NumIterations;
    constants.UseXPBD = Config.bUseXPBD ? 1 : 0;
    constants.WorldMatrix = FMatrix::Identity;

    // Upload constants to device memory
    cudaMemcpyAsync(ConstantsDevicePtr, &constants, sizeof(FClothSimConstants), cudaMemcpyHostToDevice, stream);

    // Map D3D11 resources for CUDA access
    cudaGraphicsResource *resources[] = {
        PositionCudaResource[0],
        PositionCudaResource[1],
        VelocityCudaResource,
        NormalCudaResource
    };

    if (!CudaInterop->MapResources(resources, 4, stream))
    {
        UE_LOG(ELogLevel::Error, TEXT("SimulateCUDA: Failed to map resources"));
        return;
    }

    // Get device pointers for mapped resources
    size_t size;
    CudaInterop->GetMappedPointer(PositionCudaResource[readIdx], &PositionDevicePtr[readIdx], &size);
    CudaInterop->GetMappedPointer(PositionCudaResource[writeIdx], &PositionDevicePtr[writeIdx], &size);
    CudaInterop->GetMappedPointer(VelocityCudaResource, &VelocityDevicePtr, &size);
    CudaInterop->GetMappedPointer(NormalCudaResource, &NormalDevicePtr, &size);

    // ===== CUDA Simulation Pipeline =====

    // Apply forces and update velocities/positions
    LaunchIntegrateKernel(
        PositionDevicePtr[readIdx],
        PositionDevicePtr[writeIdx],
        VelocityDevicePtr,
        constants,
        stream);

    // Swap buffer index for next step
    readIdx = writeIdx;
    writeIdx = 1 - writeIdx;

    // Constraint solving iterations
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        // Clear delta accumulation buffers at start of each iteration
        cudaMemsetAsync(PositionDeltasDevice, 0, sizeof(FVector) * NumParticles, stream);
        cudaMemsetAsync(PositionWeightsDevice, 0, sizeof(float) * NumParticles, stream);

        // Solve constraints - accumulate corrections using atomic float ops
        LaunchConstraintSolverKernel(
            PositionDevicePtr[readIdx],
            ConstraintDevicePtr,
            PositionDeltasDevice,
            PositionWeightsDevice,
            constants,
            stream);

        // Apply averaged corrections
        LaunchApplyDeltasKernel(
            PositionDevicePtr[readIdx],
            PositionDevicePtr[writeIdx],
            PositionDeltasDevice,
            PositionWeightsDevice,
            constants,
            stream);

        // Swap buffers for next iteration
        readIdx = writeIdx;
        writeIdx = 1 - writeIdx;
    }

    // Update normals for lighting
    LaunchUpdateNormalsKernel(
        PositionDevicePtr[readIdx],
        IndicesDevicePtr,
        NormalDevicePtr,
        NumParticles,
        NumTriangles,
        stream);

    // Synchronize CUDA stream before unmapping
    cudaStreamSynchronize(stream);

    // Unmap resources - return control to D3D11 for rendering
    CudaInterop->UnmapResources(resources, 4, stream);

    // Update current buffer index for rendering
    CurrentBufferIndex = 1 - CurrentBufferIndex;
}
#endif // CUDA_ENABLED

uint32 FClothSolver::GetDispatchCount(uint32 ElementCount, uint32 ThreadGroupSize) const
{
    return (ElementCount + ThreadGroupSize - 1) / ThreadGroupSize;
}
