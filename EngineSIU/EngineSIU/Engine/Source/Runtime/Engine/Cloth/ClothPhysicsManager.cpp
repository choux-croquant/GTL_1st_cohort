#include "ClothPhysicsManager.h"
#include "Engine/UserInterface/Console.h"
#include "World/World.h"
#include "Cloth/ClothWorld.h"

FClothPhysicsManager::FClothPhysicsManager() = default;

FClothPhysicsManager::~FClothPhysicsManager()
{
    Shutdown();
}

void FClothPhysicsManager::Initialize(FGraphicsDevice* InGraphics,
    FDXDBufferManager* InBufferManager,
    FDXDShaderManager* InShaderManager)
{
    Graphics = InGraphics;
    BufferManager = InBufferManager;
    ShaderManager = InShaderManager;
}

FClothWorld* FClothPhysicsManager::CreateClothWorld(UWorld* World)
{
    if (!World || !Graphics || !BufferManager || !ShaderManager) return nullptr;

    if (FClothWorld* const* Found = ClothWorldMap.Find(World)) return *Found;

    FClothWorld* NewWorld = new FClothWorld();
    NewWorld->Initialize(Graphics, BufferManager, ShaderManager);
    ClothWorldMap.Add(World, NewWorld);

    return NewWorld;
}

FClothWorld* FClothPhysicsManager::GetClothWorld(UWorld* World) const
{
    if (!World) return nullptr;

    if (FClothWorld* const* Found = ClothWorldMap.Find(World)) return *Found;

    return nullptr;
}

void FClothPhysicsManager::RemoveClothWorld(UWorld* World)
{
    if (!World) return;

    FClothWorld* Found = ClothWorldMap[World];
    if (Found)
    {
        DestroyClothWorld(Found);
        ClothWorldMap.Remove(World);
    }

    if (CurrentWorld == Found) CurrentWorld = nullptr;
}

void FClothPhysicsManager::Simulate(float DeltaTime)
{
    if (!Graphics || !BufferManager || !ShaderManager) return;
    if (!CurrentWorld) return;

    CurrentWorld->Update(DeltaTime);
}

void FClothPhysicsManager::Shutdown()
{
    for (auto& Pair : ClothWorldMap)
    {
        DestroyClothWorld(Pair.Value);
    }
    ClothWorldMap.Empty();

    Graphics = nullptr;
    BufferManager = nullptr;
    ShaderManager = nullptr;
    CurrentWorld = nullptr;
}

void FClothPhysicsManager::DestroyClothWorld(FClothWorld* ClothWorld)
{
    if (!ClothWorld) return;

    ClothWorld->Release();
    delete ClothWorld;
}
