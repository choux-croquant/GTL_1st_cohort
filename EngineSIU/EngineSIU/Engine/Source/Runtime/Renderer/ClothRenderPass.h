/**
 * Cloth Production Render Pass
 * Production-quality cloth rendering with GPU skinning and full material support
 * Renders high-resolution render mesh driven by low-resolution simulation mesh
 */

#pragma once

#include "RenderPassBase.h"
#include "EngineBaseTypes.h"
#include "Container/Set.h"
#include "Define.h"
#include "Cloth/ClothGPURenderStructs.h"

#include <d3d11.h>

// Forward declarations
class UClothMeshComponent;
class UMaterial;
class FDXDShaderManager;
class FGraphicsDevice;
class FDXDBufferManager;

/**
 * Production cloth render pass
 * Renders cloth with full materials, textures, and GPU skinning
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
    void UpdateClothInstanceConstantBuffer(const FClothInstanceConstants &Constants);
    void BindMaterial(UMaterial *Material);

private:
    // Cloth components to render
    TArray<UClothMeshComponent *> ClothComponents;

    // Production shaders
    ID3D11VertexShader *ProductionVertexShader;
    ID3D11PixelShader *ProductionPixelShader;
    ID3D11InputLayout *ProductionInputLayout;

    // Constant buffer for per-instance cloth data
    ID3D11Buffer *ClothInstanceConstantBuffer;

    // Rasterizer state (two-sided rendering for cloth)
    ID3D11RasterizerState *ClothRasterizerState;

    // Dummy vertex buffer (for vertex ID generation)
    FVertexInfo DummyVertexInfo;
    
    // Material binding cache (for optimization)
    UMaterial *LastBoundMaterial;
};
