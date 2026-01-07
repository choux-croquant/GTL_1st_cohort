#include "BehellaGameMode.h"

#include "Engine/Classes/Actors/Player.h"
#include "Engine/Classes/Actors/Enemy.h"
#include "Engine/Engine.h"
#include "Engine/World/World.h"
#include "Engine/Classes/Actors/BehellaGameMode.h"
#include "Physics/PhysicsManager.h"
#include "SoundManager.h"
#include "Engine/TimerManager.h"
#include "Engine/Contents/Objects/DamageCameraShake.h"


EBehellaGameState ABehellaGameMode::GameState = EBehellaGameState::Ready;

void ABehellaGameMode::PostSpawnInitialize()
{
    AActor::PostSpawnInitialize();

}

void ABehellaGameMode::BeginPlay()
{
    Super::BeginPlay();

    OnGameInit.AddLambda([]() { UE_LOG(ELogLevel::Display, TEXT("Game Initialized")); });

    SetActorTickInEditor(false); // PIE 모드에서만 Tick 수행

    //배경위치 찍게 카메라 옮기기
    if (APlayerController* PC = GEngine->ActiveWorld->GetPlayerController())
    {
        if (APlayerCameraManager* PCM = PC->PlayerCameraManager)
        {
            FVector BackgroundLocation = FVector(205.38f, 130.33f, 142.96f);
            FRotator BackgroundRotation = FRotator(-3.f, -122.f, 0.f);
            FMinimalViewInfo BackgroundPOV;
            BackgroundPOV.Location = BackgroundLocation;
            BackgroundPOV.Rotation = BackgroundRotation;
            
            PCM->ViewTarget.POV = BackgroundPOV;
        }
    }
    
    if (FSlateAppMessageHandler* Handler = GEngineLoop.GetAppMessageHandler())
    {
        /*Handler->OnPIEModeStartDelegate.AddLambda([this]()
        {
            this->InitGame();
        });*/
        Handler->OnKeyDownDelegate.AddLambda([this](const FKeyEvent& KeyEvent)
            {
                // 키가 Space, 아직 게임이 안 시작됐고, 실패 또는 종료되지 않았다면
                if (KeyEvent.GetKeyCode() == VK_SPACE)
                {
                    if (!bGameRunning && bGameEnded) 
                    {
                        PrepareMatch();
                    }
                    if (GameState == EBehellaGameState::Dead) 
                    {
                        EndMatchWrap(false);
                    }
                }
                if (KeyEvent.GetKeyCode() == 'R')
                {
                    if (GameState == EBehellaGameState::GameOver)
                    {
                        RestartMatch();
                    }
                }
            });
    }

    // Hero 와 Enemy 포인터로 가져오기
    for (auto Actor : GEngine->ActiveWorld->GetActiveLevel()->Actors)
    {
        if (AHeroPlayer* heroPlayer = Cast<AHeroPlayer>(Actor))
        {
            HeroPlayer = heroPlayer;
        }

        if (AEnemy* enemy = Cast<AEnemy>(Actor))
        {
            Enemy = enemy;
        }
    }

    if (HeroPlayer)
    {
        HeroPlayer->OnParry.AddLambda([this]()
            {
                CheckFatality();
            }
        );
        HeroPlayer->OnHeroDied.AddLambda([this](bool trashValue)
            {
                if (GameState == EBehellaGameState::Play)
                {
                    // Play 중에 플레이어가 사망했는지
                    PlayerDead();
                }
            }
        );
        HeroPlayer->OnCinematicFinish.AddLambda([this]()
            {
                StartMatch();
            }
        );
    }
}

UObject* ABehellaGameMode::Duplicate(UObject* InOuter)
{
    ABehellaGameMode* NewActor = Cast<ABehellaGameMode>(Super::Duplicate(InOuter));


    return NewActor;
}

void ABehellaGameMode::CloseScreen(FBehellaScreenUI* Screen)
{
    if (ClosingScreenUI != nullptr) 
    {
        ClosingScreenUI->EndScreen();
    }

    Screen->CloseScreen(ClosingUITime);
    ClosingScreenUI = Screen;
}

void ABehellaGameMode::InitGame()
{
    Super::InitGame();  // Super에서 Delegate BroadCast 걸어줌

    // 게임 State 설정
    GameState = EBehellaGameState::Ready;
    // 게임 초기 화면 UI 키기
    CurScreenUI = &InitScreenUI;
    CurScreenUI->InitScreen();

    // 캐릭터에 대한 Input 차단 명령 GameState를 공유해서 해결

    
}

void ABehellaGameMode::PrepareMatch()
{
    bGameRunning = true;
    GameState = EBehellaGameState::PrepareToPlay;

    CloseScreen(CurScreenUI);
    
    // 카메라 무빙 시키기
    HeroPlayer->bWaitingStart = false;
}

void ABehellaGameMode::StartMatch()
{
    Super::StartMatch();   //  Super에서 Delegate BroadCast 걸어줌

    // 게임 State 설정
    GameState = EBehellaGameState::Play;
    CurScreenUI = &PlayScreenUI;
    CurScreenUI->InitScreen();

    // 캐릭터에 대한 Input 차단 풀기 GameState가 바뀌며 자동으로 해제됨

}

void ABehellaGameMode::RestartMatch()
{
    ResetValue(); //Player 체력 값이나 Enemy 게이지 값 초기화

    GetWorld()->GetPlayerController()->PlayerCameraManager->StartLetterBoxAnimation(0.0f, 1.2f, 2.0f);

    GameState = EBehellaGameState::Play;
    HeroPlayer->SetCameraMoveCounter(3);
    CloseScreen(CurScreenUI);
    // 지금 Fade로 Closing하는게 잘 안됨 일단 시간 없어서 아래처럼 바로 끄기로
    ClosingScreenUI->EndScreen();
    ClosingScreenUI = nullptr;

    CurScreenUI = &PlayScreenUI;
    CurScreenUI->InitScreen();
}

