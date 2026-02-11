/**
 * Cloth Component - Base component for cloth simulation
 * Handles simulation updates and external forces
 */

#pragma once

#include "SceneComponent.h"
#include "../../Cloth/ClothSimulationData.h"
#include "../../Cloth/ClothBatchTypes.h"

// Forward declarations
class UClothAsset;
class FClothInstanceHandle;
class USceneComponent;
class USkeletalMeshComponent;

/**
 * Base cloth component - manages simulation lifecycle
 */
class UClothComponent : public USceneComponent
{
    DECLARE_CLASS(UClothComponent, USceneComponent)

public:
    UClothComponent();
    virtual ~UClothComponent() override;

    // Component lifecycle
    virtual void InitializeComponent() override;
    virtual void TickComponent(float DeltaTime) override;
    virtual void BeginPlay() override;
    // virtual void EndPlay() override;

    virtual UObject* Duplicate(UObject* InOuter) override;

    // Cloth asset management
    void SetClothAsset(UClothAsset *InAsset);
    UClothAsset *GetClothAsset() const { return ClothAsset; }

    // Simulation control
    void StartSimulation();
    void StopSimulation();
    void ResetSimulation();
    bool IsSimulating() const { return bIsSimulating; }

    // External forces
    void AddImpulse(const FVector &Impulse);
    void SetWind(const FVector &WindVelocity);
    void SetGravity(const FVector &InGravity);

    // Attachment
    void AttachToComponent(USceneComponent *Parent, FName SocketName);
    void AttachToSkeletalMesh(USkeletalMeshComponent *SkelMesh, const TArray<FName> &BoneNames);

protected:
    // Cloth asset
    UClothAsset *ClothAsset;

    // Cloth instance handle - Batched mode
    FClothInstanceHandle *ClothInstanceHandle;

    // State
    bool bIsSimulating;
    bool bUseBatchedMode;

    // Cached attachment data
    TArray<FClothAttachmentData> Attachments;

public:
    // Access to cloth instance handle (Batched mode)
    FClothInstanceHandle *GetClothInstanceHandle() const { return ClothInstanceHandle; }

    // Check which mode is active
    bool IsBatchedMode() const { return bUseBatchedMode; }

    // Access to attachment data
    const TArray<FClothAttachmentData> &GetAttachments() const { return Attachments; }
    TArray<FClothAttachmentData> &GetAttachmentsRef() { return Attachments; }
};
