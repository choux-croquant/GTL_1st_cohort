# Cloth Attachment Test Actor - Usage Guide

## Overview

[`ATestClothAttachmentActor`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothAttachmentActor.h) is a ready-to-use test actor that demonstrates cloth attachment to a moving component. It creates a cloth that is attached to a rotating and bobbing pole, showing how cloth follows component motion in real-time.

## Features

- **Automatic Setup**: No manual configuration needed - just spawn and run
- **Visual Demonstration**: Pole rotates and moves up/down, cloth follows
- **Attachment System**: Top vertices of cloth are attached to the pole component
- **PIE Ready**: Works immediately in Play-In-Editor mode

## Quick Start

### Method 1: Spawn from Code

```cpp
// In your world setup or test code
ATestClothAttachmentActor* TestActor = World->SpawnActor<ATestClothAttachmentActor>();
TestActor->SetActorLocation(FVector(0.0f, 0.0f, 100.0f));
TestActor->PostSpawnInitialize();
```

### Method 2: Spawn from Editor Console

```
SpawnActor ATestClothAttachmentActor 0 0 100
```

## What It Does

### Initialization Sequence

1. **Creates Pole Component**
   - Loads `Contents/pole/pole.obj` mesh
   - Sets up as a `UStaticMeshComponent`
   - Positioned at actor location

2. **Creates Cloth Component**
   - Loads `Contents/TestClothMesh/TestClothMesh.obj` as source
   - Generates cloth asset with QEM decimation (15% reduction)
   - Positioned 100 units to the side of pole
   - Configures physics parameters:
     - Stretch stiffness: 0.9
     - Bend stiffness: 0.1
     - Area stiffness: 0.001
     - Total mass: 1.0

3. **Generates Cloth Asset**
   - Decimates render mesh to ~400 simulation vertices
   - Generates distance, bend, and area constraints
   - Creates skinning weights for render mesh

4. **Registers with Cloth World**
   - Adds cloth to batched simulation system
   - Starts simulation

5. **Attaches Cloth to Pole**
   - Finds top 5% of vertices (by Z coordinate)
   - Binds each vertex to pole component using `BindAttachmentToComponent`
   - Uses hard kinematic constraints (stiffness=1.0, distance=0.0)

### Runtime Animation

The actor continuously animates the pole:

- **Rotation**: 30 degrees per second around Z axis
- **Vertical Motion**: Sine wave bobbing (1 Hz, ±50 cm amplitude)

The attached cloth vertices follow the pole motion, while the rest of the cloth simulates freely.

## Code Structure

### Header File: [`TestClothAttachmentActor.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothAttachmentActor.h)

```cpp
class ATestClothAttachmentActor : public AActor
{
    DECLARE_CLASS(ATestClothAttachmentActor, AActor)

public:
    ATestClothAttachmentActor();
    virtual void PostSpawnInitialize() override;
    virtual void Tick(float DeltaTime) override;

private:
    UStaticMeshComponent* PoleComponent;
    UClothMeshComponent* ClothComponent;
    float AnimationTime;
    bool bInitialized;
};
```

### Implementation: [`TestClothAttachmentActor.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothAttachmentActor.cpp)

Key methods:

- **`PostSpawnInitialize()`**: Sets up all components and attachments
- **`Tick(float DeltaTime)`**: Animates the pole component

## Attachment API Usage Example

The test actor demonstrates the new attachment API:

```cpp
// Find top vertices to attach
TArray<uint32> TopVertices;
float MaxZ = -FLT_MAX;
for (const FVector& Pos : ClothAsset->RestPositions)
{
    MaxZ = FMath::Max(MaxZ, Pos.Z);
}
float AttachThreshold = MaxZ - (ZRange * 0.05f);  // Top 5%

for (int32 i = 0; i < ClothAsset->RestPositions.Num(); ++i)
{
    if (ClothAsset->RestPositions[i].Z >= AttachThreshold)
    {
        TopVertices.Add(i);
    }
}

// Attach each vertex to pole component
for (uint32 VertexIndex : TopVertices)
{
    // Calculate local offset from pole center
    FVector VertexWorldPos = ClothComponent->GetWorldLocation() + ClothAsset->RestPositions[VertexIndex];
    FVector PoleWorldPos = PoleComponent->GetWorldLocation();
    FVector LocalOffset = VertexWorldPos - PoleWorldPos;
    
    FTransform LocalTransform;
    LocalTransform.SetLocation(LocalOffset);
    LocalTransform.SetRotation(FQuat::Identity);
    LocalTransform.SetScale3D(FVector::OneVector);
    
    // Bind attachment
    ClothComponent->BindAttachmentToComponent(
        VertexIndex,           // Which simulation vertex to attach
        PoleComponent,         // Target component to follow
        LocalTransform,        // Local offset from component
        1.0f,                  // Stiffness (1.0 = hard constraint)
        0.0f                   // AttachDistance (0.0 = kinematic, no stretch)
    );
}
```

## Expected Behavior

When running in PIE mode, you should see:

