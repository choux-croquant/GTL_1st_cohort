/**
 * Cloth Batched Solver
 * GPU-based cloth simulation for multiple instances using unified buffers
 * Replaces per-instance FClothSolver with batched approach
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include <d3d11.h>

#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"
#include "ClothSimulationData.h"
#include "ClothGPUStructs.h"
#include "ClothBatchTypes.h"

// Forward declarations
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;

/**
 * Batched cloth solver
 * Handles GPU compute shader simulation for multiple cloth instances in unified buffers
 */
class FClothBatchedSolver
{
public:
    FClothBatchedSolver();
    ~FClothBatchedSolver();

    // Initialization
    void Initialize(FGraphicsDevice *Graphics,
                    FDXDBufferManager *BufferMgr,
                    FDXDShaderManager *ShaderMgr);
    void Release();

    // Buffer allocation
    bool AllocateBuffers(uint32 MaxParticles,
                         uint32 MaxConstraints,
                         uint32 MaxBendConstraints,
                         uint32 MaxKinematicTargets,
                         uint32 MaxTriangles,
                         uint32 MaxInstances);

    // Simulation
    void Simulate(float DeltaTime);

    // Data upload
    void UploadParticleData(const TArray<FVector> &Positions,
                            const TArray<float> &InvMasses,
                            const TArray<uint32> &InstanceIDs,
                            uint32 DestOffset);

    void UploadConstraintData(const TArray<FClothDistanceConstraintGPU> &Constraints,
                              uint32 DestOffset);

    void UploadBendConstraintData(const TArray<FClothBendConstraintGPU> &BendConstraints,
                                  uint32 DestOffset);

    void UploadShearConstraintData(const TArray<FClothShearConstraintGPU> &ShearConstraints,
                                   uint32 DestOffset); // NEW: Phase 3

    void UploadAreaConstraintData(const TArray<FClothAreaConstraintGPU> &AreaConstraints,
                                  uint32 DestOffset); // NEW: Phase 4

    void UploadKinematicTargets(const TArray<FClothKinematicTargetGPU> &Targets,
                                uint32 DestOffset);

    void UploadInstanceParameters(const TArray<FClothInstanceParameters> &Parameters);

    void UploadIndexData(const TArray<uint32> &Indices, uint32 DestOffset);

    // Index buffer access
    ID3D11Buffer *GetUnifiedIndexBuffer() const { return UnifiedIndexBuffer; }

    // Configuration
    void SetConfig(const FClothConfig &InConfig);
    const FClothConfig &GetConfig() const { return Config; }

    // Data access for rendering
    ID3D11ShaderResourceView *GetPositionBufferSRV() const;
    ID3D11ShaderResourceView *GetNormalBufferSRV() const;

    // Initialization state
    bool IsInitialized() const { return bInitialized; }

    // Statistics
    uint32 GetAllocatedParticleCapacity() const { return AllocatedParticleCapacity; }
    uint32 GetUsedParticleCount() const { return UsedParticleCount; }
    uint32 GetUsedConstraintCount() const { return UsedConstraintCount; }
    uint32 GetUsedInstanceCount() const { return UsedInstanceCount; }

    // Update tracking counts
    void SetUsedCounts(uint32 Particles, uint32 Constraints, uint32 BendConstraints,
                       uint32 ShearConstraints, // NEW: Phase 3
                       uint32 AreaConstraints,  // NEW: Phase 4
                       uint32 KinematicTargets, uint32 Triangles, uint32 Instances);

private:
    // Simulation methods
    void SimulateSubstep(float SubstepDeltaTime); // NEW: Substep simulation

    // Dispatch methods
    void DispatchIntegration(uint32 ParticleCount);
    void DispatchConstraintSolver(uint32 ConstraintCount);
    void DispatchBendConstraintSolver(uint32 BendConstraintCount);
    void DispatchShearConstraintSolver(uint32 ShearConstraintCount); // NEW: Phase 3
    void DispatchAreaConstraintSolver(uint32 AreaConstraintCount);   // NEW: Phase 4
    void DispatchApplyDeltas(uint32 ParticleCount);
    void DispatchApplyKinematicTargets(uint32 TargetCount);
    void DispatchFinalize(uint32 ParticleCount, int32 OldPositionBufferIndex); // NEW: Velocity finalization
    void DispatchClearNormals(uint32 ParticleCount);
    void DispatchUpdateNormals(uint32 TriangleCount);
    void DispatchNormalizeNormals(uint32 ParticleCount);

    void ClearAccumulationBuffers(uint32 ParticleCount);
    void UpdateConstantBuffers(float DeltaTime);

    bool CreateGPUResources();
    bool LoadComputeShaders();

    uint32 GetDispatchCount(uint32 ElementCount, uint32 ThreadGroupSize = 64) const;

private:
    // Graphics resources
    FGraphicsDevice *Graphics;
    FDXDBufferManager *BufferManager;
    FDXDShaderManager *ShaderManager;

