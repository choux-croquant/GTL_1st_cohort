# PIE Controller Enhancements - Right Mouse Camera & Mouse Wheel Speed

## Summary of Changes

Extended the PIE (Play In Editor) test environment with two critical features for better cloth simulation testing:

1. **Right-Mouse Camera Rotation** - Hold RMB and drag to rotate camera
2. **Mouse Wheel Speed Adjustment** - Scroll to adjust movement speed on-the-fly

## Critical Crash Fix (EndPlay Safety)

**Problem:** Engine crashed when returning from PIE to Editor mode because mouse delegates were still firing after the pawn was destroyed.

**Solution:**
- Added `bMouseInputEnabled` safety flag
- `EndPlay()` disables flag BEFORE pawn destruction
- All mouse handlers check flag and exit early if disabled
- Prevents accessing destroyed `FreeFlyPawn` pointer

## Files Modified

### 1. PIEFreeFlyPawn.h
**Added:**
- `MinMoveSpeed` (100.0f) - Minimum movement speed limit
- `MaxMoveSpeed` (50000.0f) - Maximum movement speed limit  
- `SpeedAdjustmentFactor` (1.2f) - Multiplier per mouse wheel notch
- `AdjustMoveSpeed(float Delta)` - Method to adjust speed via mouse wheel
- **Increased default `MoveSpeed` from 1000 to 5000** - Much better default speed

### 2. PIEFreeFlyPawn.cpp
**Added:**
- Constructor initialization for speed adjustment parameters
- `AdjustMoveSpeed()` implementation:
  - Wheel up: `MoveSpeed *= 1.2f`
  - Wheel down: `MoveSpeed /= 1.2f`
  - Clamps to min/max range
  - Logs current speed to console

### 3. PIEPlayerController.h
**Added:**
- `bRightMouseDown` - Tracks right mouse button state
- `LastMousePosition` (POINT) - Stores last mouse position for delta calculation
- `bMousePositionInitialized` - Prevents initial jump when RMB first pressed
- `OnRightMouseDown()` / `OnRightMouseUp()` - RMB event handlers
- `OnMouseWheel()` - Mouse wheel event handler
- **Increased `MouseSensitivity` from 0.1f to 0.15f** - Better default feel

### 4. PIEPlayerController.cpp
**Modified:**
- Constructor: Initialize new member variables
- `SetupInputComponent()`: Added input bindings for RMB and mouse wheel
- `Tick()`: Added mouse delta tracking and camera rotation when RMB held

**Added:**
- `OnRightMouseDown()`: Enables camera rotation mode
- `OnRightMouseUp()`: Disables camera rotation mode
- `OnMouseWheel()`: Delegates to pawn's `AdjustMoveSpeed()`

## How It Works

### Right-Mouse Camera Rotation

1. **User presses RMB:**
   - `OnRightMouseDown()` is called
   - Sets `bRightMouseDown = true`
   - Resets `bMousePositionInitialized` to capture fresh position

2. **User moves mouse while RMB held:**
   - `Tick()` checks `bRightMouseDown` each frame
   - Gets current cursor position via `GetCursorPos()`
   - Calculates delta from `LastMousePosition`
   - Applies rotation to pawn via `AddYawInput()` / `AddPitchInput()`
   - Y-axis is inverted for natural camera feel

3. **User releases RMB:**
   - `OnRightMouseUp()` is called
   - Sets `bRightMouseDown = false`
   - Camera stops rotating with mouse

### Mouse Wheel Speed Adjustment

1. **User scrolls wheel:**
   - `OnMouseWheel(float Value)` receives delta
   - Value > 0: wheel up (faster)
   - Value < 0: wheel down (slower)

2. **Speed is adjusted:**
   - Calls `FreeFlyPawn->AdjustMoveSpeed(delta)`
   - Speed multiplied/divided by `SpeedAdjustmentFactor` (1.2f)
   - Clamped to range [100, 50000]
   - New speed logged to console

3. **Movement updated immediately:**
   - Next WASDQE input uses new `MoveSpeed`
   - Allows smooth transition between slow inspection and fast traversal

## Input Bindings Summary

