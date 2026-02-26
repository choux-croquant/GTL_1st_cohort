/**
 * Cloth Attachment Paint Tool Implementation
 * Editor tool for painting cloth-to-bone attachments in viewport
 * Phase 3: Viewport Interaction (Box Selection + Visual Feedback)
 */

#include "ClothAttachmentPaintTool.h"
#include "Components/ClothMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Classes/Engine/ClothAsset.h"
#include "EditorViewportClient.h"
#include "UnrealClient.h"
#include "Math/Box.h"
#include "Math/Color.h"

FClothAttachmentPaintTool::FClothAttachmentPaintTool()
    : ClothComponent(nullptr)
    , bIsActive(false)
    , PaintMode(EClothAttachmentPaintMode::None)
    , bIsBoxSelecting(false)
    , BoxSelectStart(FVector2D::ZeroVector)
    , BoxSelectEnd(FVector2D::ZeroVector)
    , BoxWorldMin(FVector::ZeroVector)
    , BoxWorldMax(FVector::ZeroVector)
    , BrushRadius(10.0f)
    , BrushStrength(1.0f)
    , BrushWorldPosition(FVector::ZeroVector)
{
}

FClothAttachmentPaintTool::~FClothAttachmentPaintTool()
{
    Deactivate();
}

void FClothAttachmentPaintTool::Activate(UClothMeshComponent* InClothComponent)
{
    if (!InClothComponent)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAttachmentPaintTool: Cannot activate with null component"));
        return;
    }

    ClothComponent = InClothComponent;
    bIsActive = true;
    PaintMode = EClothAttachmentPaintMode::BoxSelect;
    SelectedVertexIndices.Empty();

    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Activated for component '%s'"),
        *ClothComponent->GetName());
}

void FClothAttachmentPaintTool::Deactivate()
{
    if (bIsActive)
    {
        UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Deactivated"));
    }

    ClothComponent = nullptr;
    bIsActive = false;
    PaintMode = EClothAttachmentPaintMode::None;
    SelectedVertexIndices.Empty();
    bIsBoxSelecting = false;
}

bool FClothAttachmentPaintTool::HandleClick(const FViewportClick& Click, FEditorViewportClient* ViewportClient)
{
    if (!bIsActive || !ClothComponent)
        return false;

    // Handle based on paint mode
    switch (PaintMode)
    {
    case EClothAttachmentPaintMode::VertexPick:
        // TODO: Implement vertex picking
        return true;

    case EClothAttachmentPaintMode::BoxSelect:
        // Box selection handled in MouseDown/MouseUp
        return false;

    default:
        return false;
    }
}

bool FClothAttachmentPaintTool::HandleMouseMove(const FVector2D& MousePosition, FEditorViewportClient* ViewportClient)
{
    if (!bIsActive || !ClothComponent)
        return false;

    if (bIsBoxSelecting)
    {
        // Update box selection end point
        BoxSelectEnd = MousePosition;
        return true;
    }

    return false;
}

bool FClothAttachmentPaintTool::HandleMouseDown(const FVector2D& MousePosition, FEditorViewportClient* ViewportClient)
{
    if (!bIsActive || !ClothComponent)
        return false;

    if (PaintMode == EClothAttachmentPaintMode::BoxSelect)
    {
        // Start box selection
        bIsBoxSelecting = true;
        BoxSelectStart = MousePosition;
        BoxSelectEnd = MousePosition;
        
        UE_LOG(ELogLevel::Verbose, TEXT("ClothAttachmentPaintTool: Started box selection at (%.1f, %.1f)"),
            MousePosition.X, MousePosition.Y);
        
        return true;
    }

    return false;
}

bool FClothAttachmentPaintTool::HandleMouseUp(const FVector2D& MousePosition, FEditorViewportClient* ViewportClient)
{
    if (!bIsActive || !ClothComponent)
        return false;

    if (bIsBoxSelecting && PaintMode == EClothAttachmentPaintMode::BoxSelect)
    {
        // End box selection
        bIsBoxSelecting = false;
        BoxSelectEnd = MousePosition;

        // TODO: Convert screen-space box to world-space box
        // TODO: Call SelectVerticesByBox()

        UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Completed box selection from (%.1f, %.1f) to (%.1f, %.1f)"),
            BoxSelectStart.X, BoxSelectStart.Y, BoxSelectEnd.X, BoxSelectEnd.Y);

        return true;
    }

    return false;
}

void FClothAttachmentPaintTool::Render(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    if (!bIsActive || !ClothComponent || !PDI)
        return;

    // Render selected vertices
    RenderSelectedVertices(View, PDI);

    // Render attached vertices (from existing paint data)
    RenderAttachedVertices(View, PDI);

    // Render selection box (if active)
    if (bIsBoxSelecting)
    {
        RenderSelectionBox(View, PDI);
    }
}

