/**
 * Cloth Production Render Pass Implementation
 * Production-quality cloth rendering with GPU skinning and materials
 */

#include "ClothRenderPass.h"

#include "RendererHelpers.h"
#include "D3D11RHI/DXDBufferManager.h"
#include "D3D11RHI/GraphicDevice.h"
#include "D3D11RHI/DXDShaderManager.h"
#include "UnrealClient.h"
#include "UnrealEd/EditorViewportClient.h"
#include "Engine/EditorEngine.h"
#include "Components/ClothMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Cloth/ClothBatchManager.h"
#include "Cloth/ClothBatchedSolver.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Components/Material/Material.h"
#include "Classes/Engine/ClothAsset.h"

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

void FClothRenderPass::Initialize(FDXDBufferManager *InBufferManager, FGraphicsDevice *InGraphics, FDXDShaderManager *InShaderManager)
{
    FRenderPassBase::Initialize(InBufferManager, InGraphics, InShaderManager);
    
    /*ProductionVertexShader = nullptr;
    ProductionPixelShader = nullptr;
    ProductionInputLayout = nullptr;
    ClothInstanceConstantBuffer = nullptr;
    ClothRasterizerState = nullptr;*/
    LastBoundMaterial = nullptr;
}

void FClothRenderPass::PrepareRenderArr()
{
    ClothComponents.Empty();

    // Collect all cloth mesh components in the active world that use production rendering
    for (const auto Iter : TObjectRange<UClothMeshComponent>())
    {
        if (Iter->GetWorld() == GEngine->ActiveWorld && Iter->IsVisible())
        {
            // Only add components that have render mesh data (production rendering)
            if (Iter->GetClothAsset() && Iter->GetClothAsset()->bUseRenderMesh)
            {
                ClothComponents.Add(Iter);
            }
        }
    }
    
    // Phase 6 Optimization: Sort by material to reduce state changes
    //ClothComponents.Sort([](const UClothMeshComponent& A, const UClothMeshComponent& B)
    //{
    //    UMaterial* MatA = A.GetMaterial(0);
    //    UMaterial* MatB = B.GetMaterial(0);
    //    
    //    // Sort by material pointer (groups same materials together)
    //    return MatA < MatB;
    //});
}

void FClothRenderPass::ClearRenderArr()
{
    ClothComponents.Empty();
}

void FClothRenderPass::Render(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    if (ClothComponents.Num() == 0)
        return;

    /*if (!ProductionVertexShader || !ProductionPixelShader)
        return;*/

    PrepareRender(Viewport);

    // Render each cloth component
    for (UClothMeshComponent *ClothComp : ClothComponents)
    {
        if (ClothComp && ClothComp->IsSimulating())
        {
            RenderClothComponent(ClothComp, Viewport);
        }
    }

    CleanUpRender(Viewport);
}

void FClothRenderPass::Release()
{
    FRenderPassBase::Release();

    ProductionVertexShader = nullptr;
    ProductionPixelShader = nullptr;

    SAFE_RELEASE(ProductionInputLayout);
    SAFE_RELEASE(ClothInstanceConstantBuffer);
    SAFE_RELEASE(ClothRasterizerState);
}

