# Cloth Attachment System Implementation

## Overview

This document describes the cloth attachment functionality that allows cloth simulation vertices to be attached to moving objects (e.g., a flag attached to a pole, a cape attached to character shoulders).

## Implementation Summary

### Files Modified/Created

1. **Shader Implementation** (NEW)
   - `EngineSIU/EngineSIU/Shaders/Cloth/ClothApplyKinematicTargets.hlsl`
     - Compute shader that applies kinematic constraints to cloth particles
     - Snaps attached vertices to target positions each frame
     - Supports both hard (stiffness=1.0) and soft (stiffness<1.0) attachments

2. **Test Actor Implementation** (MODIFIED)
   - `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothActor.h`
   - `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothActor.cpp`
     - Added attachment driver spawning
     - Added scripted motion for attachment testing
     - Added runtime attachment position updates

3. **Existing Infrastructure** (Already in place)
   - CPU Side:
     - `ClothSimulationData.h`: Defines `FClothAttachmentData` structure
     - `ClothGPUStructs.h`: Defines `FClothKinematicTargetGPU` GPU-compatible structure
     - `ClothSolver.cpp`: `UpdateKinematicTargets()` converts attachments to GPU format
     - `ClothInstance.cpp`: `UpdateAttachments()` updates attachment data each frame
   
   - GPU Side:
     - `ClothCommon.hlsli`: Defines `FKinematicTarget` shader structure
     - Kinematic target buffer and SRV already created in ClothSolver
     - Shader already loaded and dispatched in simulation loop

## Architecture

### Attachment Data Flow

```
1. Asset Creation (CreateTestCloth):
   ClothAsset->AddAttachmentData(FClothAttachmentData)
   
2. Instance Initialization (BeginPlay):
   CachedAttachments = ClothAsset->GetAttachmentData()
   
3. Runtime Update (Each Frame):
   UpdateAttachments() 
   -> Updates CachedAttachments[i].WorldPosition
   -> ClothInstance->UpdateAttachments(CachedAttachments)
   -> ClothSolver->UpdateKinematicTargets(Attachments)
   -> Uploads FClothKinematicTargetGPU to GPU buffer
   
4. GPU Simulation:
   Integration -> ApplyKinematicTargets -> Constraints -> ApplyKinematicTargets
```

### Attachment Types

The system supports three attachment types (defined in `EClothAttachmentType`):

1. **WorldPosition**: Static world-space position
   - Used for fixed attachment points
   - WorldPosition is constant

2. **ActorTransform**: Follow an actor's transform
   - Attachment moves with the actor
   - WorldPosition = ActorPosition + LocalOffset

3. **SkeletalBone**: Follow a skeletal mesh bone
   - Attachment follows bone transform
   - Requires skeletal mesh component reference

## Test Implementation

The `TestClothActor` demonstrates the attachment system:

### Setup (BeginPlay)

1. **Creates a 4x4 cloth grid**
   - Top-left corner (vertex 0) is set as attachment point
   - Other vertices simulate normally

2. **Spawns an AttachmentDriver** (AStaticMeshActor)
   - Positioned above the cloth
   - Acts as the moving attachment point

3. **Caches attachment data**
   - Stores attachments from asset for runtime updates

### Runtime Behavior (Tick)

1. **Animates the driver**
   - Sinusoidal horizontal motion: `sin(time * 2.0) * 10cm`
   - Sinusoidal vertical motion: `cos(time * 1.5) * 5cm`

2. **Updates attachment positions**
   - Reads driver's current world position
   - Updates `CachedAttachments[i].WorldPosition`
   - Sends to cloth instance for GPU upload

3. **GPU processes attachments**
   - `ApplyKinematicTargetsCS` shader enforces constraints
   - Attached vertices snap to target positions
   - Rest of cloth simulates naturally

## How to Use the Attachment System

### 1. Define Attachments in Asset

```cpp
// In your cloth setup function
FClothAttachmentData attachment;
attachment.Type = EClothAttachmentType::WorldPosition;
attachment.ClothVertexIndex = 0; // Which cloth vertex to attach
attachment.WorldPosition = FVector(0, 0, 100); // Target position
attachment.Stiffness = 1.0f; // 1.0 = hard, <1.0 = soft spring
attachment.bIsKinematic = true;

ClothAsset->AddAttachmentData(attachment);
```

### 2. Update Attachments at Runtime

```cpp
// Get current attachments
TArray<FClothAttachmentData> attachments = ClothAsset->GetAttachmentData();

// Update positions (e.g., to follow a moving object)
for (FClothAttachmentData& attach : attachments)
{
    if (attach.Type == EClothAttachmentType::ActorTransform)
    {
        attach.WorldPosition = MyActor->GetActorLocation() + attach.LocalOffset.GetTranslation();
    }
}

// Send to cloth instance
ClothInstance->UpdateAttachments(attachments);
```

### 3. Changing Attachment Driver

To use a different StaticMesh as the attachment driver:

