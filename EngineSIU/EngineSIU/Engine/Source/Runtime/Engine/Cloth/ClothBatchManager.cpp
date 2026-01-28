/**
 * Cloth Batch Manager Implementation
 * Manages a batch of cloth instances at a specific LOD level
 */

#include "ClothBatchManager.h"
#include "ClothBatchedSolver.h"
#include "ClothInstanceHandle.h"
#include "Classes/Engine/ClothAsset.h"
#include "Classes/Components/ClothComponent.h"
#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"
#include "Engine/UserInterface/Console.h"
#include "Core/Math/MathUtility.h"

FClothBatchManager::FClothBatchManager(EClothLODLevel InLODLevel)
    : LODLevel(InLODLevel), BatchedSolver(nullptr), TotalParticleCount(0), TotalConstraintCount(0), TotalBendConstraintCount(0), TotalKinematicTargetCount(0), TotalTriangleCount(0), AllocatedParticleCapacity(0), AllocatedConstraintCapacity(0), AllocatedBendConstraintCapacity(0), AllocatedKinematicTargetCapacity(0), AllocatedTriangleCapacity(0), AllocatedInstanceCapacity(0), bNeedsReallocation(false), bNeedsCompaction(false), GrowthFactor(1.5f), Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), bIsInitialized(false)
{
}

FClothBatchManager::~FClothBatchManager()
{
    Release();
}

void FClothBatchManager::Initialize(FGraphicsDevice *InGraphics,
                                    FDXDBufferManager *InBufferMgr,
                                    FDXDShaderManager *InShaderMgr,
                                    FClothCollisionManager *CollisionMgr)
{
    Graphics = InGraphics;
    BufferManager = InBufferMgr;
    ShaderManager = InShaderMgr;

    if (!Graphics || !BufferManager || !ShaderManager)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager[LOD%d]: Invalid initialization parameters"),
               static_cast<int32>(LODLevel));
        return;
    }

    // Create batched solver with shared collision manager
    BatchedSolver = new FClothBatchedSolver();
    BatchedSolver->Initialize(Graphics, BufferManager, ShaderManager, CollisionMgr);

    // Initial buffer allocation (generous estimate to avoid reallocation)
    // Increased to handle large batches without needing dynamic reallocation
    uint32 initialParticles = 200000;    // ~500 instances @ 400 particles
    uint32 initialConstraints = 8000000; // ~40 constraints per particle
    uint32 initialBendConstraints = 50000;
    uint32 initialKinematicTargets = 20000;
    uint32 initialTriangles = 5000000; // CRITICAL: 5M triangles = 15M indices
    uint32 initialInstances = 512;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Allocating buffers - Triangles: %u (Indices: %u), Particles: %u"),
           static_cast<int32>(LODLevel), initialTriangles, initialTriangles * 3, initialParticles);

    if (!BatchedSolver->AllocateBuffers(initialParticles, initialConstraints,
                                        initialBendConstraints, initialKinematicTargets,
                                        initialTriangles, initialInstances))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager[LOD%d]: Failed to allocate initial buffers"),
               static_cast<int32>(LODLevel));
        return;
    }

    AllocatedParticleCapacity = initialParticles;
    AllocatedConstraintCapacity = initialConstraints;
    AllocatedBendConstraintCapacity = initialBendConstraints;
    AllocatedKinematicTargetCapacity = initialKinematicTargets;
    AllocatedTriangleCapacity = initialTriangles;
    AllocatedInstanceCapacity = initialInstances;

    // Check if solver is fully initialized (shaders + buffers)
    if (BatchedSolver && BatchedSolver->IsInitialized())
    {
        bIsInitialized = true;
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Initialized with capacity for %d particles, %d instances"),
               static_cast<int32>(LODLevel), initialParticles, initialInstances);
    }
    else
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager[LOD%d]: Solver failed to initialize fully"),
               static_cast<int32>(LODLevel));
        bIsInitialized = false;
    }
}

void FClothBatchManager::Release()
{
    if (!bIsInitialized)
        return;

    // Clean up all instance handles
    for (FClothInstanceHandle *Handle : Instances)
    {
        if (Handle)
        {
            delete Handle;
        }
    }
    Instances.Empty();
    InstanceMetadata.Empty();
    InstanceToMetadataIndex.Empty();

    // Release batched solver
    if (BatchedSolver)
    {
        BatchedSolver->Release();
        delete BatchedSolver;
        BatchedSolver = nullptr;
    }

    bIsInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Released"),
           static_cast<int32>(LODLevel));
}

