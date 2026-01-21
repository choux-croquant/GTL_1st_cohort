/**
 * Cloth World - Centralized cloth simulation manager
 * Manages all cloth instances and runs simulation once per frame
 * Similar to PhysicsManager for physics bodies
 */

#pragma once

#include "HAL/PlatformType.h"
#include "ClothInstance.h"
#include "ClothSolver.h"
#include "Container/Array.h"

// Forward declarations
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class UClothComponent;
class UWorld;

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

    /**
     * Global force API - Forces applied to all cloth instances in world-space
     */
    void SetGlobalGravity(const FVector &InGravity);
    void SetGlobalWind(const FVector &InWind);
    void AddExplosionForce(const FVector &Position, float Strength, float Radius, float Duration);
    const FClothGlobalForces &GetGlobalForces() const { return GlobalForces; }

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

    // Global forces applied to all instances
    FClothGlobalForces GlobalForces;

    // State
    bool bIsInitialized;

    // Statistics
    uint32 TotalParticleCount;
    uint32 TotalConstraintCount;
};
