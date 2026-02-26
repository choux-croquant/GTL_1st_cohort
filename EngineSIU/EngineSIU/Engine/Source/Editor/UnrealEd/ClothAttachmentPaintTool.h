/**
 * Cloth Attachment Paint Tool
 * Editor tool for painting cloth-to-bone attachments in viewport
 * Phase 3: Viewport Interaction (Box Selection + Visual Feedback)
 */

#pragma once

#include "Define.h"
#include "Math/Vector.h"
#include "Container/Array.h"
#include "CoreUObject/UObject/NameTypes.h"

// Forward declarations
class UClothMeshComponent;
class USkeletalMeshComponent;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class FSceneView;
struct FViewportClick;

/**
 * Paint tool mode
 */
enum class EClothAttachmentPaintMode : uint8
{
    None,           // No active tool
    BoxSelect,      // Box selection mode
    BrushPaint,     // Brush painting mode (Phase 4)
    VertexPick      // Individual vertex picking
};

/**
 * Selection operation type
 */
enum class ESelectionOperation : uint8
{
    Replace,        // Replace current selection
    Add,            // Add to current selection
    Subtract,       // Remove from current selection
    Toggle          // Toggle selection state
};

/**
 * Cloth Attachment Paint Tool
 * Manages vertex selection, bone assignment, and visualization
 */
class FClothAttachmentPaintTool
{
public:
    FClothAttachmentPaintTool();
    ~FClothAttachmentPaintTool();

    // Tool lifecycle
    void Activate(UClothMeshComponent* InClothComponent);
    void Deactivate();
    bool IsActive() const { return bIsActive; }

    // Input handling
    bool HandleClick(const FViewportClick& Click, FEditorViewportClient* ViewportClient);
    bool HandleMouseMove(const FVector2D& MousePosition, FEditorViewportClient* ViewportClient);
    bool HandleMouseDown(const FVector2D& MousePosition, FEditorViewportClient* ViewportClient);
    bool HandleMouseUp(const FVector2D& MousePosition, FEditorViewportClient* ViewportClient);

    // Rendering
    void Render(const FSceneView* View, FPrimitiveDrawInterface* PDI);

    // Selection operations
    void SelectVerticesByBox(const FVector& BoxMin, const FVector& BoxMax, ESelectionOperation Operation = ESelectionOperation::Replace);
    void SelectVerticesByZThreshold(float Threshold, ESelectionOperation Operation = ESelectionOperation::Replace);
    void SelectVertexAtPoint(const FVector& WorldPoint, float Radius, ESelectionOperation Operation = ESelectionOperation::Toggle);
    void ClearSelection();
    
    // Bone assignment
    void AssignBoneToSelection(FName BoneName, float Weight = 1.0f, float Stiffness = 1.0f, float AttachDistance = 0.0f);
    
    // Data management
    void ApplyToAsset();
    void ClearAttachmentData();

    // Accessors
    const TArray<uint32>& GetSelectedVertices() const { return SelectedVertexIndices; }
    int32 GetSelectionCount() const { return SelectedVertexIndices.Num(); }
    UClothMeshComponent* GetClothComponent() const { return ClothComponent; }

    // Mode control
    void SetPaintMode(EClothAttachmentPaintMode Mode) { PaintMode = Mode; }
    EClothAttachmentPaintMode GetPaintMode() const { return PaintMode; }

private:
    // Internal helpers
    bool IsVertexInBox(const FVector& VertexPos, const FVector& BoxMin, const FVector& BoxMax) const;
    void UpdateSelection(uint32 VertexIndex, ESelectionOperation Operation);
    FVector GetVertexWorldPosition(uint32 VertexIndex) const;

    // Rendering helpers
    void RenderSelectedVertices(const FSceneView* View, FPrimitiveDrawInterface* PDI);
    void RenderAttachedVertices(const FSceneView* View, FPrimitiveDrawInterface* PDI);
    void RenderSelectionBox(const FSceneView* View, FPrimitiveDrawInterface* PDI);

private:
    // Target component
    UClothMeshComponent* ClothComponent;

    // Selection state
    TArray<uint32> SelectedVertexIndices;
    
    // Tool state
    bool bIsActive;
    EClothAttachmentPaintMode PaintMode;

    // Box selection state
    bool bIsBoxSelecting;
    FVector2D BoxSelectStart;
    FVector2D BoxSelectEnd;
    FVector BoxWorldMin;
    FVector BoxWorldMax;

    // Brush state (Phase 4)
    float BrushRadius;
    float BrushStrength;
    FVector BrushWorldPosition;
};
