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
#include "Cloth/ClothAssetGenerator.h"

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
    
    bool AllocateRenderBuffers(uint32 MaxRenderVertices, uint32 MaxRenderIndices);

    // Simulation
    void Simulate(float DeltaTime, TArray<FClothInstanceMetadata>& InstanceMetadata);

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

    void UploadInstanceParameters(const TArray<FClothInstanceParameters> &Parameters);
    void UploadIndexData(const TArray<uint32> &Indices, uint32 DestOffset);
    void UploadAttachmentData(const TArray<FKinematicAttachmentGPU> &Attachments);
    void UploadComponentTransforms(const TArray<FMatrix> &Transforms);

    void UploadRenderMeshData(
        const TArray<FVector>& RenderPositions,
        const TArray<FVector>& RenderNormals,
        const TArray<FVector2D>& RenderUVs,
        const TArray<uint32>& RenderIndices,
        uint32 RenderVertexOffset,
        uint32 RenderIndexOffset);
    
    void UploadSkinningWeights(
        const TArray<struct FClothSkinningWeight>& Weights,
        uint32 RenderVertexOffset);
    
    void UploadTriangleSkinningWeights(
        const TArray<struct FClothSkinningWeightTriangle>& Weights,
        uint32 RenderVertexOffset);

    // Index buffer access
    ID3D11Buffer *GetUnifiedIndexBuffer() const { return UnifiedIndexBuffer; }
    ID3D11Buffer *GetUnifiedRenderIndexBuffer() const { return UnifiedRenderIndexBuffer; }
    
    ID3D11ShaderResourceView* GetSkinningWeightBufferSRV() const { return SkinningWeightsSRV; }
    ID3D11ShaderResourceView* GetTriangleSkinningWeightBufferSRV() const { return TriangleSkinningWeightsSRV; }
    ID3D11Buffer* GetUnifiedRenderVertexBuffer() const { return UnifiedRenderVertexBuffer; }

    // Configuration
    void SetConfig(const FClothConfig &InConfig);
    const FClothConfig &GetConfig() const { return Config; }

    // Data access for rendering
    ID3D11ShaderResourceView *GetPositionBufferSRV() const;
    ID3D11ShaderResourceView *GetNormalBufferSRV() const;

    ID3D11Buffer* GetInvMassBuffer() const { return UnifiedInvMassBuffer; }

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
        uint32 Attachments, uint32 Triangles, uint32 Instances, uint32 AreaConstraints = 0, uint32 EdgeCollisions = 0);

    void SetAttachmentCount(uint32 AttachmentCount) { UsedAttachmentCount = AttachmentCount; }

    FClothCollisionManager *GetCollisionManager() { return CollisionManager; }

