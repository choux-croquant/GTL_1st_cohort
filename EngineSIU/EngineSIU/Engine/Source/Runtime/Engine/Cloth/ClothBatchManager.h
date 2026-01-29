/**
 * Cloth Batch Manager
 * Manages a batch of cloth instances at a specific LOD level
 * Central component of the batched architecture
 */

#pragma once

#include "HAL/PlatformType.h"
#include "Container/Array.h"
#include "Container/Map.h"
#include "Math/Vector.h"
#include "ClothBatchTypes.h"
#include "ClothSimulationData.h"
#include "UObject/WeakObjectPtr.h"

// Forward declarations
class FClothBatchedSolver;
class FClothInstanceHandle;
class FClothCollisionManager;
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class USceneComponent;
struct ID3D11ShaderResourceView;

/**
 * Batch manager for a specific LOD level
 * Owns the batched solver and manages instance lifecycle
 */
class FClothBatchManager
{
public:
    FClothBatchManager(EClothLODLevel InLODLevel);
    ~FClothBatchManager();

    // Initialization
    void Initialize(FGraphicsDevice *Graphics,
                    FDXDBufferManager *BufferMgr,
                    FDXDShaderManager *ShaderMgr,
                    FClothCollisionManager *CollisionMgr);
    void Release();

    // Instance management
    FClothInstanceHandle *AddInstance(const FClothInstanceCreationParams &Params);
    void RemoveInstance(FClothInstanceHandle *Instance);
    void UpdateInstanceParameters(FClothInstanceHandle *Instance,
                                  const FClothInstanceParameters &Params);

    // Simulation
    void Update(float DeltaTime);
    void Simulate(float DeltaTime);
    void SimulateFixedTimestep(float DeltaTime);

    // Query
    int32 GetInstanceCount() const { return Instances.Num(); }
    uint32 GetTotalParticleCount() const { return TotalParticleCount; }
    uint32 GetTotalTriangleCount() const { return TotalTriangleCount; }
    EClothLODLevel GetLODLevel() const { return LODLevel; }
    bool CanAcceptInstance(uint32 ParticleCount) const;

    // Rendering data access
    ID3D11ShaderResourceView *GetPositionBufferSRV() const;
    ID3D11ShaderResourceView *GetNormalBufferSRV() const;
    const FClothInstanceMetadata &GetInstanceMetadata(int32 InstanceIndex) const;

    // Batch solver access
    FClothBatchedSolver *GetSolver() const { return BatchedSolver; }

    // Fixed timestep configuration
    void SetFixedTimestepConfig(const FClothFixedTimestepState &State) { FixedTimestepState = State; }
    const FClothFixedTimestepState &GetFixedTimestepConfig() const { return FixedTimestepState; }

private:
    void ReallocateBuffers();
    void CompactBuffers(); // Remove gaps from deleted instances
    void UpdateGPUBuffers();
    void UpdateInstanceParameterBuffer();
    void UpdateKinematicTargets(float DeltaTime);

    // NEW: GPU-based kinematic target methods
    void BuildKinematicAttachmentData();
    void UpdateKinematicTargetsGPU(float DeltaTime);

    bool NeedsReallocation(uint32 RequiredParticles, uint32 RequiredConstraints) const;
    uint32 CalculateNewCapacity(uint32 CurrentCapacity, uint32 RequiredCapacity) const;

private:
    EClothLODLevel LODLevel;
    FClothBatchedSolver *BatchedSolver;

    // Instance tracking
    TArray<FClothInstanceHandle *> Instances;
    TArray<FClothInstanceMetadata> InstanceMetadata;
    TMap<FClothInstanceHandle *, int32> InstanceToMetadataIndex;

    // Statistics
    uint32 TotalParticleCount;
    uint32 TotalConstraintCount;
    uint32 TotalBendConstraintCount;
    uint32 TotalKinematicTargetCount;
    uint32 TotalTriangleCount;
    uint32 TotalAreaConstraintCount; // NEW: Total area constraints

    // Buffer management
    uint32 AllocatedParticleCapacity;
    uint32 AllocatedConstraintCapacity;
    uint32 AllocatedBendConstraintCapacity;
    uint32 AllocatedKinematicTargetCapacity;
    uint32 AllocatedTriangleCapacity;
    uint32 AllocatedInstanceCapacity;
    uint32 AllocatedAreaConstraintCapacity; // NEW: Allocated area constraints
    bool bNeedsReallocation;
    bool bNeedsCompaction;
    float GrowthFactor;

    // Fixed timestep
    FClothFixedTimestepState FixedTimestepState;

    // Graphics resources
    FGraphicsDevice *Graphics;
    FDXDBufferManager *BufferManager;
    FDXDShaderManager *ShaderManager;

    // NEW: Component deduplication for GPU-based kinematic targets (P1 optimization)
    TMap<USceneComponent *, uint32> ComponentIndexMap;        // Component -> Index mapping
    TArray<TWeakObjectPtr<USceneComponent>> UniqueComponents; // Deduplicated component list
    uint32 TotalAttachmentCount;                              // Total number of attachments built for GPU
    bool bAttachmentDataDirty;                                // Needs rebuild when attachments change

    bool bIsInitialized;
};