FClothInstanceHandle *FClothBatchManager::AddInstance(const FClothInstanceCreationParams &Params)
{
    if (!bIsInitialized)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager[LOD%d]: Cannot add instance - not initialized"),
               static_cast<int32>(LODLevel));
        return nullptr;
    }

    uint32 particleCount = Params.RestPositions.Num();
    uint32 constraintCount = Params.Constraints.Num();
    uint32 bendConstraintCount = Params.BendConstraints.Num();
    uint32 kinematicTargetCount = Params.Attachments.Num();
    uint32 triangleCount = Params.Indices.Num() / 3;

    // Check if we need reallocation
    uint32 requiredParticles = TotalParticleCount + particleCount;
    uint32 requiredConstraints = TotalConstraintCount + constraintCount;

    if (NeedsReallocation(requiredParticles, requiredConstraints))
    {
        ReallocateBuffers();
    }

    // Create instance metadata
    FClothInstanceMetadata metadata;
    metadata.ParticleOffset = TotalParticleCount;
    metadata.ParticleCount = particleCount;
    metadata.ConstraintOffset = TotalConstraintCount;
    metadata.ConstraintCount = constraintCount;
    metadata.BendConstraintOffset = TotalBendConstraintCount;
    metadata.BendConstraintCount = bendConstraintCount;
    metadata.KinematicTargetOffset = TotalKinematicTargetCount;
    metadata.KinematicTargetCount = kinematicTargetCount;
    metadata.TriangleOffset = TotalTriangleCount;
    metadata.TriangleCount = triangleCount;
    metadata.InstanceParameterIndex = Instances.Num();
    metadata.bIsActive = Params.bStartActive;
    metadata.CurrentLOD = Params.InitialLOD;

    // ENHANCED VALIDATION LOGGING
    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: ===== Instance %d Metadata ====="),
           static_cast<int32>(LODLevel), Instances.Num());
    UE_LOG(ELogLevel::Display, TEXT("  Particles: Offset=%u, Count=%u, Range=[%u-%u]"),
           metadata.ParticleOffset, metadata.ParticleCount,
           metadata.ParticleOffset, metadata.ParticleOffset + metadata.ParticleCount - 1);
    UE_LOG(ELogLevel::Display, TEXT("  Triangles: Offset=%u, Count=%u, IndexRange=[%u-%u]"),
           metadata.TriangleOffset, metadata.TriangleCount,
           metadata.TriangleOffset * 3, (metadata.TriangleOffset + metadata.TriangleCount) * 3 - 1);
    UE_LOG(ELogLevel::Display, TEXT("  Constraints: Offset=%u, Count=%u"),
           metadata.ConstraintOffset, metadata.ConstraintCount);

    // Create instance handle
    int32 metadataIndex = InstanceMetadata.Add(metadata);
    FClothInstanceHandle *handle = new FClothInstanceHandle(this, metadataIndex);
    handle->SetOwnerComponent(Params.OwnerComponent);
    handle->SetParameters(Params.InstanceParams);

    // Track instance
    Instances.Add(handle);
    InstanceToMetadataIndex.Add(handle, metadataIndex);

    // Update totals
    TotalParticleCount += particleCount;
    TotalConstraintCount += constraintCount;
    TotalBendConstraintCount += bendConstraintCount;
    TotalKinematicTargetCount += kinematicTargetCount;
    TotalTriangleCount += triangleCount;

    // Update solver counts
    BatchedSolver->SetUsedCounts(TotalParticleCount, TotalConstraintCount, TotalBendConstraintCount,
                                 TotalKinematicTargetCount, TotalTriangleCount, Instances.Num());

    // ===== UPLOAD INSTANCE DATA TO GPU BUFFERS =====

    // 1. Transform particle positions from LOCAL space to WORLD space
    // This ensures each instance simulates at its correct world location
    TArray<FVector> worldSpacePositions;
    worldSpacePositions.Reserve(particleCount);

    for (uint32 i = 0; i < particleCount; ++i)
    {
        // Transform each particle position to world space using the instance's WorldTransform
        FVector localPos = Params.RestPositions[i];
        FVector worldPos = Params.WorldTransform.TransformPosition(localPos);
        worldSpacePositions.Add(worldPos);
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Transforming %d particles to world space at offset (%f, %f, %f)"),
           static_cast<int32>(LODLevel), particleCount,
           Params.WorldTransform.GetTranslation().X,
           Params.WorldTransform.GetTranslation().Y,
           Params.WorldTransform.GetTranslation().Z);
    
    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Instance %d Metadata - ParticleOffset=%d, ParticleCount=%d, ConstraintOffset=%d, TotalParticlesBefore=%d"),
           static_cast<int32>(LODLevel), Instances.Num(), metadata.ParticleOffset, metadata.ParticleCount,
           metadata.ConstraintOffset, TotalParticleCount - particleCount);

    // 2. Upload particle data with instance IDs
    TArray<uint32> instanceIDs;
    instanceIDs.SetNum(particleCount);
    for (uint32 i = 0; i < particleCount; ++i)
    {
        instanceIDs[i] = metadata.InstanceParameterIndex; // All particles belong to this instance
    }

    // Upload transformed world-space positions (NOT local-space positions!)
    BatchedSolver->UploadParticleData(
        worldSpacePositions, // Changed from Params.RestPositions
        Params.InvMasses,
        instanceIDs,
        metadata.ParticleOffset);

    // 3. Upload distance constraints with global particle indices
    if (Params.Constraints.Num() > 0)
    {
        TArray<FClothDistanceConstraintGPU> constraintsGPU;
        constraintsGPU.Reserve(Params.Constraints.Num());

        for (const FClothDistanceConstraint &c : Params.Constraints)
        {
            FClothDistanceConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = c.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = c.ParticleB + metadata.ParticleOffset;
            gpu.RestLength = c.RestLength;
            gpu.Stiffness = c.Stiffness;
            gpu.Compliance = c.Compliance;
            gpu.Lambda = c.Lambda;
            gpu.Padding0 = 0.0f;
            gpu.Padding1 = 0.0f;

            constraintsGPU.Add(gpu);
        }

        BatchedSolver->UploadConstraintData(constraintsGPU, metadata.ConstraintOffset);
    }

    // 4. Upload bend constraints with global particle indices
    if (Params.BendConstraints.Num() > 0)
    {
        TArray<FClothBendConstraintGPU> bendConstraintsGPU;
        bendConstraintsGPU.Reserve(Params.BendConstraints.Num());

        for (const FClothBendConstraint &bc : Params.BendConstraints)
        {
            FClothBendConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = bc.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = bc.ParticleB + metadata.ParticleOffset;
            gpu.ParticleC = bc.ParticleC + metadata.ParticleOffset;
            gpu.ParticleD = bc.ParticleD + metadata.ParticleOffset;
            gpu.RestAngle = bc.RestAngle;
            gpu.Stiffness = bc.Stiffness;
            gpu.Compliance = bc.Compliance;
            gpu.Lambda = bc.Lambda;

            bendConstraintsGPU.Add(gpu);
        }

        BatchedSolver->UploadBendConstraintData(bendConstraintsGPU, metadata.BendConstraintOffset);
    }

    // 5. Upload triangle indices with global particle indices
    if (Params.Indices.Num() > 0)
    {
        TArray<uint32> globalIndices;
        globalIndices.Reserve(Params.Indices.Num());

        uint32 minGlobalIdx = UINT32_MAX;
        uint32 maxGlobalIdx = 0;

        for (uint32 localIdx : Params.Indices)
        {
            uint32 globalIdx = localIdx + metadata.ParticleOffset;
            globalIndices.Add(globalIdx);

            minGlobalIdx = FMath::Min(minGlobalIdx, globalIdx);
            maxGlobalIdx = FMath::Max(maxGlobalIdx, globalIdx);
        }

        uint32 indexOffset = metadata.TriangleOffset * 3; // Convert triangles to indices

        // CRITICAL VALIDATION: Verify index buffer has enough space
        uint32 requiredIndexCapacity = indexOffset + globalIndices.Num();
        uint32 allocatedIndexCapacity = AllocatedTriangleCapacity * 3;

        if (requiredIndexCapacity > allocatedIndexCapacity)
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager[LOD%d]: INDEX BUFFER OVERFLOW! Required: %u, Allocated: %u, Instance: %d"),
                   static_cast<int32>(LODLevel), requiredIndexCapacity, allocatedIndexCapacity, Instances.Num());
            UE_LOG(ELogLevel::Error, TEXT("  IndexOffset: %u, IndexCount: %u, TotalTriangles: %u"),
                   indexOffset, globalIndices.Num(), TotalTriangleCount);
            return nullptr; // ABORT - would cause rendering corruption
        }

        // ENHANCED INDEX VALIDATION: Verify indices reference correct particle range
        UE_LOG(ELogLevel::Display, TEXT("  Global index range: [%u-%u], Expected particle range: [%u-%u]"),
               minGlobalIdx, maxGlobalIdx,
               metadata.ParticleOffset, metadata.ParticleOffset + metadata.ParticleCount - 1);

        // Validate that indices reference only this instance's particles
        if (minGlobalIdx < metadata.ParticleOffset ||
            maxGlobalIdx >= metadata.ParticleOffset + metadata.ParticleCount)
        {
            UE_LOG(ELogLevel::Error, TEXT("  *** INDEX OUT OF RANGE! Indices reference particles outside instance range! ***"));
            UE_LOG(ELogLevel::Error, TEXT("  This will cause rendering corruption and out-of-bounds buffer access!"));
        }

        UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Uploading %d indices at offset %u (Triangle offset: %u)"),
               static_cast<int32>(LODLevel), globalIndices.Num(), indexOffset, metadata.TriangleOffset);

        BatchedSolver->UploadIndexData(globalIndices, indexOffset);
    }

    // 6. DON'T upload kinematic targets during AddInstance
    // Kinematic targets are uploaded each frame in UpdateKinematicTargets()
    // because they need to be updated with current driver positions.
    // Initial upload with WRITE_DISCARD would overwrite previous instances' targets.
    // The targets will be properly uploaded on the first Update() call.

    // 7. Update instance parameters
    UpdateInstanceParameterBuffer();

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Added instance - %d particles, %d constraints, Total instances: %d, Total particles: %d"),
           static_cast<int32>(LODLevel), particleCount, constraintCount, Instances.Num(), TotalParticleCount);

    return handle;
}

