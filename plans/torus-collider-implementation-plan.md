# Torus Collider Implementation Plan

## Overview

This plan outlines the implementation of a Torus-shaped collider for cloth collision in the EngineSIU cloth simulation system. Since PhysX does not natively support torus shapes, this will be implemented as a custom collision shape with SDF-based collision detection in compute shaders.

## Architecture Analysis

### Current Collision System

The existing collision system follows this architecture:

```
┌─────────────────────────────────────────────────────────────┐
│                    Collision Flow                            │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  1. Registration Phase (CPU)                                 │
│     └─> ClothCollisionManager::RegisterCollider()           │
│         └─> Extract from PhysX shapes (Sphere/Capsule/Box)  │
│         └─> Store in FClothColliderSource (CPU tracking)    │
│                                                               │
│  2. Transform Update Phase (CPU)                             │
│     └─> ClothCollisionManager::UpdateTransforms()           │
│         └─> Track component transforms                       │
│         └─> Mark dirty colliders                             │
│                                                               │
│  3. GPU Upload Phase (CPU→GPU)                               │
│     └─> ClothCollisionManager::UploadToGPU()                │
│         └─> Convert to FClothColliderGPU                     │
│         └─> Upload to structured buffer                      │
│                                                               │
│  4. Collision Solve Phase (GPU)                              │
│     └─> ClothSDFCollision.hlsl::SolveCollisionsCS()         │
│         └─> Query SDF for each collider                      │
│         └─> Apply position corrections + friction            │
│                                                               │
│  5. Debug Rendering Phase (CPU/GPU)                          │
│     └─> EditorRenderPass::RenderClothColliders()            │
│         └─> Render wireframe visualization                   │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

1. **[`EClothColliderType`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:26)** - Enum defining collider types (Sphere=0, Capsule=1, Box=2)
2. **[`FClothColliderSource`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:38)** - CPU-side collider tracking structure
3. **[`FClothColliderGPU`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h:189)** - GPU-side collider data (64 bytes, aligned)
4. **[`FClothCollider`](EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli:251)** - HLSL shader structure (matches GPU struct)
5. **[`ClothSDFCollision.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSDFCollision.hlsl:1)** - Compute shader with SDF functions
6. **[`ClothCollisionManager`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:86)** - Manages collider registration and GPU upload
7. **[`EditorRenderPass::RenderClothColliders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:1017)** - Debug visualization

## Torus Collider Design

### Mathematical Definition

A torus is defined by:
- **Major Radius (R)**: Distance from torus center to tube center
- **Minor Radius (r)**: Radius of the tube itself
- **Center**: World-space position
- **Axis**: Up vector (torus plane normal)

### Torus SDF Function

```hlsl
// Signed Distance Function for Torus
// pos: query point in world space
// center: torus center
// axis: torus up vector (normalized)
// majorRadius: distance from center to tube center
// minorRadius: tube radius
float sdTorus(float3 pos, float3 center, float3 axis, float majorRadius, float minorRadius)
{
    // Transform to torus local space (axis-aligned)
    float3 localPos = pos - center;
    
    // Project onto torus plane (perpendicular to axis)
    float axisProj = dot(localPos, axis);
    float3 planePos = localPos - axis * axisProj;
    
    // Distance from center in plane
    float distInPlane = length(planePos);
    
    // 2D distance in (radial, vertical) space
    float2 q = float2(distInPlane - majorRadius, axisProj);
    
    return length(q) - minorRadius;
}
```

### Normal Calculation

```hlsl
float3 ComputeTorusNormal(float3 pos, float3 center, float3 axis, float majorRadius)
{
    float3 localPos = pos - center;
    float axisProj = dot(localPos, axis);
    float3 planePos = localPos - axis * axisProj;
    
    float distInPlane = length(planePos);
    float3 radialDir = SafeNormalizeWithFallback(planePos, float3(1, 0, 0));
    
    // Point on major circle closest to query point
    float3 torusRingPoint = radialDir * majorRadius;
    
    // Normal points from ring point to query point
    float3 normal = SafeNormalizeWithFallback(localPos - torusRingPoint, float3(0, 1, 0));
    
    return normal;
}
```

## Implementation Plan

### Phase 1: Data Structure Extensions

#### 1.1 Update [`EClothColliderType`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:26)

```cpp
enum class EClothColliderType : uint32
{
    Sphere = 0,
    Capsule = 1,
    Box = 2,
    Torus = 3,      // NEW
    Count           // Update count
};
```

#### 1.2 Extend [`FClothColliderSource`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:38)

Add torus-specific fields:
```cpp
struct FClothColliderSource
{
    // ... existing fields ...
    
    // Torus-specific (reuse existing fields where possible)
    // CachedRadius -> Minor radius (tube radius)
    // CachedExtents.X -> Major radius (ring radius)
    // CachedLocalAxis -> Torus up vector (axis)
    float CachedMajorRadius;  // NEW: Explicit major radius storage
};
```

**Note**: The existing 64-byte [`FClothColliderGPU`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h:189) structure has sufficient padding to accommodate torus parameters without breaking alignment:
- `Radius` → Minor radius (tube radius)
- `HalfHeight` → Major radius (ring radius) 
- `Axis` → Torus up vector
- `Center` → Torus center
- `Extents` → Unused (keep as zero)

#### 1.3 Update HLSL [`FClothCollider`](EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli:251)

No changes needed - structure already matches GPU layout.

### Phase 2: Compute Shader Implementation

#### 2.1 Add Torus SDF to [`ClothSDFCollision.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSDFCollision.hlsl:1)

