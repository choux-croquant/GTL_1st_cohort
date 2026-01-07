/**
 * Cloth Component - Base component for cloth simulation
 * Handles simulation updates and external forces
 */

#pragma once

#include "ActorComponent.h"
#include "../../Cloth/ClothSimulationData.h"

// Forward declarations
class UClothAsset;
class FClothInstance;
class USceneComponent;
class USkeletalMeshComponent;

/**
 * Base cloth component - manages simulation lifecycle
 */
class UClothComponent : public UActorComponent
{
    DECLARE_CLASS(UClothComponent, UActorComponent)

public:
    UClothComponent();
    virtual ~UClothComponent() override;

    // Component lifecycle
    virtual void InitializeComponent() override;
    virtual void TickComponent(float DeltaTime) override;
    virtual void BeginPlay() override;
    // virtual void EndPlay() override;

    // Cloth asset management
    void SetClothAsset(UClothAsset *InAsset);
    UClothAsset *GetClothAsset() const { return ClothAsset; }

    // Simulation control
    void StartSimulation();
    void StopSimulation();
    void ResetSimulation();
    bool IsSimulating() const { return bIsSimulating; }

    // External forces
    void AddForce(const FVector &Force);
    void AddImpulse(const FVector &Impulse);
    void SetWind(const FVector &WindVelocity);
    void SetGravity(const FVector &InGravity);

    // Attachment
    void AttachToComponent(USceneComponent *Parent, FName SocketName);
    void AttachToSkeletalMesh(USkeletalMeshComponent *SkelMesh, const TArray<FName> &BoneNames);

    // Configuration
    void SetClothConfig(const FClothConfig &InConfig);
    const FClothConfig &GetClothConfig() const;

    // Debug
    void SetDebugDrawEnabled(bool bEnabled) { bDebugDrawEnabled = bEnabled; }
    bool GetDebugDrawEnabled() const { return bDebugDrawEnabled; }

protected:
    // Cloth asset
    UClothAsset *ClothAsset;

    // Cloth instance (registered with ClothWorld)
    FClothInstance *ClothInstance;

    // State
    bool bIsSimulating;

    bool bDebugDrawEnabled;

    // Cached attachment data
    TArray<FClothAttachmentData> Attachments;

    // External force accumulation
    FVector AccumulatedForce;

public:
    // Access to cloth instance for rendering
    FClothInstance *GetClothInstance() const { return ClothInstance; }
};
