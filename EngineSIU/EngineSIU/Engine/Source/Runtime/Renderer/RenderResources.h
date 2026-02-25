#pragma once

#include <memory>
#define _TCHAR_DEFINED
#include <d3d11.h>

#include "Define.h"
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"

#include "ShaderConstants.h"
#include "Engine/Classes/Engine/Texture.h"

/// <summary>
/// Shader관련 모음.
/// VS, PS, InputLayout
/// 상수 버퍼 관련은 ShaderConstants.h로
/// </summary>
struct FShaderResource
{
    ID3D11VertexShader* Vertex = nullptr;
    ID3D11PixelShader* Pixel = nullptr;
    ID3D11InputLayout* Layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct FRenderResources
{
    struct FWorldComponentContainer
    {
        TArray<class UStaticMeshComponent*> StaticMeshObjs;
        TArray<class UBillboardComponent*> BillboardObjs;
        TArray<class UTextRenderComponent*> TextObjs;
        TArray<class ULightComponentBase*> LightObjs;
    } Components;

    struct FShaderResourceContainer
    {
        FShaderResource StaticMesh;
        FShaderResource Texture;
        FShaderResource Text;

    } Shaders;

    struct FConstantBufferContainer
    {
        FConstantBuffersStaticMesh StaticMesh;
        // texture관련 cb필요.
        //FConstantBuffersBatchLine BatchLine;
        //FConstantBuffersBatchLine BatchLine; // line text 추가해야함
        //FConstantBuffersBatchLine BatchLine;
    } ConstantBuffers;
};

struct FDebugPrimitiveData
{
    FVertexInfo VertexInfo;
    FIndexInfo IndexInfo;
};

// Icon
enum class EIconType
{
    None,
    DirectionalLight,
    PointLight,
    SpotLight,
    AmbientLight,
    ExponentialFog,
    AtmosphericFog,
};

struct FRenderResourcesDebug
{
    struct FWorldComponentContainer
    {
        TArray<class UStaticMeshComponent*> StaticMeshComponent;
        TArray<class ULightComponentBase*> Light;
        TArray<class UHeightFogComponent*> Fog;

        TArray<class UBoxComponent*> BoxComponents;
        TArray<class USphereComponent*> SphereComponents;
        TArray<class UCapsuleComponent*> CapsuleComponents;
        TArray<class UTorusComponent*> TorusComponents;
    } Components;

    struct FPrimitiveResourceContainer
    {
        FDebugPrimitiveData Box;
        FDebugPrimitiveData Sphere;
        FDebugPrimitiveData Cone;
        FDebugPrimitiveData Arrow;
        FDebugPrimitiveData Capsule;
        FDebugPrimitiveData Torus;
    } Primitives;

    TMap<EIconType, std::shared_ptr<FTexture>> IconTextures; // 사용 X
};
