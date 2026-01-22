/**
 * Cloth World Implementation
 * Central manager for all cloth simulations in a world
 * Now supports both Legacy and Batched modes
 */

#include "ClothWorld.h"
#include "ClothInstance.h"
#include "ClothSolver.h"
#include "ClothBatchManager.h"
#include "ClothInstanceHandle.h"
#include "Engine/ClothAsset.h"
#include "Components/ClothComponent.h"
#include "World/World.h"
#include "Container/Map.h"
#include "Engine/UserInterface/Console.h"

// Global configuration for cloth system mode
// Set to Batched to enable new batched simulation system
// Change to Legacy for backward compatibility if needed
static EClothSystemMode GClothSystemMode = EClothSystemMode::Batched;

FClothWorld::FClothWorld()
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), SystemMode(GClothSystemMode), Solver(nullptr), bIsInitialized(false), TotalParticleCount(0), TotalConstraintCount(0)
{
    // Initialize batch manager array to nullptr
    for (int32 i = 0; i < static_cast<int32>(EClothLODLevel::Max); ++i)
    {
        LODBatches[i] = nullptr;
    }
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

    if (SystemMode == EClothSystemMode::Legacy)
    {
        // Create shared solver for legacy mode
        Solver = new FClothSolver();
        Solver->Initialize(Graphics, BufferManager, ShaderManager);
        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Initialized in Legacy mode"));
    }
    else if (SystemMode == EClothSystemMode::Batched)
    {
        // Initialize batch managers for batched mode
        InitializeBatchManagers();
        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Initialized in Batched mode"));
    }

    bIsInitialized = true;
}

void FClothWorld::Release()
{
    // Clean up legacy instances
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

    // Clean up batched instances
    for (FClothInstanceHandle *Handle : BatchedInstances)
    {
        if (Handle)
        {
            delete Handle;
        }
    }
    BatchedInstances.Empty();

    for (FClothInstanceHandle *Handle : BatchedPendingRemoval)
    {
        if (Handle)
        {
            delete Handle;
        }
    }
    BatchedPendingRemoval.Empty();

    // Release solver (legacy mode)
    if (Solver)
    {
        Solver->Release();
        delete Solver;
        Solver = nullptr;
    }

    // Release batch managers (batched mode)
    ReleaseBatchManagers();

    bIsInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Released"));
}

void FClothWorld::Update(float DeltaTime)
{
    if (!bIsInitialized)
        return;

    // Early out if no instances
    if (SystemMode == EClothSystemMode::Legacy && ActiveInstances.Num() == 0)
        return;
    if (SystemMode == EClothSystemMode::Batched && BatchedInstances.Num() == 0)
        return;

    QUICK_SCOPE_CYCLE_COUNTER(ClothSimulate_Tick)
    QUICK_GPU_SCOPE_CYCLE_COUNTER(ClothSimulate_Tick_GPU, *FEngineLoop::Renderer.GPUTimingManager)

    // Update transient explosion forces
    for (int32 i = GlobalForces.Explosions.Num() - 1; i >= 0; --i)
    {
        GlobalForces.Explosions[i].TimeRemaining -= DeltaTime;
        if (GlobalForces.Explosions[i].TimeRemaining <= 0.0f)
        {
            GlobalForces.Explosions.RemoveAt(i);
        }
    }

    if (SystemMode == EClothSystemMode::Legacy)
    {
        UpdateKinematicData(DeltaTime);
        SimulateAllInstances(DeltaTime);
        CleanupDestroyedInstances();
    }
    else if (SystemMode == EClothSystemMode::Batched)
    {
        // Process LOD transitions first
        ProcessLODTransitions();

        // Update and simulate batches
        SimulateAllBatches(DeltaTime);

        // Cleanup
        for (FClothInstanceHandle *Handle : BatchedPendingRemoval)
        {
            if (Handle)
                delete Handle;
        }
        BatchedPendingRemoval.Empty();
    }
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

    // Link to component and world
    Instance->SetOwnerComponent(Component);
    Instance->SetClothWorld(this);

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
    // Legacy mode: Simulate all active cloth instances
    // Each instance owns its own solver and GPU resources

    for (FClothInstance *Instance : ActiveInstances)
    {
        if (Instance && Instance->IsActive() && Instance->IsValid())
        {
            // Each instance simulates itself with its own solver
            Instance->Simulate(DeltaTime);
        }
    }
}

