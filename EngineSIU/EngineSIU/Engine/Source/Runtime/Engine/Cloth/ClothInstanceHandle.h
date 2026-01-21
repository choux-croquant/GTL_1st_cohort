/**
 * Cloth Instance Handle
 * Lightweight handle for batched cloth instances
 * Replaces the heavy FClothInstance in the batched architecture
 */

#pragma once

#include "HAL/PlatformType.h"
#include "Container/Array.h"
#include "Math/Vector.h"
#include "ClothBatchTypes.h"
#include "ClothSimulationData.h"

// Forward declarations
class FClothBatchManager;
class UClothComponent;

/**
 * Lightweight instance handle for batched cloth simulation
 * Does not own GPU resources, only tracks metadata and configuration
 */
class FClothInstanceHandle
{
public:
    FClothInstanceHandle(FClothBatchManager *InBatch, int32 InMetadataIndex);
    ~FClothInstanceHandle();

    // Configuration
    void SetParameters(const FClothInstanceParameters &Params);
    const FClothInstanceParameters &GetParameters() const { return Parameters; }

    void SetActive(bool bActive);
    bool IsActive() const { return Metadata.bIsActive; }

    // LOD management
    void RequestLODChange(EClothLODLevel TargetLOD);
    EClothLODLevel GetCurrentLOD() const { return Metadata.CurrentLOD; }
    bool HasPendingLODChange() const { return bLODChangePending; }
    EClothLODLevel GetPendingLOD() const { return PendingLOD; }
    void ClearPendingLODChange() { bLODChangePending = false; }

    // Kinematic target updates (called every frame)
    void UpdateKinematicTargets(const TArray<FClothAttachmentData> &Attachments);

    // Owner tracking
    void SetOwnerComponent(UClothComponent *InOwner) { OwnerComponent = InOwner; }
    UClothComponent *GetOwnerComponent() const { return OwnerComponent; }

    // Metadata access
    const FClothInstanceMetadata &GetMetadata() const { return Metadata; }
    FClothInstanceMetadata &GetMetadataRef() { return Metadata; }
    FClothBatchManager *GetBatchManager() const { return BatchManager; }

    // For internal batch manager use
    void SetMetadataIndex(int32 Index) { MetadataIndex = Index; }
    int32 GetMetadataIndex() const { return MetadataIndex; }

private:
    FClothBatchManager *BatchManager;
    FClothInstanceMetadata Metadata;
    FClothInstanceParameters Parameters;
    int32 MetadataIndex; // Index into batch manager's metadata array

    UClothComponent *OwnerComponent;

    EClothLODLevel PendingLOD;
    bool bLODChangePending;
};
