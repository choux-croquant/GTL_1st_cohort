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
#include "ShaderConstants.h"

// Forward declarations
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class FClothCollisionManager;

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
                    FDXDShaderManager *ShaderMgr,
                    FClothCollisionManager *InCollisionManager);
    void Release();

    // Buffer allocation
    bool AllocateBuffers(uint32 MaxParticles,
                         uint32 MaxConstraints,
                         uint32 MaxBendConstraints,
                         uint32 MaxKinematicTargets,
                         uint32 MaxTriangles,
                         uint32 MaxInstances,
                         uint32 MaxAreaConstraints = 0,
                         uint32 MaxEdgeCollisions = 0);

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

    void UploadAreaConstraintData(const TArray<FClothAreaConstraintGPU> &AreaConstraints,
                                  uint32 DestOffset);

    void UploadEdgeCollisionData(const TArray<FClothEdgeCollisionConstraintGPU> &EdgeCollisions,
                                 uint32 DestOffset);

    void UploadKinematicTargets(const TArray<FClothKinematicTargetGPU> &Targets,
                                uint32 DestOffset);

    void UploadInstanceParameters(const TArray<FClothInstanceParameters> &Parameters);

    void UploadIndexData(const TArray<uint32> &Indices, uint32 DestOffset);

    // NEW: GPU-based kinematic target computation (P1 optimization)
    void UploadAttachmentData(const TArray<FKinematicAttachmentGPU> &Attachments);
    void UploadComponentTransforms(const TArray<FMatrix> &Transforms);

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
    uint32 GetUsedBendConstraintCount() const { return UsedBendConstraintCount; }
    uint32 GetUsedAttachmentCount() const { return UsedAttachmentCount; }
    uint32 GetUsedInstanceCount() const { return UsedInstanceCount; }

    // Update tracking counts
    void SetUsedCounts(uint32 Particles, uint32 Constraints, uint32 BendConstraints,
                       uint32 KinematicTargets, uint32 Triangles, uint32 Instances,
                       uint32 AreaConstraints = 0, uint32 EdgeCollisions = 0);

    // NEW: Set attachment count for GPU-based kinematic targets (P1)
    void SetAttachmentCount(uint32 AttachmentCount) { UsedAttachmentCount = AttachmentCount; }

    FClothCollisionManager *GetCollisionManager() { return CollisionManager; }

