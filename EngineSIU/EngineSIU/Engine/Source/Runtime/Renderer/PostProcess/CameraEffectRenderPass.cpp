#include "CameraEffectRenderPass.h"
#include "Engine/Engine.h"
#include "UnrealEd/EditorViewportClient.h"
#include "Engine/World/World.h"
#include "Classes/Camera/PlayerCameraManager.h"
#include "Renderer/ShaderConstants.h"
#include "UnrealClient.h"
#include "D3D11RHI/DXDShaderManager.h"

void FCameraEffectRenderPass::Initialize(FDXDBufferManager* InBufferManager, FGraphicsDevice* InGraphics, FDXDShaderManager* InShaderManager)
{
    FRenderPassBase::Initialize(InBufferManager, InGraphics, InShaderManager);
}

void FCameraEffectRenderPass::CreateResource()
{
    ShaderManager->AddVertexShader(L"CameraEffectVertexShader", L"Shaders/CameraEffectShader.hlsl", "mainVS");
    ShaderManager->AddPixelShader(L"CameraEffectPixelShader", L"Shaders/CameraEffectShader.hlsl", "mainPS");
    
    BufferManager->CreateBufferGeneric<FConstantBufferCameraFade>("CameraFadeConstantBuffer", nullptr, sizeof(FConstantBufferCameraFade), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    BufferManager->CreateBufferGeneric<FConstantBufferCameraVignette>("CameraVignetteConstantBuffer", nullptr, sizeof(FConstantBufferCameraVignette), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    BufferManager->CreateBufferGeneric<FConstantBufferLetterBox>("LetterBoxConstantBuffer", nullptr, sizeof(FConstantBufferLetterBox), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
}

void FCameraEffectRenderPass::UpdateCameraEffectConstant(const std::shared_ptr<FEditorViewportClient>& Viewport)
{
    FConstantBufferCameraFade FadeParams;
    FConstantBufferCameraVignette VignetteParams;
    FConstantBufferLetterBox LetterBoxParams;
    LetterBoxParams.ScreenAspectRatio = Viewport->AspectRatio;
    LetterBoxParams.LetterBoxAspectRatio = Viewport->AspectRatio;

    if (UWorld* World = GEngine->ActiveWorld)
    {
        if (APlayerController* PC = World->GetPlayerController())
        {
            if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
            {
                FadeParams.FadeColor = PCM->FadeColor;
                FadeParams.FadeAmount = PCM->FadeAmount;
                VignetteParams.VignetteColor = PCM->VignetteColor;
                VignetteParams.VignetteCenter = PCM->VignetteCenter;
                VignetteParams.VignetteRadius = PCM->VignetteRadius;
                VignetteParams.VignetteSmoothness = PCM->VignetteSmoothness;
                VignetteParams.VignetteIntensity = PCM->VignetteIntensity;
                /*LetterBoxParams.LetterBoxColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
                LetterBoxParams.ScreenAspectRatio = Viewport->AspectRatio;
                LetterBoxParams.LetterBoxAspectRatio = PCM->GetLetterBoxRatio();*/
            }
        }
    }

    BufferManager->UpdateConstantBuffer<FConstantBufferCameraFade>("CameraFadeConstantBuffer", FadeParams);
    BufferManager->UpdateConstantBuffer<FConstantBufferCameraVignette>("CameraVignetteConstantBuffer", VignetteParams);
    BufferManager->UpdateConstantBuffer<FConstantBufferLetterBox>("LetterBoxConstantBuffer", LetterBoxParams);
}

void FCameraEffectRenderPass::Render(const std::shared_ptr<FEditorViewportClient>& Viewport)
{
    PrepareRender(Viewport);

    UpdateCameraEffectConstant(Viewport);
    
    Graphics->DeviceContext->Draw(6, 0);

    CleanUpRender(Viewport);
}

void FCameraEffectRenderPass::PrepareRender(const std::shared_ptr<FEditorViewportClient>& Viewport)
{
    FViewportResource* ViewportResource = Viewport->GetViewportResource();
    const EResourceType ResourceType = EResourceType::ERT_PP_CameraEffect;
    FRenderTargetRHI* RenderTargetRHI = ViewportResource->GetRenderTarget(ResourceType);
    
    Graphics->DeviceContext->OMSetRenderTargets(1, &RenderTargetRHI->RTV, nullptr);
    
    Graphics->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Graphics->DeviceContext->IASetInputLayout(nullptr);
    
    Graphics->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Graphics->DeviceContext->RSSetState(Graphics->RasterizerSolidBack);
    
    ID3D11VertexShader* VertexShader = ShaderManager->GetVertexShaderByKey(L"CameraEffectVertexShader");
    ID3D11PixelShader* PixelShader = ShaderManager->GetPixelShaderByKey(L"CameraEffectPixelShader");

    Graphics->DeviceContext->VSSetShader(VertexShader, nullptr, 0);
    Graphics->DeviceContext->PSSetShader(PixelShader, nullptr, 0);

    BufferManager->BindConstantBuffer("CameraFadeConstantBuffer", 0, EShaderStage::Pixel);
    BufferManager->BindConstantBuffer("CameraVignetteConstantBuffer", 1, EShaderStage::Pixel);
    BufferManager->BindConstantBuffer("LetterBoxConstantBuffer", 2, EShaderStage::Pixel);
}

void FCameraEffectRenderPass::CleanUpRender(const std::shared_ptr<FEditorViewportClient>& Viewport)
{
    Graphics->DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
}


