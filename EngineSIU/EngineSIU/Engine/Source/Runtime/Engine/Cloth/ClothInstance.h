/**
 * Cloth Instance
 * Represents a single cloth simulation instance within the ClothWorld
 * Separates per-instance data from the shared solver
 */

#pragma once

#include "ClothSimulationData.h"
#include "Container/Array.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "HAL/PlatformType.h"

// Forward declarations
class FClothSolver;
class UClothAsset;
class UClothComponent;
class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;

/**
 * Cloth Instance - Per-cloth simulation state
 * Owned by ClothWorld, referenced by ClothComponent
 */
class FClothInstance
{
public:
    FClothInstance();
    ~FClothInstance();

    /**
     * Initialize from cloth asset
     */
    bool Initialize(UClothAsset *InAsset, const FClothConfig &InConfig,
                    FGraphicsDevice *Graphics, FDXDBufferManager *BufferMgr, FDXDShaderManager *ShaderMgr);

    /**
     * Release resources
     */
    void Release();

    /**
     * Run simulation for this instance
     * Called by ClothWorld::SimulateAllInstances
     */
    void Simulate(float DeltaTime);

    /**
     * Update kinematic data (attachments, external forces)
     * Called before simulation
     */
    void UpdateKinematicData(float DeltaTime);

    /**
     * Apply external forces for this frame
     */
    void AddExternalForce(const FVector &Force);
    void SetWind(const FVector &WindVelocity);
    void SetGravity(const FVector &InGravity);

    /**
     * Update attachment positions (from skeletal mesh bones, etc.)
     */
    void UpdateAttachments(const TArray<FClothAttachmentData> &InAttachments);

    /**
     * Reset to initial state
     */
    void Reset();

    /**
     * Get simulation data for rendering
     */
    const FClothSimulationData &GetSimulationData() const { return SimData; }
    const FClothConfig &GetConfig() const { return Config; }

    /**
     * Check if instance is ready for simulation
     */
    bool IsValid() const { return bIsInitialized && NumParticles > 0; }
    bool IsActive() const { return bIsActive; }
    void SetActive(bool bActive) { bIsActive = bActive; }

    /**
     * Get owner component (for debugging)
     */
    void SetOwnerComponent(UClothComponent *InOwner) { OwnerComponent = InOwner; }
    UClothComponent *GetOwnerComponent() const { return OwnerComponent; }

    // Data access for solver
    uint32 GetNumParticles() const { return NumParticles; }
    uint32 GetNumConstraints() const { return NumConstraints; }
    const TArray<FVector> &GetRestPositions() const { return RestPositions; }
    const TArray<float> &GetInvMasses() const { return InvMasses; }
    const TArray<FClothConstraint> &GetConstraints() const { return Constraints; }
    const TArray<uint32> &GetIndices() const { return Indices; }
    
    FClothSolver* GetSolver() const { return Solver; }

private:
    // Solver for this instance
    FClothSolver* Solver;

    // Simulation state
    FClothSimulationData SimData;
    FClothConfig Config;

    // Asset data
    TArray<FVector> RestPositions;
    TArray<float> InvMasses;
    TArray<FClothConstraint> Constraints;
    TArray<uint32> Indices;

    // Runtime data
    TArray<FClothAttachmentData> Attachments;
    FVector ExternalForceAccum;

    // State
    uint32 NumParticles;
    uint32 NumConstraints;
    uint32 NumTriangles;
    bool bIsInitialized;
    bool bIsActive;

    // Back-reference to owner (for debugging)
    UClothComponent *OwnerComponent;
};