private:
    // Simulation methods
    void SimulateSubstep(float SubstepDeltaTime); // NEW: Substep simulation

    // Dispatch methods
    void DispatchIntegration(uint32 ParticleCount);
    void DispatchConstraintSolver(uint32 ConstraintCount);
    void DispatchBendConstraintSolver(uint32 BendConstraintCount);
    void DispatchAreaConstraintSolver(uint32 AreaConstraintCount);
    void DispatchApplyDeltas(uint32 ParticleCount);
    void DispatchApplyKinematicTargets(uint32 TargetCount);
    void DispatchComputeKinematicTargets(uint32 AttachmentCount); // NEW: GPU-based kinematic (P1)
    void DispatchFinalize(uint32 ParticleCount);                  // NEW: Velocity finalization
    void DispatchClearNormals(uint32 ParticleCount);
    void DispatchUpdateNormals(uint32 TriangleCount);
    void DispatchNormalizeNormals(uint32 ParticleCount);
    void DispatchCollisionSDF(uint32 ParticleCount);         // NEW: SDF collision solver
    void DispatchEdgeCollisionSDF(uint32 EdgeCollisionCount); // NEW: Edge-based SDF collision

    void ClearAccumulationBuffers(uint32 ParticleCount);
    // void UpdateConstantBuffers(float DeltaTime);
    void UpdateFrameConstants(float DeltaTime);            // NEW: P2 optimization
    void UpdateIterationConstants(int32 CurrentIteration); // NEW: P2 optimization

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
    ID3D11ComputeShader *AreaConstraintSolverCS; // NEW: Area constraint solver
    ID3D11ComputeShader *ApplyDeltasCS;
    ID3D11ComputeShader *ApplyKinematicTargetsCS;
    ID3D11ComputeShader *ComputeKinematicTargetsCS; // NEW: GPU-based kinematic (P1)
    ID3D11ComputeShader *FinalizeCS;                // NEW: Velocity finalization shader
    ID3D11ComputeShader *ClearNormalsCS;
    ID3D11ComputeShader *UpdateNormalsCS;
    ID3D11ComputeShader *NormalizeNormalsCS;
    ID3D11ComputeShader *CollisionSolverCS;     // NEW: SDF collision shader
    ID3D11ComputeShader *EdgeCollisionSolverCS; // NEW: Edge-based SDF collision shader

    // Collision manager (NEW)
    FClothCollisionManager *CollisionManager;

    // Unified GPU buffers (Velvet pattern - single working buffer)
    ID3D11Buffer *UnifiedPositionBuffer;  // Final position buffer (for rendering)
    ID3D11Buffer *UnifiedPredictedBuffer; // Working buffer (for constraint solving)
    ID3D11Buffer *UnifiedVelocityBuffer;
    ID3D11Buffer *UnifiedInvMassBuffer;
    ID3D11Buffer *UnifiedConstraintBuffer;
    ID3D11Buffer *UnifiedBendConstraintBuffer;
    ID3D11Buffer *UnifiedAreaConstraintBuffer;       // NEW: Area constraint buffer
    ID3D11Buffer *UnifiedEdgeCollisionBuffer;        // NEW: Edge collision constraint buffer
    ID3D11Buffer *UnifiedKinematicTargetBuffer;
    ID3D11Buffer *UnifiedIndexBuffer;
    ID3D11Buffer *UnifiedNormalBuffer;
    ID3D11Buffer *UnifiedPositionDeltaBuffer;
    ID3D11Buffer *UnifiedPositionWeightBuffer;

    // NEW: GPU-based kinematic target buffers (P1 optimization)
    ID3D11Buffer *AttachmentDataBuffer;     // Static attachment data
    ID3D11Buffer *ComponentTransformBuffer; // Dynamic component transforms

    // UAVs and SRVs
    ID3D11UnorderedAccessView *UnifiedPositionUAV;
    ID3D11UnorderedAccessView *UnifiedPredictedUAV;
    ID3D11UnorderedAccessView *UnifiedVelocityUAV;
    ID3D11UnorderedAccessView *UnifiedNormalUAV;
    ID3D11UnorderedAccessView *UnifiedPositionDeltaUAV;
    ID3D11UnorderedAccessView *UnifiedPositionWeightUAV;
    ID3D11UnorderedAccessView *UnifiedConstraintUAV;     // NEW: For XPBD lambda write-back (distance constraints)
    ID3D11UnorderedAccessView *UnifiedBendConstraintUAV; // NEW: For XPBD lambda write-back (bend constraints)
    ID3D11UnorderedAccessView *UnifiedAreaConstraintUAV; // NEW: For XPBD lambda write-back (area constraints)

    ID3D11ShaderResourceView *UnifiedPositionSRV;
    ID3D11ShaderResourceView *UnifiedPredictedSRV;
    ID3D11ShaderResourceView *UnifiedVelocitySRV;
    ID3D11ShaderResourceView *UnifiedInvMassSRV;
    ID3D11ShaderResourceView *UnifiedConstraintSRV;
    ID3D11ShaderResourceView *UnifiedBendConstraintSRV;
    ID3D11ShaderResourceView *UnifiedAreaConstraintSRV;       // NEW: Area constraint SRV
    ID3D11ShaderResourceView *UnifiedEdgeCollisionSRV;        // NEW: Edge collision SRV
    ID3D11ShaderResourceView *UnifiedKinematicTargetSRV;
    ID3D11ShaderResourceView *UnifiedIndexSRV;
    ID3D11ShaderResourceView *UnifiedNormalSRV;

    // NEW: GPU-based kinematic target SRVs (P1 optimization)
    ID3D11ShaderResourceView *AttachmentDataSRV;
    ID3D11ShaderResourceView *ComponentTransformSRV;

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
    uint32 AllocatedKinematicTargetCapacity;
    uint32 AllocatedTriangleCapacity;
    uint32 AllocatedInstanceCapacity;
    uint32 AllocatedAreaConstraintCapacity;      // NEW: Area constraint capacity
    uint32 AllocatedEdgeCollisionCapacity;       // NEW: Edge collision capacity

    uint32 UsedParticleCount;
    uint32 UsedConstraintCount;
    uint32 UsedBendConstraintCount;
    uint32 UsedKinematicTargetCount;
    uint32 UsedTriangleCount;
    uint32 UsedInstanceCount;
    uint32 UsedAttachmentCount;                  // NEW: For GPU-based kinematic targets (P1)
    uint32 UsedAreaConstraintCount;              // NEW: Area constraint count
    uint32 UsedEdgeCollisionCount;               // NEW: Edge collision count

    bool bInitialized;

    // NEW: Substep timing state
    float AccumulatedTime;

    // NEW: P2 optimization - cached constants to avoid repeated full buffer uploads
    FClothSimConstants CachedConstants;

    static constexpr uint32 THREAD_GROUP_SIZE = 64;
};