void FClothBatchManager::RemoveInstance(FClothInstanceHandle *Instance)
{
    if (!Instance)
        return;

    int32 *MetadataIndexPtr = InstanceToMetadataIndex.Find(Instance);
    if (!MetadataIndexPtr)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchManager[LOD%d]: Instance not found in batch"),
               static_cast<int32>(LODLevel));
        return;
    }

    int32 metadataIndex = *MetadataIndexPtr;
    const FClothInstanceMetadata &metadata = InstanceMetadata[metadataIndex];

    // Update totals
    TotalParticleCount -= metadata.ParticleCount;
    TotalConstraintCount -= metadata.ConstraintCount;
    TotalBendConstraintCount -= metadata.BendConstraintCount;
    TotalKinematicTargetCount -= metadata.KinematicTargetCount;
    TotalTriangleCount -= metadata.TriangleCount;

    // Remove from tracking
    Instances.Remove(Instance);
    InstanceToMetadataIndex.Remove(Instance);

    // Mark for compaction (don't compact immediately)
    bNeedsCompaction = true;

    // Update solver counts
    BatchedSolver->SetUsedCounts(TotalParticleCount, TotalConstraintCount, TotalBendConstraintCount,
                                 TotalKinematicTargetCount, TotalTriangleCount, Instances.Num());

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Removed instance - Remaining: %d instances, %d particles"),
           static_cast<int32>(LODLevel), Instances.Num(), TotalParticleCount);
}

