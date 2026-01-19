/**
 * Cloth World Implementation
 * Central manager for all cloth simulations in a world
 */

#include "ClothWorld.h"
#include "ClothInstance.h"
#include "ClothSolver.h"
#include "Engine/ClothAsset.h"
#include "Components/ClothComponent.h"
#include "World/World.h"
#include "Container/Map.h"
#include "Engine/UserInterface/Console.h"


FClothWorld::FClothWorld()
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), Solver(nullptr), bIsInitialized(false), TotalParticleCount(0), TotalConstraintCount(0)
{
}

FClothWorld::~FClothWorld()
{
    Release();
}

void FClothWorld::Initialize(FGraphicsDevice *InGraphics, FDXDBufferManager *InBufferManager, FDXDShaderManager *InShaderManager)
{
    Graphics = InGraphics;
    BufferManager = InBufferManager;
    ShaderManager = InShaderManager;

    // Create shared solver
    Solver = new FClothSolver();
    Solver->Initialize(Graphics, BufferManager, ShaderManager);

    bIsInitialized = true;

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Initialized"));
}

void FClothWorld::Release()
{
    // Clean up
    for (FClothInstance *Instance : ActiveInstances)
    {
        if (Instance)
        {
            Instance->Release();
            delete Instance;
        }
    }
    ActiveInstances.Empty();

    for (FClothInstance *Instance : PendingRemoval)
    {
        if (Instance)
        {
            Instance->Release();
            delete Instance;
        }
    }
    PendingRemoval.Empty();

    // Release solver
    if (Solver)
    {
        Solver->Release();
        delete Solver;
        Solver = nullptr;
    }

    bIsInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Released"));
}

void FClothWorld::Update(float DeltaTime)
{
    if (!bIsInitialized || ActiveInstances.Num() == 0) return;
    QUICK_SCOPE_CYCLE_COUNTER(ClothSimulate_Tick)
    QUICK_GPU_SCOPE_CYCLE_COUNTER(ClothSimulate_Tick_GPU, *FEngineLoop::Renderer.GPUTimingManager)
    UpdateKinematicData(DeltaTime);

    SimulateAllInstances(DeltaTime);

    CleanupDestroyedInstances();
}

FClothInstance *FClothWorld::RegisterClothInstance(UClothComponent *Component, UClothAsset *Asset, const FClothConfig &Config)
{
    if (!bIsInitialized || !Component || !Asset)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothWorld: Cannot register cloth - invalid parameters"));
        return nullptr;
    }

    // Create new instance with graphics resources
    FClothInstance *Instance = new FClothInstance();

    if (!Instance->Initialize(Asset, Config, Graphics, BufferManager, ShaderManager))
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothWorld: Failed to initialize cloth instance"));
        delete Instance;
        return nullptr;
    }

    // Link to component
    Instance->SetOwnerComponent(Component);

    // Add to active list
    ActiveInstances.Add(Instance);

    // Update statistics
    TotalParticleCount += Instance->GetNumParticles();
    TotalConstraintCount += Instance->GetNumConstraints();

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Registered cloth instance (%d particles) - Total: %d instances, %d particles"),
           Instance->GetNumParticles(), ActiveInstances.Num(), TotalParticleCount);

    return Instance;
}

void FClothWorld::UnregisterClothInstance(FClothInstance *Instance)
{
    if (!Instance) return;

    int32 Index = ActiveInstances.Find(Instance);

    if (Index != INDEX_NONE)
    {
        ActiveInstances.RemoveAt(Index);

        // Update statistics
        TotalParticleCount -= Instance->GetNumParticles();
        TotalConstraintCount -= Instance->GetNumConstraints();

        PendingRemoval.Add(Instance);

        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Unregistered cloth instance - Remaining: %d instances"), ActiveInstances.Num());
    }
}

int32 FClothWorld::GetNumActiveInstances() const
{
    return ActiveInstances.Num();
}

void FClothWorld::UpdateKinematicData(float DeltaTime)
{
    // Update kinematic data for all instances
    // This is called before simulation
    for (FClothInstance *Instance : ActiveInstances)
    {
        if (Instance && Instance->IsActive())
        {
            Instance->UpdateKinematicData(DeltaTime);
        }
    }
}

void FClothWorld::SimulateAllInstances(float DeltaTime)
{
    // Simulate all active cloth instances
    // Each instance owns its own solver and GPU resources
    // TODO Future optimization: Batch all instances into single GPU dispatch

    for (FClothInstance *Instance : ActiveInstances)
    {
        if (Instance && Instance->IsActive() && Instance->IsValid())
        {
            // Each instance simulates itself with its own solver
            Instance->Simulate(DeltaTime);
        }
    }
}

void FClothWorld::CleanupDestroyedInstances()
{
    // Delete instances that were unregistered this frame
    for (FClothInstance *Instance : PendingRemoval)
    {
        if (Instance)
        {
            Instance->Release();
            delete Instance;
        }
    }
    PendingRemoval.Empty();
}
