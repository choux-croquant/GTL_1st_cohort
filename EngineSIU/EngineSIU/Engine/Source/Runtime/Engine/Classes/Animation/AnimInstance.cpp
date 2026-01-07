#include "AnimInstance.h"

#include "Actors/BehellaGameMode.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/GameMode.h"
#include "UObject/Casts.h"
#include "World/World.h"

void UAnimInstance::InitializeAnimation()
{
    USkeletalMeshComponent* OwnerComponent = GetSkelMeshComponent();
    if (OwnerComponent->GetSkeletalMeshAsset() != nullptr)
    {
        CurrentSkeleton = OwnerComponent->GetSkeletalMeshAsset()->GetSkeleton();
    }
    else
    {
        CurrentSkeleton = nullptr;
    }
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds, FPoseContext& OutPose)
{
    //ABehellaGameMode* BGM = Cast<ABehellaGameMode>(GetWorld()->GetGameMode());
    //DeltaSeconds *= BGM->AnimSlowFactor;
    
    NativeUpdateAnimation(DeltaSeconds, OutPose);
}

void UAnimInstance::NativeInitializeAnimation()
{
}

void UAnimInstance::NativeUpdateAnimation(float DeltaSeconds, FPoseContext& OutPose)
{
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const
{
    return Cast<USkeletalMeshComponent>(GetOuter());
}