| Input | Action | Notes |
|-------|--------|-------|
| W/S | Forward/Backward | Uses forward vector |
| A/D | Strafe Left/Right | Uses right vector |
| Q/E | Move Down/Up | Uses world Z-axis |
| Shift | Speed Boost (2x) | Temporary multiplier |
| **RMB Hold** | **Enable Camera Rotation** | **NEW** |
| **RMB + Mouse** | **Rotate Camera** | **NEW** |
| **Mouse Wheel** | **Adjust Speed** | **NEW** |
| F1/F2/F3 | Debug Actions | Extensible |

## Tunable Parameters

All parameters are public members of `PIEFreeFlyPawn` for easy tweaking:

```cpp
// In PIEFreeFlyPawn.h:
float MoveSpeed = 5000.0f;           // Current movement speed
float MinMoveSpeed = 100.0f;          // Wheel down limit
float MaxMoveSpeed = 50000.0f;        // Wheel up limit
float SpeedAdjustmentFactor = 1.2f;   // 20% per wheel notch
float RotationSpeed = 1.0f;           // Degrees per mouse pixel
float SpeedMultiplier = 1.0f;         // Shift key multiplier (set to 2.0 when active)
```

```cpp
// In PIEPlayerController.h:
float MouseSensitivity = 0.15f;       // Mouse look sensitivity
```

## Usage Examples

### Inspecting Cloth from Multiple Angles

1. Enter PIE mode
2. Hold RMB and move mouse to look around cloth
3. Use WASD to move closer/away
4. Use QE to change vertical perspective
5. Release RMB when desired view found

### Fast Navigation Across Large Scenes

1. Scroll mouse wheel **up** several times (e.g., 5-10 scrolls)
2. Console shows: "PIE Camera Speed: 15000 units/sec" (or similar)
3. Use WASD to quickly traverse the scene
4. When near target, scroll **down** to slow to inspection speed
5. Hold RMB + mouse to fine-tune camera angle

### Precise Cloth Detail Inspection

1. Scroll mouse wheel **down** to minimum speed (100 units/sec)
2. Use WASD for slow, precise positioning
3. Hold RMB and make small mouse movements for fine camera adjustment
4. Perfect for examining cloth vertex behavior, collision responses, etc.

## Technical Implementation Notes

### Mouse Position Tracking

- Uses Windows `GetCursorPos()` for raw cursor position
- Calculates delta each frame when RMB held
- `bMousePositionInitialized` prevents initial "jump" when RMB first pressed
- Mouse position only tracked when RMB active (zero overhead when not used)

### Speed Adjustment Formula

```cpp
// Wheel up (faster):
MoveSpeed *= 1.2f;

// Wheel down (slower):
MoveSpeed /= 1.2f;

// Then clamp:
MoveSpeed = Clamp(MoveSpeed, 100.0f, 50000.0f);
```

**Speed Progression Example (starting from 5000):**
- 3 scrolls up: 5000 → 6000 → 7200 → 8640
- 3 scrolls down: 5000 → 4167 → 3472 → 2894
- ~6 octaves of speed range (100 to 50000)

### Camera Rotation Behavior

- Yaw rotates around world Z-axis (horizontal look)
- Pitch clamped to [-89°, +89°] to prevent gimbal lock
- Sensitivity can be tuned via `MouseSensitivity` parameter
- Rotation applied via pawn's existing `AddYawInput()` / `AddPitchInput()` methods

## Integration with Existing Systems

### Input Component

- Uses existing `InputComponent->BindAction()` system
- Leverages key names from `InputCoreTypes.h`:
  - `"RightMouseButton"` for RMB
  - `"MouseWheelAxis"` for wheel
- Compatible with existing input event pipeline

### Camera System

- Works with existing `UCameraComponent` in `PIEFreeFlyPawn`
- Camera rotation uses same methods as WASD movement
- No conflicts with existing viewport camera system

## Debugging / Troubleshooting

### Console Logs

Watch for these messages when testing:

```
PIE: Camera rotation enabled (RMB held)
PIE: Camera rotation disabled (RMB released)
PIE Camera Speed: 5000 units/sec
PIE Camera Speed: 8640 units/sec  // After scrolling up
PIE Camera Speed: 2894 units/sec  // After scrolling down
```

### Common Issues