1. **Pole**: Rotating continuously and bobbing up/down
2. **Cloth**: Top edge attached to pole, following its motion
3. **Simulation**: Rest of cloth simulating freely with gravity and constraints
4. **No Artifacts**: Smooth motion, no stretching or tearing at attachment points

## Troubleshooting

### Cloth Not Appearing

**Check:**
- Mesh files exist: `Contents/pole/pole.obj` and `Contents/TestClothMesh/TestClothMesh.obj`
- Console logs for errors during initialization
- ClothWorld is initialized for the current world

**Solution:**
```cpp
// Verify cloth world exists
FClothWorld* ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(GetWorld());
if (!ClothWorld)
{
    UE_LOG(ELogLevel::Error, TEXT("ClothWorld not initialized!"));
}
```

### Cloth Not Following Pole

**Check:**
- Attachments were created (check console logs)
- Attachment count: `ClothComponent->GetAttachmentCount()`
- Pole component is actually moving

**Solution:**
```cpp
// Verify attachments
int32 AttachmentCount = ClothComponent->GetAttachmentCount();
UE_LOG(ELogLevel::Display, TEXT("Attachment count: %d"), AttachmentCount);

// Check if attachments are active
for (const FClothAttachmentBinding& Binding : ClothComponent->GetAttachmentBindings())
{
    UE_LOG(ELogLevel::Display, TEXT("Vertex %d attached: %s"), 
           Binding.SimVertexIndex, 
           Binding.bIsActive ? TEXT("Yes") : TEXT("No"));
}
```

### Cloth Stretching at Attachment Points

**Possible Causes:**
- Stiffness too low (should be 1.0 for hard constraints)
- AttachDistance > 0 (allows stretching)
- Local offset incorrect

**Solution:**
```cpp
// Use hard kinematic constraints
ClothComponent->BindAttachmentToComponent(
    VertexIndex,
    PoleComponent,
    LocalTransform,
    1.0f,    // Hard constraint
    0.0f     // No stretch allowed
);
```

## Customization

### Change Animation Speed

```cpp
// In Tick()
float RotationSpeed = 60.0f;  // Faster rotation (was 30.0f)
float BobSpeed = 2.0f;         // Faster bobbing (was 1.0f)
```

### Attach Different Vertices

```cpp
// Attach bottom vertices instead
float AttachThreshold = MinZ + (ZRange * 0.05f);  // Bottom 5%
```

### Change Attachment Stiffness

```cpp
// Softer attachment (allows some stretch)
ClothComponent->BindAttachmentToComponent(
    VertexIndex,
    PoleComponent,
    LocalTransform,
    0.5f,    // Softer (0.0-1.0)
    10.0f    // Allow 10cm stretch
);
```

### Use Different Meshes

```cpp
// Change mesh paths in PostSpawnInitialize()
FString PoleMeshName = "Contents/YourMesh/pole.obj";
FString ClothMeshName = "Contents/YourMesh/cloth.obj";
```

## Performance Notes

- **Single Cloth Instance**: ~400 simulation vertices
- **Attachment Count**: Typically 10-20 vertices (top 5%)
- **Frame Time**: < 0.1ms for single instance
- **Batched**: Can run 100+ instances efficiently

## Related Documentation

- [Cloth Attachment System Architecture](cloth-attachment-editor-architecture.md)
- [Cloth Attachment Phase 2 Complete](../plans/cloth-attachment-phase2-complete.md)
- [Cloth Attachment Refactoring](../plans/cloth-attachment-refactoring-final.md)
- [Cloth Save/Load Implementation Plan](../plans/cloth-save-load-implementation-plan.md)

## API Reference

### UClothComponent::BindAttachmentToComponent

```cpp
void BindAttachmentToComponent(
    uint32 SimVertexIndex,              // Which simulation vertex to attach
    USceneComponent* TargetComponent,   // Component to follow
    const FTransform& LocalOffset,      // Offset from component origin
    float Stiffness = -1.0f,            // Constraint stiffness (1.0 = hard)
    float AttachDistance = 0.0f         // Allowed stretch distance (0.0 = kinematic)
);
```

**Parameters:**
- `SimVertexIndex`: Index into cloth asset's `RestPositions` array
- `TargetComponent`: Any `USceneComponent` (StaticMesh, SkeletalMesh, etc.)
- `LocalOffset`: Transform relative to component's origin
- `Stiffness`: 0.0 (soft) to 1.0 (hard), -1.0 uses asset default
- `AttachDistance`: 0.0 for kinematic (no stretch), >0 for Long Range Attachment

**Returns:** void

**Side Effects:**
- Modifies `RuntimeInvMasses` for attached vertex (sets to 0.0)
- Adds entry to `AttachmentBindings` array
- Marks attachments dirty for GPU update

## Conclusion

[`ATestClothAttachmentActor`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/Actors/TestClothAttachmentActor.h) provides a complete, working example of cloth attachment that requires no additional setup. It demonstrates:

- Component-based attachment
- Runtime attachment binding
- Kinematic constraint configuration
- Proper initialization sequence

Use this as a reference for implementing cloth attachment in your own actors and systems.
