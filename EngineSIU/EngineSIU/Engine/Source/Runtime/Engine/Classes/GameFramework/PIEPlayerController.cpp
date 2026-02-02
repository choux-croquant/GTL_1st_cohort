#include "PIEPlayerController.h"
#include "PIEFreeFlyPawn.h"
#include "PIETestGameMode.h"
#include "World/World.h"
#include "Camera/CameraComponent.h"
#include "LevelEditor/SlateAppMessageHandler.h"
#include "Launch/EngineLoop.h"
#include "InputCore/InputCoreTypes.h"

extern FEngineLoop GEngineLoop;

APiePlayerController::APiePlayerController()
{
    bSpeedBoostActive = false;
    bRightMouseDown = false;
    bMouseInputEnabled = false;
    MouseSensitivity = 0.15f;
    bMousePositionInitialized = false;
    LastMousePosition = FVector2D::ZeroVector;
}

APiePlayerController::~APiePlayerController()
{
    MouseDelegateHandles.Empty();
}

void APiePlayerController::PostSpawnInitialize()
{
    Super::PostSpawnInitialize();

    UE_LOG(ELogLevel::Display, TEXT("PIEPlayerController: Initialized"));
}

void APiePlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Cache references
    FreeFlyPawn = Cast<APieFreeFlyPawn>(GetPossessedActor());

    // Find GameMode by iterating through actors (World doesn't have GetGameMode)
    UWorld *World = GetWorld();
    if (World && World->GetActiveLevel())
    {
        for (AActor *Actor : World->GetActiveLevel()->Actors)
        {
            if (APieTestGameMode *FoundGameMode = Cast<APieTestGameMode>(Actor))
            {
                TestGameMode = FoundGameMode;
                break;
            }
        }
    }

    if (FreeFlyPawn)
    {
        UE_LOG(ELogLevel::Display, TEXT("PIEPlayerController: Possessing FreeFlyPawn"));

        // Set as view target
        UCameraComponent *Camera = FreeFlyPawn->GetCameraComponent();
        if (Camera)
        {
            SetViewTarget(FreeFlyPawn, FViewTargetTransitionParams());
        }
    }
    else
    {
        UE_LOG(ELogLevel::Warning, TEXT("PIEPlayerController: No FreeFlyPawn possessed!"));
    }

    // Setup mouse input via delegates
    SetupMouseInput();
}

void APiePlayerController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Process input each frame
    ProcessInput(DeltaTime);
}

void APiePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Cleanup mouse input BEFORE transitioning out of PIE
    CleanupMouseInput();

    // Clear pawn reference to prevent access after destruction
    FreeFlyPawn = nullptr;
    TestGameMode = nullptr;

    UE_LOG(ELogLevel::Display, TEXT("PIEPlayerController: EndPlay called, mouse input cleaned up"));

    Super::EndPlay(EndPlayReason);
}

void APiePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (!InputComponent)
    {
        UE_LOG(ELogLevel::Error, TEXT("PIEPlayerController: InputComponent is null!"));
        return;
    }

    // Movement bindings
    BindAction("W", [this](float Value)
               { OnMoveForward(Value); });
    BindAction("S", [this](float Value)
               { OnMoveForward(-Value); });
    BindAction("D", [this](float Value)
               { OnMoveRight(Value); });
    BindAction("A", [this](float Value)
               { OnMoveRight(-Value); });
    BindAction("E", [this](float Value)
               { OnMoveUp(Value); });
    BindAction("Q", [this](float Value)
               { OnMoveUp(-Value); });

    // Speed boost
    BindAction("LShift", [this](float Value)
               { OnSpeedBoost(Value); });
    BindAction("RShift", [this](float Value)
               { OnSpeedBoost(Value); });

    // Debug actions (F1, F2, F3) - Extensible
    BindAction("F1", [this](float Value)
               { OnDebugAction1(Value); });
    BindAction("F2", [this](float Value)
               { OnDebugAction2(Value); });
    BindAction("F3", [this](float Value)
               { OnDebugAction3(Value); });

    UE_LOG(ELogLevel::Display, TEXT("PIEPlayerController: Input bindings setup complete"));
    UE_LOG(ELogLevel::Display, TEXT("  Movement: WASD (horizontal), Q/E (vertical)"));
    UE_LOG(ELogLevel::Display, TEXT("  Camera: Right Mouse Button + drag"));
    UE_LOG(ELogLevel::Display, TEXT("  Speed: Mouse Wheel"));
    UE_LOG(ELogLevel::Display, TEXT("  Speed Boost: Hold Shift"));
    UE_LOG(ELogLevel::Display, TEXT("  Debug Actions: F1/F2/F3"));
}

void APiePlayerController::SetupMouseInput()
{
    Handler = GEngineLoop.GetAppMessageHandler();
    if (!Handler)
    {
        UE_LOG(ELogLevel::Error, TEXT("PIEPlayerController: Cannot get SlateAppMessageHandler"));
        return;
    }

    // Enable mouse input processing
    bMouseInputEnabled = true;

    // Register mouse down/up delegates
    MouseDelegateHandles.Add(Handler->OnMouseDownDelegate.AddLambda(
        [this](const FPointerEvent &MouseEvent)
        {
            if (bMouseInputEnabled)
                OnMouseButtonDown(MouseEvent);
        }));

    MouseDelegateHandles.Add(Handler->OnMouseUpDelegate.AddLambda(
        [this](const FPointerEvent &MouseEvent)
        {
            if (bMouseInputEnabled)
                OnMouseButtonUp(MouseEvent);
        }));

    // Register mouse move delegate
    MouseDelegateHandles.Add(Handler->OnMouseMoveDelegate.AddLambda(
        [this](const FPointerEvent &MouseEvent)
        {
            if (bMouseInputEnabled)
                OnMouseMove(MouseEvent);
        }));

    // Register mouse wheel delegate
    MouseDelegateHandles.Add(Handler->OnMouseWheelDelegate.AddLambda(
        [this](const FPointerEvent &MouseEvent)
        {
            if (bMouseInputEnabled)
                OnMouseWheelEvent(MouseEvent);
        }));

    UE_LOG(ELogLevel::Display, TEXT("PIEPlayerController: Mouse input delegates registered"));
}