**Option A: Set a specific mesh asset**
```cpp
// In BeginPlay, after spawning AttachmentDriver
UStaticMeshComponent* meshComp = AttachmentDriver->GetStaticMeshComponent();
if (meshComp)
{
    // Load and assign your mesh
    UStaticMesh* myMesh = LoadObject<UStaticMesh>(nullptr, TEXT("Path/To/Your/Mesh.obj"));
    meshComp->SetStaticMesh(myMesh);
}
```

**Option B: Spawn a different actor type**
```cpp
// Instead of AStaticMeshActor, spawn your custom actor
MyCustomActor* customDriver = world->SpawnActor<MyCustomActor>(...);
AttachmentDriver = Cast<AStaticMeshActor>(customDriver);
```

### 4. Modifying Attachment Points

To change which cloth vertices are attached:

```cpp
// In CreateTestCloth(), modify the attachment section:

// Attach top-left AND top-right corners
FClothAttachmentData leftCorner;
leftCorner.ClothVertexIndex = 0; // Top-left
leftCorner.WorldPosition = positions[0];
leftCorner.Stiffness = 1.0f;
attachments.Add(leftCorner);

FClothAttachmentData rightCorner;
rightCorner.ClothVertexIndex = 3; // Top-right (for 4x4 grid)
rightCorner.WorldPosition = positions[3];
rightCorner.Stiffness = 1.0f;
attachments.Add(rightCorner);
```

### 5. Adjusting Motion Script

To change how the attachment driver moves:

```cpp
// In Tick(), modify the animation section:

// Linear motion instead of sinusoidal
float speed = 10.0f; // cm per second
FVector movement = FVector(0.0f, speed * AnimationTime, 0.0f);
FVector newPosition = DriverInitialPosition + movement;

// Or circular motion
float radius = 15.0f;
float angularSpeed = 2.0f;
FVector circular = FVector(
    0.0f,
    radius * FMath::Cos(AnimationTime * angularSpeed),
    radius * FMath::Sin(AnimationTime * angularSpeed)
);
FVector newPosition = DriverInitialPosition + circular;
```

## GPU Shader Details

### ClothApplyKinematicTargets.hlsl

**Execution**: 64 threads per group, dispatched once per kinematic target

**Process**:
1. Read kinematic target data (particle index, target position, stiffness)
2. Read current particle position from cloth buffer
3. Interpolate between current and target position based on stiffness:
   - `newPos = lerp(currentPos, targetPos, stiffness)`
   - Stiffness = 1.0: Full snap (hard constraint)
   - Stiffness < 1.0: Soft spring behavior
4. Write updated position back to particle buffer

**Timing**: Called twice per simulation frame:
- Once after integration (before constraints)
- Once after constraint solving (to enforce attachment)

## Performance Considerations

- **GPU Buffer Updates**: Attachment data is uploaded every frame using MAP_WRITE_DISCARD
- **Constraint Priority**: Kinematic targets override physics constraints
- **Memory**: Kinematic target buffer sized at 10% of particles or minimum 16

## Debugging

To verify attachment system:

1. **Check attachment count**:
   ```cpp
   UE_LOG(ELogLevel::Display, TEXT("Attachments: %d"), CachedAttachments.Num());
   ```

2. **Verify position updates**:
   ```cpp
   for (const FClothAttachmentData& attach : CachedAttachments)
   {
       UE_LOG(ELogLevel::Display, TEXT("Vertex %d at %s"), 
              attach.ClothVertexIndex, 
              *attach.WorldPosition.ToString());
   }
   ```

3. **Visual indicators**: Add debug drawing for attachment points

## Testing in PIE

1. Open the project in Visual Studio
2. Build the solution (Debug or Release configuration)
3. Launch the application
4. Enter Play-In-Editor (PIE) mode
5. Observe:
   - Cloth spawns at origin
   - Attachment driver appears above cloth
   - Driver oscillates left-right and up-down
   - Cloth top-left corner follows driver
   - Rest of cloth simulates naturally

## Future Enhancements

Possible extensions to the attachment system:

1. **Multiple drivers**: Support attachments to different actors
2. **Socket attachments**: Attach to named sockets on skeletal meshes
3. **Bone following**: Implement skeletal bone attachment type
4. **Distance constraints**: Limit how far attached vertices can move
5. **Soft constraint curves**: Non-linear stiffness falloff
6. **Attachment blending**: Smooth transition between attachment states

## Summary

The cloth attachment system is now fully functional:
- ✅ Shader implementation (`ClothApplyKinematicTargets.hlsl`)
- ✅ CPU-side attachment management (existing infrastructure)
- ✅ GPU buffer and dispatch integration (existing in `ClothSolver`)
- ✅ Test actor with dynamic attachment driver (`TestClothActor`)
- ✅ Scripted motion demonstration
- ✅ Runtime attachment position updates

The system allows cloth vertices to follow moving objects while maintaining natural cloth simulation for non-attached vertices.