void FClothAttachmentPaintTool::SelectVerticesByBox(const FVector& BoxMin, const FVector& BoxMax, ESelectionOperation Operation)
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
        return;

    const TArray<FVector>& RestPositions = ClothComponent->GetClothAsset()->RestPositions;

    // Iterate through all vertices
    for (int32 i = 0; i < RestPositions.Num(); ++i)
    {
        FVector WorldPos = GetVertexWorldPosition(i);

        if (IsVertexInBox(WorldPos, BoxMin, BoxMax))
        {
            UpdateSelection(i, Operation);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Box selection complete, %d vertices selected"),
        SelectedVertexIndices.Num());
}

void FClothAttachmentPaintTool::SelectVerticesByZThreshold(float Threshold, ESelectionOperation Operation)
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
        return;

    const TArray<FVector>& RestPositions = ClothComponent->GetClothAsset()->RestPositions;

    // Find Z range
    float MaxZ = -FLT_MAX;
    float MinZ = FLT_MAX;
    for (const FVector& Pos : RestPositions)
    {
        MaxZ = FMath::Max(MaxZ, Pos.Z);
        MinZ = FMath::Min(MinZ, Pos.Z);
    }

    float ZRange = MaxZ - MinZ;
    float AttachThreshold = MaxZ - (ZRange * (1.0f - Threshold));

    // Select vertices above threshold
    for (int32 i = 0; i < RestPositions.Num(); ++i)
    {
        if (RestPositions[i].Z >= AttachThreshold)
        {
            UpdateSelection(i, Operation);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Z-threshold selection complete, %d vertices selected"),
        SelectedVertexIndices.Num());
}

void FClothAttachmentPaintTool::SelectVertexAtPoint(const FVector& WorldPoint, float Radius, ESelectionOperation Operation)
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
        return;

    const TArray<FVector>& RestPositions = ClothComponent->GetClothAsset()->RestPositions;

    // Find closest vertex within radius
    float ClosestDistSq = Radius * Radius;
    int32 ClosestIndex = INDEX_NONE;

    for (int32 i = 0; i < RestPositions.Num(); ++i)
    {
        FVector WorldPos = GetVertexWorldPosition(i);
        float DistSq = (WorldPos - WorldPoint).SizeSquared();

        if (DistSq < ClosestDistSq)
        {
            ClosestDistSq = DistSq;
            ClosestIndex = i;
        }
    }

    if (ClosestIndex != INDEX_NONE)
    {
        UpdateSelection(ClosestIndex, Operation);
        UE_LOG(ELogLevel::Verbose, TEXT("ClothAttachmentPaintTool: Selected vertex %d"), ClosestIndex);
    }
}

void FClothAttachmentPaintTool::ClearSelection()
{
    SelectedVertexIndices.Empty();
    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Selection cleared"));
}

void FClothAttachmentPaintTool::AssignBoneToSelection(FName BoneName, float Weight, float Stiffness, float AttachDistance)
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAttachmentPaintTool: No cloth component or asset"));
        return;
    }

    if (SelectedVertexIndices.Num() == 0)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAttachmentPaintTool: No vertices selected"));
        return;
    }

    if (BoneName == NAME_None)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAttachmentPaintTool: No bone name specified"));
        return;
    }

    UClothAsset* ClothAsset = ClothComponent->GetClothAsset();

    // Create or update paint data for selected vertices
    for (uint32 VertexIndex : SelectedVertexIndices)
    {
        // Check if paint data already exists
        FClothAttachmentPaintData* ExistingData = ClothAsset->AttachmentPaintData.FindByPredicate(
            [VertexIndex](const FClothAttachmentPaintData& Data)
            {
                return Data.SimVertexIndex == VertexIndex;
            }
        );

        if (ExistingData)
        {
            // Update existing
            ExistingData->BoneName = BoneName;
            ExistingData->bHasBoneAssignment = true;
            ExistingData->KinematicWeight = Weight;
            ExistingData->Stiffness = Stiffness;
            ExistingData->AttachDistance = AttachDistance;
            ExistingData->bIsActive = true;
        }
        else
        {
            // Create new
            FClothAttachmentPaintData NewData;
            NewData.SimVertexIndex = VertexIndex;
            NewData.BoneName = BoneName;
            NewData.bHasBoneAssignment = true;
            NewData.KinematicWeight = Weight;
            NewData.Stiffness = Stiffness;
            NewData.AttachDistance = AttachDistance;
            NewData.bIsActive = true;
            NewData.DebugLabel = FString::Printf(TEXT("Vertex_%d"), VertexIndex);

            ClothAsset->AttachmentPaintData.Add(NewData);
        }
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Assigned bone '%s' to %d vertices"),
        *BoneName.ToString(), SelectedVertexIndices.Num());
}

