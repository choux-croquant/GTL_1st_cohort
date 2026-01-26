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

/**
 * Explosion force - Transient radial force affecting all cloth
 */
struct FClothExplosionForce
{
    FVector WorldPosition; // Explosion center in world space
    float Strength;        // Force magnitude
    float Radius;          // Radius of effect
    float TimeRemaining;   // Auto-remove when expired

    FClothExplosionForce()
        : WorldPosition(FVector::ZeroVector), Strength(0.0f), Radius(0.0f), TimeRemaining(0.0f)
    {
    }

    FClothExplosionForce(const FVector &InPosition, float InStrength, float InRadius, float InDuration)
        : WorldPosition(InPosition), Strength(InStrength), Radius(InRadius), TimeRemaining(InDuration)
    {
    }
};

/**
 * Global forces applied to all cloth instances in world-space coordinates
 * These forces do not transform with individual cloth instances
 */
struct FClothGlobalForces
{
    FVector GlobalGravity;                   // World-space gravity (e.g., 0, 0, -980)
    FVector GlobalWind;                      // World-space wind direction and strength
    TArray<FClothExplosionForce> Explosions; // Transient explosion forces

    FClothGlobalForces()
        : GlobalGravity(0.0f, 0.0f, -1980.0f), GlobalWind(0.0f, 0.0f, 0.0f)
    {
    }
};

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
    FClothInstanceHandle *RegisterClothInstanceBatched(UClothComponent *Component, UClothAsset *Asset,
                                                       const FClothConfig &Config, EClothLODLevel InitialLOD = EClothLODLevel::LOD_0);
    void UnregisterClothInstanceBatched(FClothInstanceHandle *Instance);

    /**
     * Query
     */
    bool IsInitialized() const { return bIsInitialized; }

    /**
     * Global force API - Forces applied to all cloth instances in world-space
     */
    void SetGlobalGravity(const FVector &InGravity);
    void SetGlobalWind(const FVector &InWind);
    void AddExplosionForce(const FVector &Position, float Strength, float Radius, float Duration);
    const FClothGlobalForces &GetGlobalForces() const { return GlobalForces; }

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

    // Global forces applied to all instances
    FClothGlobalForces GlobalForces;

    // State
    bool bIsInitialized;

    // Statistics
    uint32 TotalParticleCount;
    uint32 TotalConstraintCount;
};
