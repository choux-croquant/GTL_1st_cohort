/**
 * Cloth World - Centralized cloth simulation manager
 * Manages all cloth instances and runs simulation once per frame
 * Similar to PhysicsManager for physics bodies
 *
 * Now supports both Legacy (per-instance solver) and Batched (LOD-based batching) modes
 */

#pragma once

#include "HAL/PlatformType.h"
#include "ClothBatchTypes.h"
#include "Container/Array.h"

// Forward declarations
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class UClothComponent;
class UClothAsset;
class UWorld;
class FClothBatchManager;
class FClothInstanceHandle;
class FClothCollisionManager;

/**
 * Cloth World - Central manager for cloth simulations
 * Per-world singleton that owns and updates all cloth instances
 */
class FClothWorld
{
public:
    FClothWorld();
    ~FClothWorld();

    /**
     * Initialize the cloth world with graphics resources
     */
    void Initialize(FGraphicsDevice *InGraphics, FDXDBufferManager *InBufferManager, FDXDShaderManager *InShaderManager);

    /**
     * Release all resources
     */
    void Release();

    /**
     * Main simulation update - called once per frame from engine loop
     * This replaces individual ClothComponent::Tick solver calls
     */
    void Update(float DeltaTime);

    /**
     * Registration API for components - Batched mode
     */
    FClothInstanceHandle *RegisterClothInstanceBatched(UClothComponent *Component, UClothAsset *Asset, EClothLODLevel InitialLOD = EClothLODLevel::LOD_0);
    void UnregisterClothInstanceBatched(FClothInstanceHandle *Instance);

    /**
     * Query
     */
    bool IsInitialized() const { return bIsInitialized; }

    /**
     * Mode management
     */
    void SetSystemMode(EClothSystemMode Mode);
    EClothSystemMode GetSystemMode() const { return SystemMode; }

    /**
     * LOD management
     */
    void SetLODSelectionParams(const FClothLODSelectionParams &Params);
    FClothBatchManager *GetBatchManager(EClothLODLevel LOD);
    int32 GetNumInstancesInLOD(EClothLODLevel LOD) const;
    
    /**
     * Get shared collision manager
     */
    FClothCollisionManager *GetCollisionManager() const { return SharedCollisionManager; }

private:
    void SimulateAllBatches(float DeltaTime);

    /**
     * Process LOD transitions
     */
    void ProcessLODTransitions();

    /**
     * Initialize batch managers
     */
    void InitializeBatchManagers();
    void ReleaseBatchManagers();

private:
    // Graphics resources
    FGraphicsDevice *Graphics;
    FDXDBufferManager *BufferManager;
    FDXDShaderManager *ShaderManager;

    // System mode
    EClothSystemMode SystemMode;

    // Batched mode resources
    FClothBatchManager *LODBatches[static_cast<int32>(EClothLODLevel::Max)];
    TArray<FClothInstanceHandle *> BatchedInstances;
    TArray<FClothInstanceHandle *> BatchedPendingRemoval;
    FClothLODSelectionParams LODSelectionParams;
    
    // Shared collision manager (used by all LOD solvers)
    FClothCollisionManager *SharedCollisionManager;

    // State
    bool bIsInitialized;
};