void FClothAttachmentPaintTool::ApplyToAsset()
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAttachmentPaintTool: No cloth component or asset"));
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Applied %d attachment entries to asset"),
        ClothComponent->GetClothAsset()->AttachmentPaintData.Num());

    // Clear selection after applying
    ClearSelection();
}

void FClothAttachmentPaintTool::ClearAttachmentData()
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAttachmentPaintTool: No cloth component or asset"));
        return;
    }

    int32 Count = ClothComponent->GetClothAsset()->AttachmentPaintData.Num();
    ClothComponent->GetClothAsset()->AttachmentPaintData.Empty();
    ClearSelection();

    UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintTool: Cleared %d attachment entries"), Count);
}

// ===== Internal Helpers =====

bool FClothAttachmentPaintTool::IsVertexInBox(const FVector& VertexPos, const FVector& BoxMin, const FVector& BoxMax) const
{
    return VertexPos.X >= BoxMin.X && VertexPos.X <= BoxMax.X &&
           VertexPos.Y >= BoxMin.Y && VertexPos.Y <= BoxMax.Y &&
           VertexPos.Z >= BoxMin.Z && VertexPos.Z <= BoxMax.Z;
}

void FClothAttachmentPaintTool::UpdateSelection(uint32 VertexIndex, ESelectionOperation Operation)
{
    switch (Operation)
    {
    case ESelectionOperation::Replace:
        SelectedVertexIndices.Empty();
        SelectedVertexIndices.Add(VertexIndex);
        break;

    case ESelectionOperation::Add:
        SelectedVertexIndices.AddUnique(VertexIndex);
        break;

    case ESelectionOperation::Subtract:
        SelectedVertexIndices.Remove(VertexIndex);
        break;

    case ESelectionOperation::Toggle:
        if (SelectedVertexIndices.Contains(VertexIndex))
            SelectedVertexIndices.Remove(VertexIndex);
        else
            SelectedVertexIndices.Add(VertexIndex);
        break;
    }
}

FVector FClothAttachmentPaintTool::GetVertexWorldPosition(uint32 VertexIndex) const
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
        return FVector::ZeroVector;

    const TArray<FVector>& RestPositions = ClothComponent->GetClothAsset()->RestPositions;
    if (VertexIndex >= (uint32)RestPositions.Num())
        return FVector::ZeroVector;

    // Transform from cloth local space to world space
    FMatrix WorldTransform = ClothComponent->GetWorldTransform();
    return WorldTransform.TransformPosition(RestPositions[VertexIndex]);
}

// ===== Rendering Helpers =====

void FClothAttachmentPaintTool::RenderSelectedVertices(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
        return;

    // Render selected vertices as yellow spheres
    FLinearColor SelectedColor = FLinearColor::Yellow;
    float SphereRadius = 2.0f;

    for (uint32 VertexIndex : SelectedVertexIndices)
    {
        FVector WorldPos = GetVertexWorldPosition(VertexIndex);
        
        // Draw sphere at vertex position
        // PDI->DrawPoint(WorldPos, SelectedColor, SphereRadius, SDPG_World);
        
        // TODO: Use proper sphere rendering when PDI API is available
        // For now, log positions for debugging
    }
}

void FClothAttachmentPaintTool::RenderAttachedVertices(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    if (!ClothComponent || !ClothComponent->GetClothAsset())
        return;

    UClothAsset* ClothAsset = ClothComponent->GetClothAsset();
    
    // Render attached vertices as green spheres
    FLinearColor AttachedColor = FLinearColor::Green;
    float SphereRadius = 1.5f;

    for (const FClothAttachmentPaintData& PaintData : ClothAsset->AttachmentPaintData)
    {
        if (!PaintData.bIsActive || !PaintData.bHasBoneAssignment)
            continue;

        FVector WorldPos = GetVertexWorldPosition(PaintData.SimVertexIndex);
        
        // Draw sphere at vertex position
        // PDI->DrawPoint(WorldPos, AttachedColor, SphereRadius, SDPG_World);
        
        // TODO: Use proper sphere rendering when PDI API is available
    }
}

void FClothAttachmentPaintTool::RenderSelectionBox(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    if (!bIsBoxSelecting)
        return;

    // TODO: Render 2D box in screen space
    // This requires converting BoxSelectStart/BoxSelectEnd to screen coordinates
    // and drawing a rectangle overlay

    UE_LOG(ELogLevel::Verbose, TEXT("ClothAttachmentPaintTool: Rendering selection box"));
}
