#pragma once
#include "Define.h"
#include "IWindowToggleable.h"
#include "UnrealEd/EditorViewportClient.h"

namespace EEngineShowFlags
{
    enum Type : uint64
    {
        None = 0,
        SF_AABB = 1ULL << 0,
        SF_Primitives = 1ULL << 1,
        SF_BillboardText = 1ULL << 2,
        SF_UUIDText = 1ULL << 3,
        SF_Fog = 1ULL << 4,
        SF_LightWireframe = 1ULL << 5,
        SF_LightWireframeSelectedOnly = 1ULL << 6,
        SF_Shadow = 1ULL << 7,
        SF_Collision = 1ULL << 8,
        SF_CollisionSelectedOnly = 1ULL << 9,
        SF_SkeletalMesh = 1ULL << 10,
        SF_Particles = 1ULL << 11,
        SF_DOF = 1ULL << 12,
        SF_ClothDebug = 1ULL << 13,
    };
}

class UWorld;

class ShowFlags : public IWindowToggleable
{
private:
    ShowFlags() = default;

public:
    static ShowFlags& GetInstance();

    static void Draw(const std::shared_ptr<FEditorViewportClient>& ActiveViewport);
    static uint64 ConvertSelectionToFlags(const bool Selected[]);
    void OnResize(HWND hWnd);

    virtual void Toggle() override { bWasOpen ^= true; }
    
    uint64 CurrentFlags = ULLONG_MAX;
    
private:
    bool bWasOpen = false;
    
    UINT Width;
    UINT Height;
};
