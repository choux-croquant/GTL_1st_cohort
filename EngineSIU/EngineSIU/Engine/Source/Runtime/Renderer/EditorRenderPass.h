#pragma once
#define _TCHAR_DEFINED
#include <d3d11.h>

#include "RenderPassBase.h"
#include "RenderResources.h"
#include "D3D11RHI/DXDBufferManager.h"
#include "PhysicsEngine/PhysicsAsset.h"

class FDXDBufferManager;
class UWorld;
class FDXDShaderManager;

class FEditorRenderPass : public FRenderPassBase
{
public:
    void Initialize(FDXDBufferManager* InBufferManager, FGraphicsDevice* InGraphics, FDXDShaderManager* InShaderManager) override;
    void Render(const std::shared_ptr<FEditorViewportClient>& Viewport) override;
    void PrepareRenderArr() override;
    void ClearRenderArr() override;

private:
    virtual void PrepareRender(const std::shared_ptr<FEditorViewportClient>& Viewport) override;
    virtual void CleanUpRender(const std::shared_ptr<FEditorViewportClient>& Viewport) override;
    
    FRenderResourcesDebug Resources = FRenderResourcesDebug();

    // for Physics Asset Viewer
    TArray<USkeletalMeshComponent*> PreviewMesh;
    
private:
    void CreateShaders();
    void CreateBuffers();
    void CreateConstantBuffers();
    
    void LazyLoad();

    void BindRenderTarget(const std::shared_ptr<FEditorViewportClient>& Viewport) const;
    void BindShaderResource(const std::wstring& VertexKey, const std::wstring& PixelKey, D3D_PRIMITIVE_TOPOLOGY Topology) const;
    void BindBuffers(const FDebugPrimitiveData& InPrimitiveData) const;
    
    void RenderPointlightInstanced(uint64 ShowFlag);
    void RenderSpotlightInstanced(uint64 ShowFlag);
    void RenderArrowInstanced();
    void RenderBoxInstanced(uint64 ShowFlag);
    void RenderSphereInstanced(uint64 ShowFlag);
    void RenderCapsuleInstanced(uint64 ShowFlag);
    void RenderClothColliders(uint64 ShowFlag);

    // Grid
    // void RenderGrid(std::shared_ptr<FEditorViewportClient> ActiveViewport);

    // Icon
    void RenderIcons(const UWorld* World, std::shared_ptr<FEditorViewportClient> ActiveViewport);   // 사용 X
    void UpdateTextureIcon(EIconType Type);

    static constexpr UINT32 ConstantBufferSizeBox = 100;
    static constexpr UINT32 ConstantBufferSizeSphere = 100;
    static constexpr UINT32 ConstantBufferSizeCone = 100;
    static constexpr UINT32 ConstantBufferSizeArrow = 100;
    static constexpr UINT32 ConstantBufferSizeCapsule = 100;
};
