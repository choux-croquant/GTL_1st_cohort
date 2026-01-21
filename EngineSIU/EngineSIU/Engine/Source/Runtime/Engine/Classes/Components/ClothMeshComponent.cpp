#include "ClothMeshComponent.h"
#include "Cloth/ClothSolver.h"
#include "Cloth/ClothInstance.h"
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
    WorldTransform = GetWorldMatrix();
    FVector gravity = FTransform(WorldTransform).InverseTransformDirection(ClothInstance->GetClothWorld()->GetGlobalForces().GlobalGravity);
    ClothInstance->SetGravity(gravity);
}

void UClothMeshComponent::GetRenderData(FClothRenderData &OutData) const
{
    // Initialize to safe defaults
    OutData.PositionBufferSRV = nullptr;
    OutData.NormalBufferSRV = nullptr;
    OutData.IndexBufferSRV = nullptr;
    OutData.Indices = nullptr;
    OutData.NumVertices = 0;
    OutData.NumTriangles = 0;
    OutData.WorldTransform = WorldTransform;
    OutData.Material = Materials.Num() > 0 ? Materials[0] : nullptr;

    // Get data from solver if available
    if (ClothInstance && ClothInstance->GetSolver() && ClothInstance->GetSolver()->IsInitialized())
    {
        FClothSolver *Solver = ClothInstance->GetSolver();
        OutData.PositionBufferSRV = Solver->GetPositionBufferSRV();
        OutData.NormalBufferSRV = Solver->GetNormalBufferSRV();
        OutData.IndexBufferSRV = nullptr; // Will be added to solver
        OutData.NumVertices = Solver->GetNumParticles();
        OutData.NumTriangles = Solver->GetNumParticles() > 0 ? ClothInstance->GetIndices().Num() / 3 : 0;
        OutData.Indices = &ClothInstance->GetIndices();
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
