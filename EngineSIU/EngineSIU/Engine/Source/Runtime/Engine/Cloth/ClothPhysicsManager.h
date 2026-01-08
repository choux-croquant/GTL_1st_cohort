#pragma once

#include "Core/HAL/PlatformType.h"

#include "Container/Map.h"

class FGraphicsDevice;
class FDXDBufferManager;
class FDXDShaderManager;
class FClothWorld;
class UWorld;

/**
 * Global Cloth Physics Manager
 */
class FClothPhysicsManager
{
public:
    FClothPhysicsManager();
    ~FClothPhysicsManager();

    void Initialize(FGraphicsDevice* InGraphics, FDXDBufferManager* InBufferManager, FDXDShaderManager* InShaderManager);

    FClothWorld* CreateClothWorld(UWorld* World);
    FClothWorld* GetClothWorld(UWorld* World) const;
    void RemoveClothWorld(UWorld* World);

    void SetCurrentWorld(UWorld* World) { CurrentWorld = ClothWorldMap[World]; }
    void SetCurrentWorld(FClothWorld* World) { CurrentWorld = World; }
    FClothWorld* GetCurrentClothWorld() { return CurrentWorld ? CurrentWorld : nullptr; }

    void Simulate(float DeltaTime);

    void Shutdown();

private:
    FGraphicsDevice* Graphics = nullptr;
    FDXDBufferManager* BufferManager = nullptr;
    FDXDShaderManager* ShaderManager = nullptr;

    TMap<UWorld*, FClothWorld*> ClothWorldMap;

    FClothWorld* CurrentWorld = nullptr;

    void DestroyClothWorld(FClothWorld* ClothWorld);
};
