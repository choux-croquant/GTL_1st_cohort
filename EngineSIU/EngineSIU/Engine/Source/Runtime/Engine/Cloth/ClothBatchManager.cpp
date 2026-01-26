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
    : LODLevel(InLODLevel), BatchedSolver(nullptr), TotalParticleCount(0), TotalConstraintCount(0), TotalBendConstraintCount(0), TotalShearConstraintCount(0), TotalAreaConstraintCount(0), TotalKinematicTargetCount(0), TotalTriangleCount(0), AllocatedParticleCapacity(0), AllocatedConstraintCapacity(0), AllocatedBendConstraintCapacity(0), AllocatedKinematicTargetCapacity(0), AllocatedTriangleCapacity(0), AllocatedInstanceCapacity(0), bNeedsReallocation(false), bNeedsCompaction(false), GrowthFactor(1.5f), Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), bIsInitialized(false)
{
}

FClothBatchManager::~FClothBatchManager()
{
    Release();
}

void FClothBatchManager::Initialize(FGraphicsDevice *InGraphics,
                                    FDXDBufferManager *InBufferMgr,
                                    FDXDShaderManager *InShaderMgr)
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

    // Create batched solver
    BatchedSolver = new FClothBatchedSolver();
    BatchedSolver->Initialize(Graphics, BufferManager, ShaderManager);

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

/**
 * Graph-color distance constraints using greedy algorithm
 * Ensures no two constraints in same color share particles
 * Based on PhysixStudio BuildStretchConstraints coloring
 *
 * @param Constraints - Distance constraints to color (will be sorted by color)
 * @param NumParticles - Total particle count for vertex mask size
 * @param OutColorOffsets - Output array [NumColors+1] with offsets into constraint array
 * @return Number of color groups used
 */
static uint32 ColorConstraintsGreedy(
    TArray<FClothDistanceConstraint> &Constraints,
    uint32 NumParticles,
    TArray<uint32> &OutColorOffsets)
{
    if (Constraints.Num() == 0)
    {
        OutColorOffsets.Add(0);
        return 0;
    }

    // Vertex mask: bit i = 1 means particle is used by color i
    TArray<uint32> VertexMask;
    VertexMask.SetNumZeroed(NumParticles);

    uint32 MaxColorUsed = 0;

    // Assign color to each constraint
    for (int32 ConstraintIdx = 0; ConstraintIdx < Constraints.Num(); ++ConstraintIdx)
    {
        FClothDistanceConstraint &C = Constraints[ConstraintIdx];

        uint32 MaskA = VertexMask[C.ParticleA];
        uint32 MaskB = VertexMask[C.ParticleB];
        uint32 UsedMask = MaskA | MaskB;

        // Find first unused color (PhysixStudio pattern)
        uint32 Color = 0;
        while (UsedMask & (1u << Color))
        {
            ++Color;
            // Safety: max 32 colors with uint32 mask
            if (Color >= 32)
            {
                Color = 31;
                break;
            }
        }

        C.ColorGroup = Color;
        MaxColorUsed = FMath::Max(MaxColorUsed, Color);

        // Mark vertices as used by this color
        VertexMask[C.ParticleA] |= (1u << Color);
        VertexMask[C.ParticleB] |= (1u << Color);
    }

    uint32 NumColors = MaxColorUsed + 1;

    // Sort constraints by color for contiguous dispatch
    Constraints.Sort([](const FClothDistanceConstraint &A, const FClothDistanceConstraint &B)
                     { return A.ColorGroup < B.ColorGroup; });

    // Build color offset array (PhysixStudio pattern)
    OutColorOffsets.SetNum(NumColors + 1);
    OutColorOffsets[0] = 0;

    uint32 CurrentColor = 0;
    for (int32 i = 0; i < Constraints.Num(); ++i)
    {
        while (CurrentColor < Constraints[i].ColorGroup)
        {
            ++CurrentColor;
            OutColorOffsets[CurrentColor] = i;
        }
    }
    OutColorOffsets[NumColors] = Constraints.Num();

    return NumColors;
}

/**
 * Build shear constraints - one per triangle
 * Based on PhysixStudio BuildShearConstraints
 *
 * Shear measures the dot product of triangle edges.
 * Prevents triangles from collapsing into thin lines.
 */