Add after existing SDF functions (around line 66):

```hlsl
// Torus SDF
float sdTorus(float3 pos, float3 center, float3 axis, float majorRadius, float minorRadius)
{
    float3 localPos = pos - center;
    float axisProj = dot(localPos, axis);
    float3 planePos = localPos - axis * axisProj;
    float distInPlane = length(planePos);
    float2 q = float2(distInPlane - majorRadius, axisProj);
    return length(q) - minorRadius;
}
```

#### 2.2 Update [`QueryColliderSDF()`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSDFCollision.hlsl:72)

Add torus case after box handling (around line 126):

```hlsl
else if (collider.Type == 3)  // Torus
{
    float majorRadius = collider.HalfHeight;  // Reuse HalfHeight field
    float minorRadius = collider.Radius;
    
    dist = sdTorus(pos, collider.Center, collider.Axis, majorRadius, minorRadius);
    
    // Compute normal
    float3 localPos = pos - collider.Center;
    float axisProj = dot(localPos, collider.Axis);
    float3 planePos = localPos - collider.Axis * axisProj;
    float distInPlane = length(planePos);
    float3 radialDir = SafeNormalizeWithFallback(planePos, float3(1, 0, 0));
    float3 torusRingPoint = radialDir * majorRadius;
    normal = SafeNormalizeWithFallback(localPos - torusRingPoint, float3(0, 1, 0));
}
```

### Phase 3: Collision Manager Extensions

#### 3.1 Add Manual Torus Registration API

Add to [`ClothCollisionManager.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:86):

```cpp
/**
 * Manually add a torus collider
 * @param WorldCenter - Torus center in world space
 * @param Axis - Torus up vector (will be normalized)
 * @param MajorRadius - Distance from center to tube center
 * @param MinorRadius - Tube radius
 */
void AddTorusCollider(
    const FVector& WorldCenter,
    const FVector& Axis,
    float MajorRadius,
    float MinorRadius
);
```

#### 3.2 Implement in [`ClothCollisionManager.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp:1)

Add after [`AddBoxCollider()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp:157) (around line 171):

```cpp
void FClothCollisionManager::AddTorusCollider(
    const FVector& WorldCenter,
    const FVector& Axis,
    float MajorRadius,
    float MinorRadius)
{
    FClothColliderSource Source;
    Source.Type = EClothColliderType::Torus;
    Source.Component = nullptr;  // Manual collider
    Source.ElementIndex = -1;
    Source.CachedTransform = FTransform::Identity;
    Source.CachedLocalCenter = WorldCenter;
    Source.CachedLocalAxis = Axis.GetSafeNormal();
    Source.CachedRadius = MinorRadius;  // Tube radius
    Source.CachedExtents = FVector(MajorRadius, 0, 0);  // Major radius in X
    Source.bIsDirty = true;
    Source.GPUBufferIndex = ColliderSources.Num();
    
    ColliderSources.Add(Source);
    bGPUDirty = true;
}
```

#### 3.3 Update [`ConvertToGPU()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp:468)