void FClothRenderPass::PrepareRender(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    const EResourceType ResourceType = EResourceType::ERT_Scene;
    FViewportResource *ViewportResource = Viewport->GetViewportResource();
    FRenderTargetRHI *RenderTargetRHI = ViewportResource->GetRenderTarget(ResourceType);
    FDepthStencilRHI *DepthStencilRHI = ViewportResource->GetDepthStencil(ResourceType);

    Graphics->DeviceContext->OMSetRenderTargets(1, &RenderTargetRHI->RTV, DepthStencilRHI->DSV);
    Graphics->DeviceContext->OMSetDepthStencilState(Graphics->DepthStencilState_Default, 0);

    // CRITICAL FIX: Unbind all compute shader UAVs to prevent resource hazards
    // Cloth simulation uses position/normal buffers as UAVs in compute shaders
    // but needs them as SRVs in vertex shader for rendering
    ID3D11UnorderedAccessView *nullUAVs[8] = {nullptr};
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);

    // Set production shaders
    Graphics->DeviceContext->VSSetShader(ProductionVertexShader, nullptr, 0);
    Graphics->DeviceContext->PSSetShader(ProductionPixelShader, nullptr, 0);

    // Set input layout
    Graphics->DeviceContext->IASetInputLayout(ProductionInputLayout);

    // Set primitive topology
    Graphics->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    TArray<FString> PSBufferKeys = {
        TEXT("FLightInfoBuffer"),
        TEXT("FMaterialConstants"),
        TEXT("FLitUnlitConstants"),
        TEXT("FSubMeshConstants"),
        TEXT("FTextureConstants"),
        TEXT("FIsShadowConstants"),
    };

    BufferManager->BindConstantBuffers(PSBufferKeys, 0, EShaderStage::Pixel);

    BufferManager->BindConstantBuffer(TEXT("FLightInfoBuffer"), 0, EShaderStage::Vertex);
    BufferManager->BindConstantBuffer(TEXT("FMaterialConstants"), 1, EShaderStage::Vertex);
    BufferManager->BindConstantBuffer(TEXT("FObjectConstantBuffer"), 12, EShaderStage::Vertex);

    // NOTE: Vertex buffer is bound per-component in RenderClothComponent()
    // Each cloth instance uses the unified render vertex buffer from its solver

    // Bind common constant buffers (Camera and Object buffers)
    ID3D11Buffer *CameraConstantBuffer = BufferManager->GetConstantBuffer(TEXT("FCameraConstantBuffer"));
    ID3D11Buffer *ObjectConstantBuffer = BufferManager->GetConstantBuffer(TEXT("FObjectConstantBuffer"));
    ID3D11Buffer *LightConstantBuffer = BufferManager->GetConstantBuffer(TEXT("FLightInfoBuffer"));

    if (CameraConstantBuffer)
    {
        Graphics->DeviceContext->VSSetConstantBuffers(13, 1, &CameraConstantBuffer);
        Graphics->DeviceContext->PSSetConstantBuffers(13, 1, &CameraConstantBuffer);
    }

    if (ObjectConstantBuffer)
    {
        Graphics->DeviceContext->VSSetConstantBuffers(12, 1, &ObjectConstantBuffer);
        Graphics->DeviceContext->PSSetConstantBuffers(12, 1, &ObjectConstantBuffer);
    }
    
    if (LightConstantBuffer)
    {
        Graphics->DeviceContext->PSSetConstantBuffers(0, 1, &LightConstantBuffer);
    }

    // Set rasterizer state (two-sided solid rendering for cloth)
    if (ClothRasterizerState)
    {
        Graphics->DeviceContext->RSSetState(ClothRasterizerState);
    }
    
    // Reset material cache
    LastBoundMaterial = nullptr;
}

void FClothRenderPass::CleanUpRender(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    // Unbind cloth simulation buffers (t14-t16)
    ID3D11ShaderResourceView *nullSimSRVs[3] = {nullptr};
    Graphics->DeviceContext->VSSetShaderResources(14, 3, nullSimSRVs);
    
    // Unbind material textures (t0-t8)
    ID3D11ShaderResourceView *nullMatSRVs[9] = {nullptr};
    Graphics->DeviceContext->PSSetShaderResources(0, 9, nullMatSRVs);
}

void FClothRenderPass::CreateResource()
{
    // NOTE: Vertex buffer is now the unified render vertex buffer from ClothBatchedSolver
    // No dummy buffer needed - actual render mesh data is uploaded to solver's unified buffer
    // The unified buffer will be bound per-component in RenderClothComponent()

    // Create input layout for production cloth rendering
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    // Load production vertex shader with input layout
    HRESULT hr = ShaderManager->AddVertexShaderAndInputLayout(
        L"ClothProductionVertexShader",
        L"Shaders/Cloth/ClothProductionVertexShader.hlsl",
        "main",
        layout,
        ARRAYSIZE(layout));
    
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile Cloth Production Vertex Shader"));
        return;
    }

    // Load production pixel shader
    hr = ShaderManager->AddPixelShader(
        L"ClothProductionPixelShader",
        L"Shaders/Cloth/ClothProductionPixelShader.hlsl",
        "main");
    
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile Cloth Production Pixel Shader"));
        return;
    }

    ProductionVertexShader = ShaderManager->GetVertexShaderByKey(L"ClothProductionVertexShader");
    ProductionInputLayout = ShaderManager->GetInputLayoutByKey(L"ClothProductionVertexShader");
    ProductionPixelShader = ShaderManager->GetPixelShaderByKey(L"ClothProductionPixelShader");

    // Create constant buffer for per-instance cloth data
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = (sizeof(FClothInstanceConstants) + 0xf) & 0xfffffff0; // 16-byte aligned

    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &ClothInstanceConstantBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create Cloth Instance Constant Buffer"));
    }

    // Create rasterizer state for two-sided solid rendering
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;  // Solid fill for production
    rastDesc.CullMode = D3D11_CULL_NONE;   // Two-sided rendering
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;

    hr = Graphics->Device->CreateRasterizerState(&rastDesc, &ClothRasterizerState);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create Cloth Production Rasterizer State"));
    }
}