void FClothBatchManager::UpdateInstanceParameters(FClothInstanceHandle *Instance,
                                                  const FClothInstanceParameters &Params)
{
    if (!Instance)
        return;

    Instance->SetParameters(Params);

    // TODO: Update GPU parameter buffer
    UpdateInstanceParameterBuffer();
}

void FClothBatchManager::Update(float DeltaTime)
{
    if (!bIsInitialized || Instances.Num() == 0)
        return;

    // Update kinematic targets
    UpdateKinematicTargets(DeltaTime);

    // Simulate using fixed or variable timestep
    if (FixedTimestepState.bUseFixedTimestep)
    {
        SimulateFixedTimestep(DeltaTime);
    }
    else
    {
        Simulate(DeltaTime);
    }
}

void FClothBatchManager::Simulate(float DeltaTime)
{
    if (!bIsInitialized || !BatchedSolver)
        return;

    if (TotalParticleCount == 0)
        return;

    // Delegate to batched solver
    BatchedSolver->Simulate(DeltaTime);
}

void FClothBatchManager::SimulateFixedTimestep(float DeltaTime)
{
    if (!FixedTimestepState.bUseFixedTimestep)
    {
        // Variable timestep mode
        BatchedSolver->Simulate(DeltaTime);
        return;
    }

    // Clamp maximum DeltaTime to prevent spiral of death
    float clampedDT = FMath::Min(DeltaTime, 0.1f); // Max 100ms

    // Accumulate time
    FixedTimestepState.AccumulatedTime += clampedDT;

    // If we're falling too far behind, reset accumulator
    if (FixedTimestepState.AccumulatedTime > FixedTimestepState.FixedTimestep * 10.0f)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchManager[LOD%d]: Simulation falling behind - resetting accumulator"),
               static_cast<int32>(LODLevel));
        FixedTimestepState.AccumulatedTime = FixedTimestepState.FixedTimestep;
    }

    // Fixed timestep loop
    int32 substepCount = 0;
    float fixedDT = FixedTimestepState.FixedTimestep;

    while (FixedTimestepState.AccumulatedTime >= fixedDT &&
           substepCount < FixedTimestepState.MaxSubsteps)
    {
        // Simulate one fixed timestep
        BatchedSolver->Simulate(fixedDT);

        FixedTimestepState.AccumulatedTime -= fixedDT;
        substepCount++;
    }
}