**Problem:** Camera doesn't rotate when holding RMB
- **Check:** Is `bRightMouseDown` being set? (add breakpoint in `OnRightMouseDown()`)
- **Check:** Is `GetCursorPos()` working? (Windows-specific API)
- **Check:** Is mouse delta being calculated? (verify in `Tick()`)

**Problem:** Speed changes too slowly/quickly
- **Solution:** Adjust `SpeedAdjustmentFactor` in `PIEFreeFlyPawn.h`
- Lower = finer control (e.g., 1.1f = 10% per scroll)
- Higher = coarser control (e.g., 1.5f = 50% per scroll)

**Problem:** Default speed still too slow/fast
- **Solution:** Change `MoveSpeed` default in `PIEFreeFlyPawn` constructor
- Currently 5000 units/sec (was 1000)

**Problem:** Mouse too sensitive/insensitive
- **Solution:** Adjust `MouseSensitivity` in `PIEPlayerController.h`
- Currently 0.15f (was 0.1f)
- Lower = slower rotation, Higher = faster rotation

## Future Enhancement Ideas

1. **Toggle Mode:** Add key to toggle between "always rotate" and "RMB to rotate"
2. **Smooth Speed:** Interpolate speed changes instead of instant jumps
3. **FOV Adjustment:** Use Ctrl+Wheel to adjust camera FOV
4. **Speed Presets:** Number keys (1-5) for instant speed presets
5. **Mouse Cursor Hide:** Hide cursor when RMB held for immersive look
6. **Speed HUD:** Display current speed on-screen overlay

## Comparison: Editor vs PIE Camera

| Feature | Editor Viewport | PIE Free-Fly |
|---------|----------------|--------------|
| Movement | WASD + Mouse | WASD + RMB+Mouse |
| Vertical | QE | QE |
| Speed | Fixed | **Mouse Wheel Adjustable** |
| Rotation | Always on | **RMB Toggle** |
| Camera | EditorViewportClient | UCameraComponent |
| Purpose | Scene editing | Runtime testing |

## Performance Notes

- **Negligible overhead:** Mouse tracking only active when RMB held
- **No allocations:** Uses stack variables and member tracking
- **No GC pressure:** Pure input handling, no object creation
- **Frame-independent:** All movement scaled by DeltaTime

## Configuration Quick Reference

Want to tweak the feel? Edit these values:

```cpp
// In PIEFreeFlyPawn constructor:
MoveSpeed = 5000.0f;              // Starting speed (was 1000, now 5x faster)
MinMoveSpeed = 100.0f;             // Slowest speed
MaxMoveSpeed = 50000.0f;           // Fastest speed
SpeedAdjustmentFactor = 1.2f;      // Change per wheel notch

// In PIEPlayerController constructor:
MouseSensitivity = 0.15f;          // Camera rotation sensitivity (was 0.1)
```

**Quick Tuning Recipes:**

- **Slower default:** `MoveSpeed = 2000.0f`
- **Faster default:** `MoveSpeed = 10000.0f`
- **Finer speed control:** `SpeedAdjustmentFactor = 1.1f`
- **Coarser speed control:** `SpeedAdjustmentFactor = 1.5f`
- **More sensitive mouse:** `MouseSensitivity = 0.25f`
- **Less sensitive mouse:** `MouseSensitivity = 0.1f`

## Testing Checklist

- [ ] Enter PIE mode successfully
- [ ] WASD movement works at good speed
- [ ] QE vertical movement works
- [ ] Hold RMB - console shows "Camera rotation enabled"
- [ ] Move mouse while RMB held - camera rotates smoothly
- [ ] Release RMB - console shows "Camera rotation disabled"
- [ ] Move mouse without RMB - camera doesn't rotate
- [ ] Scroll wheel up - speed increases, console shows new speed
- [ ] Scroll wheel down - speed decreases, console shows new speed
- [ ] Movement uses new speed immediately
- [ ] Shift key doubles current speed
- [ ] Can combine all features (RMB + WASD + Wheel + Shift)

## Conclusion

These enhancements provide professional-grade camera controls for PIE mode, matching the quality and feel of modern game engines. The right-mouse rotation toggle gives users precise control over when camera rotation is active, while the mouse wheel speed adjustment enables seamless transition between detailed inspection and fast scene traversal - perfect for testing cloth simulations from various distances and angles.
