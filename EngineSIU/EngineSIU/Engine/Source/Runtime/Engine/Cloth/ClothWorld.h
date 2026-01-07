/**
 * Cloth World - Centralized cloth simulation manager
 * Manages all cloth instances and runs simulation once per frame
 * Similar to PhysicsManager for physics bodies
 */

#pragma once

#include "ClothInstance.h"
#include "ClothSolver.h"
#include "Container/Array.h"
#include "HAL/PlatformType.h"

// Forward declarations
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class UClothComponent;
class UWorld;

/**
 * Cloth World - Central manager for all cloth simulations
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
     * Registration API for components
     */
    FClothInstance *RegisterClothInstance(UClothComponent *Component, UClothAsset *Asset, const FClothConfig &Config);
    void UnregisterClothInstance(FClothInstance *Instance);

    /**
     * Query
     */
    int32 GetNumActiveInstances() const;
    bool IsInitialized() const { return bIsInitialized; }

    /**
     * Get solver for direct access (rendering, debug)
     */
    FClothSolver *GetSolver() { return Solver; }
    const FClothSolver *GetSolver() const { return Solver; }

private:
    /**
     * Update all instances before simulation
     */
    void UpdateKinematicData(float DeltaTime);

    /**
     * Run GPU simulation for all instances
     */
    void SimulateAllInstances(float DeltaTime);

    /**
     * Clean up destroyed instances
     */
    void CleanupDestroyedInstances();

private:
    // Graphics resources
    FGraphicsDevice *Graphics;
    FDXDBufferManager *BufferManager;
    FDXDShaderManager *ShaderManager;

    // Shared solver for all instances
    FClothSolver *Solver;

    // All active cloth instances
    TArray<FClothInstance *> ActiveInstances;

    // Instances pending removal
    TArray<FClothInstance *> PendingRemoval;

    // State
    bool bIsInitialized;

    // Statistics
    uint32 TotalParticleCount;
    uint32 TotalConstraintCount;
};

/**
 * Get the cloth world for a given UWorld
 * Returns nullptr if cloth world doesn't exist for this world
 */
FClothWorld *GetClothWorld(UWorld *World);

/**
 * Create or get the cloth world for a given UWorld
 */
FClothWorld *GetOrCreateClothWorld(UWorld *World);
