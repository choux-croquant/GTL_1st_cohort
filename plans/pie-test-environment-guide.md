# PIE Test Environment Guide

## Overview

The PIE (Play In Editor) test environment provides a free-fly camera system and extensible debug functionality for testing cloth simulation and other gameplay features within the editor.

## Architecture

The system consists of three main components:

### 1. APieTestGameMode
**Location:** `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/GameFramework/PIETestGameMode.h`

The GameMode that's automatically used when entering PIE mode. It manages the overall test session and provides extensible debug action handlers.

**Key Features:**
- Automatically spawned when entering PIE
- Tracks time in PIE session
- Provides three extensible debug action methods that can be triggered via F1/F2/F3

### 2. APieFreeFlyPawn
**Location:** `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/GameFramework/PIEFreeFlyPawn.h`

A free-flying pawn with editor-style camera controls, replacing the traditional player character during PIE sessions.

**Key Features:**
- Built-in camera component
- Smooth movement in all directions
- Configurable movement speed and rotation sensitivity
- Speed boost support

### 3. APiePlayerController
**Location:** `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/GameFramework/PIEPlayerController.h`

Handles input bindings for the free-fly pawn and routes debug actions to the GameMode.

**Key Features:**
- WASD movement bindings
- Q/E vertical movement
- Mouse look (requires right-click drag in current viewport implementation)
- Shift for speed boost
- F1/F2/F3 for debug actions

## Default Controls

### Movement
- **W**: Move forward
- **S**: Move backward
- **A**: Strafe left
- **D**: Strafe right
- **Q**: Move down
- **E**: Move up
- **Shift**: Speed boost (2x movement speed)

### Camera
- **Mouse**: Camera yaw/pitch (similar to editor viewport)

### Debug Actions
- **F1**: Debug Action 1 (extensible)
- **F2**: Debug Action 2 (extensible)
- **F3**: Debug Action 3 (extensible)

## Extending the Input System

The input system is designed to be easily extensible for adding custom debug functionality.

### Adding New Debug Actions

#### Step 1: Add Handler to PIETestGameMode

Edit `PIETestGameMode.h` to add a new debug action method:

```cpp
// In APieTestGameMode class
public:
    virtual void OnDebugAction4(); // Add new action
```

Edit `PIETestGameMode.cpp` to implement it:

```cpp
void APieTestGameMode::OnDebugAction4()
{
    UE_LOG(ELogLevel::Display, TEXT("PIETestGameMode: Debug Action 4 triggered (Time: %.2f)"), TimeInPIE);
    
    // Your custom functionality here
    // Example: Toggle cloth simulation
    // Example: Spawn test objects
    // Example: Adjust physics parameters
}
```

#### Step 2: Add Input Binding to PIEPlayerController

Edit `PIEPlayerController.h` to add the handler declaration:

```cpp
// In APiePlayerController class
protected:
    void OnDebugAction4(float Value);
```

Edit `PIEPlayerController.cpp`:

Add to `SetupInputComponent()`:
```cpp
BindAction("F4", [this](float Value) { OnDebugAction4(Value); });
```

Implement the handler:
```cpp
void APiePlayerController::OnDebugAction4(float Value)
{
    if (Value > 0.0f && TestGameMode)
    {
        TestGameMode->OnDebugAction4();
    }
}
```

### Example: Adding Cloth Debug Toggle

Here's a complete example of adding a cloth simulation toggle:

**PIETestGameMode.h:**
```cpp
public:
    void ToggleClothSimulation();
    
private:
    bool bClothEnabled = true;
```

**PIETestGameMode.cpp:**
```cpp
void APieTestGameMode::ToggleClothSimulation()
{
    bClothEnabled = !bClothEnabled;
    
    UWorld* World = GetWorld();
    if (World)
    {
        for (AActor* Actor : World->GetActiveLevel()->Actors)
        {
            if (UClothMeshComponent* Cloth = Actor->GetComponentByClass<UClothMeshComponent>())
            {
                Cloth->bSimulate = bClothEnabled;
            }
        }
    }
    
    UE_LOG(ELogLevel::Display, TEXT("Cloth Simulation: %s"), 
        bClothEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
}
```

**PIEPlayerController.cpp:**
```cpp
// In SetupInputComponent():
BindAction("F4", [this](float Value) { 
    if (Value > 0.0f && TestGameMode) 
        TestGameMode->ToggleClothSimulation(); 
});
```

### Customizing Movement Parameters

You can adjust movement behavior by modifying the pawn's public properties:

