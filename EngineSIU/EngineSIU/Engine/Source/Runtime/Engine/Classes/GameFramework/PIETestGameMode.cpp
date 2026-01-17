#include "PIETestGameMode.h"
#include "PIEPlayerController.h"
#include "PIEFreeFlyPawn.h"
#include "UObject/ObjectFactory.h"


APieTestGameMode::APieTestGameMode()
{
    bFreeFlyMode = true;
    TimeInPIE = 0.0f;
}

void APieTestGameMode::PostSpawnInitialize()
{
    Super::PostSpawnInitialize();

    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Initialized"));
}

void APieTestGameMode::InitGame()
{
    Super::InitGame();

    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Game Initialized - Free-Fly Mode Active"));
}

void APieTestGameMode::StartMatch()
{
    Super::StartMatch();

    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Match Started"));
}

void APieTestGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TimeInPIE += DeltaTime;
}

void APieTestGameMode::OnDebugAction1()
{
    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Debug Action 1 triggered (Time: %.2f)"), TimeInPIE);
    // Extensible: Add custom debug functionality here
}

void APieTestGameMode::OnDebugAction2()
{
    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Debug Action 2 triggered (Time: %.2f)"), TimeInPIE);
    // Extensible: Add custom debug functionality here
}

void APieTestGameMode::OnDebugAction3()
{
    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Debug Action 3 triggered (Time: %.2f)"), TimeInPIE);
    // Extensible: Add custom debug functionality here
}