Add torus case after box handling (around line 512):

```cpp
else if (Source.Type == EClothColliderType::Torus)
{
    // Transform center and axis to world space
    FVector WorldCenter = Source.CachedTransform.TransformPosition(Source.CachedLocalCenter);
    FVector WorldAxis = Source.CachedTransform.TransformVector(Source.CachedLocalAxis).GetSafeNormal();
    
    GPU.Center = WorldCenter;
    GPU.Axis = WorldAxis;
    GPU.Radius = Source.CachedRadius;  // Minor radius (tube)
    GPU.HalfHeight = Source.CachedExtents.X;  // Major radius (ring)
    GPU.Extents = FVector::ZeroVector;  // Unused
}
```

### Phase 4: Debug Rendering

#### 4.1 Create Torus Geometry Buffer

Add to [`EditorRenderPass::CreateBuffers()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:81):

```cpp
// Torus wireframe generation
TArray<FVector> TorusVertices;
TArray<uint32> TorusIndices;

const int MajorSegments = 32;  // Around the ring
const int MinorSegments = 16;  // Around the tube

// Generate vertices
for (int i = 0; i <= MajorSegments; ++i)
{
    float theta = (float)i / MajorSegments * 2.0f * PI;
    float cosTheta = cosf(theta);
    float sinTheta = sinf(theta);
    
    for (int j = 0; j <= MinorSegments; ++j)
    {
        float phi = (float)j / MinorSegments * 2.0f * PI;
        float cosPhi = cosf(phi);
        float sinPhi = sinf(phi);
        
        // Torus parametric equation (major=1, minor=0.25 for unit torus)
        float x = (1.0f + 0.25f * cosPhi) * cosTheta;
        float y = (1.0f + 0.25f * cosPhi) * sinTheta;
        float z = 0.25f * sinPhi;
        
        TorusVertices.Add(FVector(x, y, z));
    }
}

// Generate line indices (wireframe)
for (int i = 0; i < MajorSegments; ++i)
{
    for (int j = 0; j < MinorSegments; ++j)
    {
        int current = i * (MinorSegments + 1) + j;
        int next = current + MinorSegments + 1;
        
        // Major circle lines
        TorusIndices.Add(current);
        TorusIndices.Add(next);
        
        // Minor circle lines
        TorusIndices.Add(current);
        TorusIndices.Add(current + 1);
    }
}

BufferManager->CreateVertexBuffer<FVector>(TEXT("TorusVertexBuffer"), TorusVertices, OutVertexInfo, D3D11_USAGE_IMMUTABLE, 0);
BufferManager->CreateIndexBuffer<uint32>(TEXT("TorusIndexBuffer"), TorusIndices, OutIndexInfo);

Resources.Primitives.Torus.VertexInfo = OutVertexInfo;
Resources.Primitives.Torus.IndexInfo = OutIndexInfo;
```

#### 4.2 Add Torus Shader

Add to [`EditorRenderPass::CreateShaders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:41):

```cpp
AddShaderSet(L"Torus", "TorusVS", "TorusPS", LayoutPosOnly, ARRAYSIZE(LayoutPosOnly));
```

#### 4.3 Add Torus Constant Buffer

Add to [`EditorRenderPass.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.h:1):

```cpp
static constexpr UINT32 ConstantBufferSizeTorus = 100;
```

Add to [`RenderResources.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/RenderResources.h) (assumed location):

```cpp
struct FConstantBufferDebugTorus
{
    FMatrix WorldMatrix;  // Transform (includes scale for major/minor radii)
    FVector Axis;         // Up vector
    float MajorRadius;
    float MinorRadius;
    float Padding[3];
};
```

#### 4.4 Implement Torus Rendering

