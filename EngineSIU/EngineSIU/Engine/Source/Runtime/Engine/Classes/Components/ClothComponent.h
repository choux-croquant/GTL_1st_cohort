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

    // NEW: Runtime Attachment Management API
    void BindAttachmentToWorldPosition(uint32 SimVertexIndex,
                                       const FVector& WorldPosition,
                                       float Stiffness = -1.0f,
                                       float AttachDistance = 0.0f);
    
    void BindAttachmentToComponent(uint32 SimVertexIndex,
                                   USceneComponent* TargetComponent,
                                   const FTransform& LocalOffset = FTransform::Identity,
                                   float Stiffness = -1.0f,
                                   float AttachDistance = 0.0f);
    
    void BindAttachmentToBone(uint32 SimVertexIndex,
                             USkeletalMeshComponent* SkeletalMesh,
                             FName BoneName,
                             const FTransform& LocalOffset = FTransform::Identity,
                             float Stiffness = -1.0f,
                             float AttachDistance = 0.0f);
    
    bool UnbindAttachment(uint32 SimVertexIndex);
    void ClearAllAttachments();
    bool UpdateAttachmentTarget(uint32 SimVertexIndex, const FClothAttachmentTarget& NewTarget);
    void SetAttachmentEnabled(uint32 SimVertexIndex, bool bEnabled);
    
    bool IsVertexAttached(uint32 SimVertexIndex) const;
    int32 GetAttachmentCount() const { return AttachmentBindings.Num(); }
    const TArray<FClothAttachmentBinding>& GetAttachmentBindings() const { return AttachmentBindings; }
    const TArray<FClothAttachmentCapability>& GetAttachmentCapabilities() const;

    // DEPRECATED: Old attachment API (for backward compatibility)
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

    // NEW: Per-instance runtime data
    TArray<float> RuntimeInvMasses;                     // Copy of asset's BaseInvMasses, modified per-instance
    TArray<FClothAttachmentBinding> AttachmentBindings; // Instance-specific attachment bindings
    bool bAttachmentsDirty;                             // Flag to trigger GPU update

    // DEPRECATED: Old cached attachment data (for backward compatibility)
    TArray<FClothAttachmentData> Attachments;

private:
    // Internal helpers
    void InitializeRuntimeInvMasses();
    void UpdateRuntimeInvMass(uint32 VertexIndex, bool bIsAttached);
    void MarkAttachmentsDirty();
    void BindAttachment(uint32 SimVertexIndex, const FClothAttachmentTarget& Target,
                       float Stiffness = -1.0f, float AttachDistance = 0.0f);

public:
    // Access to cloth instance handle (Batched mode)
    FClothInstanceHandle *GetClothInstanceHandle() const { return ClothInstanceHandle; }

    // Check which mode is active
    bool IsBatchedMode() const { return bUseBatchedMode; }

    // NEW: Access to per-instance runtime data
    const TArray<float>& GetRuntimeInvMasses() const { return RuntimeInvMasses; }
    TArray<float>& GetRuntimeInvMassesRef() { return RuntimeInvMasses; }

    // DEPRECATED: Old attachment data accessors (for backward compatibility)
    const TArray<FClothAttachmentData> &GetAttachments() const { return Attachments; }
    TArray<FClothAttachmentData> &GetAttachmentsRef() { return Attachments; }
};
