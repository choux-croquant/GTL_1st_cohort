#include "CharacterClothTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SocketComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimCustomNotify.h"
#include "Animation/AnimSoundNotify.h"
#include "Engine/Contents/AnimInstance/LuaScriptAnimInstance.h"
#include "Actors/Player.h"
#include "UObject/UObjectIterator.h"
#include "Engine/SkeletalMesh.h"
#include "Userinterface/Console.h"
#include "Engine/Engine.h"
#include "Physics/PhysicsManager.h"

void ACharacterClothTest::PostSpawnInitialize()
{
    Super::PostSpawnInitialize();

    // SetActorTickInEditor(true);
    USkeletalMeshComponent* SkeletalMeshComponent = AddComponent<USkeletalMeshComponent>("SkeletalMeshComponent");
    SkeletalMeshComponent->SetSkeletalMeshAsset(UAssetManager::Get().GetSkeletalMesh(FName("Contents/GameJamEnemy/GameJamEnemy")));
    SkeletalMeshComponent->StateMachineFileName = TEXT("LuaScripts/Animations/EnemyStateMachine.lua");
    SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    SkeletalMeshComponent->SetAnimClass(UClass::FindClass(FName("ULuaScriptAnimInstance")));

    UObject* AssetObject = UAssetManager::Get().GetAsset(EAssetType::PhysicsAsset, FName("Contents/PhysicsAsset/UPhysicsAsset_212"));

    if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(AssetObject))
    {
        SkeletalMeshComponent->GetSkeletalMeshAsset()->SetPhysicsAsset(PhysicsAsset);
    }

    SkeletalMeshComponent->bSimulate = true;

    UAnimSequence* IdleAnim = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Enemy_Idle/Armature|Enemy_Idle")));
    UAnimSequence* ReactionAnim = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Enemy_Impact/Armature|Enemy_Impact")));

    UAnimSequence* Horizontal1 = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Horizontal1/Armature|Horizontal1")));
    UAnimSequence* Horizontal2 = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Horizontal2/Armature|Horizontal2")));
    UAnimSequence* Vertical1 = Cast<UAnimSequence>(UAssetManager::Get().GetAnimation(FString("Contents/Vertical1/Armature|Vertical1")));
}

void ACharacterClothTest::BeginPlay()
{
    Super::BeginPlay();
    GetComponentByClass<USkeletalMeshComponent>()->bSimulate = false;
    InitialTransform = RootComponent->GetComponentTransform();
}

void ACharacterClothTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

UObject* ACharacterClothTest::Duplicate(UObject* InOuter)
{
    ACharacterClothTest* NewActor = Cast<ACharacterClothTest>(Super::Duplicate(InOuter));

    return NewActor;
}