void ABehellaGameMode::PlayerWin()
{
    // 게임 State 설정
    GameState = EBehellaGameState::PlayToGameOver;

    CloseScreen(CurScreenUI);
    // 지금 Fade로 Closing하는게 잘 안됨 일단 시간 없어서 아래처럼 바로 끄기로
    ClosingScreenUI->EndScreen();
    ClosingScreenUI = nullptr;
    CurScreenUI = nullptr;

    // TODO 처형 시네마틱 재생
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // 1. 화면을 3초 동안 검게 만들기
    APlayerCameraManager* CameraManager = GetWorld()->GetPlayerController()->PlayerCameraManager;
    CameraManager->VignetteColor = FLinearColor::Black;
    CameraManager->StartCameraFade(0.0f, 1.0f, 3.0f, FLinearColor::Black, true);

    // 2. 3초 후 화면을 3초 동안 복구하기
    FTimerManager::GetInstance().AddTimer(3.0f, [CameraManager, this]() {
            // CameraManager->SetViewTarget();
            CameraManager->StartCameraFade(1.0f, 0.0f, 3.0f, FLinearColor::Black, false);
            HeroPlayer->OnFinalScene();
            Enemy->OnFinalScene();
        
            // 타이머 대신 처형버튼 으로 수정
            FTimerManager::GetInstance().AddTimer(2.0f, [this]()
            {
                HeroPlayer->SetAnimState(FString("FinalAttack"));
                FSoundManager::GetInstance().PlaySound("footprint");
                FSoundManager::GetInstance().PlaySoundWithDelay("footprint", 0.4f);
                FSoundManager::GetInstance().PlaySoundWithDelay("footprint", 0.8f);
            });
            FTimerManager::GetInstance().AddTimer(4.0f, [this]()
            {
                Enemy->GetComponentByClass<USkeletalMeshComponent>()->ChangeRigidBodyFlag(ERigidBodyType::DYNAMIC);
                Enemy->GetComponentByClass<USkeletalMeshComponent>()->bSimulate = true;
                // GEngine->PhysicsManager->GetScene(GetWorld())->simulate(FLT_MIN);
                GEngine->PhysicsManager->GetScene(GetWorld())->fetchResults(true);
                Enemy->GetComponentByClass<USkeletalMeshComponent>()->AddImpulseToBones(FVector(-1.0f, 0.0f, 1.0f), 10000.0f);
                FSoundManager::GetInstance().PlaySound("Roar");
            });
        }
    );

    FTimerManager::GetInstance().AddTimer(10.0f, [this]() {
        EndMatchWrap(true);
        }
    );
    // 처형이 끝났을 때는 처형 시네마틱에서 EndMatch 호출하도록 해야함 Delegate 던지 함수 호출이던지
}

void ABehellaGameMode::PlayerDead()
{
    // 게임 State 설정
    GameState = EBehellaGameState::Dead;
    CloseScreen(CurScreenUI);
    // 지금 Fade로 Closing하는게 잘 안됨 일단 시간 없어서 아래처럼 바로 끄기로
    ClosingScreenUI->EndScreen();
    ClosingScreenUI = nullptr;

    CurScreenUI = &DeadScreenUI;
    // You Died 뜨는 UI FadeIn
    CurScreenUI->InitScreen();

    // You Died를 충분히 봤다고 하면 EndMatch로 넘어감

}

void ABehellaGameMode::EndMatchWrap(bool bIsWin)
{
    EndMatch(bIsWin);
}

void ABehellaGameMode::EndMatch(bool bIsWin)    // 현재는 bIsWin이 쓰이진 않음
{
    GameState = EBehellaGameState::GameOver;

    GetWorld()->GetPlayerController()->PlayerCameraManager->StartLetterBoxAnimation(1.2f, 0.0f, 2.0f);

    // 게임 End 화면 UI 켜기
    if (CurScreenUI != nullptr) 
    {
        CloseScreen(CurScreenUI);
    }
    
    if (ClosingScreenUI != nullptr) 
    {
        // 지금 Fade로 Closing하는게 잘 안됨 일단 시간 없어서 아래처럼 바로 끄기로
        ClosingScreenUI->EndScreen();
        ClosingScreenUI = nullptr;
    }

    CurScreenUI = &GameOverScreenUI;
    CurScreenUI->InitScreen();
    // 캐릭터에 대한 Input 차단 걸기


    //Super::EndMatch(bIsWin);  //  Super에서 Delegate BroadCast 걸어줌
}

void ABehellaGameMode::ResetValue()
{
    HeroPlayer->ResetHero();
    Enemy->ResetEnemyProperties();
}

void ABehellaGameMode::CheckFatality()
{
    if (GameState == EBehellaGameState::Play && Enemy->ParryGauge >= MaxParryGauge)
    {
        // 그로기 게이지를 다 모아서 처형을 입력했는지 판단
        PlayerWin();
    }
}

void ABehellaGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // ClosingScreen이 닫힐 시간이 충분하다면 아래처럼 없애기
    if (ClosingScreenUI != nullptr && ClosingScreenUI->ClosingTimer > ClosingUITime)
    {
        ClosingScreenUI->EndScreen();
        ClosingScreenUI = nullptr;
    }

    if (CurScreenUI != nullptr)
    {
        CurScreenUI->TickScreen(DeltaTime);
    }
    

    //PlayScreenUI.ParryRatio = Enemy->ParryGauge / MaxParryGauge;

    StepTimer += DeltaTime;
}