```cpp
// In PIEPlayerController::BeginPlay() or elsewhere:
if (FreeFlyPawn)
{
    FreeFlyPawn->MoveSpeed = 2000.0f;      // Faster movement
    FreeFlyPawn->RotationSpeed = 0.5f;     // Slower camera rotation
}
```

### Adding Mouse Look Support

The current implementation uses key-based input bindings. To add proper mouse look:

Edit `PIEPlayerController::Tick()`:
```cpp
void APiePlayerController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    // Get mouse delta from viewport if right mouse button is held
    // This would require integration with the viewport's mouse event system
    
    ProcessInput(DeltaTime);
}
```

## Integration Points

### EditorEngine Integration

The PIE test environment is automatically activated in `UEditorEngine::BindEssentialObjects()` when entering PIE mode:

**File:** `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Engine/EditorEngine.cpp`

```cpp
void UEditorEngine::BindEssentialObjects()
{
    // Spawns PIETestGameMode
    // Spawns PIEFreeFlyPawn
    // Spawns PIEPlayerController
    // Sets up possession chain
}
```

To use a different GameMode or Pawn, modify this function to spawn your custom classes instead.

### Viewport Camera Compatibility

The free-fly pawn uses the same camera conventions as the editor viewport (`FEditorViewportClient`):
- Pitch: Y-axis rotation (-89° to +89° clamped)
- Yaw: Z-axis rotation
- Movement uses rotated forward/right vectors

This ensures familiar controls for users accustomed to editor navigation.

## Testing Workflow

1. **Enter PIE Mode:**
   - Press Play in Editor button
   - PIE test environment automatically activates
   - Console shows initialization messages

2. **Navigate the Scene:**
   - Use WASD + QE to move around
   - Hold Shift for faster movement
   - Use mouse to look around (if integrated with viewport)

3. **Test Features:**
   - Press F1/F2/F3 to trigger debug actions
   - Monitor console output for feedback
   - Observe cloth simulation and other gameplay features

4. **Exit PIE Mode:**
   - Press Stop in Editor
   - Environment automatically cleans up

## Advanced Customization

### Creating Custom Test Scenarios

Create a derived GameMode for specific test scenarios:

**CustomClothTestGameMode.h:**
```cpp
class ACustomClothTestGameMode : public APieTestGameMode
{
    DECLARE_CLASS(ACustomClothTestGameMode, APieTestGameMode)
    
public:
    virtual void InitGame() override;
    virtual void OnDebugAction1() override;
    
private:
    void SpawnClothTestActors();
    TArray<AActor*> TestActors;
};
```

Then modify `UEditorEngine::BindEssentialObjects()` to spawn your custom GameMode instead.

### Adding UI Overlays

You can add debug UI by integrating with the existing UI system:

```cpp
// In APieTestGameMode::StartMatch():
if (APlayerController* PC = GetWorld()->GetPlayerController())
{
    // Create debug UI widget showing controls, FPS, etc.
}
```

## Troubleshooting

### Movement Not Working
- Check console for "PIEPlayerController: Input bindings setup complete"
- Verify pawn possession succeeded
- Check that InputComponent is valid

### Camera Not Moving
- Ensure mouse look is integrated with viewport's mouse event system
- Check MouseSensitivity value
- Verify right mouse button detection (current implementation)

### Debug Actions Not Firing
- Confirm key bindings in SetupInputComponent()
- Check that TestGameMode pointer is valid
- Look for log messages in console

## Performance Considerations

- Free-fly movement updates every frame in `Tick()`
- Input is processed through the standard input component system
- Camera updates trigger view matrix recalculation
- Minimal overhead compared to editor viewport camera

## Future Enhancements

Potential improvements for the system:

1. **Saved Camera Bookmarks:** Store and recall camera positions
2. **Movement Smoothing:** Add acceleration/deceleration
3. **Speed Presets:** Quick-switch between slow/medium/fast movement
4. **Recording Mode:** Record camera paths for cinematics
5. **Multi-camera Support:** Switch between different view angles
6. **Gizmo Integration:** Manipulate objects while in PIE mode

## Related Files

- `GameMode.h/cpp` - Base GameMode class
- `Pawn.h/cpp` - Base Pawn class
- `PlayerController.h/cpp` - Base PlayerController class
- `CameraComponent.h/cpp` - Camera component implementation
- `EditorEngine.cpp` - PIE startup integration
- `EditorViewportClient.cpp` - Editor camera movement reference

## See Also

- [Cloth Simulation Testing](cloth-system-complete-fix-summary.md)
- [CUDA Cloth Implementation](cuda-cloth-implementation-summary.md)
