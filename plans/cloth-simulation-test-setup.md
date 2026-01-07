# Cloth Simulation - Test Setup Guide

## How to Test the Cloth Simulation System

### Overview
This guide shows how to create a simple cloth simulation test in the EngineSIU editor.

---

## Method 1: Code-Based Setup (Recommended for Testing)

### Step 1: Create a Test Cloth Actor

Create a new actor class for testing:

**File**: `EngineSIU/Engine/Contents/Actors/TestClothActor.h`

```cpp
#pragma once
#include "GameFramework/Actor.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"

class ATestClothActor : public AActor
{
    DECLARE_CLASS(ATestClothActor, AActor)
    
public:
    ATestClothActor();
    virtual void BeginPlay() override;
    
    PROPERTY()
    UClothMeshComponent* ClothMesh;
    
    PROPERTY()
    UClothAsset* ClothAsset;
    
    // Create a simple test cloth (10x10 grid)
    void CreateTestCloth();
};
```

**File**: `EngineSIU/Engine/Contents/Actors/TestClothActor.cpp`

```cpp
#include "TestClothActor.h"
#include "Math/Vector.h"

IMPLEMENT_CLASS(ATestClothActor)

ATestClothActor::ATestClothActor()
{
    // Create cloth mesh component
    ClothMesh = CreateDefaultSubobject<UClothMeshComponent>(TEXT("ClothMesh"));
    RootComponent = ClothMesh;
    
    // Create cloth asset
    ClothAsset = NewObject<UClothAsset>(this, TEXT("TestClothAsset"));
}

void ATestClothActor::BeginPlay()
{
    Super::BeginPlay();
    
    // Generate simple test cloth
    CreateTestCloth();
    
    // Assign to component
    ClothMesh->SetClothAsset(ClothAsset);
    ClothMesh->StartSimulation();
}

void ATestClothActor::CreateTestCloth()
{
    const int32 GridSize = 10;
    const float Spacing = 10.0f;  // 10 cm between particles
    
    TArray<FVector> positions;
    TArray<uint32> indices;
    TArray<float> invMasses;
    
    // Generate grid vertices
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            FVector pos;
            pos.X = x * Spacing;
            pos.Y = y * Spacing;
            pos.Z = 0.0f;
            
            positions.Add(pos);
            
            // Top row is fixed (invMass = 0)
            float invMass = (y == 0) ? 0.0f : 1.0f;
            invMasses.Add(invMass);
        }
    }
    
    // Generate triangle indices
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);
            
            // Triangle 1
            indices.Add(i0);
            indices.Add(i1);
            indices.Add(i2);
            
            // Triangle 2
            indices.Add(i1);
            indices.Add(i3);
            indices.Add(i2);
        }
    }
    
    // Generate distance constraints
    TArray<FClothConstraint> constraints;
    
    // Horizontal and vertical springs
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            int32 idx = y * GridSize + x;
            
            // Horizontal constraint
            if (x < GridSize - 1)
            {
                int32 neighborIdx = y * GridSize + (x + 1);
                float restLength = (positions[neighborIdx] - positions[idx]).Size();
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 0.9f));
            }
            
            // Vertical constraint
            if (y < GridSize - 1)
            {
                int32 neighborIdx = (y + 1) * GridSize + x;
                float restLength = (positions[neighborIdx] - positions[idx]).Size();
                constraints.Add(FClothConstraint(idx, neighborIdx, restLength, 0.9f));
            }
        }
    }
    
    // Set cloth asset data
    ClothAsset->SetRestPositions(positions);
    ClothAsset->SetIndices(indices);
    ClothAsset->SetInvMasses(invMasses);
    
    for (const FClothConstraint& constraint : constraints)
    {
        ClothAsset->AddDistanceConstraint(constraint);
    }
    
    // Configure simulation parameters
    FClothConfig config;
    config.Mass = 1.0f;
    config.Damping = 0.05f;
    config.StretchStiffness = 0.9f;
    config.NumIterations = 5;
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;
    
    ClothAsset->SetConfig(config);
}
```

### Step 2: Add to Project

Add these files to `EngineSIU.vcxproj`:

