#include "ClothPhysicsManager.h"
#include "Engine/UserInterface/Console.h"
#include "World/World.h"
#include "Cloth/ClothWorld.h"
#include "Cloth/ClothCollisionManager.h"
#include "Cloth/ClothBatchManager.h"
#include "Cloth/ClothBatchedSolver.h"
#include "Engine/Engine.h"

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

    FClothCollisionManager* CollisionMgr = NewWorld->GetCollisionManager();
    TArray<UPrimitiveComponent*> Primitives;

    for (const auto Iter : TObjectRange<UPrimitiveComponent>())
    {
        if (Iter->GetWorld() == GEngine->ActiveWorld)
        {
            Primitives.Add(Iter);
        }
    }

    for (UPrimitiveComponent* Primitive : Primitives)
    {
        // Register colliders from this component's BodySetup
        int32 NumColliders = CollisionMgr->RegisterCollider(Primitive);

        /*UE_LOG(ELogLevel::Display, TEXT("Registered %d colliders from %s"),
            NumColliders, *Actor->GetName());*/
    }

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
