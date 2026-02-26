/**
 * Cloth Attachment Paint Renderer Implementation
 * Editor-only renderer for visualizing cloth attachment paint data
 * Phase 3: Viewport Interaction - Visual Feedback
 */

#include "ClothAttachmentPaintRenderer.h"
#include "PrimitiveDrawBatch.h"
#include "Components/ClothMeshComponent.h"
#include "Classes/Engine/ClothAsset.h"
#include "D3D11RHI/GraphicDevice.h"
#include "Math/Matrix.h"

FClothAttachmentPaintRenderer::FClothAttachmentPaintRenderer()
    : Graphics(nullptr)
    , PrimitiveDrawBatch(nullptr)
    , bIsInitialized(false)
{
}

FClothAttachmentPaintRenderer::~FClothAttachmentPaintRenderer()
{
    Release();
}

void FClothAttachmentPaintRenderer::Initialize(FGraphicsDevice* InGraphics, UPrimitiveDrawBatch* InPrimitiveDrawBatch)
{
    Graphics = InGraphics;
    PrimitiveDrawBatch = InPrimitiveDrawBatch;
    bIsInitialized = (Graphics != nullptr && PrimitiveDrawBatch != nullptr);

    if (bIsInitialized)
    {
        UE_LOG(ELogLevel::Display, TEXT("ClothAttachmentPaintRenderer: Initialized"));
    }
}

void FClothAttachmentPaintRenderer::Release()
{
    Graphics = nullptr;
    PrimitiveDrawBatch = nullptr;
    bIsInitialized = false;
}

void FClothAttachmentPaintRenderer::RenderAttachmentVisualization(
    UClothMeshComponent* ClothComponent,
    const TArray<uint32>& SelectedVertices
)
{
    if (!bIsInitialized || !ClothComponent || !ClothComponent->GetClothAsset())
        return;

    // Render attached vertices (green)
    RenderAttachedVertices(ClothComponent, FLinearColor::Green, 1.5f);

    // Render selected vertices (yellow)
    if (SelectedVertices.Num() > 0)
    {
        TArray<FVector> SelectedPositions;
        for (uint32 VertexIndex : SelectedVertices)
        {
            FVector WorldPos = GetVertexWorldPosition(ClothComponent, VertexIndex);
            SelectedPositions.Add(WorldPos);
        }
        RenderSelectedVertices(SelectedPositions, FLinearColor::Yellow, 2.0f);
    }
}

void FClothAttachmentPaintRenderer::RenderSelectedVertices(
    const TArray<FVector>& Positions,
    const FLinearColor& Color,
    float Radius
)
{
    if (!bIsInitialized || Positions.Num() == 0)
        return;

    // Use primitive draw batch to render spheres
    for (const FVector& Position : Positions)
    {
        AddSphereToDrawBatch(Position, Radius, Color);
    }

    UE_LOG(ELogLevel::Verbose, TEXT("ClothAttachmentPaintRenderer: Rendered %d selected vertices"),
        Positions.Num());
}

void FClothAttachmentPaintRenderer::RenderAttachedVertices(
    UClothMeshComponent* ClothComponent,
    const FLinearColor& Color,
    float Radius
)
{
    if (!bIsInitialized || !ClothComponent || !ClothComponent->GetClothAsset())
        return;

    UClothAsset* ClothAsset = ClothComponent->GetClothAsset();

    // Render each attached vertex
    for (const FClothAttachmentPaintData& PaintData : ClothAsset->AttachmentPaintData)
    {
        if (!PaintData.bIsActive || !PaintData.bHasBoneAssignment)
            continue;

        FVector WorldPos = GetVertexWorldPosition(ClothComponent, PaintData.SimVertexIndex);
        AddSphereToDrawBatch(WorldPos, Radius, Color);
    }

    UE_LOG(ELogLevel::Verbose, TEXT("ClothAttachmentPaintRenderer: Rendered %d attached vertices"),
        ClothAsset->AttachmentPaintData.Num());
}

void FClothAttachmentPaintRenderer::RenderSelectionBox(
    const FVector2D& ScreenMin,
    const FVector2D& ScreenMax,
    const FLinearColor& Color
)
{
    if (!bIsInitialized)
        return;

    // TODO: Implement 2D screen-space box rendering
    // This requires integration with 2D canvas or overlay rendering system
    
    UE_LOG(ELogLevel::Verbose, TEXT("ClothAttachmentPaintRenderer: Rendering selection box from (%.1f, %.1f) to (%.1f, %.1f)"),
        ScreenMin.X, ScreenMin.Y, ScreenMax.X, ScreenMax.Y);
}

// ===== Helper Functions =====

FVector FClothAttachmentPaintRenderer::GetVertexWorldPosition(UClothMeshComponent* ClothComp, uint32 VertexIndex) const
{
    if (!ClothComp || !ClothComp->GetClothAsset())
        return FVector::ZeroVector;

    const TArray<FVector>& RestPositions = ClothComp->GetClothAsset()->RestPositions;
    if (VertexIndex >= (uint32)RestPositions.Num())
        return FVector::ZeroVector;

    // Transform from cloth local space to world space
    FMatrix WorldTransform = ClothComp->GetWorldTransform();
    return WorldTransform.TransformPosition(RestPositions[VertexIndex]);
}

void FClothAttachmentPaintRenderer::AddSphereToDrawBatch(const FVector& Center, float Radius, const FLinearColor& Color)
{
    if (!PrimitiveDrawBatch)
        return;

    // Create bounding box for sphere visualization
    FBoundingBox SphereBox;
    SphereBox.Min = Center - FVector(Radius, Radius, Radius);
    SphereBox.Max = Center + FVector(Radius, Radius, Radius);

    // Add to primitive draw batch
    FMatrix IdentityMatrix = FMatrix::Identity;
    PrimitiveDrawBatch->AddAABBToBatch(SphereBox, Center, IdentityMatrix);
}
