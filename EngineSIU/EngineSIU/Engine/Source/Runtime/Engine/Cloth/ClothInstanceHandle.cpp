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

    // Sync metadata offsets/counts from parameters
    // These should match what was set during instance creation
    if (BatchManager)
    {
        FClothInstanceMetadata &batchMetadata = const_cast<FClothInstanceMetadata &>(BatchManager->GetInstanceMetadata(MetadataIndex));
        batchMetadata.bIsActive = (Params.IsActive != 0);

        // Sync the parameters' offset/count fields with metadata
        // (These are set during AddInstance and shouldn't change)
        Parameters.ParticleOffset = batchMetadata.ParticleOffset;
        Parameters.ParticleCount = batchMetadata.ParticleCount;
        Parameters.ConstraintOffset = batchMetadata.ConstraintOffset;
        Parameters.ConstraintCount = batchMetadata.ConstraintCount;
        Parameters.BendConstraintOffset = batchMetadata.BendConstraintOffset;
        Parameters.BendConstraintCount = batchMetadata.BendConstraintCount;
        Parameters.KinematicTargetOffset = batchMetadata.KinematicTargetOffset;
        Parameters.KinematicTargetCount = batchMetadata.KinematicTargetCount;
        Parameters.TriangleOffset = batchMetadata.TriangleOffset;
        Parameters.TriangleCount = batchMetadata.TriangleCount;
    }
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
    // Forward to batch manager - kinematic targets are updated in batch during Update()
    // The batch manager collects all attachments from all instances in UpdateKinematicTargets()
}

const FClothInstanceMetadata &FClothInstanceHandle::GetMetadata() const
{
    // Get metadata from batch manager's array (the authoritative source)
    if (BatchManager)
    {
        return BatchManager->GetInstanceMetadata(MetadataIndex);
    }

    // Fallback to local metadata if batch manager is invalid
    return Metadata;
}

FClothInstanceMetadata &FClothInstanceHandle::GetMetadataRef()
{
    // For modification, we need to update the batch manager's array
    // Return a reference to the batch manager's metadata
    if (BatchManager)
    {
        // This is potentially unsafe - need to cast away const
        // Better design would be to have batch manager provide mutable access
        return const_cast<FClothInstanceMetadata &>(BatchManager->GetInstanceMetadata(MetadataIndex));
    }

    // Fallback to local metadata if batch manager is invalid
    return Metadata;
}