bool FClothBatchManager::CanAcceptInstance(uint32 ParticleCount) const
{
    uint32 requiredTotal = TotalParticleCount + ParticleCount;
    return requiredTotal <= AllocatedParticleCapacity * 2; // Allow up to 2x before refusing
}

ID3D11ShaderResourceView *FClothBatchManager::GetPositionBufferSRV() const
{
    return BatchedSolver ? BatchedSolver->GetPositionBufferSRV() : nullptr;
}

ID3D11ShaderResourceView *FClothBatchManager::GetNormalBufferSRV() const
{
    return BatchedSolver ? BatchedSolver->GetNormalBufferSRV() : nullptr;
}

const FClothInstanceMetadata &FClothBatchManager::GetInstanceMetadata(int32 InstanceIndex) const
{
    static FClothInstanceMetadata EmptyMetadata;
    if (InstanceIndex >= 0 && InstanceIndex < InstanceMetadata.Num())
    {
        return InstanceMetadata[InstanceIndex];
    }
    return EmptyMetadata;
}

void FClothBatchManager::ReallocateBuffers()
{
    // Calculate new capacities
    uint32 newParticleCapacity = CalculateNewCapacity(AllocatedParticleCapacity, TotalParticleCount);
    uint32 newConstraintCapacity = CalculateNewCapacity(AllocatedConstraintCapacity, TotalConstraintCount);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Reallocating buffers - Particles: %d -> %d, Constraints: %d -> %d"),
           static_cast<int32>(LODLevel), AllocatedParticleCapacity, newParticleCapacity,
           AllocatedConstraintCapacity, newConstraintCapacity);

    // TODO: Implement actual buffer reallocation
    // 1. Create new larger buffers
    // 2. Copy existing data
    // 3. Release old buffers
    // 4. Update capacity tracking

    AllocatedParticleCapacity = newParticleCapacity;
    AllocatedConstraintCapacity = newConstraintCapacity;
    bNeedsReallocation = false;
}

void FClothBatchManager::CompactBuffers()
{
    // Only compact if fragmentation is significant
    float utilization = (AllocatedParticleCapacity > 0) ? float(TotalParticleCount) / float(AllocatedParticleCapacity) : 0.0f;

    if (utilization > 0.7f)
    {
        bNeedsCompaction = false;
        return; // Good utilization, don't compact
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Compacting buffers - Utilization: %.1f%%"),
           static_cast<int32>(LODLevel), utilization * 100.0f);

    // TODO: Implement buffer compaction
    // 1. Build compaction map
    // 2. Move data on GPU
    // 3. Update instance metadata

    bNeedsCompaction = false;
}

