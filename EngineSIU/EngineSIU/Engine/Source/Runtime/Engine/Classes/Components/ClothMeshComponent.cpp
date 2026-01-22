#include "ClothMeshComponent.h"
#include "Cloth/ClothSolver.h"
#include "Cloth/ClothInstance.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothBatchManager.h"
#include "Cloth/ClothBatchedSolver.h"
#include "Cloth/ClothWorld.h"

UClothMeshComponent::UClothMeshComponent()
    : WorldTransform(FMatrix::Identity), DebugDrawMode(EClothDebugDrawMode::None), bIsVisible(true), bSimulate(true)
{
    // WorldTransform will be updated from component hierarchy in InitializeComponent and TickComponent
}

UClothMeshComponent::~UClothMeshComponent()
{
}

void UClothMeshComponent::InitializeComponent()
{
    Super::InitializeComponent();

    // Initialize world transform from component hierarchy
    // GetWorldMatrix() is inherited from USceneComponent
    WorldTransform = GetWorldMatrix();
}

void UClothMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // Update world transform from component hierarchy every frame
    // This ensures the cloth follows its parent component/actor transforms
    if (!ClothInstance)
        return;
    WorldTransform = GetWorldMatrix();
    FVector gravity = FTransform(WorldTransform).InverseTransformDirection(ClothInstance->GetClothWorld()->GetGlobalForces().GlobalGravity);
    ClothInstance->SetGravity(gravity);
}

void UClothMeshComponent::GetRenderData(FClothRenderData &OutData) const
{
    // Initialize to safe defaults
    OutData = FClothRenderData(); // Use default constructor
    OutData.WorldTransform = WorldTransform;
    OutData.Material = Materials.Num() > 0 ? Materials[0] : nullptr;

    // Check which mode we're in
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode - get data from batch manager
        FClothBatchManager *batchMgr = ClothInstanceHandle->GetBatchManager();
        if (!batchMgr || !batchMgr->GetSolver())
            return;

        FClothBatchedSolver *batchedSolver = batchMgr->GetSolver();
        const FClothInstanceMetadata &metadata = ClothInstanceHandle->GetMetadata();

        // Set unified buffer SRVs (shared by all instances in batch)
        OutData.PositionBufferSRV = batchedSolver->GetPositionBufferSRV();
        OutData.NormalBufferSRV = batchedSolver->GetNormalBufferSRV();

        // Set unified index buffer (shared by all instances)
        OutData.UnifiedIndexBuffer = batchedSolver->GetUnifiedIndexBuffer();

        // Set instance-specific offsets and counts
        OutData.ParticleOffset = metadata.ParticleOffset;
        OutData.NumVertices = metadata.ParticleCount;
        OutData.IndexOffset = metadata.TriangleOffset * 3; // Convert triangle offset to index offset
        OutData.NumTriangles = metadata.TriangleCount;

        // Mark as batched mode
        OutData.bIsBatchedMode = true;

        // No per-instance index buffer in batched mode (indices are in unified buffer)
        OutData.Indices = nullptr;
        OutData.IndexBufferSRV = nullptr;
    }
    else if (ClothInstance && ClothInstance->GetSolver() && ClothInstance->GetSolver()->IsInitialized())
    {
        // Legacy mode - get data from per-instance solver
        FClothSolver *Solver = ClothInstance->GetSolver();
        OutData.PositionBufferSRV = Solver->GetPositionBufferSRV();
        OutData.NormalBufferSRV = Solver->GetNormalBufferSRV();
        OutData.IndexBufferSRV = nullptr;
        OutData.NumVertices = Solver->GetNumParticles();
        OutData.NumTriangles = Solver->GetNumParticles() > 0 ? ClothInstance->GetIndices().Num() / 3 : 0;
        OutData.Indices = &ClothInstance->GetIndices();
        OutData.UnifiedIndexBuffer = nullptr;
        OutData.ParticleOffset = 0;
        OutData.IndexOffset = 0;
        OutData.bIsBatchedMode = false;
    }
}

UMaterial *UClothMeshComponent::GetMaterial(uint32 Index) const
{
    if (Index < static_cast<uint32>(Materials.Num()))
    {
        return Materials[Index];
    }
    return nullptr;
}

void UClothMeshComponent::SetMaterial(uint32 Index, UMaterial *InMaterial)
{
    if (Index >= static_cast<uint32>(Materials.Num()))
    {
        Materials.SetNum(Index + 1);
    }
    Materials[Index] = InMaterial;
}