    // Compute shaders
    ID3D11ComputeShader *IntegrateCS;
    ID3D11ComputeShader *ConstraintSolverCS;
    ID3D11ComputeShader *BendConstraintSolverCS;
    ID3D11ComputeShader *ShearConstraintSolverCS; // NEW: Phase 3
    ID3D11ComputeShader *AreaConstraintSolverCS;  // NEW: Phase 4
    ID3D11ComputeShader *ApplyDeltasCS;
    ID3D11ComputeShader *ApplyKinematicTargetsCS;
    ID3D11ComputeShader *FinalizeCS; // NEW: Velocity finalization shader
    ID3D11ComputeShader *ClearNormalsCS;
    ID3D11ComputeShader *UpdateNormalsCS;
    ID3D11ComputeShader *NormalizeNormalsCS;

    // Unified GPU buffers
    ID3D11Buffer *UnifiedPositionBuffer[2]; // Ping-pong
    ID3D11Buffer *UnifiedVelocityBuffer;
    ID3D11Buffer *UnifiedInvMassBuffer;
    ID3D11Buffer *UnifiedConstraintBuffer;
    ID3D11Buffer *UnifiedBendConstraintBuffer;
    ID3D11Buffer *UnifiedShearConstraintBuffer; // NEW: Shear constraints (Phase 3)
    ID3D11Buffer *UnifiedAreaConstraintBuffer;  // NEW: Area constraints (Phase 4)
    ID3D11Buffer *UnifiedKinematicTargetBuffer;
    ID3D11Buffer *UnifiedIndexBuffer;
    ID3D11Buffer *UnifiedNormalBuffer;
    ID3D11Buffer *UnifiedPositionDeltaBuffer;
    ID3D11Buffer *UnifiedPositionWeightBuffer;

    // UAVs and SRVs
    ID3D11UnorderedAccessView *UnifiedPositionUAV[2];
    ID3D11UnorderedAccessView *UnifiedVelocityUAV;
    ID3D11UnorderedAccessView *UnifiedNormalUAV;
    ID3D11UnorderedAccessView *UnifiedPositionDeltaUAV;
    ID3D11UnorderedAccessView *UnifiedPositionWeightUAV;
    ID3D11UnorderedAccessView *UnifiedConstraintUAV; // NEW: For lambda updates

    ID3D11ShaderResourceView *UnifiedPositionSRV[2];
    ID3D11ShaderResourceView *UnifiedVelocitySRV;
    ID3D11ShaderResourceView *UnifiedInvMassSRV;
    ID3D11ShaderResourceView *UnifiedConstraintSRV;
    ID3D11ShaderResourceView *UnifiedBendConstraintSRV;
    ID3D11UnorderedAccessView *UnifiedBendConstraintUAV;  // NEW: For lambda updates (Phase 5)
    ID3D11ShaderResourceView *UnifiedShearConstraintSRV;  // NEW: Shear (Phase 3)
    ID3D11UnorderedAccessView *UnifiedShearConstraintUAV; // NEW: For lambda updates
    ID3D11ShaderResourceView *UnifiedAreaConstraintSRV;   // NEW: Area (Phase 4)
    ID3D11UnorderedAccessView *UnifiedAreaConstraintUAV;  // NEW: For lambda updates
    ID3D11ShaderResourceView *UnifiedKinematicTargetSRV;
    ID3D11ShaderResourceView *UnifiedIndexSRV;
    ID3D11ShaderResourceView *UnifiedNormalSRV;

    // Per-instance parameter buffer
    ID3D11Buffer *InstanceParameterBuffer;
    ID3D11ShaderResourceView *InstanceParameterSRV;

    // Constant buffer
    ID3D11Buffer *BatchSimConstantBuffer;

    // State
    FClothConfig Config;
    uint32 AllocatedParticleCapacity;
    uint32 AllocatedConstraintCapacity;
    uint32 AllocatedBendConstraintCapacity;
    uint32 AllocatedShearConstraintCapacity; // NEW: Phase 3
    uint32 AllocatedAreaConstraintCapacity;  // NEW: Phase 4
    uint32 AllocatedKinematicTargetCapacity;
    uint32 AllocatedTriangleCapacity;
    uint32 AllocatedInstanceCapacity;

    uint32 UsedParticleCount;
    uint32 UsedConstraintCount;
    uint32 UsedBendConstraintCount;
    uint32 UsedShearConstraintCount; // NEW: Phase 3
    uint32 UsedAreaConstraintCount;  // NEW: Phase 4
    uint32 UsedKinematicTargetCount;
    uint32 UsedTriangleCount;
    uint32 UsedInstanceCount;

    int32 CurrentBufferIndex;
    bool bInitialized;

    // NEW: Substep timing state
    float AccumulatedTime;

    static constexpr uint32 THREAD_GROUP_SIZE = 64;
};