void FClothWorld::SimulateAllBatches(float DeltaTime)
{
    // Batched mode: Simulate all LOD batches
    for (int32 i = 0; i < static_cast<int32>(EClothLODLevel::Max); ++i)
    {
        if (LODBatches[i] && LODBatches[i]->GetInstanceCount() > 0)
        {
            LODBatches[i]->Update(DeltaTime);
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

void FClothWorld::SetGlobalGravity(const FVector &InGravity)
{
    GlobalForces.GlobalGravity = InGravity;
}

void FClothWorld::SetGlobalWind(const FVector &InWind)
{
    GlobalForces.GlobalWind = InWind;
}

void FClothWorld::AddExplosionForce(const FVector &Position, float Strength, float Radius, float Duration)
{
    FClothExplosionForce explosion(Position, Strength, Radius, Duration);
    GlobalForces.Explosions.Add(explosion);

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Added explosion force at (%f, %f, %f) with strength %f, radius %f"),
           Position.X, Position.Y, Position.Z, Strength, Radius);
}

// ==================== BATCHED MODE METHODS ====================

FClothInstanceHandle *FClothWorld::RegisterClothInstanceBatched(UClothComponent *Component, UClothAsset *Asset,
                                                                const FClothConfig &Config, EClothLODLevel InitialLOD)
{
    if (!bIsInitialized || !Component || !Asset)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothWorld: Cannot register cloth - invalid parameters"));
        return nullptr;
    }

    if (SystemMode != EClothSystemMode::Batched)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothWorld: RegisterClothInstanceBatched called but system is in Legacy mode"));
        return nullptr;
    }

    // Get appropriate batch manager for LOD level
    FClothBatchManager *BatchMgr = GetBatchManager(InitialLOD);
    if (!BatchMgr)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothWorld: Batch manager not found for LOD %d"), static_cast<int32>(InitialLOD));
        return nullptr;
    }

    // Create instance creation params
    FClothInstanceCreationParams Params;
    // TODO: Fill params from Asset
    Params.Config = Config;
    Params.OwnerComponent = Component;
    Params.InitialLOD = InitialLOD;

    Params.RestPositions = Asset->GetRestPositions();
    Params.InvMasses = Asset->GetInvMasses();
    Params.Indices = Asset->GetIndices(); 
    Params.Constraints = Asset->GetDistanceConstraints();
    Params.BendConstraints = Asset->GetBendConstraints();
    Params.Attachments = Asset->GetAttachmentData();

    // Add instance to batch
    FClothInstanceHandle *Handle = BatchMgr->AddInstance(Params);
    if (!Handle)
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothWorld: Failed to add instance to batch"));
        return nullptr;
    }

    // Track instance
    BatchedInstances.Add(Handle);

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Registered batched cloth instance - LOD %d, Total: %d instances"),
           static_cast<int32>(InitialLOD), BatchedInstances.Num());

    return Handle;
}

void FClothWorld::UnregisterClothInstanceBatched(FClothInstanceHandle *Instance)
{
    if (!Instance)
        return;

    int32 Index = BatchedInstances.Find(Instance);
    if (Index != INDEX_NONE)
    {
        BatchedInstances.RemoveAt(Index);

        // Remove from batch manager
        FClothBatchManager *BatchMgr = Instance->GetBatchManager();
        if (BatchMgr)
        {
            BatchMgr->RemoveInstance(Instance);
        }

        BatchedPendingRemoval.Add(Instance);

        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Unregistered batched cloth instance - Remaining: %d instances"),
               BatchedInstances.Num());
    }
}

void FClothWorld::SetSystemMode(EClothSystemMode Mode)
{
    if (bIsInitialized)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothWorld: Cannot change system mode after initialization"));
        return;
    }
    SystemMode = Mode;
}

void FClothWorld::SetLODSelectionParams(const FClothLODSelectionParams &Params)
{
    LODSelectionParams = Params;
}

FClothBatchManager *FClothWorld::GetBatchManager(EClothLODLevel LOD)
{
    int32 Index = static_cast<int32>(LOD);
    if (Index >= 0 && Index < static_cast<int32>(EClothLODLevel::Max))
    {
        return LODBatches[Index];
    }
    return nullptr;
}

int32 FClothWorld::GetNumInstancesInLOD(EClothLODLevel LOD) const
{
    int32 Index = static_cast<int32>(LOD);
    if (Index >= 0 && Index < static_cast<int32>(EClothLODLevel::Max) && LODBatches[Index])
    {
        return LODBatches[Index]->GetInstanceCount();
    }
    return 0;
}

void FClothWorld::ProcessLODTransitions()
{
    // Process any pending LOD transitions
    for (FClothInstanceHandle *Handle : BatchedInstances)
    {
        if (Handle && Handle->HasPendingLODChange())
        {
            EClothLODLevel CurrentLOD = Handle->GetCurrentLOD();
            EClothLODLevel TargetLOD = Handle->GetPendingLOD();

            // TODO: Implement actual migration between batches
            // This requires:
            // 1. Extract data from current batch
            // 2. Add to target batch
            // 3. Update handle metadata

            Handle->ClearPendingLODChange();

            UE_LOG(ELogLevel::Display, TEXT("ClothWorld: LOD transition %d -> %d (TODO: implement migration)"),
                   static_cast<int32>(CurrentLOD), static_cast<int32>(TargetLOD));
        }
    }
}

void FClothWorld::InitializeBatchManagers()
{
    for (int32 i = 0; i < static_cast<int32>(EClothLODLevel::Max); ++i)
    {
        EClothLODLevel LOD = static_cast<EClothLODLevel>(i);
        LODBatches[i] = new FClothBatchManager(LOD);
        LODBatches[i]->Initialize(Graphics, BufferManager, ShaderManager);
    }
}

void FClothWorld::ReleaseBatchManagers()
{
    for (int32 i = 0; i < static_cast<int32>(EClothLODLevel::Max); ++i)
    {
        if (LODBatches[i])
        {
            LODBatches[i]->Release();
            delete LODBatches[i];
            LODBatches[i] = nullptr;
        }
    }
}