void APiePlayerController::CleanupMouseInput()
{
    // Disable mouse input processing IMMEDIATELY to prevent crashes
    bMouseInputEnabled = false;
    bRightMouseDown = false;
    bMousePositionInitialized = false;

    // Clear pawn reference
    FreeFlyPawn = nullptr;

    // Clear handles (delegates will auto-cleanup when handler is destroyed)
    Handler->OnMouseDownDelegate.Clear();
    Handler->OnMouseUpDelegate.Clear();
    Handler->OnMouseMoveDelegate.Clear();
    Handler->OnMouseWheelDelegate.Clear();
}

void APiePlayerController::OnMoveForward(float Value)
{
    if (FreeFlyPawn && Value != 0.0f)
    {
        FreeFlyPawn->MoveForward(Value);
    }
}

void APiePlayerController::OnMoveRight(float Value)
{
    if (FreeFlyPawn && Value != 0.0f)
    {
        FreeFlyPawn->MoveRight(Value);
    }
}

void APiePlayerController::OnMoveUp(float Value)
{
    if (FreeFlyPawn && Value != 0.0f)
    {
        FreeFlyPawn->MoveUp(Value);
    }
}

void APiePlayerController::OnLookYaw(float Value)
{
    if (FreeFlyPawn && Value != 0.0f)
    {
        FreeFlyPawn->AddYawInput(Value * MouseSensitivity);
    }
}

void APiePlayerController::OnLookPitch(float Value)
{
    if (FreeFlyPawn && Value != 0.0f)
    {
        FreeFlyPawn->AddPitchInput(Value * MouseSensitivity);
    }
}

void APiePlayerController::OnSpeedBoost(float Value)
{
    if (FreeFlyPawn)
    {
        // Shift key pressed/released
        bSpeedBoostActive = (Value > 0.0f);
        // Boost speed to 2x when active
        FreeFlyPawn->SpeedMultiplier = bSpeedBoostActive ? 2.0f : 1.0f;
    }
}

void APiePlayerController::OnMouseButtonDown(const FPointerEvent &MouseEvent)
{
    // Safety check: ensure input is enabled and pawn is valid
    if (!bMouseInputEnabled || !FreeFlyPawn)
        return;

    // Check if right mouse button was pressed
    if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
    {
        bRightMouseDown = true;
        bMousePositionInitialized = false; // Reset for next frame
        LastMousePosition = MouseEvent.GetScreenSpacePosition();

        //UE_LOG(ELogLevel::Display, TEXT("PIE: Camera rotation enabled (RMB held)"));
    }
}

void APiePlayerController::OnMouseButtonUp(const FPointerEvent &MouseEvent)
{
    // Safety check: always allow button up to clear state, even if input disabled
    if (!bMouseInputEnabled)
    {
        bRightMouseDown = false;
        return;
    }

    // Check if right mouse button was released
    if (!MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) && bRightMouseDown)
    {
        bRightMouseDown = false;
        bMousePositionInitialized = false;

        //UE_LOG(ELogLevel::Display, TEXT("PIE: Camera rotation disabled (RMB released)"));
    }
}

void APiePlayerController::OnMouseMove(const FPointerEvent &MouseEvent)
{
    // Critical safety checks: prevent accessing destroyed pawn
    if (!bMouseInputEnabled || !bRightMouseDown || !FreeFlyPawn)
        return;

    FVector2D currentPos = MouseEvent.GetScreenSpacePosition();

    if (bMousePositionInitialized)
    {
        // Calculate mouse delta
        FVector2D delta = currentPos - LastMousePosition;

        // Apply camera rotation based on mouse movement
        if (delta.X != 0.0f || delta.Y != 0.0f)
        {
            FreeFlyPawn->AddYawInput(-delta.Y * MouseSensitivity);
            FreeFlyPawn->AddPitchInput(delta.X * MouseSensitivity); // Invert Y for natural feel
        }
    }

    LastMousePosition = currentPos;
    bMousePositionInitialized = true;
}

void APiePlayerController::OnMouseWheelEvent(const FPointerEvent &MouseEvent)
{
    // Safety check: ensure pawn is still valid
    if (!bMouseInputEnabled || !FreeFlyPawn)
        return;

    float wheelDelta = MouseEvent.GetWheelDelta();
    if (wheelDelta != 0.0f)
    {
        // Adjust movement speed based on mouse wheel
        FreeFlyPawn->AdjustMoveSpeed(wheelDelta);
    }
}

void APiePlayerController::OnDebugAction1(float Value)
{
    if (Value > 0.0f && TestGameMode)
    {
        TestGameMode->OnDebugAction1();
    }
}

void APiePlayerController::OnDebugAction2(float Value)
{
    if (Value > 0.0f && TestGameMode)
    {
        TestGameMode->OnDebugAction2();
    }
}

void APiePlayerController::OnDebugAction3(float Value)
{
    if (Value > 0.0f && TestGameMode)
    {
        TestGameMode->OnDebugAction3();
    }
}
