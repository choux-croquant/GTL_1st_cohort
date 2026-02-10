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
    
    // NEW: Production rendering buffer allocation (called on-demand)
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

    // NEW: GPU-based kinematic target computation (P1 optimization)
    void UploadAttachmentData(const TArray<FKinematicAttachmentGPU> &Attachments);
    void UploadComponentTransforms(const TArray<FMatrix> &Transforms);

    // NEW: Production rendering - render mesh data upload
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

    // Index buffer access
    ID3D11Buffer *GetUnifiedIndexBuffer() const { return UnifiedIndexBuffer; }
    ID3D11Buffer *GetUnifiedRenderIndexBuffer() const { return UnifiedRenderIndexBuffer; }
    
    // NEW: Production rendering - buffer access for rendering
    ID3D11ShaderResourceView* GetSkinningWeightBufferSRV() const { return SkinningWeightsSRV; }
    ID3D11Buffer* GetUnifiedRenderVertexBuffer() const { return UnifiedRenderVertexBuffer; }

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
        uint32 Attachments, uint32 Triangles, uint32 Instances, uint32 AreaConstraints = 0, uint32 EdgeCollisions = 0);

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
    void DispatchComputeKinematicTargets(uint32 AttachmentCount);
    void DispatchFinalize(uint32 ParticleCount);
    void DispatchUpdateNormals(uint32 TriangleCount);
    void DispatchCollisionSDF(uint32 ParticleCount);
    void DispatchEdgeCollisionSDF(uint32 EdgeCollisionCount);
    void DispatchSelfCollision(uint32 ParticleCount);

    void UpdateFrameConstants(float DeltaTime);
    void UpdateSelfCollisionParams(const TArray<FClothInstanceMetadata>& InstanceMetadata);
    void UpdateIterationConstants(int32 CurrentIteration);
    
    // NEW: Dynamic bounds tracking methods
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
    ID3D11ComputeShader *AreaConstraintSolverCS; // NEW: Area constraint solver
    ID3D11ComputeShader *ApplyDeltasCS;
    ID3D11ComputeShader *ComputeKinematicTargetsCS; // NEW: GPU-based kinematic (P1)
    ID3D11ComputeShader *FinalizeCS;                // NEW: Velocity finalization shader
    ID3D11ComputeShader *UpdateNormalsCS;           // Legacy single-pass normal update
    ID3D11ComputeShader *ComputeTriangleNormalsCS;  // NEW: Pass 1 - Triangle normal accumulation
    ID3D11ComputeShader *NormalizeVertexNormalsCS;  // NEW: Pass 2 - Vertex normal normalization
    ID3D11ComputeShader *CollisionSolverCS;        // NEW: SDF collision shader
    ID3D11ComputeShader *EdgeCollisionSolverCS;    // NEW: Edge-based SDF collision shader
    
    // Self-collision compute shaders
    ID3D11ComputeShader *SelfCollisionBuildGridCS;  // NEW: Build spatial hash grid
    ID3D11ComputeShader *SelfCollisionSolverCS;     // NEW: Solve self-collisions
    
    // NEW: GPU bounds computation shaders
    ID3D11ComputeShader *ComputeBoundsPass1CS;      // Pass 1: Per-group reduction
    ID3D11ComputeShader *ComputeBoundsPass2CS;      // Pass 2: Final reduction

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
    ID3D11Buffer *UnifiedNormalAccumulationBuffer;  // NEW: Integer accumulation buffer for atomic normal updates
    ID3D11Buffer *UnifiedPositionDeltaBuffer;
    ID3D11Buffer *UnifiedPositionWeightBuffer;

    // NEW: GPU-based kinematic target buffers (P1 optimization)
    ID3D11Buffer *AttachmentDataBuffer;     // Static attachment data
    ID3D11Buffer *ComponentTransformBuffer; // Dynamic component transforms
    
    // NEW: Production rendering buffers (for high-res render mesh with GPU skinning)
    ID3D11Buffer *UnifiedRenderVertexBuffer;        // Unified render vertex buffer (position, normal, UV)
    ID3D11Buffer *UnifiedRenderIndexBuffer;         // Unified render index buffer
    ID3D11Buffer *UnifiedSkinningWeightBuffer;      // Unified skinning weight buffer (render → sim mapping)
    ID3D11Buffer *RenderNormalsBuffer;              // Interpolated render mesh normals (optional, for compute-based skinning)
    ID3D11Buffer *RenderPositionsBuffer;            // Skinned render mesh positions (optional, for compute-based skinning)
    
    // Self-collision buffers
    ID3D11Buffer *SelfCollisionCellCountersBuffer;  // Per-cell particle counters
    ID3D11Buffer *SelfCollisionCellDataBuffer;      // Flat array of particle indices per cell
    ID3D11Buffer *SelfCollisionParamsBuffer;        // Constant buffer for self-collision parameters
    
    // NEW: GPU bounds computation buffers
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
    ID3D11ShaderResourceView *UnifiedAreaConstraintSRV;       // NEW: Area constraint SRV
    ID3D11ShaderResourceView *UnifiedEdgeCollisionSRV;        // NEW: Edge collision SRV
    ID3D11ShaderResourceView *UnifiedKinematicTargetSRV;
    ID3D11ShaderResourceView *UnifiedIndexSRV;
    ID3D11ShaderResourceView *UnifiedNormalSRV;

    // NEW: GPU-based kinematic target SRVs (P1 optimization)
    ID3D11ShaderResourceView *AttachmentDataSRV;
    ID3D11ShaderResourceView *ComponentTransformSRV;
    
    // NEW: Production rendering UAVs/SRVs
    ID3D11ShaderResourceView *UnifiedRenderVertexSRV;
    ID3D11ShaderResourceView *UnifiedRenderIndexSRV;
    ID3D11ShaderResourceView *SkinningWeightsSRV;
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
    uint32 AllocatedAreaConstraintCapacity;      // NEW: Area constraint capacity
    uint32 AllocatedEdgeCollisionCapacity;       // NEW: Edge collision capacity
    uint32 AllocatedRenderVertexCapacity;        // NEW: Render vertex capacity (for production rendering)
    uint32 AllocatedRenderIndexCapacity;         // NEW: Render index capacity (for production rendering)

    uint32 UsedParticleCount;
    uint32 UsedConstraintCount;
    uint32 UsedBendConstraintCount;
    uint32 UsedTriangleCount;
    uint32 UsedInstanceCount;
    uint32 UsedAttachmentCount;                  // NEW: For GPU-based kinematic targets (P1)
    uint32 UsedAreaConstraintCount;              // NEW: Area constraint count
    uint32 UsedEdgeCollisionCount;               // NEW: Edge collision count
    uint32 UsedRenderVertexCount;                // NEW: Render vertex count (for production rendering)
    uint32 UsedRenderIndexCount;                 // NEW: Render index count (for production rendering)

    bool bInitialized;

    // NEW: Substep timing state
    float AccumulatedTime;

    // NEW: P2 optimization - cached constants to avoid repeated full buffer uploads
    FClothSimConstants CachedConstants;
    
    // Self-collision state
    FClothSelfCollisionParams SelfCollisionParams;
    uint32 AllocatedSelfCollisionCells;
    bool bSelfCollisionInitialized;

    static constexpr uint32 THREAD_GROUP_SIZE = 64;
};
