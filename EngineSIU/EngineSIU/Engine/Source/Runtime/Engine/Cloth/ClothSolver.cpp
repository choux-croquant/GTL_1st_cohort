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
#include "Core/Math/Matrix.h"
#include "ShaderConstants.h"
#include "Classes/Engine/ClothAsset.h"

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

FClothSolver::FClothSolver()
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), IntegrateCS(nullptr), ConstraintSolverCS(nullptr), BendConstraintSolverCS(nullptr), ApplyKinematicTargetsCS(nullptr), UpdateNormalsCS(nullptr), ClearNormalsCS(nullptr), NormalizeNormalsCS(nullptr), ClothSimConstantBuffer(nullptr), NormalUpdateConstantBuffer(nullptr), CurrentBufferIndex(0), NumParticles(0), NumConstraints(0), NumBendConstraints(0), NumKinematicTargets(0), NumTriangles(0), bInitialized(false), ExternalForceAccum(FVector::ZeroVector)
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
    BendConstraintBuffer = nullptr;
    KinematicTargetBuffer = nullptr;
    IndexBuffer = nullptr;
    NormalBuffer = nullptr;

    VelocityUAV = nullptr;
    NormalUAV = nullptr;

    VelocitySRV = nullptr;
    ConstraintSRV = nullptr;
    BendConstraintSRV = nullptr;
    KinematicTargetSRV = nullptr;
    IndexSRV = nullptr;
    NormalSRV = nullptr;

    readIdx = 0;
    writeIdx = 1;
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

    // Load compute shaders (DX11 fallback path)
    if (!LoadComputeShaders())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: Failed to load compute shaders"));
        return;
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
    const TArray<FClothDistanceConstraint> &AssetDistConstraints = InAsset->GetDistanceConstraints();
    const TArray<FClothBendConstraint> &AssetBendConstraints = InAsset->GetBendConstraints();

    RestPositions = AssetRestPositions;
    Indices = AssetIndices;
    InvMasses = AssetInvMasses;

    Constraints.Empty();
    BendConstraints.Empty();
    Constraints.Append(AssetDistConstraints);
    BendConstraints.Append(AssetBendConstraints);

    // TODO : Attachment, VertexPaint
    // AttachmentIndices = InAsset->GetAttachmentIndices();
    // VertexPaintData = InAsset->GetVertexPaintData();

    NumParticles = RestPositions.Num();
    NumConstraints = AssetDistConstraints.Num();
    NumBendConstraints = AssetBendConstraints.Num();
    NumTriangles = Indices.Num() / 3;

    if (NumParticles == 0)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothSolver: No particles in cloth asset"));
        return false;
    }

    SimData.NumParticles = NumParticles;
    SimData.NumConstraints = NumConstraints;
    SimData.NumBendConstraints = NumBendConstraints;

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

    bInitialized = true;

    return true;
}

void FClothSolver::Release()
{
    if (!bInitialized)
        return;

    // Release compute shaders
    // We just null out our pointers
    IntegrateCS = nullptr;
    ConstraintSolverCS = nullptr;
    BendConstraintSolverCS = nullptr;
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
    SAFE_RELEASE(BendConstraintBuffer);
    SAFE_RELEASE(KinematicTargetBuffer);
    SAFE_RELEASE(ConstraintSRV);
    SAFE_RELEASE(BendConstraintSRV);
    SAFE_RELEASE(KinematicTargetSRV);

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
    if (!bInitialized)
        return;

    // Clamp delta time
    float DeltaTime = FMath::Clamp(InDeltaTime, 0.0001f, 0.033f);
    // Test for runtime wind change
    // Config.AirDrag = sin(SimData.CurrentTime) * 10.f;
    // Config.AirDrag = 10.f;
    // const int32 NumSubsteps = 3;
    // const float SubstepDeltaTime = InDeltaTime / (float)NumSubsteps;

    // for (int32 i = 0; i < NumSubsteps; ++i)
    //{
    //     // 기존의 SimulateCS 호출 (내부에서 Integration -> Constraints -> VelocityUpdate 수행)
    //     // 주의: 내부 셰이더에는 반드시 'SubstepDeltaTime'을 넘겨줘야 함
    //     SimulateCS(SubstepDeltaTime);

    //    // 시간 누적
    //    SimData.CurrentTime += SubstepDeltaTime;
    //}
    SimulateCS(DeltaTime);

    // Update simulation time
    SimData.CurrentTime += DeltaTime;

    // Reset external force accumulator
    ExternalForceAccum = FVector::ZeroVector;
}

