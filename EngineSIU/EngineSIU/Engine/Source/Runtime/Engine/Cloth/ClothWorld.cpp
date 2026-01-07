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

// Per-world cloth world storage
static TMap<UWorld *, FClothWorld *> GClothWorlds;

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
    // Clean up all instances
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
    if (!bIsInitialized || ActiveInstances.Num() == 0)
        return;

    // Step 1: Update kinematic data for all instances
    UpdateKinematicData(DeltaTime);

    // Step 2: Run GPU simulation for all instances
    SimulateAllInstances(DeltaTime);

    // Step 3: Clean up destroyed instances
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
    if (!Instance)
        return;

    // Remove from active list
    int32 Index = ActiveInstances.Find(Instance);
    if (Index != INDEX_NONE)
    {
        ActiveInstances.RemoveAt(Index);

        // Update statistics
        TotalParticleCount -= Instance->GetNumParticles();
        TotalConstraintCount -= Instance->GetNumConstraints();

        // Add to pending removal (will be deleted at end of frame)
        PendingRemoval.Add(Instance);

        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Unregistered cloth instance - Remaining: %d instances"),
               ActiveInstances.Num());
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

//------------------------------------------------------------------------------
// Global accessor functions
//------------------------------------------------------------------------------

FClothWorld *GetClothWorld(UWorld *World)
{
    if (!World)
        return nullptr;

    FClothWorld **Found = GClothWorlds.Find(World);
    return Found ? *Found : nullptr;
}

FClothWorld *GetOrCreateClothWorld(UWorld *World)
{
    if (!World)
        return nullptr;

    // Check if already exists
    FClothWorld **Found = GClothWorlds.Find(World);
    if (Found && *Found)
    {
        return *Found;
    }

    // Create new cloth world
    FClothWorld *ClothWorld = new FClothWorld();
    GClothWorlds.Add(World, ClothWorld);

    // Note: ClothWorld will be initialized when graphics resources are available
    // This is typically done in World::Initialize or Engine initialization

    return ClothWorld;
}
