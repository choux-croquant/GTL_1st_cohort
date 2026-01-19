/**
 * Cloth Solver - GPU-based cloth simulation using Position-Based Dynamics
 * Manages compute shader execution and GPU resources for cloth simulation
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include <d3d11.h>

#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Matrix.h"
#include "ClothSimulationData.h"
#include "ClothGPUStructs.h"

// Forward declarations
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class UClothAsset;

/**
 * Main cloth solver class
 * Handles GPU compute shader simulation
 */
class FClothSolver
{
public:
    FClothSolver();
    ~FClothSolver();

    /**
     * Initialize the solver with graphics resources
     */
    void Initialize(FGraphicsDevice *InGraphics, FDXDBufferManager *InBufferManager, FDXDShaderManager *InShaderManager);

    /**
     * Setup simulation from cloth asset
     */
    bool SetupFromAsset(UClothAsset *InAsset, const FClothConfig &InConfig);

    /**
     * Release all GPU resources
     */
    void Release();

    /**
     * Main simulation step
     */
    void Simulate(float InDeltaTime);

    /**
     * Reset simulation to initial state
     */
    void ResetSimulation();

    // External forces
    void SetGravity(const FVector &InGravity);
    void SetWind(const FVector &InWind);
    void AddExternalForce(const FVector &InForce);

    // Configuration
    void SetConfig(const FClothConfig &InConfig);
    const FClothConfig &GetConfig() const { return Config; }

    // Data access
    const FClothSimulationData &GetSimulationData() const { return SimData; }
    ID3D11ShaderResourceView *GetPositionBufferSRV() const { return PositionSRV[CurrentBufferIndex]; }
    ID3D11ShaderResourceView *GetNormalBufferSRV() const { return NormalSRV; }

    // Constraint management
    void UpdateAttachmentConstraints(const TArray<FClothAttachmentData> &Attachments);
    void UpdateKinematicTargets(const TArray<FClothAttachmentData> &Attachments);
    void SetCollisionBodies(const TArray<FClothCollisionPrimitive> &Primitives);

    // Debug
    bool IsInitialized() const { return bInitialized; }
    uint32 GetNumParticles() const { return NumParticles; }
    uint32 GetNumConstraints() const { return NumConstraints; }

private:
    /**
     * Create GPU resources (buffers, views, shaders)
     */
    bool CreateGPUResources();

    /**
     * Create compute shaders
     */
    bool LoadComputeShaders();

    /**
     * Create structured buffers
     */
    bool CreateBuffers();

    /**
     * Create UAVs and SRVs
     */
    bool CreateViews();

    /**
     * Upload initial data to GPU
     */
    bool UploadInitialData();

    /**
     * Update constant buffer with current parameters
     */
    void UpdateConstantBuffers();

    /**
     * Dispatch compute shaders
     */
    void DispatchIntegration(float DeltaTime);
    void DispatchConstraintSolver(int32 Iteration);
    void DispatchBendConstraintSolver(int32 Iteration);
    void DispatchApplyKinematicTargets();
    void DispatchApplyConstraintDeltas();
    void DispatchNormalUpdate();

    /**
     * Helper to calculate dispatch thread group count
     */
    uint32 GetDispatchCount(uint32 ElementCount, uint32 ThreadGroupSize = 64) const;

    /**
     * DX11 simulation loop
     */
    void SimulateCS(float DeltaTime);

private:
    // Engine references
    FGraphicsDevice *Graphics;
    FDXDBufferManager *BufferManager;
    FDXDShaderManager *ShaderManager;

    // Compute shaders
    ID3D11ComputeShader *IntegrateCS;
    ID3D11ComputeShader *ConstraintSolverCS;
    ID3D11ComputeShader *BendConstraintSolverCS;
    ID3D11ComputeShader *ApplyKinematicTargetsCS;
    ID3D11ComputeShader *UpdateNormalsCS;
    ID3D11ComputeShader *ClearNormalsCS;
    ID3D11ComputeShader *NormalizeNormalsCS;
    ID3D11ComputeShader *ApplyConstraintDeltasCS;

    // Simulation buffers (ping-pong for positions)
    ID3D11Buffer *PositionBuffer[2];
    ID3D11Buffer *VelocityBuffer;
    ID3D11Buffer *InvMassBuffer;
    ID3D11Buffer *ConstraintBuffer;
    ID3D11Buffer *BendConstraintBuffer;
    ID3D11Buffer *KinematicTargetBuffer;
    ID3D11Buffer *IndexBuffer;
    ID3D11Buffer *NormalBuffer;

    // UAVs (Unordered Access Views) for compute shader write
    ID3D11UnorderedAccessView *PositionUAV[2];
    ID3D11UnorderedAccessView *VelocityUAV;
    ID3D11UnorderedAccessView *NormalUAV;
    ID3D11UnorderedAccessView *PositionDeltaUAV = nullptr;
    ID3D11UnorderedAccessView *PositionWeightUAV = nullptr;

    // SRVs (Shader Resource Views) for compute shader read
    ID3D11ShaderResourceView *PositionSRV[2];
    ID3D11ShaderResourceView *VelocitySRV;
    ID3D11ShaderResourceView *ConstraintSRV;
    ID3D11ShaderResourceView *BendConstraintSRV;
    ID3D11ShaderResourceView *KinematicTargetSRV;
    ID3D11ShaderResourceView *IndexSRV;
    ID3D11ShaderResourceView *NormalSRV;

    // Constant buffers
    ID3D11Buffer *ClothSimConstantBuffer;
    ID3D11Buffer *NormalUpdateConstantBuffer;
    ID3D11Buffer *PositionDeltaBuffer = nullptr;
    ID3D11Buffer *PositionWeightBuffer = nullptr;

    // Simulation state
    FClothSimulationData SimData;
    FClothConfig Config;

    // CPU-side data (for initialization and debug)
    TArray<FVector> RestPositions;
    TArray<float> InvMasses;
    TArray<FClothDistanceConstraint> Constraints;
    TArray<FClothBendConstraint> BendConstraints;
    TArray<FClothKinematicTargetGPU> KinematicTargets;
    TArray<uint32> Indices;

    // State tracking
    int32 CurrentBufferIndex;
    int32 readIdx;
    int32 writeIdx;

    uint32 NumParticles;
    uint32 NumConstraints;
    uint32 NumBendConstraints;
    uint32 NumKinematicTargets;
    uint32 NumTriangles;
    bool bInitialized;

    // External forces accumulator
    FVector ExternalForceAccum;

    static constexpr uint32 THREAD_GROUP_SIZE = 64;
};