void FClothSolver::SimulateCS(float DeltaTime)
{
    if (!Graphics || !Graphics->DeviceContext)
        return;

    // Swap ping-pong buffers BEFORE simulation
    readIdx = CurrentBufferIndex;
    writeIdx = 1 - CurrentBufferIndex;

    // Update constant buffer
    UpdateConstantBuffers();

    // Integration step - apply forces and predict positions
    DispatchIntegration(DeltaTime);
    readIdx = writeIdx;
    writeIdx = 1 - writeIdx;

    // Apply kinematic targets after integration (override attached particle positions)
    if (NumKinematicTargets > 0)
    {
        DispatchApplyKinematicTargets();
    }

    // Constraint solving iterations
    for (int32 i = 0; i < Config.NumIterations; ++i)
    {
        DispatchConstraintSolver(i);
        DispatchBendConstraintSolver(i);
        DispatchApplyConstraintDeltas();

        // Reapply kinematic targets after constraints to enforce attachment
        if (NumKinematicTargets > 0)
        {
            DispatchApplyKinematicTargets();
        }

        readIdx = writeIdx;
        writeIdx = 1 - writeIdx;
    }

    // Update normals for rendering
    // DispatchNormalUpdate();

    CurrentBufferIndex = 1 - CurrentBufferIndex;
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
    // Deprecated - use UpdateKinematicTargets instead
    UpdateKinematicTargets(Attachments);
}

void FClothSolver::UpdateKinematicTargets(const TArray<FClothAttachmentData> &Attachments)
{
    if (!Graphics || !Graphics->DeviceContext || !KinematicTargetBuffer)
        return;

    if (Attachments.Num() == 0)
    {
        NumKinematicTargets = 0;
        return;
    }

    // Convert attachments to GPU format
    KinematicTargets.Empty();
    KinematicTargets.Reserve(Attachments.Num());

    for (const FClothAttachmentData &attach : Attachments)
    {
        FClothKinematicTargetGPU target;
        target.ParticleIndex = attach.ClothVertexIndex;
        target.TargetPosition = attach.WorldPosition; // Already transformed to world space
        target.Stiffness = attach.Stiffness;
        target.Padding0 = 0.0f;
        target.Padding1 = 0.0f;
        target.Padding2 = 0.0f;

        KinematicTargets.Add(target);
    }

    NumKinematicTargets = KinematicTargets.Num();

    // Upload to GPU (dynamic buffer with MAP_WRITE_DISCARD)
    if (NumKinematicTargets > 0)
    {
        D3D11_MAPPED_SUBRESOURCE msr;
        HRESULT hr = Graphics->DeviceContext->Map(KinematicTargetBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
        if (SUCCEEDED(hr))
        {
            uint32 bytesToCopy = sizeof(FClothKinematicTargetGPU) * NumKinematicTargets;
            memcpy(msr.pData, KinematicTargets.GetData(), bytesToCopy);
            Graphics->DeviceContext->Unmap(KinematicTargetBuffer, 0);
        }
    }
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

    // Load bend constraint solver shader
    hr = ShaderManager->AddComputeShader(
        L"ClothBendConstraintSolverCS",
        L"Shaders/Cloth/ClothBendConstraintSolver.hlsl",
        "SolveBendConstraintsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile ClothConstraintSolver shader"));
        bSuccess = false;
    }
    else
    {
        BendConstraintSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothBendConstraintSolverCS");
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

    // Load apply kinematic targets shader
    hr = ShaderManager->AddComputeShader(
        L"ClothApplyKinematicTargetsCS",
        L"Shaders/Cloth/ClothApplyKinematicTargets.hlsl",
        "ApplyKinematicTargetsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("Failed to compile ClothApplyKinematicTargets shader (optional feature)"));
        // Not critical - kinematic targets are optional
    }
    else
    {
        ApplyKinematicTargetsCS = ShaderManager->GetComputeShaderByKey(L"ClothApplyKinematicTargetsCS");
    }

    return bSuccess;
}

