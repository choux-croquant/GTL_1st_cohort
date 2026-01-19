#include "PIEFreeFlyPawn.h"
#include "Camera/CameraComponent.h"
#include "UObject/ObjectFactory.h"
#include "Math/JungleMath.h"

APieFreeFlyPawn::APieFreeFlyPawn()
{
    MoveSpeed = 5000.0f; // Increased default speed
    RotationSpeed = 1.0f;
    SpeedMultiplier = 1.0f;
    CurrentPitch = 0.0f;
    CurrentYaw = 0.0f;

    // Speed adjustment settings
    MinMoveSpeed = 100.0f;
    MaxMoveSpeed = 50000.0f;
    SpeedAdjustmentFactor = 1.2f;
}

void APieFreeFlyPawn::PostSpawnInitialize()
{
    Super::PostSpawnInitialize();

    // Create camera component
    CameraComponent = AddComponent<UCameraComponent>();
    if (CameraComponent)
    {
        SetRootComponent(CameraComponent);
        CameraComponent->ViewFOV = 90.0f;
        CameraComponent->NearClip = 0.1f;
        CameraComponent->FarClip = 15000.0f;
    }

    // Set initial position (elevated for better view)
    SetActorLocation(FVector(0.0f, 0.0f, 200.0f));

    UE_LOG(ELogLevel::Display, TEXT("PIEFreeFlyPawn: Initialized with camera at %s"),
           *GetActorLocation().ToString());
}

void APieFreeFlyPawn::BeginPlay()
{
    Super::BeginPlay();

    // Initialize camera rotation based on current actor rotation
    FRotator Rotation = GetActorRotation();
    CurrentPitch = Rotation.Pitch;
    CurrentYaw = Rotation.Yaw;
}

void APieFreeFlyPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!CameraComponent)
        return;

    // Apply rotation
    if (YawInput != 0.0f || PitchInput != 0.0f)
    {
        CurrentPitch += PitchInput * RotationSpeed;
        CurrentYaw = FMath::Clamp(CurrentYaw + YawInput * RotationSpeed, -89.0f, 89.0f);

        // Update actor rotation
        SetActorRotation(FRotator(CurrentYaw, CurrentPitch, 0.0f));

        // Reset input for next frame
        YawInput = 0.0f;
        PitchInput = 0.0f;
    }

    // Apply movement
    if (MovementInput.SizeSquared() > 0.0f)
    {
        FVector CurrentLocation = GetActorLocation();
        FRotator CurrentRotationVec = GetActorRotation();

        // Calculate movement vectors
        FVector Forward = FVector(1.0f, 0.0f, 0.0f);
        Forward = JungleMath::FVectorRotate(Forward, CurrentRotationVec);

        FVector Right = FVector(0.0f, 1.0f, 0.0f);
        Right = JungleMath::FVectorRotate(Right, CurrentRotationVec);

        FVector Up = FVector(0.0f, 0.0f, 1.0f);

        // Calculate final movement delta
        FVector Movement = FVector::ZeroVector;
        Movement += Forward * MovementInput.X;
        Movement += Right * MovementInput.Y;
        Movement += Up * MovementInput.Z;

        // Apply speed and delta time
        Movement = Movement * MoveSpeed * SpeedMultiplier * DeltaTime;

        // Update location
        SetActorLocation(CurrentLocation + Movement);

        // Reset movement input for next frame
        MovementInput = FVector::ZeroVector;
    }
}

void APieFreeFlyPawn::MoveForward(float Value)
{
    MovementInput.X += Value;
}

void APieFreeFlyPawn::MoveRight(float Value)
{
    MovementInput.Y += Value;
}

void APieFreeFlyPawn::MoveUp(float Value)
{
    MovementInput.Z += Value;
}

void APieFreeFlyPawn::AddYawInput(float Value)
{
    YawInput += Value;
}

void APieFreeFlyPawn::AddPitchInput(float Value)
{
    PitchInput += Value;
}

void APieFreeFlyPawn::AdjustMoveSpeed(float Delta)
{
    if (Delta > 0.0f)
    {
        // Mouse wheel up - increase speed
        MoveSpeed *= SpeedAdjustmentFactor;
    }
    else if (Delta < 0.0f)
    {
        // Mouse wheel down - decrease speed
        MoveSpeed /= SpeedAdjustmentFactor;
    }

    // Clamp to min/max
    MoveSpeed = FMath::Clamp(MoveSpeed, MinMoveSpeed, MaxMoveSpeed);
}