static void BuildShearConstraints(
    const TArray<FVector> &RestPositions,
    const TArray<uint32> &Indices,
    TArray<FClothShearConstraint> &OutConstraints)
{
    OutConstraints.Empty();

    uint32 NumTriangles = Indices.Num() / 3;
    OutConstraints.Reserve(NumTriangles);

    for (uint32 t = 0; t < NumTriangles; ++t)
    {
        uint32 i0 = Indices[3 * t + 0];
        uint32 i1 = Indices[3 * t + 1];
        uint32 i2 = Indices[3 * t + 2];

        const FVector &x0 = RestPositions[i0];
        const FVector &x1 = RestPositions[i1];
        const FVector &x2 = RestPositions[i2];

        FVector e1 = x1 - x0;
        FVector e2 = x2 - x0;

        float restDot = FVector::DotProduct(e1, e2);

        FClothShearConstraint c;
        c.ParticleA = i0;
        c.ParticleB = i1;
        c.ParticleC = i2;
        c.RestDot = restDot;
        c.Compliance = 1e-6f; // PhysixStudio default
        c.Lambda = 0.0f;

        OutConstraints.Add(c);
    }
}

/**
 * Build area constraints - one per triangle
 * Based on PhysixStudio BuildAreaConstraints
 * Preserves triangle area to prevent volume loss
 */
static void BuildAreaConstraints(
    const TArray<FVector> &RestPositions,
    const TArray<uint32> &Indices,
    TArray<FClothAreaConstraint> &OutConstraints)
{
    OutConstraints.Empty();

    uint32 NumTriangles = Indices.Num() / 3;
    OutConstraints.Reserve(NumTriangles);

    for (uint32 t = 0; t < NumTriangles; ++t)
    {
        uint32 i0 = Indices[3 * t + 0];
        uint32 i1 = Indices[3 * t + 1];
        uint32 i2 = Indices[3 * t + 2];

        FVector p0 = RestPositions[i0];
        FVector p1 = RestPositions[i1];
        FVector p2 = RestPositions[i2];

        FVector e0 = p1 - p0;
        FVector e1 = p2 - p0;

        FVector restNormalVec = FVector::CrossProduct(e0, e1);
        float restArea = 0.5f * restNormalVec.Size();

        // Normalized rest normal (PhysixStudio cloth_sim_data.h line 460)
        FVector normalizedRestNormal = (restArea > 0.0f)
                                           ? (restNormalVec / (2.0f * restArea))
                                           : FVector(0.0f, 0.0f, 1.0f);

        FClothAreaConstraint c;
        c.ParticleA = i0;
        c.ParticleB = i1;
        c.ParticleC = i2;
        c.RestArea = restArea;
        c.RestNormal = normalizedRestNormal;
        c.Compliance = 1e-2f; // PhysixStudio default
        c.Lambda = 0.0f;

        OutConstraints.Add(c);
    }
}

/**
 * Compute rest dihedral angle for bending constraint
 * Based on PhysixStudio ComputeRestBendAngle (cloth_sim_data.h lines 153-178)
 */
static float ComputeRestBendAngle(
    uint32 i0, uint32 i1, uint32 i2, uint32 i3,
    const TArray<FVector> &Positions)
{
    FVector p0 = Positions[i0]; // Shared edge vertex 1
    FVector p1 = Positions[i1]; // Shared edge vertex 2
    FVector p2 = Positions[i2]; // Triangle 1 opposite
    FVector p3 = Positions[i3]; // Triangle 2 opposite

    FVector e = p1 - p0;
    float el = e.Size();
    if (el < 1e-8f)
        return 0.0f;
    FVector ehat = e / el;

    FVector n1 = FVector::CrossProduct(p1 - p0, p2 - p0).GetSafeNormal();
    FVector n2 = FVector::CrossProduct(p1 - p0, p3 - p0).GetSafeNormal();

    float c = FMath::Clamp(FVector::DotProduct(n1, n2), -1.0f, 1.0f);
    FVector cross_n1n2 = FVector::CrossProduct(n1, n2);
    float s = FVector::DotProduct(ehat, cross_n1n2);

    float phi = FMath::Atan2(s, c);

    return phi;
}

/**
 * Build bend constraints using edge-to-triangle adjacency
 * Based on PhysixStudio BuildBendConstraints (cloth_sim_data.h lines 346-432)
 */