```xml
<!-- In <ItemGroup> with other ClCompile items -->
<ClCompile Include="Engine\Contents\Actors\TestClothActor.cpp" />

<!-- In <ItemGroup> with other ClInclude items -->
<ClInclude Include="Engine\Contents\Actors\TestClothActor.h" />
```

### Step 3: Spawn in Scene

In your main game mode or world setup, spawn the actor:

```cpp
// In your world/level initialization code:
ATestClothActor* ClothActor = NewObject<ATestClothActor>(World);
ClothActor->SetActorLocation(FVector(0, 0, 200)); // 200cm above ground
World->SpawnActor(ClothActor);
```

---

## Method 2: Editor-Based Setup (Future Enhancement)

Once the asset pipeline is implemented (Phase 5), you'll be able to:

1. **Import Static Mesh** → Right-click in content browser
2. **Convert to Cloth Asset** → Select "Create Cloth Asset"
3. **Add to Actor**:
   - Select actor in outliner
   - Add `ClothMeshComponent` in details panel
   - Assign cloth asset
   - Press Play

---

## Expected Behavior

### What You Should See:
1. **A 10x10 grid cloth** appears in the viewport
2. **Top row is fixed** (pinned)
3. **Cloth falls under gravity** like a curtain
4. **Distance constraints** prevent stretching
5. **Real-time simulation** at 60 FPS

### Debug Visualization:
```cpp
// Enable debug draw to see particles and constraints
ClothMesh->SetDebugDrawMode(EClothDebugDrawMode::Particles);
ClothMesh->SetDebugDrawEnabled(true);
```

---

## Simulation Parameters

### Adjust in Code:
```cpp
FClothConfig config;
config.StretchStiffness = 0.9f;   // Higher = stiffer cloth
config.Damping = 0.05f;           // Higher = more damping
config.NumIterations = 5;         // More = more accurate
config.TimeStep = 0.016f;         // Fixed 60fps
```

### Adjust Wind:
```cpp
ClothMesh->SetWind(FVector(100, 0, 0));  // Wind in X direction
```

### Adjust Gravity:
```cpp
ClothMesh->SetGravity(FVector(0, 0, -980));  // Standard gravity
```

---

## Troubleshooting

### Cloth Not Visible
- Check that `bIsVisible` is true
- Verify cloth asset has valid data
- Check that material is assigned

### Cloth Not Simulating
- Verify `bIsSimulating` is true
- Check that solver initialized successfully
- Look for error logs in console

### Cloth Exploding/Unstable
- Reduce TimeStep (e.g., 0.01f)
- Increase NumIterations (e.g., 10)
- Reduce StretchStiffness (e.g., 0.5f)
- Add more damping (e.g., 0.1f)

### Performance Issues
- Reduce grid size (e.g., 5x5 instead of 10x10)
- Reduce NumIterations
- Check GPU profiler for bottlenecks

---

## Verification Checklist

- [ ] TestClothActor compiles
- [ ] Actor spawns in scene
- [ ] Cloth component initializes
- [ ] Solver creates GPU resources
- [ ] Compute shaders compile successfully
- [ ] Cloth renders in viewport
- [ ] Simulation runs at 60 FPS
- [ ] Top row stays fixed
- [ ] Bottom rows fall naturally
- [ ] Constraints maintain shape

---

## Next Steps After Successful Test

1. **Add Materials** - Assign a material to see proper shading
2. **Test Wind** - Add wind forces to see cloth movement
3. **Add Collision** - Implement Phase 7 for collision response
4. **Skeletal Attachment** - Implement Phase 6 for character cloth
5. **Asset Pipeline** - Implement Phase 5 to convert static meshes

---

## Console Commands (Future)

```
// Toggle cloth simulation
cloth.pause
cloth.reset

// Adjust parameters at runtime
cloth.stiffness 0.9
cloth.damping 0.05
cloth.iterations 10

// Debug visualization
cloth.debug particles
cloth.debug constraints
cloth.debug normals
```

---

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Particle Count | 100 (10x10) | Initial test |
| Frame Rate | 60 FPS | Should maintain |
| GPU Time | < 0.5ms | For 100 particles |
| CPU Time | < 0.1ms | Minimal CPU overhead |

---

**Status**: ✅ Ready for Testing
**Version**: 1.0
**Last Updated**: 2026-01-07
