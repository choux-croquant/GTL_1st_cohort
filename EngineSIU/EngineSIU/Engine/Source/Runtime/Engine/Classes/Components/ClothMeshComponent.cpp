#include "ClothMeshComponent.h"
#include "Cloth/ClothSolver.h"

UClothMeshComponent::UClothMeshComponent()
    : WorldTransform(FMatrix::Identity), DebugDrawMode(EClothDebugDrawMode::None), bIsVisible(true), bSimulate(true)
{
    // Set initial bSimulate to true for test
}

UClothMeshComponent::~UClothMeshComponent()
{
}

void UClothMeshComponent::InitializeComponent()
{
    Super::InitializeComponent();

    // Initialize world transform from component transform if attached to actor
    if (GetOwner())
    {
        // TODO: Get transform from actor/component hierarchy
        WorldTransform = FMatrix::Identity;
    }
}

void UClothMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // Update world transform
    if (GetOwner())
    {
        // TODO: Update transform from actor/component hierarchy
    }
}

void UClothMeshComponent::GetRenderData(FClothRenderData &OutData) const
{
    OutData.PositionBufferSRV = nullptr;
    OutData.NormalBufferSRV = nullptr;
    OutData.Indices = nullptr;
    OutData.NumVertices = 0;
    OutData.NumTriangles = 0;
    OutData.WorldTransform = WorldTransform;
    OutData.Material = Materials.Num() > 0 ? Materials[0] : nullptr;

    // Get data from solver
    //if (Solver && Solver->IsInitialized())
    //{
    //    OutData.PositionBufferSRV = Solver->GetPositionBufferSRV();
    //    OutData.NormalBufferSRV = Solver->GetNormalBufferSRV();
    //    OutData.NumVertices = Solver->GetNumParticles();
    //    OutData.NumTriangles = Solver->GetNumParticles() / 3; // Simplified

    //    // Get indices from cloth asset
    //    if (ClothAsset)
    //    {
    //        //OutData.Indices = &ClothAsset->GetIndices();
    //        //OutData.NumTriangles = ClothAsset->GetIndices().Num() / 3;
    //    }
    //}
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