bool FClothSolver::CreateBuffers()
{
    if (!Graphics || !Graphics->Device || NumParticles == 0)
        return false;

    HRESULT hr;

    // Create position buffers (ping-pong)
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
        bufferDesc.ByteWidth = sizeof(FClothDistanceConstraintGPU) * NumConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothDistanceConstraintGPU);

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &ConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create constraint buffer"));
            return false;
        }
    }

    // Create constraint buffer (if we have constraints)
    if (NumBendConstraints > 0)
    {
        bufferDesc.ByteWidth = sizeof(FClothBendConstraintGPU) * NumBendConstraints;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(FClothBendConstraintGPU);

        hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &BendConstraintBuffer);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create bend constraint buffer"));
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
    bufferDesc.ByteWidth = sizeof(FVector) * NumParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FVector);
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_SHARED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &NormalBuffer);
    if (FAILED(hr))
    {
        // Check
        UE_LOG(ELogLevel::Error, TEXT("Failed to create normal buffer"));
        return false;
    }

    // Create kinematic target buffer (dynamic - updated each frame)
    // Start with reasonable capacity, will be reallocated if needed
    uint32 maxKinematicTargets = FMath::Max(NumParticles / 10, 2560u); // Reserve 10% of particles or min 16
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(FClothKinematicTargetGPU) * maxKinematicTargets;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.StructureByteStride = sizeof(FClothKinematicTargetGPU);
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    hr = Graphics->Device->CreateBuffer(&bufferDesc, nullptr, &KinematicTargetBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("Failed to create kinematic target buffer (optional feature)"));
        // Not critical - kinematic targets are optional
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
    bufferDesc.ByteWidth = sizeof(int32) * 3 * NumParticles; // int3
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(int32) * 3;
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
    bufferDesc.ByteWidth = sizeof(int32) * NumParticles;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.StructureByteStride = sizeof(int32);
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

    // Create SRV for bend constraint buffer
    if (BendConstraintBuffer && NumBendConstraints > 0)
    {
        srvDesc.Buffer.NumElements = NumBendConstraints;
        hr = Graphics->Device->CreateShaderResourceView(BendConstraintBuffer, &srvDesc, &BendConstraintSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Error, TEXT("Failed to create bend constraint SRV"));
            return false;
        }
    }

    // Create SRV for kinematic target buffer (dynamic, will be updated each frame)
    if (KinematicTargetBuffer)
    {
        uint32 maxKinematicTargets = FMath::Max(NumParticles / 10, 16u);
        srvDesc.Buffer.NumElements = maxKinematicTargets;
        hr = Graphics->Device->CreateShaderResourceView(KinematicTargetBuffer, &srvDesc, &KinematicTargetSRV);
        if (FAILED(hr))
        {
            UE_LOG(ELogLevel::Warning, TEXT("Failed to create kinematic target SRV (optional feature)"));
            // Not critical
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
            particlesGPU[i].InstanceID = 0; // Legacy mode uses InstanceID=0 (single instance)
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
        TArray<FClothDistanceConstraintGPU> constraintsGPU;
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

    // Upload bend constraint data
    if (BendConstraintBuffer && BendConstraints.Num() > 0)
    {
        TArray<FClothBendConstraintGPU> bendConstraintsGPU;
        bendConstraintsGPU.SetNum(BendConstraints.Num());

        for (int32 i = 0; i < BendConstraints.Num(); ++i)
        {
            bendConstraintsGPU[i].ParticleA = BendConstraints[i].ParticleA;
            bendConstraintsGPU[i].ParticleB = BendConstraints[i].ParticleB;
            bendConstraintsGPU[i].ParticleC = BendConstraints[i].ParticleC;
            bendConstraintsGPU[i].ParticleD = BendConstraints[i].ParticleD;
            bendConstraintsGPU[i].RestAngle = BendConstraints[i].RestAngle;
            bendConstraintsGPU[i].Stiffness = BendConstraints[i].Stiffness;
        }

        Graphics->DeviceContext->UpdateSubresource(BendConstraintBuffer, 0, nullptr, bendConstraintsGPU.GetData(), 0, 0);

        UE_LOG(ELogLevel::Display, TEXT("ClothSolver: Uploaded %d bend constraints"), BendConstraints.Num());
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

    // Initialize delta accumulation buffers to zero
    if (PositionDeltaBuffer)
    {
        TArray<int32> initialDeltas;
        initialDeltas.SetNum(NumParticles * 3); // int3 per particle
        for (uint32 i = 0; i < NumParticles * 3; ++i)
        {
            initialDeltas[i] = 0;
        }
        Graphics->DeviceContext->UpdateSubresource(PositionDeltaBuffer, 0, nullptr, initialDeltas.GetData(), 0, 0);
    }

    if (PositionWeightBuffer)
    {
        TArray<int32> initialWeights;
        initialWeights.SetNum(NumParticles); // int per particle
        for (uint32 i = 0; i < NumParticles; ++i)
        {
            initialWeights[i] = 0;
        }
        Graphics->DeviceContext->UpdateSubresource(PositionWeightBuffer, 0, nullptr, initialWeights.GetData(), 0, 0);
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
    constants.NumBendConstraints = NumBendConstraints;
    constants.NumKinematicTargets = NumKinematicTargets;
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
            PositionUAV[readIdx],  // u0: ParticlesRead
            PositionUAV[writeIdx], // u1: ParticlesWrite
            VelocityUAV            // u2: VelocityBuffer (in-place)
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

    // Clear delta accumulation buffers at the start of each iteration
    if (Iteration == 0 || true) // Always clear before constraint solving
    {
        UINT clearValues[4] = {0, 0, 0, 0};
        if (PositionDeltaUAV)
            Graphics->DeviceContext->ClearUnorderedAccessViewUint(PositionDeltaUAV, clearValues);
        if (PositionWeightUAV)
            Graphics->DeviceContext->ClearUnorderedAccessViewUint(PositionWeightUAV, clearValues);
    }

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // t0: ParticlesRead (현재 step의 읽기 버퍼)
    ID3D11ShaderResourceView *srvs[] =
        {
            PositionSRV[readIdx], // t0
            ConstraintSRV         // t1
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

void FClothSolver::DispatchBendConstraintSolver(int32 Iteration)
{
    if (!Graphics || !Graphics->DeviceContext || !BendConstraintSolverCS || NumBendConstraints == 0)
        return;

    // Clear delta accumulation buffers at the start of each iteration
    // if (Iteration == 0 || true) // Always clear before constraint solving
    //{
    //    UINT clearValues[4] = { 0, 0, 0, 0 };
    //    if (PositionDeltaUAV)
    //        Graphics->DeviceContext->ClearUnorderedAccessViewUint(PositionDeltaUAV, clearValues);
    //    if (PositionWeightUAV)
    //        Graphics->DeviceContext->ClearUnorderedAccessViewUint(PositionWeightUAV, clearValues);
    //}

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // t0: ParticlesRead (현재 step의 읽기 버퍼)
    ID3D11ShaderResourceView *srvs[] =
        {
            PositionSRV[readIdx], // t0
            BendConstraintSRV     // t1
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
    Graphics->DeviceContext->CSSetShader(BendConstraintSolverCS, nullptr, 0);

    // Dispatch (제약 개수 기준)
    uint32 dispatchCount = GetDispatchCount(NumBendConstraints);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
}

void FClothSolver::DispatchApplyKinematicTargets()
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyKinematicTargetsCS || NumKinematicTargets == 0)
        return;

    // Bind constant buffer
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // Bind kinematic target buffer (t0)
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &KinematicTargetSRV);

    // Bind position buffer for write (u0) - writeIdx is the current output buffer
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &PositionUAV[writeIdx], nullptr);

    // Set shader
    Graphics->DeviceContext->CSSetShader(ApplyKinematicTargetsCS, nullptr, 0);

    // Dispatch (one thread per kinematic target)
    uint32 dispatchCount = GetDispatchCount(NumKinematicTargets);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAV = nullptr;
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView *nullSRV = nullptr;
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &nullSRV);
}

void FClothSolver::DispatchApplyConstraintDeltas()
{
    if (!Graphics || !Graphics->DeviceContext || !ApplyConstraintDeltasCS)
        return;

    // t0: PositionRead = 현재 읽기 버퍼
    ID3D11ShaderResourceView *srvs[] =
        {
            PositionSRV[readIdx] // t0
        };
    Graphics->DeviceContext->CSSetShaderResources(0, 1, srvs);

    // u0: PositionDelta, u1: PositionWeight, u2: PositionWrite
    ID3D11UnorderedAccessView *uavs[] =
        {
            PositionDeltaUAV,      // u0
            PositionWeightUAV,     // u1
            PositionUAV[writeIdx], // u2: 다음 버퍼에 결과 기록
            VelocityUAV            // u3: VelocityBuffer (in-place)
        };
    UINT initialCounts[3] = {0, 0, 0};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 4, uavs, initialCounts);

    // Bind constants
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &ClothSimConstantBuffer);

    // Bind shader
    Graphics->DeviceContext->CSSetShader(ApplyConstraintDeltasCS, nullptr, 0);

    // Dispatch (파티클 개수 기준)
    uint32 dispatchCount = GetDispatchCount(NumParticles);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView *nullUAVs[4] = {nullptr, nullptr, nullptr, nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
    ID3D11ShaderResourceView *nullSRV = nullptr;
    Graphics->DeviceContext->CSSetShaderResources(0, 1, &nullSRV);
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

uint32 FClothSolver::GetDispatchCount(uint32 ElementCount, uint32 ThreadGroupSize) const
{
    return (ElementCount + ThreadGroupSize - 1) / ThreadGroupSize;
}