static void BuildBendConstraints(
    const TArray<FVector> &RestPositions,
    const TArray<uint32> &Indices,
    TArray<FClothBendConstraint> &OutConstraints)
{
    OutConstraints.Empty();

    struct FEdgeKey
    {
        uint32 A, B; // A < B always

        bool operator==(const FEdgeKey &Other) const
        {
            return A == Other.A && B == Other.B;
        }

        friend uint32 GetTypeHash(const FEdgeKey &Key)
        {
            return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
        }
    };

    struct FTriRef
    {
        uint32 TriIndex;
        uint32 OppVertex;
    };

    // Build edge-to-triangle map
    TMap<FEdgeKey, TPair<FTriRef, FTriRef>> EdgeMap;

    uint32 NumTriangles = Indices.Num() / 3;
    EdgeMap.Reserve(Indices.Num()); // Rough estimate

    auto MakeEdge = [](uint32 i, uint32 j) -> FEdgeKey
    {
        return (i < j) ? FEdgeKey{i, j} : FEdgeKey{j, i};
    };

    for (uint32 t = 0; t < NumTriangles; ++t)
    {
        uint32 i0 = Indices[3 * t + 0];
        uint32 i1 = Indices[3 * t + 1];
        uint32 i2 = Indices[3 * t + 2];

        FEdgeKey e01 = MakeEdge(i0, i1);
        FEdgeKey e12 = MakeEdge(i1, i2);
        FEdgeKey e20 = MakeEdge(i2, i0);

        FTriRef r0{t, i2}; // Edge e01, opposite vertex i2
        FTriRef r1{t, i0}; // Edge e12, opposite vertex i0
        FTriRef r2{t, i1}; // Edge e20, opposite vertex i1

        auto InsertRef = [&EdgeMap](const FEdgeKey &E, const FTriRef &R)
        {
            TPair<FTriRef, FTriRef> *Found = EdgeMap.Find(E);
            if (!Found)
            {
                EdgeMap.Add(E, TPair<FTriRef, FTriRef>(R, FTriRef{0xFFFFFFFF, 0xFFFFFFFF}));
            }
            else if (Found->Value.TriIndex == 0xFFFFFFFF)
            {
                Found->Value = R;
            }
        };

        InsertRef(e01, r0);
        InsertRef(e12, r1);
        InsertRef(e20, r2);
    }

    // Create bend constraints for shared edges
    OutConstraints.Reserve(EdgeMap.Num());

    for (const auto &Pair : EdgeMap)
    {
        const FEdgeKey &E = Pair.Key;
        const FTriRef &T0 = Pair.Value.Key;
        const FTriRef &T1 = Pair.Value.Value;

        // Skip boundary edges (only one adjacent triangle)
        if (T1.TriIndex == 0xFFFFFFFF)
            continue;

        uint32 i0 = E.A;          // Shared edge vertex 1
        uint32 i1 = E.B;          // Shared edge vertex 2
        uint32 i2 = T0.OppVertex; // Triangle 0 opposite
        uint32 i3 = T1.OppVertex; // Triangle 1 opposite

        float restAngle = ComputeRestBendAngle(i0, i1, i2, i3, RestPositions);

        FClothBendConstraint bc;
        bc.ParticleA = i0;
        bc.ParticleB = i1;
        bc.ParticleC = i2;
        bc.ParticleD = i3;
        bc.RestAngle = restAngle;
        bc.Stiffness = 1.0f;
        bc.Compliance = 500.0f; // PhysixStudio default (higher = softer bending)
        bc.Lambda = 0.0f;

        OutConstraints.Add(bc);
    }
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

    // Apply graph coloring to distance constraints (PhysixStudio Phase 2)
    TArray<FClothDistanceConstraint> ColoredConstraints = Params.Constraints;
    TArray<uint32> ColorOffsets;
    uint32 NumColors = ColorConstraintsGreedy(ColoredConstraints, particleCount, ColorOffsets);

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Graph coloring complete - %d constraints colored into %d groups"),
           static_cast<int32>(LODLevel), ColoredConstraints.Num(), NumColors);

    // Build shear constraints (PhysixStudio Phase 3) - one per triangle
    TArray<FClothShearConstraint> ShearConstraints;
    BuildShearConstraints(Params.RestPositions, Params.Indices, ShearConstraints);
    uint32 shearConstraintCount = ShearConstraints.Num();

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Built %d shear constraints (one per triangle)"),
           static_cast<int32>(LODLevel), shearConstraintCount);

    // Build area constraints (PhysixStudio Phase 4) - one per triangle
    TArray<FClothAreaConstraint> AreaConstraints;
    BuildAreaConstraints(Params.RestPositions, Params.Indices, AreaConstraints);
    uint32 areaConstraintCount = AreaConstraints.Num();

    UE_LOG(ELogLevel::Display, TEXT("ClothBatchManager[LOD%d]: Built %d area constraints (one per triangle)"),
           static_cast<int32>(LODLevel), areaConstraintCount);

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
    metadata.ShearConstraintOffset = TotalShearConstraintCount;
    metadata.ShearConstraintCount = shearConstraintCount;
    metadata.AreaConstraintOffset = TotalAreaConstraintCount; // NEW: Phase 4
    metadata.AreaConstraintCount = areaConstraintCount;       // NEW: Phase 4
    metadata.LRAOffset = 0;                                   // Will be set when LRA is implemented
    metadata.LRACount = 0;
    metadata.NumColors = NumColors; // Store graph coloring result
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
    TotalShearConstraintCount += shearConstraintCount; // NEW: Phase 3
    TotalAreaConstraintCount += areaConstraintCount;   // NEW: Phase 4
    TotalKinematicTargetCount += kinematicTargetCount;
    TotalTriangleCount += triangleCount;

    // Update solver counts
    BatchedSolver->SetUsedCounts(TotalParticleCount, TotalConstraintCount, TotalBendConstraintCount,
                                 TotalShearConstraintCount, // NEW: Phase 3
                                 TotalAreaConstraintCount,  // NEW: Phase 4
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

    // 3. Upload distance constraints with global particle indices (using colored constraints)
    if (ColoredConstraints.Num() > 0)
    {
        TArray<FClothDistanceConstraintGPU> constraintsGPU;
        constraintsGPU.Reserve(ColoredConstraints.Num());

        for (const FClothDistanceConstraint &c : ColoredConstraints)
        {
            FClothDistanceConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = c.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = c.ParticleB + metadata.ParticleOffset;
            gpu.RestLength = c.RestLength;
            gpu.Stiffness = c.Stiffness;
            gpu.Compliance = c.Compliance;
            gpu.Lambda = c.Lambda;
            gpu.ColorGroup = c.ColorGroup; // NEW: Graph coloring group
            gpu.Padding0 = 0.0f;

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

    // NEW: Upload shear constraints with global particle indices (Phase 3)
    if (ShearConstraints.Num() > 0)
    {
        TArray<FClothShearConstraintGPU> shearConstraintsGPU;
        shearConstraintsGPU.Reserve(ShearConstraints.Num());

        for (const FClothShearConstraint &sc : ShearConstraints)
        {
            FClothShearConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = sc.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = sc.ParticleB + metadata.ParticleOffset;
            gpu.ParticleC = sc.ParticleC + metadata.ParticleOffset;
            gpu.RestDot = sc.RestDot;
            gpu.Compliance = sc.Compliance;
            gpu.Lambda = sc.Lambda;
            gpu.Padding0 = 0.0f;
            gpu.Padding1 = 0.0f;

            shearConstraintsGPU.Add(gpu);
        }

        BatchedSolver->UploadShearConstraintData(shearConstraintsGPU, metadata.ShearConstraintOffset);
    }

    // NEW: Upload area constraints with global particle indices (Phase 4)
    if (AreaConstraints.Num() > 0)
    {
        TArray<FClothAreaConstraintGPU> areaConstraintsGPU;
        areaConstraintsGPU.Reserve(AreaConstraints.Num());

        for (const FClothAreaConstraint &ac : AreaConstraints)
        {
            FClothAreaConstraintGPU gpu;
            // Convert local particle indices to global indices
            gpu.ParticleA = ac.ParticleA + metadata.ParticleOffset;
            gpu.ParticleB = ac.ParticleB + metadata.ParticleOffset;
            gpu.ParticleC = ac.ParticleC + metadata.ParticleOffset;
            gpu.RestArea = ac.RestArea;
            gpu.RestNormal = ac.RestNormal;
            gpu.Lambda = ac.Lambda;

            areaConstraintsGPU.Add(gpu);
        }

        BatchedSolver->UploadAreaConstraintData(areaConstraintsGPU, metadata.AreaConstraintOffset);
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
    TotalShearConstraintCount -= metadata.ShearConstraintCount; // NEW: Phase 3
    TotalAreaConstraintCount -= metadata.AreaConstraintCount;   // NEW: Phase 4
    TotalKinematicTargetCount -= metadata.KinematicTargetCount;
    TotalTriangleCount -= metadata.TriangleCount;

    // Remove from tracking
    Instances.Remove(Instance);
    InstanceToMetadataIndex.Remove(Instance);

    // Mark for compaction (don't compact immediately)
    bNeedsCompaction = true;

    // Update solver counts
    BatchedSolver->SetUsedCounts(TotalParticleCount, TotalConstraintCount, TotalBendConstraintCount,
                                 TotalShearConstraintCount, // NEW: Phase 3
                                 TotalAreaConstraintCount,  // NEW: Phase 4
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