void FClothBatchManager::UpdateGPUBuffers()
{
    // TODO: Update dynamic GPU buffers (kinematic targets, parameters)
}

void FClothBatchManager::UpdateInstanceParameterBuffer()
{
    if (!BatchedSolver)
        return;

    // Collect all instance parameters
    TArray<FClothInstanceParameters> params;
    params.Reserve(Instances.Num());

    for (FClothInstanceHandle *Handle : Instances)
    {
        if (Handle)
        {
            params.Add(Handle->GetParameters());
        }
    }

    // Upload to GPU
    if (params.Num() > 0)
    {
        BatchedSolver->UploadInstanceParameters(params);
    }
}

void FClothBatchManager::UpdateKinematicTargets(float DeltaTime)
{
    if (!BatchedSolver || TotalKinematicTargetCount == 0)
        return;

    // SOLVER-DRIVEN ATTACHMENT UPDATE FLOW
    // This method is called by the simulation system each frame.
    // It reads attachment definitions from cloth assets, resolves world positions,
    // and uploads kinematic targets to the GPU.
    // Actors no longer need to manually update attachments.

    TArray<FClothKinematicTargetGPU> allTargets;
    allTargets.Reserve(TotalKinematicTargetCount);

    for (FClothInstanceHandle *Handle : Instances)
    {
        if (!Handle || !Handle->IsActive())
            continue;

        const FClothInstanceMetadata &metadata = Handle->GetMetadata();
        UClothComponent *owner = Handle->GetOwnerComponent();

        if (!owner || !owner->GetClothAsset())
            continue;

        // Read attachment data directly from the cloth asset
        // This is the single source of truth for attachment configuration
        const TArray<FClothAttachmentData> &attachments = owner->GetClothAsset()->AttachmentsData;

        if (attachments.Num() == 0)
            continue;

        // For each attachment, resolve world position and create GPU target
        for (const FClothAttachmentData &attachment : attachments)
        {
            FClothKinematicTargetGPU target;

            // Convert local particle index to global batch index
            target.ParticleIndex = attachment.ClothVertexIndex + metadata.ParticleOffset;

            // AUTOMATIC WORLD POSITION RESOLUTION
            // Resolve world position based on driver component reference
            FVector worldPosition = FVector::ZeroVector;

            // Component-based attachment (preferred and most reliable)
            if (attachment.DriverComponent != nullptr)
            {
                FTransform driverTransform = attachment.DriverComponent->GetComponentTransform();
                FTransform attachmentWorldTransform = driverTransform * attachment.LocalOffset;
                worldPosition = attachmentWorldTransform.GetTranslation();
            }
            else
            {
                // Fallback to manually-set WorldPosition (for static attachments or backward compatibility)
                // Actor-based attachments should set DriverComponent instead
                worldPosition = attachment.WorldPosition;
            }

            target.TargetPosition = worldPosition;
            target.Stiffness = attachment.Stiffness;
            target.AttachDistance = attachment.AttachDistance;  // CRITICAL FIX: Initialize AttachDistance field
            target.Padding0 = 0.0f;
            target.Padding1 = 0.0f;

            allTargets.Add(target);
        }
    }

    // Upload all kinematic targets to GPU in one batch
    if (allTargets.Num() > 0)
    {
        BatchedSolver->UploadKinematicTargets(allTargets, 0);
    }
}

bool FClothBatchManager::NeedsReallocation(uint32 RequiredParticles, uint32 RequiredConstraints) const
{
    return RequiredParticles > AllocatedParticleCapacity ||
           RequiredConstraints > AllocatedConstraintCapacity;
}

uint32 FClothBatchManager::CalculateNewCapacity(uint32 CurrentCapacity, uint32 RequiredCapacity) const
{
    uint32 newCapacity = FMath::Max(CurrentCapacity, 1u);

    while (newCapacity < RequiredCapacity)
    {
        newCapacity = static_cast<uint32>(newCapacity * GrowthFactor);
    }

    return newCapacity;
}