void FClothRenderPass::RenderClothComponent(UClothMeshComponent *ClothComponent, const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    if (!ClothComponent)
        return;

    // Get render data from component
    FClothRenderData renderData;
    ClothComponent->GetRenderData(renderData);

    // Validate production rendering data
    if (!renderData.bUseProductionRendering)
        return; // Fall back to debug rendering

    if (!renderData.UnifiedRenderVertexBuffer || !renderData.UnifiedRenderIndexBuffer || !renderData.SkinningWeightBufferSRV)
        return;

    if (renderData.RenderIndexCount == 0)
        return;

    // CRITICAL FIX: Bind actual unified render vertex buffer (contains rest positions, normals, UVs)
    UINT stride = sizeof(FClothRenderVertex);  // 32 bytes: position(12) + normal(12) + UV(8)
    UINT offset = 0;  // Always 0 - vertex offset handled via baseVertexLocation in DrawIndexed
    Graphics->DeviceContext->IASetVertexBuffers(0, 1, &renderData.UnifiedRenderVertexBuffer, &stride, &offset);

    // CRITICAL FIX: Bind simulation buffers to slots that don't conflict with light buffers
    // Light buffers use t10-t13, so use t14-t16 for cloth simulation data
    ID3D11ShaderResourceView *simSRVs[] = {
        renderData.PositionBufferSRV,       // t14: Simulation positions
        renderData.NormalBufferSRV,         // t15: Simulation normals
        renderData.SkinningWeightBufferSRV  // t16: Skinning weights
    };
    Graphics->DeviceContext->VSSetShaderResources(14, 3, simSRVs);

    // Update per-instance constant buffer
    FClothInstanceConstants constants;
    constants.SetWorldMatrix(renderData.WorldTransform);
    constants.ClothRenderVertexOffset = renderData.RenderVertexOffset;
    constants.ClothRenderIndexOffset = renderData.RenderIndexOffset;
    constants.ClothSimParticleOffset = renderData.ParticleOffset;
    constants.ClothNumRenderVertices = renderData.RenderVertexCount;
    constants.ClothNumSimParticles = renderData.NumVertices;

    UpdateClothInstanceConstantBuffer(constants);
    Graphics->DeviceContext->VSSetConstantBuffers(10, 1, &ClothInstanceConstantBuffer);

    // Bind material
    UMaterial *Material = ClothComponent->GetMaterial(0);
    if (Material)
    {
        BindMaterial(Material);
    }

    // Bind render index buffer
    Graphics->DeviceContext->IASetIndexBuffer(renderData.UnifiedRenderIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

    // Draw the render mesh
    // CRITICAL FIX: Use baseVertexLocation to offset into unified vertex buffer
    uint32 indexCount = renderData.RenderIndexCount;
    uint32 startIndexLocation = renderData.RenderIndexOffset;
    int32 baseVertexLocation = renderData.RenderVertexOffset;  // Vertex offset for this instance

    Graphics->DeviceContext->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
}

void FClothRenderPass::UpdateClothInstanceConstantBuffer(const FClothInstanceConstants &Constants)
{
    if (!ClothInstanceConstantBuffer)
        return;

    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(ClothInstanceConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &Constants, sizeof(FClothInstanceConstants));
        Graphics->DeviceContext->Unmap(ClothInstanceConstantBuffer, 0);
    }
}

void FClothRenderPass::BindMaterial(UMaterial *Material)
{
    if (!Material)
        return;

    // Optimization: Skip rebinding if same material
    if (Material == LastBoundMaterial)
        return;

    LastBoundMaterial = Material;

    // Use MaterialUtils to update material (matches engine pattern)
    FMaterialInfo materialInfo = Material->GetMaterialInfo();
    MaterialUtils::UpdateMaterial(BufferManager, Graphics, materialInfo);
}
