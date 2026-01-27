# Cloth Configuration Panel Implementation

## Overview
Added an ImGui-based runtime UI panel for modifying FClothConfig parameters during PIE mode, enabling real-time cloth simulation tuning without restarting the game.

## Files Created

### 1. ClothConfigPanel.h
**Location:** `EngineSIU/EngineSIU/Engine/Source/Editor/PropertyEditor/ClothConfigPanel.h`

Header file defining the ClothConfigPanel class that inherits from UEditorPanel.

### 2. ClothConfigPanel.cpp
**Location:** `EngineSIU/EngineSIU/Engine/Source/Editor/PropertyEditor/ClothConfigPanel.cpp`

Implementation providing:
- Runtime access to ClothWorld and FClothBatchedSolver
- ImGui controls for all FClothConfig parameters
- Immediate application of changes to active simulation
- Statistics display
- Reset to defaults functionality

## Files Modified

### 3. UnrealEd.cpp
**Location:** `EngineSIU/EngineSIU/Engine/Source/Editor/UnrealEd/UnrealEd.cpp`

Added:
- Include for ClothConfigPanel.h
- Panel registration in Initialize() method

### 4. EngineSIU.vcxproj
**Location:** `EngineSIU/EngineSIU/EngineSIU.vcxproj`

Added:
- ClothConfigPanel.cpp to ClCompile entries
- ClothConfigPanel.h to ClInclude entries

### 5. EngineSIU.vcxproj.filters
**Location:** `EngineSIU/EngineSIU/EngineSIU.vcxproj.filters`

Added proper filtering for ClothConfigPanel files in PropertyEditor folder.

## Features Implemented

### Exposed FClothConfig Parameters

#### Global Settings Section
- **Mass** (0.1 - 10.0): Particle mass
- **Damping** (0.0 - 1.0): Velocity damping
- **Friction** (0.0 - 1.0): Surface friction

#### Constraint Stiffness Section
- **Stretch Stiffness** (0.0 - 1.0): Fabric stretching resistance
- **Bend Stiffness** (0.0 - 1.0): Fabric bending/folding resistance
- **Attach Stiffness** (0.0 - 1.0): Attachment point stiffness
- **Long Range Stretchiness** (1.0 - 2.0): LRA slack multiplier

#### Solver Settings Section
- **Iterations** (1 - 10): Constraint solver iterations per substep
- **Substeps** (1 - 10): Simulation substeps per frame
- **Fixed Substep Time** (1/1200 - 1/60): Target time per substep
- **Max Substeps/Frame** (1 - 20): Safety limit for performance
- **Max Speed** (100 - 5000 cm/s): Velocity clamping
- **Relaxation Factor** (0.5 - 1.5): Jacobi solver convergence control
- **Use XPBD** (checkbox): Toggle Extended Position-Based Dynamics

#### Wind & Forces Section
- **Air Drag** (0.0 - 5.0): Air resistance
- **Wind Strength** (0.0 - 10.0): Wind force multiplier

#### Collision Section
- **Collision Thickness** (0.001 - 1.0): Collision margin
- **Enable Self Collision** (checkbox): Toggle self-collision

### Additional Features

#### Statistics Display
- Active instance count
- Total particle count
- Total constraint count
- Current system mode (Batched/Legacy)

#### Reset Functionality
- "Reset to Defaults" button to restore default FClothConfig values

## Technical Implementation

### Panel Activation
- Only renders in PIE mode (Play-in-Editor)
- Set via `SetSupportedWorldTypes(EWorldTypeBitFlag::PIE)`

### Data Flow
1. Panel accesses `GEngine->ClothPhysicsManager`
2. Retrieves `FClothWorld` for active world
3. Gets `FClothBatchManager` for LOD_0
4. Accesses `FClothBatchedSolver` to get/set config
5. Changes tracked via `bConfigChanged` flag
6. Applied immediately via `Solver->SetConfig()`

### UI Organization
- Collapsible tree sections for logical grouping
- Tooltips on hover for parameter descriptions
- Sliders for numeric values with appropriate ranges
- Checkboxes for boolean flags
- Color-coded status messages for error states

## Usage

### How to Use
1. Build the project in Visual Studio
2. Run the editor
3. Enter PIE mode (Play-in-Editor)
4. The "Cloth Configuration" panel will appear automatically
5. Adjust sliders and checkboxes to tune simulation in real-time
6. Changes apply immediately to all cloth instances

### Panel Controls
- The panel can be moved and resized (first use only, then position is remembered)
- Close the panel using the X button in title bar
- Hover over parameters to see tooltips with descriptions

## Integration Architecture

```
EngineLoop::Tick()
  └─> UnrealEditor->Render()
      └─> For each Panel in Panels
          └─> If Panel.SupportedWorldTypes includes current WorldType
              └─> Panel->Render()
                  └─> ClothConfigPanel::Render() [PIE mode only]
                      └─> ClothConfigPanel::RenderClothConfig()
                          └─> Access FClothWorld
                          └─> Access FClothBatchedSolver
                          └─> Get/Set FClothConfig
```

## Benefits
- **Zero iteration time**: Tune parameters while simulation is running
- **Immediate feedback**: See changes take effect instantly
- **Organized UI**: Parameters grouped logically by function
- **Safe defaults**: Easy reset to known-good configuration
- **PIE-only**: Doesn't clutter editor mode interface

## Notes
- All instances in a batch share the same FClothConfig
- Changes persist only during the current PIE session
- Panel follows existing EngineSIU editor panel patterns
- Compatible with batched cloth simulation architecture
