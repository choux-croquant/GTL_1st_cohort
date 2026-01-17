#pragma once
#include "Pawn.h"

class UCameraComponent;

/**
 * PIEFreeFlyPawn - Free-flying pawn for PIE testing
 *
 * Provides editor-style free-fly camera controls:
 * - WASD: Forward/Backward/Left/Right strafing
 * - QE: Up/Down movement
 * - Mouse: Camera yaw/pitch control (like editor fly mode)
 *
 * Movement is similar to the editor viewport camera for familiar controls
 */
class APieFreeFlyPawn : public APawn
{
    DECLARE_CLASS(APieFreeFlyPawn, APawn)

public:
    APieFreeFlyPawn();
    virtual ~APieFreeFlyPawn() override = default;

    virtual void PostSpawnInitialize() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // Movement functions called by PlayerController
    void MoveForward(float Value);
    void MoveRight(float Value);
    void MoveUp(float Value);
    void AddYawInput(float Value);
    void AddPitchInput(float Value);

    // Camera access
    UCameraComponent *GetCameraComponent() const { return CameraComponent; }

    // Movement settings (public for PlayerController access)
    float MoveSpeed = 5000.0f;    // Units per second (increased from 1000 for better default speed)
    float RotationSpeed = 1.0f;   // Degrees per mouse unit
    float SpeedMultiplier = 1.0f; // Can be adjusted with speed boost keys

    // Speed adjustment settings
    float MinMoveSpeed = 100.0f;        // Minimum movement speed
    float MaxMoveSpeed = 50000.0f;      // Maximum movement speed
    float SpeedAdjustmentFactor = 1.2f; // Speed multiplier per mouse wheel notch

    // Adjust movement speed (called by mouse wheel)
    void AdjustMoveSpeed(float Delta);

protected:
    UPROPERTY(UCameraComponent *, CameraComponent, = nullptr)

    // Current movement input
    FVector MovementInput = FVector::ZeroVector;
    float YawInput = 0.0f;
    float PitchInput = 0.0f;

    // Camera rotation tracking
    float CurrentPitch = 0.0f;
    float CurrentYaw = 0.0f;
};
