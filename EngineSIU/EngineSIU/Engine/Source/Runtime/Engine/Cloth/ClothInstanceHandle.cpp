/**
 * Cloth Instance Handle Implementation
 */

#include "ClothInstanceHandle.h"
#include "ClothBatchManager.h"

FClothInstanceHandle::FClothInstanceHandle(FClothBatchManager *InBatch, int32 InMetadataIndex)
    : BatchManager(InBatch), MetadataIndex(InMetadataIndex), OwnerComponent(nullptr), PendingLOD(EClothLODLevel::LOD_0), bLODChangePending(false)
{
}

FClothInstanceHandle::~FClothInstanceHandle()
{
}

void FClothInstanceHandle::SetParameters(const FClothInstanceParameters &Params)
{
    Parameters = Params;

    // Sync metadata state
    Metadata.bIsActive = (Params.IsActive != 0);

    // TODO: Notify batch manager to update GPU parameter buffer
}

void FClothInstanceHandle::SetActive(bool bActive)
{
    Metadata.bIsActive = bActive;
    Parameters.IsActive = bActive ? 1 : 0;

    // TODO: Notify batch manager to update GPU parameter buffer
}

void FClothInstanceHandle::RequestLODChange(EClothLODLevel TargetLOD)
{
    if (TargetLOD != Metadata.CurrentLOD)
    {
        PendingLOD = TargetLOD;
        bLODChangePending = true;
    }
}

void FClothInstanceHandle::UpdateKinematicTargets(const TArray<FClothAttachmentData> &Attachments)
{
    // TODO: Forward to batch manager to update kinematic target buffer
    // This will be implemented when we create FClothBatchManager
}
