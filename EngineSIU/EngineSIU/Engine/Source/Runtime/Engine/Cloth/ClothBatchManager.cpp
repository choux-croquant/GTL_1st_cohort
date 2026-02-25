/**
 * Cloth Batch Manager Implementation
 * Manages a batch of cloth instances at a specific LOD level
 */

#include "ClothBatchManager.h"
#include "ClothBatchedSolver.h"
#include "ClothInstanceHandle.h"
#include "ClothSkinningWeightGenerator.h"
#include "ClothMeshAnalysis.h"
#include "Classes/Engine/ClothAsset.h"
#include "Classes/Components/ClothComponent.h"
#include "Classes/Components/SceneComponent.h"
#include "Classes/Components/SkeletalMeshComponent.h"
#include "UObject/Casts.h"
#include "Core/Math/Matrix.h"
#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"
#include "Engine/UserInterface/Console.h"
#include "Core/Math/MathUtility.h"
#include "Core/Math/Matrix.h"
#include "ClothGPUStructs.h"

FClothBatchManager::FClothBatchManager(EClothLODLevel InLODLevel)
    : LODLevel(InLODLevel), BatchedSolver(nullptr), TotalParticleCount(0), TotalConstraintCount(0), TotalBendConstraintCount(0), TotalAttachmentCount(0), TotalTriangleCount(0), TotalAreaConstraintCount(0), TotalEdgeCollisionCount(0), TotalRenderVertexCount(0), TotalRenderIndexCount(0), AllocatedParticleCapacity(0), AllocatedConstraintCapacity(0), AllocatedBendConstraintCapacity(0), AllocatedKinematicTargetCapacity(0), AllocatedTriangleCapacity(0), AllocatedInstanceCapacity(0), AllocatedAreaConstraintCapacity(0), AllocatedEdgeCollisionCapacity(0), AllocatedRenderVertexCapacity(0), AllocatedRenderIndexCapacity(0), bNeedsReallocation(false), bNeedsCompaction(false), GrowthFactor(1.5f), bAttachmentDataDirty(true), Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), bIsInitialized(false)
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

    // TEMP : Initial buffer allocation (generous estimate to avoid reallocation)
    // Increased to handle large batches without needing dynamic reallocation
    uint32 initialParticles = 200000;
    uint32 initialConstraints = 8000000;
    uint32 initialBendConstraints = 50000;
    uint32 initialKinematicTargets = 20000;
    uint32 initialTriangles = 5000000;
    uint32 initialInstances = 512;
    uint32 initialAreaConstraints = 10000000;
    uint32 initialEdgeCollisions = 15000000;

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Allocating buffers - Triangles: %u (Indices: %u), Particles: %u"),
           static_cast<int32>(LODLevel), initialTriangles, initialTriangles * 3, initialParticles);

    if (!BatchedSolver->AllocateBuffers(initialParticles, initialConstraints,
                                        initialBendConstraints, initialKinematicTargets,
                                        initialTriangles, initialInstances,
                                        initialAreaConstraints, initialEdgeCollisions))
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
    AllocatedAreaConstraintCapacity = initialAreaConstraints;
    AllocatedEdgeCollisionCapacity = initialEdgeCollisions;

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
    uint32 attachmentCount = Params.Attachments.Num();
    uint32 triangleCount = Params.Indices.Num() / 3;
    uint32 areaConstraintCount = Params.AreaConstraints.Num();
    uint32 edgeCollisionCount = Params.EdgeCollisions.Num();

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
    metadata.KinematicTargetOffset = TotalAttachmentCount;
    metadata.KinematicTargetCount = attachmentCount;
    metadata.TriangleOffset = TotalTriangleCount;
    metadata.TriangleCount = triangleCount;
    metadata.AreaConstraintOffset = TotalAreaConstraintCount;
    metadata.AreaConstraintCount = areaConstraintCount;
    metadata.EdgeCollisionOffset = TotalEdgeCollisionCount;
    metadata.EdgeCollisionCount = edgeCollisionCount;
    metadata.InstanceParameterIndex = Instances.Num();
    metadata.bIsActive = Params.bStartActive;
    metadata.CurrentLOD = Params.InitialLOD;
   
    metadata.AvgEdgeLength = FClothMeshAnalysis::ComputeAverageEdgeLength(
    	Params.RestPositions, Params.Indices);
    
    // Compute AABB from rest positions (will be updated dynamically at runtime)
    FClothMeshAnalysis::ComputeAABB(
    	Params.RestPositions,
    	metadata.MeshBoundsMin,
    	metadata.MeshBoundsMax);
    
    // Initialize previous bounds
    metadata.PrevBoundsMin = metadata.MeshBoundsMin;
    metadata.PrevBoundsMax = metadata.MeshBoundsMax;
    
    // Compute adaptive spatial hash parameters
    FVector gridMin, gridMax;
    FClothMeshAnalysis::ComputeAdaptiveSpatialHashParams(
    	metadata.AvgEdgeLength,
    	metadata.MeshBoundsMin,
    	metadata.MeshBoundsMax,
    	metadata.ParticleCount,
    	metadata.AdaptiveCellSize,
    	metadata.AdaptiveCollisionRadius,
    	gridMin,
    	gridMax,
    	metadata.AdaptiveGridDimX,
    	metadata.AdaptiveGridDimY,
    	metadata.AdaptiveGridDimZ,
    	metadata.AdaptiveMaxPerCell);
    
    // Initialize motion tracking state
    metadata.AccumulatedMotion = 0.0f;
    metadata.FramesSinceLastBoundsUpdate = 0;
    metadata.bNeedsBoundsUpdate = false;
    
    // Log computed adaptive parameters
    UE_LOG(ELogLevel::Display,
    	TEXT("ClothBatchManager[LOD%d]: Instance %d Adaptive Params - AvgEdge=%.3f, CellSize=%.3f, Radius=%.3f, Grid=%ux%ux%u"),
    	static_cast<int32>(LODLevel), Instances.Num(),
    	metadata.AvgEdgeLength, metadata.AdaptiveCellSize, metadata.AdaptiveCollisionRadius,
    	metadata.AdaptiveGridDimX, metadata.AdaptiveGridDimY, metadata.AdaptiveGridDimZ);

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
    TotalAttachmentCount += attachmentCount;
    TotalTriangleCount += triangleCount;
    TotalAreaConstraintCount += areaConstraintCount;       // NEW
    TotalEdgeCollisionCount += edgeCollisionCount;         // NEW

    // Update solver counts
    BatchedSolver->SetUsedCounts(TotalParticleCount, TotalConstraintCount, TotalBendConstraintCount, TotalAttachmentCount,
                                 TotalTriangleCount, Instances.Num(), TotalAreaConstraintCount, TotalEdgeCollisionCount); // NEW

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

    TArray<uint32> instanceIDs;
    instanceIDs.SetNum(particleCount);
    for (uint32 i = 0; i < particleCount; ++i)
    {
        instanceIDs[i] = metadata.InstanceParameterIndex; // All particles belong to this instance
    }

    // This enables per-instance attachment isolation
    const TArray<float>& runtimeInvMasses = Params.OwnerComponent ?
        Params.OwnerComponent->GetRuntimeInvMasses() : Params.InvMasses;
    
    // Upload transformed world-space positions (NOT local-space positions!)
    BatchedSolver->UploadParticleData(
        worldSpacePositions, // Changed from Params.RestPositions
        runtimeInvMasses,    // NEW: Per-instance InvMass
        instanceIDs,
        metadata.ParticleOffset);

    if (Params.Constraints.Num() > 0)
    {
        TArray<FClothDistanceConstraintGPU> constraintsGPU;
        constraintsGPU.Reserve(Params.Constraints.Num());

        // Get solver config for XPBD compliance calculation
        const FClothConfig &config = BatchedSolver->GetConfig();

        for (const FClothDistanceConstraint &c : Params.Constraints)
        {
            FClothDistanceConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = c.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = c.ParticleB + metadata.ParticleOffset;
            gpu.RestLength = c.RestLength;
            gpu.Stiffness = c.Stiffness;

            if (c.Compliance > 0.0f)
            {
                // Use pre-authored compliance value (advanced usage)
                gpu.Compliance = c.Compliance;
            }
            else
            {
                // Compute compliance from stiffness and global config
                // Scale: typical range 0.0001 to 0.1 for cloth constraints
                const float complianceScale = 1e-7f; // Tunable parameter
                float effectiveStiffness = FMath::Clamp(config.StretchStiffness * c.Stiffness, 0.0f, 1.0f);

                // Map stiffness [0,1] to compliance [high, low]
                // Use power curve to maintain stiffness at high values
                float softness = 1.0f - FMath::Pow(effectiveStiffness, 4.0f);
                gpu.Compliance = softness * complianceScale;

                // Ensure minimum compliance for numerical stability
                gpu.Compliance = FMath::Max(gpu.Compliance, 1e-8f);
            }

            // Initialize lambda to 0 (no accumulated force yet)
            // Lambda will be updated by XPBD solver and warm-started across iterations
            gpu.Lambda = 0.0f;

            gpu.Padding0 = 0.0f;
            gpu.Padding1 = 0.0f;

            constraintsGPU.Add(gpu);
        }

        BatchedSolver->UploadConstraintData(constraintsGPU, metadata.ConstraintOffset);
    }

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

    if (Params.AreaConstraints.Num() > 0)
    {
        TArray<FClothAreaConstraintGPU> areaConstraintsGPU;
        areaConstraintsGPU.Reserve(Params.AreaConstraints.Num());

        for (const FClothAreaConstraint &ac : Params.AreaConstraints)
        {
            FClothAreaConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = ac.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = ac.ParticleB + metadata.ParticleOffset;
            gpu.ParticleC = ac.ParticleC + metadata.ParticleOffset;
            gpu.RestArea = ac.RestArea;
            gpu.RestNormal = ac.RestNormal;
            gpu.Compliance = ac.Compliance;
            gpu.Lambda = ac.Lambda;
            gpu.Stiffness = ac.Stiffness;
            gpu.Padding0 = 0.0f;
            gpu.Padding1 = 0.0f;

            areaConstraintsGPU.Add(gpu);
        }

        BatchedSolver->UploadAreaConstraintData(areaConstraintsGPU, metadata.AreaConstraintOffset);
    }

    if (Params.EdgeCollisions.Num() > 0)
    {
        TArray<FClothEdgeCollisionConstraintGPU> edgeCollisionsGPU;
        edgeCollisionsGPU.Reserve(Params.EdgeCollisions.Num());

        for (const FClothEdgeCollisionConstraint &ec : Params.EdgeCollisions)
        {
            FClothEdgeCollisionConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = ec.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = ec.ParticleB + metadata.ParticleOffset;
            gpu.RestLength = ec.RestLength;
            gpu.Padding = 0.0f;

            edgeCollisionsGPU.Add(gpu);
        }

        BatchedSolver->UploadEdgeCollisionData(edgeCollisionsGPU, metadata.EdgeCollisionOffset);
    }

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

        uint32 requiredIndexCapacity = indexOffset + globalIndices.Num();
        uint32 allocatedIndexCapacity = AllocatedTriangleCapacity * 3;

        if (requiredIndexCapacity > allocatedIndexCapacity)
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager[LOD%d]: INDEX BUFFER OVERFLOW! Required: %u, Allocated: %u, Instance: %d"),
                   static_cast<int32>(LODLevel), requiredIndexCapacity, allocatedIndexCapacity, Instances.Num());
            UE_LOG(ELogLevel::Error, TEXT("  IndexOffset: %u, IndexCount: %u, TotalTriangles: %u"),
                   indexOffset, globalIndices.Num(), TotalTriangleCount);
            return nullptr;
        }

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

    if (Params.bUseRenderMesh && Params.RenderRestPositions.Num() > 0)
    {
        uint32 renderVertexCount = Params.RenderRestPositions.Num();
        uint32 renderIndexCount = Params.RenderIndices.Num();
        
        // Update metadata with render mesh offsets
        metadata.RenderVertexOffset = TotalRenderVertexCount;
        metadata.RenderVertexCount = renderVertexCount;
        metadata.RenderIndexOffset = TotalRenderIndexCount;
        metadata.RenderIndexCount = renderIndexCount;
        
        // Update instance metadata in array (it was already added earlier)
        InstanceMetadata[metadataIndex] = metadata;
        
        // Transform render mesh positions to world space
        TArray<FVector> worldSpaceRenderPositions;
        worldSpaceRenderPositions.Reserve(renderVertexCount);
        
        for (uint32 i = 0; i < renderVertexCount; ++i)
        {
            FVector localPos = Params.RenderRestPositions[i];
            FVector worldPos = Params.WorldTransform.TransformPosition(localPos);
            worldSpaceRenderPositions.Add(worldPos);
        }
        
        // Transform render mesh normals to world space
        TArray<FVector> worldSpaceRenderNormals;
        worldSpaceRenderNormals.Reserve(renderVertexCount);
        
        for (uint32 i = 0; i < static_cast<uint32>(Params.RenderNormals.Num()); ++i)
        {
            FVector localNormal = Params.RenderNormals[i];
            FVector worldNormal = Params.WorldTransform.TransformVector(localNormal);
            worldNormal.Normalize();
            worldSpaceRenderNormals.Add(worldNormal);
        }
        
        // Upload render mesh data
        BatchedSolver->UploadRenderMeshData(
            worldSpaceRenderPositions,
            worldSpaceRenderNormals,
            Params.RenderUVs,
            Params.RenderIndices,
            metadata.RenderVertexOffset,
            metadata.RenderIndexOffset
        );
        
        // Convert skinning weights to use global simulation vertex indices
        TArray<FClothSkinningWeight> globalSkinningWeights;
        globalSkinningWeights.Reserve(Params.SkinningWeights.Num());
        
        for (const FClothSkinningWeight& localWeight : Params.SkinningWeights)
        {
            FClothSkinningWeight globalWeight = localWeight;
            
            // Convert local sim vertex indices to global indices
            for (int i = 0; i < 4; ++i)
            {
                if (globalWeight.Weights[i] > 0.0f)
                {
                    globalWeight.SimVertexIndices[i] += metadata.ParticleOffset;
                }
            }
            
            globalSkinningWeights.Add(globalWeight);
        }
        
        // Upload legacy K-nearest neighbor skinning weights
        BatchedSolver->UploadSkinningWeights(globalSkinningWeights, metadata.RenderVertexOffset);
        
        if (Params.TriangleSkinningWeights.Num() > 0)
        {
            // Convert triangle skinning weights to use global simulation vertex indices
            TArray<FClothSkinningWeightTriangle> globalTriangleWeights;
            globalTriangleWeights.Reserve(Params.TriangleSkinningWeights.Num());
            
            for (const FClothSkinningWeightTriangle& localWeight : Params.TriangleSkinningWeights)
            {
                FClothSkinningWeightTriangle globalWeight = localWeight;
                
                // Convert local sim vertex indices to global indices
                globalWeight.SimTriangleIndices[0] += metadata.ParticleOffset;
                globalWeight.SimTriangleIndices[1] += metadata.ParticleOffset;
                globalWeight.SimTriangleIndices[2] += metadata.ParticleOffset;
                
                // Barycentric coords and tangent-space offset remain unchanged
                
                globalTriangleWeights.Add(globalWeight);
            }
            
            // Upload triangle skinning weights
            BatchedSolver->UploadTriangleSkinningWeights(globalTriangleWeights, metadata.RenderVertexOffset);
            
            UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Uploaded triangle skinning weights - Count: %u"),
                   static_cast<int32>(LODLevel), globalTriangleWeights.Num());
        }
        
        // Update totals
        TotalRenderVertexCount += renderVertexCount;
        TotalRenderIndexCount += renderIndexCount;
        
        UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Uploaded render mesh - Vertices: %u, Indices: %u"),
               static_cast<int32>(LODLevel), renderVertexCount, renderIndexCount);
    }

    UpdateInstanceParameterBuffer();

    bAttachmentDataDirty = true;

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

    // Remove from tracking
    Instances.Remove(Instance);
    InstanceToMetadataIndex.Remove(Instance);

    // Invalidate metadata to prevent stale access
    FClothInstanceMetadata& metadataRef = InstanceMetadata[metadataIndex];
    metadataRef.bIsActive = false;
    
    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Deactivated instance at index %d (offsets preserved)"),
           static_cast<int32>(LODLevel), metadataIndex);
    UE_LOG(ELogLevel::Display, TEXT("  Particle range: [%u-%u], Constraint range: [%u-%u]"),
           metadataRef.ParticleOffset, metadataRef.ParticleOffset + metadataRef.ParticleCount - 1,
           metadataRef.ConstraintOffset, metadataRef.ConstraintOffset + metadataRef.ConstraintCount - 1);

    // Mark for compaction (don't compact immediately)
    bNeedsCompaction = true;

    // Update solver with ACTIVE instance count (not total capacity)
    // Count only active instances for simulation
    uint32 activeParticles = 0;
    uint32 activeConstraints = 0;
    uint32 activeBendConstraints = 0;
    uint32 activeAttachments = 0;
    uint32 activeTriangles = 0;
    uint32 activeAreaConstraints = 0;
    uint32 activeEdgeCollisions = 0;
    
    for (const FClothInstanceMetadata& meta : InstanceMetadata)
    {
        if (meta.bIsActive)
        {
            activeParticles += meta.ParticleCount;
            activeConstraints += meta.ConstraintCount;
            activeBendConstraints += meta.BendConstraintCount;
            activeAttachments += meta.KinematicTargetCount;
            activeTriangles += meta.TriangleCount;
            activeAreaConstraints += meta.AreaConstraintCount;
            activeEdgeCollisions += meta.EdgeCollisionCount;
        }
    }
    
    BatchedSolver->SetUsedCounts(activeParticles, activeConstraints, activeBendConstraints, activeAttachments,
                                 activeTriangles, Instances.Num(), activeAreaConstraints, activeEdgeCollisions);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Removed instance - Remaining: %d instances, %d active particles (Total capacity: %d)"),
           static_cast<int32>(LODLevel), Instances.Num(), activeParticles, TotalParticleCount);
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

    {
        UpdateKinematicTargetsGPU(DeltaTime);
    }

    {
        if (FixedTimestepState.bUseFixedTimestep)
        {
            SimulateFixedTimestep(DeltaTime);
        }
        else
        {
            Simulate(DeltaTime);
        }
    }
}

