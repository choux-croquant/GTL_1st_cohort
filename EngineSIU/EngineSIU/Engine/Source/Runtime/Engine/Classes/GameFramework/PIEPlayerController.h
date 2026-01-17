#pragma once
#include "PlayerController.h"
#include "Delegates/DelegateCombination.h"

class APieFreeFlyPawn;
class APieTestGameMode;
class FPointerEvent;

/**
 * PIEPlayerController - Player controller for PIE mode
 *
 * Handles input binding for the free-fly camera pawn and provides
 * extensible debug action bindings for testing.
 *
 * Input Mapping:
 * - W/S: Forward/Backward
 * - A/D: Left/Right strafe
 * - Q/E: Down/Up
 * - Right Mouse Button + Mouse Move: Camera look (yaw/pitch)
 * - Mouse Wheel: Adjust movement speed
 * - F1/F2/F3: Debug actions (extensible)
 * - Shift: Speed boost (2x movement speed)
 *
 * Extension Guide:
 * To add new debug actions:
 * 1. Add key binding in SetupInputComponent()
 * 2. Create handler function (e.g., void OnDebugAction4())
 * 3. Implement functionality in handler
 * 4. Optionally delegate to GameMode for global actions
 */
class APiePlayerController : public APlayerController
{
    DECLARE_CLASS(APiePlayerController, APlayerController)

public:
    APiePlayerController();
    virtual ~APiePlayerController() override;

    virtual void PostSpawnInitialize() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    virtual void SetupInputComponent() override;

    // Setup mouse event delegates
    void SetupMouseInput();
    void CleanupMouseInput();

    // Movement input handlers
    void OnMoveForward(float Value);
    void OnMoveRight(float Value);
    void OnMoveUp(float Value);
    void OnLookYaw(float Value);
    void OnLookPitch(float Value);
    void OnSpeedBoost(float Value);

    // Mouse event handlers (called by delegates)
    void OnMouseButtonDown(const FPointerEvent &MouseEvent);
    void OnMouseButtonUp(const FPointerEvent &MouseEvent);
    void OnMouseMove(const FPointerEvent &MouseEvent);
    void OnMouseWheelEvent(const FPointerEvent &MouseEvent);

    // Debug action handlers (extensible)
    void OnDebugAction1(float Value);
    void OnDebugAction2(float Value);
    void OnDebugAction3(float Value);

private:
    APieFreeFlyPawn *FreeFlyPawn = nullptr;
    APieTestGameMode *TestGameMode = nullptr;

    bool bSpeedBoostActive = false;
    bool bRightMouseDown = false;
    bool bMouseInputEnabled = false; // Safety flag to prevent access after cleanup
    float MouseSensitivity = 0.15f;

    // Mouse tracking for camera rotation
    FVector2D LastMousePosition = FVector2D::ZeroVector;
    bool bMousePositionInitialized = false;

    // Delegate handles for cleanup
    TArray<FDelegateHandle> MouseDelegateHandles;
    FSlateAppMessageHandler* Handler;
};
