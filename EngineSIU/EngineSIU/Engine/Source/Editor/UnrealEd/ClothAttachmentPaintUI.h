/**
 * Cloth Attachment Paint UI
 * ImGui-based UI panel for cloth attachment editing
 * Simple, immediately usable interface for artists
 */

#pragma once

#include "Define.h"
#include "Container/Array.h"
#include "CoreUObject/UObject/NameTypes.h"

// Forward declarations
class UClothMeshComponent;
class USkeletalMeshComponent;

/**
 * Cloth Attachment Paint UI Panel
 * Provides ImGui-based interface for attachment editing
 */
class FClothAttachmentPaintUI
{
public:
    FClothAttachmentPaintUI();
    ~FClothAttachmentPaintUI();

    // UI rendering
    void RenderUI();

    // Set target component
    void SetClothComponent(UClothMeshComponent* InClothComponent);
    void SetSkeletalMeshComponent(USkeletalMeshComponent* InSkelMeshComponent);

    // Accessors
    bool IsOpen() const { return bIsOpen; }
    void SetOpen(bool bOpen) { bIsOpen = bOpen; }

private:
    // UI sections
    void RenderHeader();
    void RenderSelectionSection();
    void RenderBoneAssignmentSection();
    void RenderAttachmentListSection();
    void RenderActionsSection();

    // Helper functions
    void PopulateBoneList();
    TArray<FName> GetAvailableBones() const;

private:
    // Target components
    UClothMeshComponent* ClothComponent;
    USkeletalMeshComponent* SkeletalMeshComponent;

    // UI state
    bool bIsOpen;

    // Selection parameters
    float ZThreshold;
    int32 SelectedVertexCount;

    // Bone assignment parameters
    int32 SelectedBoneIndex;
    TArray<FName> AvailableBones;
    char BoneNameBuffer[128];

    // Attachment parameters
    float KinematicWeight;
    float AttachmentStiffness;
    float AttachmentDistance;

    // Status
    char StatusMessage[256];
};