void FClothBatchManager::Simulate(float DeltaTime)
{
	if (!bIsInitialized || !BatchedSolver)
		return;

	if (TotalParticleCount == 0)
		return;

	// Delegate to batched solver with instance metadata
	BatchedSolver->Simulate(DeltaTime, InstanceMetadata);
}

void FClothBatchManager::SimulateFixedTimestep(float DeltaTime)
{
	if (!FixedTimestepState.bUseFixedTimestep)
	{
		// Variable timestep mode
		BatchedSolver->Simulate(DeltaTime, InstanceMetadata);
		return;
	}

	// Clamp maximum DeltaTime to prevent spiral of death
	float clampedDT = FMath::Min(DeltaTime, 0.1f); // Max 100ms

	// Accumulate time
	FixedTimestepState.AccumulatedTime += clampedDT;

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
		// Simulate one fixed timestep with instance metadata
		BatchedSolver->Simulate(fixedDT, InstanceMetadata);

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
        return;
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

void FClothBatchManager::BuildKinematicAttachmentData()
{
    ComponentIndexMap.Empty();
    UniqueComponents.Empty();
    SkeletalMeshBoneMap.Empty(); // NEW: Clear bone tracking
    TArray<FKinematicAttachmentGPU> attachmentData;
    
    uint32 NextTransformSlot = 0;

    for (FClothInstanceHandle *Handle : Instances)
    {
        if (!Handle || !Handle->GetOwnerComponent())
            continue;

        const FClothInstanceMetadata &metadata = Handle->GetMetadata();
        UClothComponent *owner = Handle->GetOwnerComponent();
        
        const TArray<FClothAttachmentBinding> &bindings = owner->GetAttachmentBindings();

        for (const FClothAttachmentBinding &binding : bindings)
        {
            // Skip inactive bindings
            if (!binding.bIsActive)
                continue;
            
            const FClothAttachmentTarget &target = binding.Target;
            
            if (target.Type == EClothAttachmentType::ActorTransform) {
                if (!target.DriverComponent)
                    continue;
                // Get or create component index (deduplication!)
                uint32 *ComponentIndexPtr = ComponentIndexMap.Find(target.DriverComponent);
                uint32 ComponentIndex;

                if (!ComponentIndexPtr)
                {
                    ComponentIndex = NextTransformSlot++;
                    ComponentIndexMap.Add(target.DriverComponent, ComponentIndex);
                    UniqueComponents.Add(target.DriverComponent);
                }
                else
                {
                    ComponentIndex = *ComponentIndexPtr;
                }

                // Build GPU attachment data
                FKinematicAttachmentGPU gpuAttachment;
                gpuAttachment.Type = 1;
                gpuAttachment.ComponentIndex = ComponentIndex;
                gpuAttachment.ParticleIndex = binding.SimVertexIndex + metadata.ParticleOffset;
                gpuAttachment.Stiffness = binding.Stiffness;
                gpuAttachment.AttachDistance = binding.AttachDistance;
                gpuAttachment.LocalOffset = target.LocalOffset.GetTranslation();
                gpuAttachment.Padding = 0.0f;

                attachmentData.Add(gpuAttachment);
            }
            else if (target.Type == EClothAttachmentType::WorldPosition) {
                FTransform componentTransform = owner->GetComponentTransform();
                FTransform invComponentTransform = componentTransform.Inverse();
                FVector localPosition = invComponentTransform.TransformPosition(target.WorldPosition);
                
                // Build GPU attachment data
                FKinematicAttachmentGPU gpuAttachment;
                gpuAttachment.Type = 0;
                gpuAttachment.ParticleIndex = binding.SimVertexIndex + metadata.ParticleOffset;
                gpuAttachment.Stiffness = binding.Stiffness;
                gpuAttachment.AttachDistance = binding.AttachDistance;
                gpuAttachment.TargetPosition = localPosition;  // Transformed to local space
                gpuAttachment.Padding = 0.0f;

                attachmentData.Add(gpuAttachment);
            }
            else if (target.Type == EClothAttachmentType::SkeletalBone) {
                USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(target.DriverComponent);
                if (!SkelMesh || target.BoneIndex == INDEX_NONE)
                    continue;
                
                // Get or create bone tracking for this skeletal mesh
                TArray<FBoneAttachmentInfo>* BoneInfoArrayPtr = SkeletalMeshBoneMap.Find(SkelMesh);
                if (!BoneInfoArrayPtr)
                {
                    // First time seeing this skeletal mesh - add to unique components
                    uint32 ComponentIndex = NextTransformSlot++;
                    ComponentIndexMap.Add(SkelMesh, ComponentIndex);
                    UniqueComponents.Add(SkelMesh);
                    
                    // Create bone tracking array
                    TArray<FBoneAttachmentInfo> BoneInfoArray;
                    SkeletalMeshBoneMap.Add(SkelMesh, BoneInfoArray);
                    BoneInfoArrayPtr = SkeletalMeshBoneMap.Find(SkelMesh);
                }
                
                // Find or add this bone to the tracking list
                uint32 BoneTransformSlot = NextTransformSlot;
                bool bFoundBone = false;
                
                for (const FBoneAttachmentInfo& BoneInfo : *BoneInfoArrayPtr)
                {
                    if (BoneInfo.BoneIndex == target.BoneIndex)
                    {
                        BoneTransformSlot = BoneInfo.TransformSlot;
                        bFoundBone = true;
                        break;
                    }
                }
                
                if (!bFoundBone)
                {
                    // New bone - allocate transform slot
                    FBoneAttachmentInfo NewBoneInfo;
                    NewBoneInfo.BoneIndex = target.BoneIndex;
                    NewBoneInfo.TransformSlot = NextTransformSlot++;
                    BoneInfoArrayPtr->Add(NewBoneInfo);
                    BoneTransformSlot = NewBoneInfo.TransformSlot;
                }

                // Build GPU attachment data
                FKinematicAttachmentGPU gpuAttachment;
                gpuAttachment.Type = 1; // Use Type 1 (component transform) - bone transform uploaded to ComponentTransforms
                gpuAttachment.ComponentIndex = BoneTransformSlot; // Use bone's transform slot
                gpuAttachment.ParticleIndex = binding.SimVertexIndex + metadata.ParticleOffset;
                gpuAttachment.Stiffness = binding.Stiffness;
                gpuAttachment.AttachDistance = binding.AttachDistance;
                gpuAttachment.LocalOffset = target.LocalOffset.GetTranslation();
                gpuAttachment.Padding = 0.0f;

                attachmentData.Add(gpuAttachment);
            }
        }
    }

    // Store total attachment count for GPU dispatch
    TotalAttachmentCount = attachmentData.Num();

    // Upload to GPU (ONCE - this data is static)
    if (attachmentData.Num() > 0 && BatchedSolver)
    {
        BatchedSolver->UploadAttachmentData(attachmentData);
        BatchedSolver->SetAttachmentCount(attachmentData.Num()); // Set count for dispatch
    }

    float dedupPercent = attachmentData.Num() > 0 ? (1.0f - (float)UniqueComponents.Num() / attachmentData.Num()) * 100.0f : 0.0f;

    bAttachmentDataDirty = false;
}

void FClothBatchManager::UpdateKinematicTargetsGPU(float DeltaTime)
{
    QUICK_SCOPE_CYCLE_COUNTER(UpdateKinematicTargets_GPU);

    //if (!BatchedSolver || TotalAttachmentCount == 0)
    if (!BatchedSolver)
        return;

    // Rebuild attachment data if needed (rare - only when attachments change)
    //if (bAttachmentDataDirty)
    {
        BuildKinematicAttachmentData();
    }

    TArray<FMatrix> componentTransforms;
    componentTransforms.Reserve(UniqueComponents.Num() + SkeletalMeshBoneMap.Num() * 10); // Reserve extra for bones

    {
        QUICK_SCOPE_CYCLE_COUNTER(UpdateKinematicTargets_CollectTransforms);
        
        // First, upload regular component transforms
        for (const TWeakObjectPtr<USceneComponent> &Component : UniqueComponents)
        {
            if (Component.IsValid())
            {
                USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Component.Get());
                
                if (SkelMesh && SkeletalMeshBoneMap.Contains(SkelMesh))
                {
                    // This is a skeletal mesh with bone attachments
                    // Upload component transform first (for the skeletal mesh itself)
                    componentTransforms.Add(SkelMesh->GetWorldMatrix());
                }
                else
                {
                    // Regular component transform
                    componentTransforms.Add(Component->GetWorldMatrix());
                }
            }
            else
            {
                // Component destroyed - add identity
                componentTransforms.Add(FMatrix::Identity);
            }
        }
        
        for (const auto& Pair : SkeletalMeshBoneMap)
        {
            USkeletalMeshComponent* SkelMesh = Pair.Key;
            const TArray<FBoneAttachmentInfo>& BoneInfos = Pair.Value;
            
            if (!SkelMesh)
                continue;
            
            // Get all bone transforms for this skeletal mesh
            for (const FBoneAttachmentInfo& BoneInfo : BoneInfos)
            {
                // Ensure we're adding at the correct slot
                while (componentTransforms.Num() < static_cast<int32>(BoneInfo.TransformSlot))
                {
                    componentTransforms.Add(FMatrix::Identity);
                }
                
                // Get bone world transform
                FTransform BoneTransform = SkelMesh->GetBoneTransform(BoneInfo.BoneIndex);
                componentTransforms.Add(BoneTransform.ToMatrixWithScale());
            }
        }
    }

    if (componentTransforms.Num() > 0)
    {
        QUICK_SCOPE_CYCLE_COUNTER(UpdateKinematicTargets_UploadTransforms);
        BatchedSolver->UploadComponentTransforms(componentTransforms);
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

void FClothBatchManager::UpdateInstanceInvMass(FClothInstanceHandle* Instance)
{
    if (!Instance || !BatchedSolver || !Graphics)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchManager: Cannot update InvMass - invalid state"));
        return;
    }

    UClothComponent* component = Instance->GetOwnerComponent();
    if (!component)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchManager: Cannot update InvMass - no owner component"));
        return;
    }

    const FClothInstanceMetadata& metadata = Instance->GetMetadata();
    const TArray<float>& runtimeInvMasses = component->GetRuntimeInvMasses();

    if (runtimeInvMasses.Num() == 0)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchManager: Cannot update InvMass - empty RuntimeInvMasses"));
        return;
    }

    // Get instance's buffer range
    uint32 offset = metadata.ParticleOffset;
    uint32 count = metadata.ParticleCount;

    // Validate range
    if (count != (uint32)runtimeInvMasses.Num())
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager: InvMass count mismatch - metadata: %d, runtime: %d"),
               count, runtimeInvMasses.Num());
        return;
    }

    // Update only this instance's range in unified buffer using D3D11_BOX
    D3D11_BOX destBox;
    destBox.left = offset * sizeof(float);
    destBox.right = (offset + count) * sizeof(float);
    destBox.top = 0;
    destBox.bottom = 1;
    destBox.front = 0;
    destBox.back = 1;

    ID3D11Buffer* invMassBuffer = BatchedSolver->GetInvMassBuffer();
    if (!invMassBuffer)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothBatchManager: InvMass buffer not available"));
        return;
    }

    Graphics->DeviceContext->UpdateSubresource(
        invMassBuffer,
        0,
        &destBox,
        runtimeInvMasses.GetData(),
        0,
        0
    );

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager: Updated InvMass for instance (offset: %d, count: %d)"),
           offset, count);
}

void FClothBatchManager::UpdateInstanceAttachments(FClothInstanceHandle* Instance)
{
    if (!Instance)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchManager: Cannot update attachments - invalid instance"));
        return;
    }

    // This ensures TotalAttachmentCount is updated before next simulation frame
    BuildKinematicAttachmentData();

    if (BatchedSolver)
    {
        BatchedSolver->SetUsedCounts(
            TotalParticleCount,
            TotalConstraintCount,
            TotalBendConstraintCount,
            TotalAttachmentCount,  // This updates UsedAttachmentCount in solver
            TotalTriangleCount,
            Instances.Num(),
            TotalAreaConstraintCount,
            TotalEdgeCollisionCount
        );
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager: Rebuilt attachment data (Total: %d attachments, Solver updated)"),
           TotalAttachmentCount);

    // TODO: Implement incremental update optimization
    // For now, we rebuild all attachments when any instance changes
    // Future optimization: Track per-instance attachment ranges and update only affected range
}