Add to [`EditorRenderPass::RenderClothColliders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:1017) after box rendering:

```cpp
// Render torus colliders
{
    BindShaderResource(L"TorusVS", L"TorusPS", D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    BindBuffers(Resources.Primitives.Torus);
    
    TArray<FConstantBufferDebugTorus> BufferAll;
    for (const FClothColliderSource& Source : Colliders)
    {
        if (Source.Type == EClothColliderType::Torus)
        {
            FConstantBufferDebugTorus b;
            
            // Build transform with proper scaling
            FVector WorldCenter = Source.CachedTransform.TransformPosition(Source.CachedLocalCenter);
            FVector WorldAxis = Source.CachedTransform.TransformVector(Source.CachedLocalAxis).GetSafeNormal();
            
            float MajorRadius = Source.CachedExtents.X;
            float MinorRadius = Source.CachedRadius;
            
            // Create rotation to align torus with axis
            FQuat Rotation = FQuat::FindBetweenVectors(FVector::UpVector, WorldAxis);
            FVector Scale(MajorRadius, MajorRadius, MinorRadius);
            
            FTransform TorusTransform(Rotation, WorldCenter, Scale);
            b.WorldMatrix = TorusTransform.ToMatrixWithScale();
            b.Axis = WorldAxis;
            b.MajorRadius = MajorRadius;
            b.MinorRadius = MinorRadius;
            
            BufferAll.Add(b);
        }
    }
    
    if (BufferAll.Num() > 0)
    {
        BufferManager->BindConstantBuffer("TorusConstantBuffer", 11, EShaderStage::Vertex);
        int BufferIndex = 0;
        for (int i = 0; i < (1 + BufferAll.Num() / ConstantBufferSizeTorus) * ConstantBufferSizeTorus; ++i)
        {
            TArray<FConstantBufferDebugTorus> SubBuffer;
            for (int j = 0; j < ConstantBufferSizeTorus; ++j)
            {
                if (BufferIndex < BufferAll.Num())
                {
                    SubBuffer.Add(BufferAll[BufferIndex]);
                    ++BufferIndex;
                }
                else
                {
                    break;
                }
            }
            
            if (SubBuffer.Num() > 0)
            {
                BufferManager->UpdateConstantBuffer<FConstantBufferDebugTorus>(TEXT("TorusConstantBuffer"), SubBuffer);
                Graphics->DeviceContext->DrawIndexedInstanced(
                    Resources.Primitives.Torus.IndexInfo.NumIndices,
                    SubBuffer.Num(), 0, 0, 0);
            }
        }
    }
}
```

#### 4.5 Add Torus Shader Code

Create shader functions in `EditorShader.hlsl`:

```hlsl
// Torus Vertex Shader
struct TorusVSInput
{
    float3 Position : POSITION;
    uint InstanceID : SV_InstanceID;
};

struct TorusVSOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

cbuffer TorusConstantBuffer : register(b11)
{
    struct
    {
        float4x4 WorldMatrix;
        float3 Axis;
        float MajorRadius;
        float MinorRadius;
        float3 Padding;
    } Torus[100];
};

TorusVSOutput TorusVS(TorusVSInput input)
{
    TorusVSOutput output;
    
    // Transform vertex by instance world matrix
    float4 worldPos = mul(float4(input.Position, 1.0f), Torus[input.InstanceID].WorldMatrix);
    output.Position = mul(worldPos, ViewProjection);
    
    // Wireframe color (cyan for torus)
    output.Color = float4(0.0f, 1.0f, 1.0f, 1.0f);
    
    return output;
}

float4 TorusPS(TorusVSOutput input) : SV_TARGET
{
    return input.Color;
}
```

### Phase 5: Component-Based Registration (Optional Future Enhancement)

For automatic registration from Actor components, create a custom `UTorusColliderComponent`:

```cpp
// TorusColliderComponent.h
class UTorusColliderComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
    
public:
    UPROPERTY(EditAnywhere, Category = "Torus")
    float MajorRadius = 50.0f;
    
    UPROPERTY(EditAnywhere, Category = "Torus")
    float MinorRadius = 20.0f;
    
    // Override to provide custom collision registration
    virtual void RegisterClothCollision(FClothCollisionManager* Manager);
};
```

## Testing Strategy

### Test Cases

1. **Basic Collision Test**
   - Create a torus collider with major radius 100, minor radius 30
   - Drop cloth onto torus from above
   - Verify cloth drapes over torus surface correctly

2. **Penetration Test**
   - Position cloth particles inside torus tube
   - Verify particles are pushed out along correct normal direction

3. **Rotation Test**
   - Create torus with different axis orientations (X, Y, Z, arbitrary)
   - Verify collision works correctly in all orientations

4. **Transform Update Test**
   - Attach torus to moving component
   - Verify collision updates correctly as torus moves

5. **Debug Rendering Test**
   - Verify torus wireframe renders correctly
   - Check alignment with actual collision surface

### Performance Considerations

- Torus SDF is slightly more expensive than sphere/capsule (requires 2 length operations)
- Expected performance impact: ~10-15% slower than capsule per collision test
- Recommend limiting to 5-10 torus colliders per scene for optimal performance

## File Modification Summary

### Files to Modify

1. **[`ClothCollisionManager.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:1)**
   - Add `Torus = 3` to [`EClothColliderType`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h:26)
   - Add `AddTorusCollider()` method declaration

