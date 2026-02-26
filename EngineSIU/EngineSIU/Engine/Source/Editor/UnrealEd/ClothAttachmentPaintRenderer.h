/**
 * Cloth Attachment Paint Renderer
 * Editor-only renderer for visualizing cloth attachment paint data
 * Phase 3: Viewport Interaction - Visual Feedback
 */

#pragma once

#include "Define.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Container/Array.h"

// Forward declarations
class UClothMeshComponent;
class FGraphicsDevice;
class UPrimitiveDrawBatch;
struct FClothAttachmentPaintData;

/**
 * Cloth Attachment Paint Renderer
 * Renders debug visualization for attachment painting
 */
class FClothAttachmentPaintRenderer
{
public:
    FClothAttachmentPaintRenderer();
    ~FClothAttachmentPaintRenderer();

    // Initialization
    void Initialize(FGraphicsDevice* InGraphics, UPrimitiveDrawBatch* InPrimitiveDrawBatch);
    void Release();

    // Rendering
    void RenderAttachmentVisualization(
        UClothMeshComponent* ClothComponent,
        const TArray<uint32>& SelectedVertices
    );

    // Render individual vertex types
    void RenderSelectedVertices(
        const TArray<FVector>& Positions,
        const FLinearColor& Color = FLinearColor::Yellow,
        float Radius = 2.0f
    );

    void RenderAttachedVertices(
        UClothMeshComponent* ClothComponent,
        const FLinearColor& Color = FLinearColor::Green,
        float Radius = 1.5f
    );

    // Render selection box (2D overlay)
    void RenderSelectionBox(
        const FVector2D& ScreenMin,
        const FVector2D& ScreenMax,
        const FLinearColor& Color = FLinearColor::Yellow
    );

private:
    // Helper functions
    FVector GetVertexWorldPosition(UClothMeshComponent* ClothComp, uint32 VertexIndex) const;
    void AddSphereToDrawBatch(const FVector& Center, float Radius, const FLinearColor& Color);

private:
    FGraphicsDevice* Graphics;
    UPrimitiveDrawBatch* PrimitiveDrawBatch;
    bool bIsInitialized;
};
