/**
 * Cloth World Implementation
 * Central manager for all cloth simulations in a world
 * Now supports both Legacy and Batched modes
 */

#include "ClothWorld.h"
#include "ClothBatchManager.h"
#include "ClothInstanceHandle.h"
#include "ClothCollisionManager.h"
#include "Engine/ClothAsset.h"
#include "Components/ClothComponent.h"
#include "World/World.h"
#include "Container/Map.h"
#include "Engine/UserInterface/Console.h"

// Global configuration for cloth system mode
static EClothSystemMode GClothSystemMode = EClothSystemMode::Batched;

FClothWorld::FClothWorld()
    : Graphics(nullptr), BufferManager(nullptr), ShaderManager(nullptr), SystemMode(GClothSystemMode), SharedCollisionManager(nullptr), bIsInitialized(false)
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

    {
        // Create shared collision manager (used by all LOD solvers)
        SharedCollisionManager = new FClothCollisionManager();
        SharedCollisionManager->Initialize(512); // Max 512 world-space colliders
        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Created shared collision manager"));

        // Initialize batch managers for batched mode
        InitializeBatchManagers();
        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Initialized in Batched mode"));
    }

    bIsInitialized = true;
}

void FClothWorld::Release()
{
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

    // Release batch managers (batched mode)
    ReleaseBatchManagers();

    // Release shared collision manager
    if (SharedCollisionManager)
    {
        SharedCollisionManager->Release();
        delete SharedCollisionManager;
        SharedCollisionManager = nullptr;
    }

    bIsInitialized = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Released"));
}

void FClothWorld::Update(float DeltaTime)
{
    if (!bIsInitialized)
        return;

    // Early out if no instances
    if (SystemMode == EClothSystemMode::Batched && BatchedInstances.Num() == 0)
        return;

    QUICK_SCOPE_CYCLE_COUNTER(ClothSimulate_Tick)
    QUICK_GPU_SCOPE_CYCLE_COUNTER(ClothSimulate_Tick_GPU, *FEngineLoop::Renderer.GPUTimingManager)

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

// ==================== BATCHED MODE METHODS ====================

FClothInstanceHandle *FClothWorld::RegisterClothInstanceBatched(UClothComponent *Component, UClothAsset *Asset, EClothLODLevel InitialLOD)
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
    Params.OwnerComponent = Component;
    Params.InitialLOD = InitialLOD;

    // Get simulation mesh data from asset (in LOCAL space)
    Params.RestPositions = Asset->GetRestPositions();
    Params.InvMasses = Asset->GetInvMasses();
    Params.Indices = Asset->GetIndices();
    Params.Constraints = Asset->GetDistanceConstraints();
    Params.BendConstraints = Asset->GetBendConstraints();
    Params.AreaConstraints = Asset->GetAreaConstraints();  // Area constraints
    Params.EdgeCollisions = Asset->GetEdgeCollisions();    // Edge collision constraints
    Params.Attachments = Asset->GetAttachmentData();

    // NEW: Get render mesh data from asset if available (for production rendering)
    if (Asset->bUseRenderMesh)
    {
        Params.bUseRenderMesh = true;
        Params.RenderRestPositions = Asset->RenderRestPositions;
        Params.RenderNormals = Asset->RenderNormals;
        Params.RenderUVs = Asset->RenderUVs;
        Params.RenderIndices = Asset->RenderIndices;
        Params.SkinningWeights = Asset->SkinningWeights;  // Legacy K-nearest neighbor weights
        Params.TriangleSkinningWeights = Asset->TriangleSkinningWeights;  // NEW: Triangle-based weights
        
        UE_LOG(ELogLevel::Display, TEXT("ClothWorld: Registering cloth with production rendering - RenderVerts: %d, SimVerts: %d, TriangleWeights: %d"),
               Params.RenderRestPositions.Num(), Params.RestPositions.Num(), Params.TriangleSkinningWeights.Num());
    }

    // FIXED: Use identity transform to keep particles in local space
    // The world transform will be applied during rendering in GetRenderData()
    // This prevents double-transformation when regenerating assets
    Params.WorldTransform = FTransform(FMatrix::Identity);

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
        LODBatches[i]->Initialize(Graphics, BufferManager, ShaderManager, SharedCollisionManager);
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