2. **[`ClothCollisionManager.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp:1)**
   - Implement `AddTorusCollider()`
   - Update [`ConvertToGPU()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp:468) with torus case

3. **[`ClothSDFCollision.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSDFCollision.hlsl:1)**
   - Add `sdTorus()` function
   - Update [`QueryColliderSDF()`](EngineSIU/EngineSIU/Shaders/Cloth/ClothSDFCollision.hlsl:72) with torus case

4. **[`EditorRenderPass.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.h:1)**
   - Add `RenderTorusInstanced()` method declaration
   - Add `ConstantBufferSizeTorus` constant

5. **[`EditorRenderPass.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:1)**
   - Add torus geometry generation in [`CreateBuffers()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:81)
   - Add torus shader registration in [`CreateShaders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:41)
   - Add torus constant buffer in [`CreateConstantBuffers()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:238)
   - Add torus rendering in [`RenderClothColliders()`](EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/EditorRenderPass.cpp:1017)

6. **`EditorShader.hlsl`** (assumed location in Shaders directory)
   - Add `TorusVS()` and `TorusPS()` shader functions

7. **`RenderResources.h`** (assumed location)
   - Add `FConstantBufferDebugTorus` structure
   - Add `Torus` member to debug primitives

### Files NOT Modified

- **[`ClothGPUStructs.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h:1)** - Existing [`FClothColliderGPU`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h:189) structure has sufficient fields
- **[`ClothCommon.hlsli`](EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli:1)** - Existing [`FClothCollider`](EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli:251) structure matches GPU layout

## Implementation Order

1. ✅ **Phase 1**: Update data structures (enum, source struct)
2. ✅ **Phase 2**: Implement SDF function in compute shader
3. ✅ **Phase 3**: Add collision manager API
4. ✅ **Phase 4**: Implement debug rendering
5. ✅ **Phase 5**: Test and validate

## Usage Example

```cpp
// In game code or test actor
FClothWorld* ClothWorld = GEngine->ClothPhysicsManager->GetClothWorld(World);
FClothCollisionManager* CollisionMgr = ClothWorld->GetCollisionManager();

// Add a torus collider
FVector TorusCenter(0, 0, 100);
FVector TorusAxis = FVector::UpVector;  // Horizontal torus
float MajorRadius = 100.0f;  // Ring radius
float MinorRadius = 30.0f;   // Tube radius

CollisionMgr->AddTorusCollider(TorusCenter, TorusAxis, MajorRadius, MinorRadius);
```

## Notes and Considerations

1. **No PhysX Integration**: Since PhysX doesn't support torus, this is a pure SDF-based implementation
2. **Manual Registration Only**: Initially only supports manual `AddTorusCollider()` API
3. **Transform Support**: Torus can be attached to moving components via the existing transform update system
4. **Memory Efficient**: Reuses existing 64-byte collider structure without expansion
5. **Shader Compatibility**: Torus SDF integrates seamlessly with existing collision solver
6. **Debug Visualization**: Wireframe rendering allows visual confirmation of collision shape

## Future Enhancements

1. **Component-Based Registration**: Create `UTorusColliderComponent` for editor placement
2. **Skeletal Mesh Support**: Add torus colliders to physics assets for character cloth
3. **Optimization**: Consider using bounding sphere culling before expensive torus SDF
4. **Elliptical Torus**: Support non-circular cross-sections for more complex shapes
5. **Swept Torus**: Add motion blur/continuous collision detection for fast-moving tori

---

**Status**: Ready for implementation
**Estimated Complexity**: Medium (3-4 hours for core implementation + testing)
**Dependencies**: None (all required systems already in place)