private:
    // Simulation methods
    void SimulateSubstep(float SubstepDeltaTime);

    // Dispatch methods
    void DispatchIntegration(uint32 ParticleCount);
    void DispatchConstraintSolver(uint32 ConstraintCount);
    void DispatchBendConstraintSolver(uint32 BendConstraintCount);
    void DispatchAreaConstraintSolver(uint32 AreaConstraintCount);
    void DispatchApplyDeltas(uint32 ParticleCount);
    void DispatchComputeKinematicTargets(uint32 AttachmentCount);
    void DispatchFinalize(uint32 ParticleCount);
    void DispatchUpdateNormals(uint32 TriangleCount);
    void DispatchCollisionSDF(uint32 ParticleCount);
    void DispatchEdgeCollisionSDF(uint32 EdgeCollisionCount);
    void DispatchSelfCollision(uint32 ParticleCount);

    void UpdateFrameConstants(float DeltaTime);
    void UpdateSelfCollisionParams(const TArray<FClothInstanceMetadata>& InstanceMetadata);
    void UpdateIterationConstants(int32 CurrentIteration);
    
    void UpdateDynamicBounds(TArray<FClothInstanceMetadata>& InstanceMetadata);
    void DispatchComputeBounds(uint32 ParticleCount);
    void ReadbackBounds(FVector& OutMin, FVector& OutMax);
   
    bool CreateGPUResources();
    bool LoadComputeShaders();
    bool AllocateSelfCollisionBuffers();
    bool AllocateBoundsComputeBuffers();

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
    ID3D11ComputeShader *AreaConstraintSolverCS;
    ID3D11ComputeShader *ApplyDeltasCS;
    ID3D11ComputeShader *ComputeKinematicTargetsCS;
    ID3D11ComputeShader *FinalizeCS;
    ID3D11ComputeShader *UpdateNormalsCS;
    ID3D11ComputeShader *ComputeTriangleNormalsCS;
    ID3D11ComputeShader *NormalizeVertexNormalsCS;
    ID3D11ComputeShader *CollisionSolverCS;
    ID3D11ComputeShader *EdgeCollisionSolverCS;
    
    // Self-collision compute shaders
    ID3D11ComputeShader *SelfCollisionBuildGridCS;
    ID3D11ComputeShader *SelfCollisionSolverCS;
    
    // NEW: GPU bounds computation shaders
    ID3D11ComputeShader *ComputeBoundsPass1CS;
    ID3D11ComputeShader *ComputeBoundsPass2CS;

    // Collision manager (NEW)
    FClothCollisionManager *CollisionManager;

    ID3D11Buffer *UnifiedPositionBuffer;
    ID3D11Buffer *UnifiedPredictedBuffer;
    ID3D11Buffer *UnifiedVelocityBuffer;
    ID3D11Buffer *UnifiedInvMassBuffer;
    ID3D11Buffer *UnifiedConstraintBuffer;
    ID3D11Buffer *UnifiedBendConstraintBuffer;
    ID3D11Buffer *UnifiedAreaConstraintBuffer;
    ID3D11Buffer *UnifiedEdgeCollisionBuffer;
    ID3D11Buffer *UnifiedKinematicTargetBuffer;
    ID3D11Buffer *UnifiedIndexBuffer;
    ID3D11Buffer *UnifiedNormalBuffer;
    ID3D11Buffer *UnifiedNormalAccumulationBuffer;
    ID3D11Buffer *UnifiedPositionDeltaBuffer;
    ID3D11Buffer *UnifiedPositionWeightBuffer;

    ID3D11Buffer *AttachmentDataBuffer;     // Static attachment data
    ID3D11Buffer *ComponentTransformBuffer; // Dynamic component transforms
    
    ID3D11Buffer *UnifiedRenderVertexBuffer;        // Unified render vertex buffer (position, normal, UV)
    ID3D11Buffer *UnifiedRenderIndexBuffer;         // Unified render index buffer
    ID3D11Buffer *UnifiedSkinningWeightBuffer;      // Unified legacy K-nearest neighbor skinning weight buffer
    ID3D11Buffer *UnifiedTriangleSkinningWeightBuffer;
    ID3D11Buffer *RenderNormalsBuffer;              // Interpolated render mesh normals (optional, for compute-based skinning)
    ID3D11Buffer *RenderPositionsBuffer;            // Skinned render mesh positions (optional, for compute-based skinning)
    
    // Self-collision buffers
    ID3D11Buffer *SelfCollisionCellCountersBuffer;  // Per-cell particle counters
    ID3D11Buffer *SelfCollisionCellDataBuffer;      // Flat array of particle indices per cell
    ID3D11Buffer *SelfCollisionParamsBuffer;        // Constant buffer for self-collision parameters
    
    ID3D11Buffer *BoundsComputeBuffer;              // Intermediate bounds (one per thread group)
    ID3D11Buffer *BoundsReadbackBuffer;             // Staging buffer for CPU readback
    ID3D11UnorderedAccessView *BoundsComputeUAV;    // UAV for bounds computation

    // UAVs and SRVs
    ID3D11UnorderedAccessView *UnifiedPositionUAV;
    ID3D11UnorderedAccessView *UnifiedPredictedUAV;
    ID3D11UnorderedAccessView *UnifiedVelocityUAV;
    ID3D11UnorderedAccessView *UnifiedNormalUAV;
    ID3D11UnorderedAccessView *UnifiedNormalAccumulationUAV;  // NEW: UAV for integer accumulation buffer
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
    ID3D11ShaderResourceView *UnifiedAreaConstraintSRV;
    ID3D11ShaderResourceView *UnifiedEdgeCollisionSRV;
    ID3D11ShaderResourceView *UnifiedKinematicTargetSRV;
    ID3D11ShaderResourceView *UnifiedIndexSRV;
    ID3D11ShaderResourceView *UnifiedNormalSRV;

    ID3D11ShaderResourceView *AttachmentDataSRV;
    ID3D11ShaderResourceView *ComponentTransformSRV;
    
    ID3D11ShaderResourceView *UnifiedRenderVertexSRV;
    ID3D11ShaderResourceView *UnifiedRenderIndexSRV;
    ID3D11ShaderResourceView *SkinningWeightsSRV;           // Legacy K-nearest neighbor weights
    ID3D11ShaderResourceView *TriangleSkinningWeightsSRV;
    ID3D11UnorderedAccessView *RenderNormalsUAV;     // For compute-based skinning (optional)
    ID3D11ShaderResourceView *RenderNormalsSRV;      // For compute-based skinning (optional)
    ID3D11ShaderResourceView *RenderPositionsSRV;    // For compute-based skinning (optional)
    
    // Self-collision UAVs and SRVs
    ID3D11UnorderedAccessView *SelfCollisionCellCountersUAV;
    ID3D11UnorderedAccessView *SelfCollisionCellDataUAV;
    ID3D11ShaderResourceView *SelfCollisionCellCountersSRV;
    ID3D11ShaderResourceView *SelfCollisionCellDataSRV;

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
    uint32 AllocatedAreaConstraintCapacity;
    uint32 AllocatedEdgeCollisionCapacity;
    uint32 AllocatedRenderVertexCapacity;
    uint32 AllocatedRenderIndexCapacity;

    uint32 UsedParticleCount;
    uint32 UsedConstraintCount;
    uint32 UsedBendConstraintCount;
    uint32 UsedTriangleCount;
    uint32 UsedInstanceCount;
    uint32 UsedAttachmentCount;
    uint32 UsedAreaConstraintCount;
    uint32 UsedEdgeCollisionCount;
    uint32 UsedRenderVertexCount;
    uint32 UsedRenderIndexCount;

    bool bInitialized;

    float AccumulatedTime;

    FClothSimConstants CachedConstants;
    
    // Self-collision state
    FClothSelfCollisionParams SelfCollisionParams;
    uint32 AllocatedSelfCollisionCells;
    bool bSelfCollisionInitialized;

    static constexpr uint32 THREAD_GROUP_SIZE = 64;
};
