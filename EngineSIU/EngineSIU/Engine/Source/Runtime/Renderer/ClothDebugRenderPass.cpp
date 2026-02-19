/**
 * Cloth Render Pass Implementation
 */

#include "ClothDebugRenderPass.h"

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

#define SAFE_RELEASE(p) \
    if (p)              \
    {                   \
        (p)->Release(); \
        (p) = nullptr;  \
    }

void FClothDebugRenderPass::Initialize(FDXDBufferManager *InBufferManager, FGraphicsDevice *InGraphics, FDXDShaderManager *InShaderManager)
{
    FRenderPassBase::Initialize(InBufferManager, InGraphics, InShaderManager);
    TempIndexBuffer = nullptr;
}

void FClothDebugRenderPass::PrepareRenderArr()
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

void FClothDebugRenderPass::ClearRenderArr()
{
    ClothComponents.Empty();
}

void FClothDebugRenderPass::Render(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    // if (ClothComponents.Num() == 0 || !ClothVertexShader || !ClothPixelShader) return;
    if (ClothComponents.Num() == 0)
        return;

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

void FClothDebugRenderPass::Release()
{
    FRenderPassBase::Release();

    ClothVertexShader = nullptr;
    ClothPixelShader = nullptr;

    SAFE_RELEASE(ClothInputLayout);
    SAFE_RELEASE(ClothMeshConstantBuffer);
    SAFE_RELEASE(ClothRasterizerState);
    SAFE_RELEASE(TempIndexBuffer);
}

void FClothDebugRenderPass::PrepareRender(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    const EResourceType ResourceType = EResourceType::ERT_Scene;
    FViewportResource *ViewportResource = Viewport->GetViewportResource();
    FRenderTargetRHI *RenderTargetRHI = ViewportResource->GetRenderTarget(ResourceType);
    FDepthStencilRHI *DepthStencilRHI = ViewportResource->GetDepthStencil(ResourceType);

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

    // Bind common constant buffers (Camera and Object buffers)
    // These are required by the cloth shaders for view/projection transforms
    ID3D11Buffer *CameraConstantBuffer = BufferManager->GetConstantBuffer(TEXT("FCameraConstantBuffer"));
    ID3D11Buffer *ObjectConstantBuffer = BufferManager->GetConstantBuffer(TEXT("FObjectConstantBuffer"));

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

    // Set rasterizer state (two-sided rendering for cloth)
    if (ClothRasterizerState)
    {
        Graphics->DeviceContext->RSSetState(ClothRasterizerState);
    }
    else
    {
        // Graphics->DeviceContext->RSSetState(Graphics->RasterizerSolidBack);
        Graphics->DeviceContext->RSSetState(Graphics->RasterizerWireframeBack);
    }
}

void FClothDebugRenderPass::CleanUpRender(const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    // Unbind cloth-specific resources
    ID3D11ShaderResourceView *nullSRVs[2] = {nullptr, nullptr};
    Graphics->DeviceContext->VSSetShaderResources(9, 2, nullSRVs);

    // Reset rasterizer state
    // Graphics->DeviceContext->RSSetState(nullptr);
}

void FClothDebugRenderPass::CreateResource()
{
    const int32 MaxClothVerts = 655360;
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
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};

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
    // rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.FillMode = D3D11_FILL_WIREFRAME;
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

void FClothDebugRenderPass::RenderClothComponent(UClothMeshComponent *ClothComponent, const std::shared_ptr<FEditorViewportClient> &Viewport)
{
    if (!ClothComponent)
        return;

    // Get render data from component
    FClothRenderData renderData;
    ClothComponent->GetRenderData(renderData);

    // DIAGNOSTIC: Log render data for each component
    static int renderCallCount = 0;
    if (renderCallCount++ < 10) // Log first 10 calls
    {
        UE_LOG(ELogLevel::Display, TEXT("ClothRenderPass: Component render - ParticleOffset=%d, NumVertices=%d, IndexOffset=%d, NumTriangles=%d, BatchedMode=%d"),
               renderData.ParticleOffset, renderData.NumVertices, renderData.IndexOffset, renderData.NumTriangles, renderData.bIsBatchedMode);
    }

    // Validate required data
    if (!renderData.PositionBufferSRV || !renderData.NormalBufferSRV)
        return;
    if (renderData.NumTriangles == 0)
        return;

    // Bind simulation buffers as SRVs (shared for both legacy and batched)
    ID3D11ShaderResourceView *clothSRVs[] = {
        renderData.PositionBufferSRV,
        renderData.NormalBufferSRV};
    Graphics->DeviceContext->VSSetShaderResources(9, 2, clothSRVs);

    // Update cloth mesh constant buffer with offsets for batched mode
    UpdateClothMeshConstantBuffer(renderData.WorldTransform, renderData.NumVertices,
                                  renderData.ParticleOffset, renderData.IndexOffset);
    Graphics->DeviceContext->VSSetConstantBuffers(10, 1, &ClothMeshConstantBuffer);

    if (renderData.bIsBatchedMode)
    {
        // Batched mode: Use unified index buffer with DrawIndexed at offset
        if (!renderData.UnifiedIndexBuffer)
            return;

        // Validate rendering parameters to prevent D3D11 errors
        uint32 indexCount = renderData.NumTriangles * 3;
        uint32 startIndexLocation = renderData.IndexOffset; // Already in index units (not triangles)
        int32 baseVertexLocation = 0;                       // Vertex offset handled in shader via ClothParticleOffset

        // Calculate the last index that will be accessed
        uint32 lastIndexAccessed = startIndexLocation + indexCount;

        // Get buffer description to verify size
        D3D11_BUFFER_DESC bufferDesc;
        renderData.UnifiedIndexBuffer->GetDesc(&bufferDesc);
        uint32 bufferIndexCapacity = bufferDesc.ByteWidth / sizeof(uint32);

        // Validate bounds
        if (lastIndexAccessed > bufferIndexCapacity)
        {
            UE_LOG(ELogLevel::Error, TEXT("ClothRenderPass: Index buffer out of bounds! StartIndex: %u, Count: %u, Last: %u, Capacity: %u"),
                   startIndexLocation, indexCount, lastIndexAccessed, bufferIndexCapacity);
            return; // Skip rendering to avoid D3D11 error
        }

        if (indexCount == 0 || renderData.NumTriangles == 0)
        {
            return; // Nothing to render
        }

        // Bind unified index buffer directly
        Graphics->DeviceContext->IASetIndexBuffer(renderData.UnifiedIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

        // Draw with offset and count
        // StartIndexLocation is in indices (not bytes)
        // BaseVertexLocation is 0 because we handle vertex offset in shader via ClothParticleOffset
        Graphics->DeviceContext->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
    }
}

void FClothDebugRenderPass::UpdateClothMeshConstantBuffer(const FMatrix &WorldTransform, uint32 NumVertices,
                                                     uint32 ParticleOffset, uint32 IndexOffset)
{
    if (!ClothMeshConstantBuffer)
        return;

    FClothMeshConstants constants;
    constants.ClothWorldMatrix = WorldTransform;
    constants.ClothNumVertices = NumVertices;
    constants.ClothParticleOffset = ParticleOffset;
    constants.ClothIndexOffset = IndexOffset;
    constants.ClothPadding = 0;

    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Graphics->DeviceContext->Map(ClothMeshConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, &constants, sizeof(FClothMeshConstants));
        Graphics->DeviceContext->Unmap(ClothMeshConstantBuffer, 0);
    }
}

ID3D11Buffer *FClothDebugRenderPass::CreateIndexBufferFromIndices(const TArray<uint32> &Indices)
{
    if (Indices.Num() == 0 || !Graphics || !Graphics->Device)
        return nullptr;

    ID3D11Buffer *indexBuffer = nullptr;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.Usage = D3D11_USAGE_DEFAULT;
    ibDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * Indices.Num());
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = Indices.GetData();

    HRESULT hr = Graphics->Device->CreateBuffer(&ibDesc, &ibData, &indexBuffer);
    if (FAILED(hr))
    {
        return nullptr;
    }

    return indexBuffer;
}
