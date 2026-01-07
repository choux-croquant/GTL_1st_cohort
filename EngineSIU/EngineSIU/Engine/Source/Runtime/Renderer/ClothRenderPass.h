/**
 * Cloth Render Pass
 * Renders simulated cloth meshes using dynamic GPU buffers
 */

#pragma once

#include "RenderPassBase.h"
#include "EngineBaseTypes.h"
#include "Container/Set.h"
#include "Define.h"

#include <d3d11.h>

// Forward declarations
class UClothMeshComponent;
class FDXDShaderManager;
class FGraphicsDevice;
class FDXDBufferManager;

/**
 * Constant buffer for cloth mesh rendering
 */
struct FClothMeshConstants
{
    alignas(16) FMatrix ClothWorldMatrix;
    uint32 ClothNumVertices;
    uint32 ClothPadding0;
    uint32 ClothPadding1;
    uint32 ClothPadding2;
};

/**
 * Render pass for cloth simulation visualization
 */
class FClothRenderPass : public FRenderPassBase
{
public:
    FClothRenderPass() = default;
    virtual ~FClothRenderPass() override = default;

    virtual void Initialize(FDXDBufferManager *InBufferManager,
                            FGraphicsDevice *InGraphics,
                            FDXDShaderManager *InShaderManager) override;

    virtual void PrepareRenderArr() override;
    virtual void ClearRenderArr() override;
    virtual void Render(const std::shared_ptr<FEditorViewportClient> &Viewport) override;
    virtual void Release() override;

protected:
    virtual void PrepareRender(const std::shared_ptr<FEditorViewportClient> &Viewport) override;
    virtual void CleanUpRender(const std::shared_ptr<FEditorViewportClient> &Viewport) override;
    virtual void CreateResource() override;

private:
    void RenderClothComponent(UClothMeshComponent *ClothComponent, const std::shared_ptr<FEditorViewportClient> &Viewport);
    void UpdateClothMeshConstantBuffer(const FMatrix &WorldTransform, uint32 NumVertices);

private:
    // Cloth components to render
    TArray<UClothMeshComponent *> ClothComponents;

    // Shaders
    ID3D11VertexShader *ClothVertexShader;
    ID3D11PixelShader *ClothPixelShader;
    ID3D11InputLayout *ClothInputLayout;

    // Constant buffer
    ID3D11Buffer *ClothMeshConstantBuffer;

    // Rasterizer state (two-sided rendering for cloth)
    ID3D11RasterizerState *ClothRasterizerState;
};
