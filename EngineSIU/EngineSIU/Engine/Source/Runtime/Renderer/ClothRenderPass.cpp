/**
 * Cloth Render Pass Implementation
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

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

void FClothRenderPass::Initialize(FDXDBufferManager *InBufferManager, FGraphicsDevice *InGraphics, FDXDShaderManager *InShaderManager)
{
    FRenderPassBase::Initialize(InBufferManager, InGraphics, InShaderManager);
}

void FClothRenderPass::PrepareRenderArr()
{
    ClothComponents.Empty();

    // Collect all cloth mesh components in the active world
    for (const auto Iter : TObjectRange<UClothMeshComponent>())
    {
        if (Iter->GetWorld() == GEngine->ActiveWorld && Iter->IsVisible())
        {
            ClothComponents.Add(Iter);
        }
    }
}

void FClothRenderPass::ClearRenderArr()
{
    ClothComponents.Empty();
}

void FClothRenderPass::Render(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    //if (ClothComponents.Num() == 0 || !ClothVertexShader || !ClothPixelShader) return;
    if (ClothComponents.Num() == 0) return;

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

    ClothVertexShader = nullptr;
    ClothPixelShader = nullptr;

    SAFE_RELEASE(ClothInputLayout);
    SAFE_RELEASE(ClothMeshConstantBuffer);
    SAFE_RELEASE(ClothRasterizerState);
}

void FClothRenderPass::PrepareRender(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    const EResourceType ResourceType = EResourceType::ERT_Scene;
    FViewportResource* ViewportResource = Viewport->GetViewportResource();
    FRenderTargetRHI* RenderTargetRHI = ViewportResource->GetRenderTarget(ResourceType);
    FDepthStencilRHI* DepthStencilRHI = ViewportResource->GetDepthStencil(ResourceType);

    Graphics->DeviceContext->OMSetRenderTargets(1, &RenderTargetRHI->RTV, DepthStencilRHI->DSV);
    Graphics->DeviceContext->OMSetDepthStencilState(Graphics->DepthStencilState_Default, 0);

    // Set shaders
    Graphics->DeviceContext->VSSetShader(ClothVertexShader, nullptr, 0);
    Graphics->DeviceContext->PSSetShader(ClothPixelShader, nullptr, 0);

    // Set input layout
    Graphics->DeviceContext->IASetInputLayout(ClothInputLayout);

    // Set primitive topology
    Graphics->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT stride = sizeof(float) * 2; // float2 UV
    UINT offset = 0;
    Graphics->DeviceContext->IASetVertexBuffers(0, 1, &ClothVertexInfo.VertexBuffer, &stride, &offset);

    // Set rasterizer state (two-sided rendering)
    if (ClothRasterizerState)
    {
        Graphics->DeviceContext->RSSetState(ClothRasterizerState);
    }
}

void FClothRenderPass::CleanUpRender(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    // Unbind cloth-specific resources
    ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->VSSetShaderResources(9, 2, nullSRVs);

    // Reset rasterizer state
    Graphics->DeviceContext->RSSetState(nullptr);
}

void FClothRenderPass::CreateResource()
{
    const int32 MaxClothVerts = 65536;
    TArray<FVector2D> DummyUVs;
    DummyUVs.SetNum(MaxClothVerts);
    for (int32 i = 0; i < MaxClothVerts; ++i)
    {
        DummyUVs[i] = FVector2D(0.0f, 0.0f);
    }

    // BufferManager는 FRenderPassBase::Initialize에서 이미 세팅되어 있음
    BufferManager->CreateVertexBuffer(TEXT("ClothDummyVB"), DummyUVs, ClothVertexInfo);

    // Create input layout for cloth (vertex ID + UV)
    D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    // Load cloth vertex shader with input layout
    HRESULT hr = ShaderManager->AddVertexShaderAndInputLayout(L"ClothVertexShader", L"Shaders/ClothVertexShader.hlsl", "main", layout, ARRAYSIZE(layout));
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile Cloth Vertex Shader"));
        return;
    }

    // Load cloth pixel shader
    hr = ShaderManager->AddPixelShader(L"ClothPixelShader", L"Shaders/ClothPixelShader.hlsl", "mainPS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to compile Cloth Pixel Shader"));
        return;
    }

    ClothVertexShader = ShaderManager->GetVertexShaderByKey(L"ClothVertexShader");
    ClothInputLayout = ShaderManager->GetInputLayoutByKey(L"ClothVertexShader");
    ClothPixelShader = ShaderManager->GetPixelShaderByKey(L"ClothPixelShader");

    // Create constant buffer for cloth mesh
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = (sizeof(FClothMeshConstants) + 0xf) & 0xfffffff0;

    hr = Graphics->Device->CreateBuffer(&cbDesc, nullptr, &ClothMeshConstantBuffer);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create Cloth Mesh Constant Buffer"));
    }

    // Create rasterizer state for two-sided rendering
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE; // Two-sided rendering
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;

    hr = Graphics->Device->CreateRasterizerState(&rastDesc, &ClothRasterizerState);
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create Cloth Rasterizer State"));
    }
}

void FClothRenderPass::RenderClothComponent(UClothMeshComponent *ClothComponent, const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    if (!ClothComponent) return;

    // Get render data from component
    FClothRenderData renderData;
    ClothComponent->GetRenderData(renderData);

    // if (!renderData.PositionBufferSRV || !renderData.NormalBufferSRV || !renderData.Indices) return;
    if (!renderData.PositionBufferSRV || !renderData.NormalBufferSRV) return;
    // Bind simulation buffers as SRVs
    ID3D11ShaderResourceView *clothSRVs[] = {
        renderData.PositionBufferSRV,
        renderData.NormalBufferSRV};
    Graphics->DeviceContext->VSSetShaderResources(9, 2, clothSRVs);

    // Update cloth mesh constant buffer
    UpdateClothMeshConstantBuffer(renderData.WorldTransform, renderData.NumVertices);
    Graphics->DeviceContext->VSSetConstantBuffers(10, 1, &ClothMeshConstantBuffer);

    // Set material (if available)
    // TODO: Bind material textures and constants

    // Draw cloth mesh
    // Note: Using DrawInstanced with vertex ID to fetch from structured buffer
    //Graphics->DeviceContext->DrawInstanced(renderData.NumTriangles * 3, 1, 0, 0);
    Graphics->DeviceContext->DrawInstanced(renderData.NumVertices, 1, 0, 0);
}

void FClothRenderPass::UpdateClothMeshConstantBuffer(const FMatrix &WorldTransform, uint32 NumVertices)
{
    if (!ClothMeshConstantBuffer)
        return;

    FClothMeshConstants constants;
    constants.ClothWorldMatrix = WorldTransform;
    constants.ClothNumVertices = NumVertices;
    constants.ClothPadding0 = 0;
    constants.ClothPadding1 = 0;
    constants.ClothPadding2 = 0;

    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(ClothMeshConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &constants, sizeof(FClothMeshConstants));
        Graphics->DeviceContext->Unmap(ClothMeshConstantBuffer, 0);
    }
}
