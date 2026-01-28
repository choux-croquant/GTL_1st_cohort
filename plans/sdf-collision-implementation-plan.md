# SDF Collision System Implementation Plan

## Executive Summary

This document provides a detailed, actionable implementation plan for adding an SDF (Signed Distance Field) collision system to the GPU-based cloth simulation. The plan is based on a thorough inspection of the existing codebase and follows a four-phase approach that integrates seamlessly with the current VELVET-style PBD architecture.

**Architecture Overview:**
- **Registration-Based Collection**: Colliders are registered explicitly, not world-scanned per frame
- **CPU-Side Management**: FClothCollisionManager tracks colliders and transforms
- **GPU-Side Evaluation**: Analytic SDF collision solved in compute shader
- **Dirty Tracking**: Only upload GPU data when transforms change
- **Integration Point**: Collision pass fits between constraint solver iterations

---

## Current System Analysis

### Existing Architecture Strengths

1. **Batched GPU Simulation** ([`ClothBatchedSolver.h/cpp`](ClothBatchedSolver.h))
   - Single unified buffer for all instances
   - VELVET-style predicted buffer + delta accumulation pattern
   - Well-structured dispatch pipeline

2. **Shader Register Convention** (from inspection)
   - **SRVs (t0-tN)**: Input data (read-only)
   - **UAVs (u0-uN)**: Output/RW data
   - **Constant Buffer (b0)**: [`FClothSimConstants`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/ShaderConstants.h:262)

3. **GPU Structures** ([`ClothGPUStructs.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h))
   - Already has `FClothCollisionSphereGPU` and `FClothCollisionCapsuleGPU`
   - 16/32 byte aligned, GPU-compatible

4. **Physics Asset Support**
   - [`UBodySetup`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/PhysicsEngine/PhysicsAsset.h:63) + [`FKAggregateGeom`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Classes/PhysicsEngine/PhysicsAsset.h:23) available
   - Can extract sphere/capsule/box elements

### Current Simulation Loop

**Location**: [`ClothBatchedSolver.cpp:612`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:612) - `SimulateSubstep()`

```cpp
void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    UpdateConstantBuffers(SubstepDeltaTime);
    
    // Step 1: Integration (predict positions)
    DispatchIntegration(UsedParticleCount);
    
    // Step 2: Pre-stabilization collision (TODO: Add here)
    // TODO: DispatchCollisionSDF(PredictedBuffer);
    
    // Step 3: Constraint solver iterations
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        ClearAccumulationBuffers(UsedParticleCount);
        
        if (UsedConstraintCount > 0)
            DispatchConstraintSolver(UsedConstraintCount);
        
        if (UsedBendConstraintCount > 0)
            DispatchBendConstraintSolver(UsedBendConstraintCount);
        
        DispatchApplyDeltas(UsedParticleCount);
        
        // TODO: Option to add collision here (post-constraint)
    }
    
    // Step 4: Kinematic targets
    if (UsedKinematicTargetCount > 0)
        DispatchApplyKinematicTargets(UsedKinematicTargetCount);
    
    // Step 5: Finalize (derive velocity, write positions)
    DispatchFinalize(UsedParticleCount);
}
```

**Integration Point**: Line 623 (marked with TODO comment) - after integration, before constraint solving.

---

## Phase 1: Collision Manager Class

### 1.1 File Structure

**NEW FILES:**
- `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.h`
- `EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothCollisionManager.cpp`

**RATIONALE**: Keep collision logic separate for modularity. Follows existing pattern of separate solver files.

### 1.2 Class Definition

**File**: `ClothCollisionManager.h`

```cpp
#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Transform.h"
#include <d3d11.h>

// Forward declarations
class UPrimitiveComponent;
class UBodySetup;
struct FKAggregateGeom;

/**
 * Collider type enumeration
 */
enum class EClothColliderType : uint32
{
    Sphere = 0,
    Capsule = 1,
    Box = 2,
    Count
};

/**
 * CPU-side collider source tracking
 * Tracks a registered collider and its dirty state
 */
struct FClothColliderSource
{
    EClothColliderType Type;
    UPrimitiveComponent* SourceComponent;  // Component providing the collider
    int32 ElementIndex;                     // Index in BodySetup->AggGeom array
    
    FTransform CachedTransform;             // Last uploaded transform
    FVector CachedLocalCenter;              // Local-space center/start point
    FVector CachedLocalAxis;                // Local-space axis/end point (for capsules)
    float CachedRadius;
    FVector CachedExtents;                  // For boxes
    
    bool bIsDirty;                          // Transform changed since last upload
    uint32 GPUBufferIndex;                  // Index in unified GPU buffer
    
    FClothColliderSource()
        : Type(EClothColliderType::Sphere)
        , SourceComponent(nullptr)
        , ElementIndex(0)
        , CachedTransform(FTransform::Identity)
        , CachedLocalCenter(FVector::ZeroVector)
        , CachedLocalAxis(FVector::ZeroVector)
        , CachedRadius(0.0f)
        , CachedExtents(FVector::ZeroVector)
        , bIsDirty(true)
        , GPUBufferIndex(0)
    {}
};

/**
 * Unified GPU collider structure
 * Single structure handles all collider types (sphere/capsule/box)
 * Must match FClothCollider in ClothCommon.hlsli
 */
struct FClothColliderGPU
{
    uint32 Type;            // 4 bytes - EClothColliderType as uint
    float Radius;           // 4 bytes - Sphere/Capsule radius
    float HalfHeight;       // 4 bytes - Capsule half-height (0 for sphere/box)
    float Padding0;         // 4 bytes
    
    FVector Center;         // 12 bytes - Sphere center, Capsule center, Box center
    float Padding1;         // 4 bytes
    
    FVector Axis;           // 12 bytes - Capsule axis (normalized), Box rotation axis
    float Padding2;         // 4 bytes
    
    FVector Extents;        // 12 bytes - Box half-extents (0 for sphere/capsule)
    float Padding3;         // 4 bytes
    
    // Total: 64 bytes (cache-line aligned)
};

static_assert(sizeof(FClothColliderGPU) == 64, "FClothColliderGPU must be 64 bytes");

/**
 * Collision Manager
 * Manages world-space colliders for cloth collision
 * 
 * DESIGN:
 * - Registration-based: Colliders are explicitly registered, not world-scanned
 * - Dirty tracking: Only updates GPU when transforms change
 * - Unified buffer: All collider types in single GPU buffer
 */
class FClothCollisionManager
{
public:
    FClothCollisionManager();
    ~FClothCollisionManager();
    
    // Initialization
    void Initialize(uint32 MaxColliders);
    void Release();
    
    // Registration API
    /**
     * Register a collider from a UPrimitiveComponent's BodySetup
     * @param Component - Component with physics shapes
     * @param bIncludeChildren - Recursively register child components
     * @return Number of colliders registered
     */
    int32 RegisterCollider(UPrimitiveComponent* Component, bool bIncludeChildren = false);
    
    /**
     * Unregister all colliders from a component
     */
    void UnregisterCollider(UPrimitiveComponent* Component);
    
    /**
     * Manually add a sphere collider
     */
    void AddSphereCollider(const FVector& WorldCenter, float Radius);
    
    /**
     * Manually add a capsule collider
     */
    void AddCapsuleCollider(const FVector& WorldStart, const FVector& WorldEnd, float Radius);
    
    /**
     * Manually add a box collider
     */
    void AddBoxCollider(const FVector& WorldCenter, const FVector& Extents, const FRotator& Rotation);
    
    // Update
    /**
     * Update transforms of registered colliders and mark dirty
     * Call once per frame before simulation
     */
    void UpdateTransforms();
    
    /**
     * Upload dirty colliders to GPU
     * Only uploads colliders marked dirty
     */
    void UploadToGPU(ID3D11Device* Device, ID3D11DeviceContext* Context);
    
    // GPU Resource Access
    ID3D11ShaderResourceView* GetColliderBufferSRV() const { return ColliderBufferSRV; }
    uint32 GetColliderCount() const { return ColliderSources.Num(); }
    
    // Debug
    void DebugDraw();

private:
    // Extract colliders from BodySetup
    void ExtractCollidersFromBodySetup(UBodySetup* BodySetup, UPrimitiveComponent* Component);
    void ExtractSphere(const struct FKSphereElem& Sphere, UPrimitiveComponent* Component, int32 ElementIndex);
    void ExtractCapsule(const struct FKSphylElem& Capsule, UPrimitiveComponent* Component, int32 ElementIndex);
    void ExtractBox(const struct FKBoxElem& Box, UPrimitiveComponent* Component, int32 ElementIndex);
    
    // Convert to GPU format
    FClothColliderGPU ConvertToGPU(const FClothColliderSource& Source) const;
    
    // GPU Resources
    ID3D11Buffer* ColliderBuffer;
    ID3D11ShaderResourceView* ColliderBufferSRV;
    uint32 MaxColliderCapacity;
    
    // CPU Tracking
    TArray<FClothColliderSource> ColliderSources;
    TMap<UPrimitiveComponent*, TArray<int32>> ComponentToColliderMap;  // Component -> ColliderSource indices
    
    bool bGPUDirty;  // Global dirty flag
};
```

### 1.3 Key Implementation Details

**Transform Tracking** (`UpdateTransforms()`)
```cpp
void FClothCollisionManager::UpdateTransforms()
{
    for (FClothColliderSource& Source : ColliderSources)
    {
        if (!Source.SourceComponent)
            continue;
        
        // Get current world transform
        FTransform CurrentTransform = Source.SourceComponent->GetComponentTransform();
        
        // Check if changed (simple equality check)
        if (!CurrentTransform.Equals(Source.CachedTransform))
        {
            Source.CachedTransform = CurrentTransform;
            Source.bIsDirty = true;
            bGPUDirty = true;
        }
    }
}
```

**Incremental Upload** (`UploadToGPU()`)
```cpp
void FClothCollisionManager::UploadToGPU(ID3D11Device* Device, ID3D11DeviceContext* Context)
{
    if (!bGPUDirty || !ColliderBuffer)
        return;
    
    // Build GPU data array (only dirty colliders)
    TArray<FClothColliderGPU> gpuColliders;
    gpuColliders.Reserve(ColliderSources.Num());
    
    for (const FClothColliderSource& Source : ColliderSources)
    {
        gpuColliders.Add(ConvertToGPU(Source));
    }
    
    // Upload entire buffer (D3D11_MAP_WRITE_DISCARD for dynamic buffer)
    D3D11_MAPPED_SUBRESOURCE msr;
    HRESULT hr = Context->Map(ColliderBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr))
    {
        memcpy(msr.pData, gpuColliders.GetData(), gpuColliders.Num() * sizeof(FClothColliderGPU));
        Context->Unmap(ColliderBuffer, 0);
        bGPUDirty = false;
    }
}
```

---

## Phase 2: GPU Data Structure & Upload

### 2.1 Extend ClothGPUStructs.h

**File**: [`ClothGPUStructs.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothGPUStructs.h)

**Add After Line 111** (after existing collision structures):

```cpp
/**
 * Unified cloth collider structure (CPU-side)
 * Single structure for all collider types (sphere/capsule/box)
 * Must match FClothCollider in ClothCommon.hlsli
 */
struct FClothColliderGPU
{
    uint32 Type;            // EClothColliderType: 0=Sphere, 1=Capsule, 2=Box
    float Radius;           // Sphere/Capsule radius
    float HalfHeight;       // Capsule half-height (0 for others)
    float Padding0;
    
    FVector Center;         // World-space center
    float Padding1;
    
    FVector Axis;           // Capsule axis (normalized), Box orientation
    float Padding2;
    
    FVector Extents;        // Box half-extents (0 for sphere/capsule)
    float Padding3;
    
    // Total: 64 bytes
};

static_assert(sizeof(FClothColliderGPU) == 64, "FClothColliderGPU must be 64 bytes");
```

### 2.2 Extend ClothCommon.hlsli

**File**: [`ClothCommon.hlsli`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli)

**Add After Line 164** (after existing collision structures):

```hlsl
/**
 * Unified collider structure (GPU-side)
 * Matches FClothColliderGPU in ClothGPUStructs.h
 */
struct FClothCollider
{
    uint Type;              // 0=Sphere, 1=Capsule, 2=Box
    float Radius;
    float HalfHeight;       // For capsule only
    float Padding0;
    
    float3 Center;          // World-space center
    float Padding1;
    
    float3 Axis;            // Capsule axis (normalized)
    float Padding2;
    
    float3 Extents;         // Box half-extents
    float Padding3;
};
```

### 2.3 Extend FClothSimConstants

**File**: [`ShaderConstants.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Renderer/ShaderConstants.h:262)

**Modify After Line 267** (add collision parameters):

```cpp
struct alignas(16) FClothSimConstants
{
    uint32 NumParticles;
    uint32 NumConstraints;
    uint32 NumBendConstraints;
    uint32 NumKinematicTargets;

    float DeltaTime;
    float Damping;
    float StretchStiffness;
    float BendStiffness;

    FVector Gravity;
    float AirDrag;

    FVector Wind;
    uint32 NumIterations;

    uint32 CurrentIteration;
    uint32 UseXPBD;
    float RelaxationFactor;
    float MaxSpeed;
    
    float LongRangeStretchiness;
    // NEW: Collision parameters
    uint32 NumColliders;              // Number of active colliders
    float CollisionThickness;         // Collision distance threshold (default: 0.1)
    float CollisionFriction;          // Friction coefficient (0-1)

    alignas(16) FMatrix WorldMatrix;
};
```

**Also update** [`ClothCommon.hlsli`](../EngineSIU/EngineSIU/Shaders/Cloth/ClothCommon.hlsli:10) to match (around line 33):

```hlsl
cbuffer ClothSimConstants : register(b0)
{
    // ... existing fields ...
    
    float LongRangeStretchiness;
    uint NumColliders;           // NEW
    float CollisionThickness;    // NEW
    float CollisionFriction;     // NEW

    float4x4 WorldMatrix;
};
```

### 2.4 Extend ClothBatchedSolver

**File**: [`ClothBatchedSolver.h`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.h)

**Add Forward Declaration** (after line 21):
```cpp
class FClothCollisionManager;
```

**Add Member Variables** (after line 171 - after existing buffer declarations):

```cpp
    // Collision manager (NEW)
    FClothCollisionManager* CollisionManager;
    
    // Collision shader (NEW)
    ID3D11ComputeShader* CollisionSolverCS;
```

**Add Method Declarations** (after line 106 - in dispatch methods section):

```cpp
    void DispatchCollisionSDF(uint32 ParticleCount);
```

**Constructor Initialization** (in `.cpp` file, line 23):
```cpp
    , CollisionManager(nullptr)
    , CollisionSolverCS(nullptr)
```

---

## Phase 3: Integration into Batch Solver

### 3.1 Modify ClothBatchedSolver::Initialize

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:67)

**Add After Line 91** (after shader loading):

```cpp
    // Initialize collision manager
    CollisionManager = new FClothCollisionManager();
    CollisionManager->Initialize(512);  // Max 512 colliders
    
    UE_LOG(ELogLevel::Display, TEXT("ClothBatchedSolver: Collision manager initialized"));
```

### 3.2 Modify ClothBatchedSolver::Release

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:94)

**Add Before Line 154**:

```cpp
    // Release collision manager
    if (CollisionManager)
    {
        CollisionManager->Release();
        delete CollisionManager;
        CollisionManager = nullptr;
    }
    
    CollisionSolverCS = nullptr;
```

### 3.3 Modify LoadComputeShaders

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:666)

**Add After Line 751** (after normalizing shader):

```cpp
    // Load or get Collision SDF shader (NEW)
    hr = ShaderManager->AddComputeShader(L"ClothCollisionSDFCS", 
                                          L"Shaders/Cloth/ClothSDFCollision.hlsl", 
                                          "SolveCollisionsCS");
    if (FAILED(hr))
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothBatchedSolver: Failed to compile ClothSDFCollision shader (optional)"));
        // Not critical - collision is optional
    }
    CollisionSolverCS = ShaderManager->GetComputeShaderByKey(L"ClothCollisionSDFCS");
```

### 3.4 Add DispatchCollisionSDF Method

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp)

**Add After Line 1217** (after DispatchFinalize):

```cpp
void FClothBatchedSolver::DispatchCollisionSDF(uint32 ParticleCount)
{
    if (!Graphics || !Graphics->DeviceContext || !CollisionSolverCS || ParticleCount == 0)
        return;
    
    if (!CollisionManager || CollisionManager->GetColliderCount() == 0)
        return;
    
    // Update collision manager and upload if dirty
    CollisionManager->UpdateTransforms();
    CollisionManager->UploadToGPU(Graphics->Device, Graphics->DeviceContext);
    
    // Bind constant buffer (contains NumColliders, CollisionThickness, etc.)
    Graphics->DeviceContext->CSSetConstantBuffers(0, 1, &BatchSimConstantBuffer);
    
    // Bind SRVs:
    // t0: Collider buffer (read-only)
    // t1: InvMass (to skip kinematic particles)
    ID3D11ShaderResourceView* srvs[2] = {
        CollisionManager->GetColliderBufferSRV(),  // t0
        UnifiedInvMassSRV                          // t1
    };
    Graphics->DeviceContext->CSSetShaderResources(0, 2, srvs);
    
    // Bind UAV:
    // u0: Predicted buffer (read-write, modify in-place)
    ID3D11UnorderedAccessView* uavs[] = { UnifiedPredictedUAV };
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    
    // Bind shader
    Graphics->DeviceContext->CSSetShader(CollisionSolverCS, nullptr, 0);
    
    // Dispatch (one thread per particle)
    uint32 dispatchCount = GetDispatchCount(ParticleCount, 256);
    Graphics->DeviceContext->Dispatch(dispatchCount, 1, 1);
    
    // Unbind
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    Graphics->DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    Graphics->DeviceContext->CSSetShaderResources(0, 2, nullSRVs);
}
```

### 3.5 Integrate into Simulation Loop

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:612)

**Modify SimulateSubstep - Replace Line 623**:

```cpp
void FClothBatchedSolver::SimulateSubstep(float SubstepDeltaTime)
{
    UpdateConstantBuffers(SubstepDeltaTime);
    
    // Step 1: Integration
    DispatchIntegration(UsedParticleCount);
    
    // Step 2: Pre-stabilization collision (NEW - ADDED)
    DispatchCollisionSDF(UsedParticleCount);
    
    // Step 3: Constraint solver iterations
    for (int32 iter = 0; iter < Config.NumIterations; ++iter)
    {
        ClearAccumulationBuffers(UsedParticleCount);
        
        if (UsedConstraintCount > 0)
            DispatchConstraintSolver(UsedConstraintCount);
        
        if (UsedBendConstraintCount > 0)
            DispatchBendConstraintSolver(UsedBendConstraintCount);
        
        DispatchApplyDeltas(UsedParticleCount);
        
        // OPTION: Add collision here for multiple collision passes per iteration
        // DispatchCollisionSDF(UsedParticleCount);
    }
    
    // Step 4: Kinematic targets
    if (UsedKinematicTargetCount > 0)
        DispatchApplyKinematicTargets(UsedKinematicTargetCount);
    
    // Step 5: Finalize
    DispatchFinalize(UsedParticleCount);
}
```

### 3.6 Update Constant Buffer

**File**: [`ClothBatchedSolver.cpp`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothBatchedSolver.cpp:1309)

**Modify UpdateConstantBuffers - After Line 1333**:

```cpp
void FClothBatchedSolver::UpdateConstantBuffers(float DeltaTime)
{
    // ... existing code ...
    
    constants.LongRangeStretchiness = Config.LongRangeStretchiness;
    
    // NEW: Collision parameters
    constants.NumColliders = CollisionManager ? CollisionManager->GetColliderCount() : 0;
    constants.CollisionThickness = 0.1f;   // TODO: Make configurable in FClothConfig
    constants.CollisionFriction = 0.2f;    // TODO: Make configurable
    
    constants.WorldMatrix = FMatrix::Identity;
    
    // ... rest of code ...
}
```

---

## Phase 4: GPU SDF Collision Shader

### 4.1 Create ClothSDFCollision.hlsl

**NEW FILE**: `EngineSIU/EngineSIU/Shaders/Cloth/ClothSDFCollision.hlsl`

```hlsl
/**
 * Cloth SDF Collision Solver
 * Analytic signed distance field collision detection and response
 * Operates on predicted particle positions (in-place modification)
 */

#include "ClothCommon.hlsli"

// Input buffers
StructuredBuffer<FClothCollider> Colliders : register(t0);  // Collider data
StructuredBuffer<float> InvMass : register(t1);             // Inverse masses

// Output buffer (read-write)
RWStructuredBuffer<FClothParticle> PredictedRW : register(u0);  // Modify in-place

static const float EPSILON = 1e-6f;

/**
 * SDF Functions - Analytic signed distance computations
 */

// Sphere SDF
float sdSphere(float3 pos, float3 center, float radius)
{
    return length(pos - center) - radius;
}

// Capsule SDF
float sdCapsule(float3 pos, float3 centerA, float3 centerB, float radius)
{
    float3 pa = pos - centerA;
    float3 ba = centerB - centerA;
    float h = saturate(dot(pa, ba) / dot(ba, ba));
    return length(pa - ba * h) - radius;
}

// Box SDF (oriented)
float sdBox(float3 pos, float3 center, float3 extents)
{
    float3 localPos = pos - center;
    float3 q = abs(localPos) - extents;
    return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

/**
 * Query SDF for a single collider
 * Returns: signed distance (negative = inside)
 */
float QueryColliderSDF(FClothCollider collider, float3 pos, out float3 normal)
{
    float dist = 0.0f;
    
    if (collider.Type == 0)  // Sphere
    {
        dist = sdSphere(pos, collider.Center, collider.Radius);
        normal = normalize(pos - collider.Center);
    }
    else if (collider.Type == 1)  // Capsule
    {
        float3 centerA = collider.Center - collider.Axis * collider.HalfHeight;
        float3 centerB = collider.Center + collider.Axis * collider.HalfHeight;
        dist = sdCapsule(pos, centerA, centerB, collider.Radius);
        
        // Normal = gradient of SDF (point to closest point on capsule axis)
        float3 pa = pos - centerA;
        float3 ba = centerB - centerA;
        float h = saturate(dot(pa, ba) / dot(ba, ba));
        float3 closestPoint = centerA + ba * h;
        normal = normalize(pos - closestPoint);
    }
    else if (collider.Type == 2)  // Box
    {
        dist = sdBox(pos, collider.Center, collider.Extents);
        
        // Normal = gradient of SDF (approximate)
        float3 localPos = pos - collider.Center;
        float3 q = abs(localPos) - collider.Extents;
        normal = normalize(sign(localPos) * max(q, 0.0f));
        
        // Handle interior case
        if (dist < 0.0f)
        {
            normal = sign(localPos) * step(q.yzx, q.xyz) * step(q.zxy, q.xyz);
        }
    }
    
    return dist;
}

/**
 * Collision response
 * Applies position correction and velocity damping
 */
void ApplyCollisionResponse(inout float3 position, float3 normal, float penetration, 
                            float friction, float invMass)
{
    if (invMass < EPSILON)
        return;  // Kinematic particle - no response
    
    // Position correction - push particle out by penetration distance
    float3 correction = normal * penetration;
    position += correction;
    
    // TODO: If you need velocity-based friction/damping:
    // This would require reading velocity buffer and applying tangential friction
    // For now, position correction is sufficient for basic collision
}

/**
 * Main collision solver kernel
 * Processes one particle per thread
 */
[numthreads(256, 1, 1)]
void SolveCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumParticles)
        return;
    
    float invMass = InvMass[idx];
    
    // Skip kinematic particles
    if (invMass < EPSILON)
        return;
    
    // Read current predicted position
    FClothParticle particle = PredictedRW[idx];
    float3 position = particle.Position;
    
    bool hadCollision = false;
    float3 totalCorrection = float3(0, 0, 0);
    
    // Test against all colliders
    for (uint i = 0; i < NumColliders; i++)
    {
        FClothCollider collider = Colliders[i];
        
        float3 normal;
        float dist = QueryColliderSDF(collider, position, normal);
        
        // Check for penetration (dist < collision thickness)
        float penetration = CollisionThickness - dist;
        
        if (penetration > 0.0f)
        {
            // Apply correction
            totalCorrection += normal * penetration;
            hadCollision = true;
        }
    }
    
    // Apply accumulated corrections
    if (hadCollision)
    {
        position += totalCorrection;
        
        // Write back modified position
        particle.Position = position;
        PredictedRW[idx] = particle;
    }
}
```

### 4.2 Alternative: Delta Accumulation Pattern

If you want collision to integrate with the existing delta accumulation system (like constraints), create an alternative version:

**File**: `ClothSDFCollision.hlsl` (alternative approach)

```hlsl
/**
 * Alternative: Collision using delta accumulation pattern
 * Matches the constraint solver pattern for consistency
 */

// Output buffers for delta accumulation
RWStructuredBuffer<int3> PositionDelta : register(u0);
RWStructuredBuffer<int> PositionWeight : register(u1);

static const float kScale = 10000.0f;  // Fixed-point scale

[numthreads(256, 1, 1)]
void SolveCollisionsCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= NumParticles)
        return;
    
    float invMass = InvMass[idx];
    if (invMass < EPSILON)
        return;
    
    FClothParticle particle = PredictedRead[idx];
    float3 position = particle.Position;
    
    float3 totalCorrection = float3(0, 0, 0);
    
    for (uint i = 0; i < NumColliders; i++)
    {
        FClothCollider collider = Colliders[i];
        float3 normal;
        float dist = QueryColliderSDF(collider, position, normal);
        float penetration = CollisionThickness - dist;
        
        if (penetration > 0.0f)
        {
            totalCorrection += normal * penetration;
        }
    }
    
    // Accumulate correction using atomic operations
    if (length(totalCorrection) > EPSILON)
    {
        int3 deltaInt = int3(totalCorrection * kScale);
        InterlockedAdd(PositionDelta[idx].x, deltaInt.x);
        InterlockedAdd(PositionDelta[idx].y, deltaInt.y);
        InterlockedAdd(PositionDelta[idx].z, deltaInt.z);
        InterlockedAdd(PositionWeight[idx], 1);
    }
}
```

**This approach** would use the same delta buffers as constraints, so collision corrections blend with constraint corrections in `ApplyDeltas`.

---

## Shader Register Slot Assignments

### Current Usage (from code inspection)

**ClothIntegrate.hlsl**:
- `t0`: PositionRead
- `t1`: VelocityRead
- `t2`: InvMass
- `t3`: InstanceParams
- `u0`: PredictedWrite

**ClothConstraintSolver.hlsl**:
- `t0`: PredictedRead
- `t1`: Constraints
- `t2`: InvMass
- `t3`: InstanceParams
- `u0`: PositionDelta
- `u1`: PositionWeight

**ClothBendConstraintSolver.hlsl**:
- `t0`: PredictedRead
- `t2`: InvMass
- `t3`: InstanceParams
- `u0`: BendConstraintUAV
- `u1`: PositionDelta
- `u2`: PositionWeight

### Proposed: ClothSDFCollision.hlsl

**Option 1: Direct Position Modification**
- `t0`: Colliders (NEW)
- `t1`: InvMass
- `u0`: PredictedRW (read-write)

**Option 2: Delta Accumulation (recommended for consistency)**
- `t0`: Colliders (NEW)
- `t1`: PredictedRead
- `t2`: InvMass
- `u0`: PositionDelta
- `u1`: PositionWeight

**Recommendation**: Use **Option 2** (delta accumulation) because:
1. Consistent with existing constraint pattern
2. Allows blending collision with constraints in same iteration
3. Can be called multiple times per substep without order-dependency

---

## Implementation Order & Dependencies

### Step-by-Step Implementation Sequence

1. **Day 1-2: CPU-Side Foundation**
   - [ ] Create `ClothCollisionManager.h/cpp` files
   - [ ] Implement `FClothColliderSource` tracking
   - [ ] Add `RegisterCollider()` API with UBodySetup extraction
   - [ ] Implement `UpdateTransforms()` with dirty tracking
   - [ ] Test: Print registered colliders to console

2. **Day 2-3: GPU Structure Definition**
   - [ ] Add `FClothColliderGPU` to `ClothGPUStructs.h`
   - [ ] Add `FClothCollider` to `ClothCommon.hlsli`
   - [ ] Extend `FClothSimConstants` with collision params
   - [ ] Test: Verify structure sizes with static_assert

3. **Day 3-4: GPU Buffer Management**
   - [ ] Implement `FClothCollisionManager::Initialize()` - create D3D11 buffer
   - [ ] Implement `FClothCollisionManager::UploadToGPU()`
   - [ ] Add collision manager to `ClothBatchedSolver`
   - [ ] Test: Upload dummy collider, verify with graphics debugger

4. **Day 4-5: Shader Implementation**
   - [ ] Create `ClothSDFCollision.hlsl`
   - [ ] Implement SDF functions (sphere, capsule, box)
   - [ ] Implement collision kernel with delta accumulation
   - [ ] Test: Single static sphere collision

5. **Day 5-6: Integration**
   - [ ] Add `LoadComputeShaders()` entry for collision shader
   - [ ] Implement `DispatchCollisionSDF()` in solver
   - [ ] Wire into `SimulateSubstep()` loop
   - [ ] Update constant buffer upload
   - [ ] Test: Dynamic collider with moving transform

6. **Day 6-7: Polish & Optimization**
   - [ ] Add per-instance collision parameters
   - [ ] Implement spatial culling (optional)
   - [ ] Add debug visualization
   - [ ] Performance profiling
   - [ ] Test: Multiple colliders with multiple cloth instances

---

## Potential Conflicts & Mitigation

### 1. Shader Register Slot Conflicts

**Issue**: Different shaders use overlapping register slots (e.g., t0 is used by multiple shaders)

**Mitigation**: 
- Register slots are **per-dispatch**, not global
- Each shader dispatch binds/unbinds its own resources
- No conflict as long as resources are unbound after each dispatch (current code already does this)

**Example** (from `DispatchConstraintSolver`, line 1067):
```cpp
// Unbind after dispatch
ID3D11ShaderResourceView* nullSRVs[4] = {nullptr, nullptr, nullptr, nullptr};
Graphics->DeviceContext->CSSetShaderResources(0, 4, nullSRVs);
```

### 2. Collision vs. Kinematic Targets

**Issue**: Kinematic targets pin particles; collision shouldn't override these

**Mitigation**: 
- Check `InvMass == 0` in collision shader (skip kinematic particles)
- Kinematic targets applied AFTER collision (already in correct order)

**Code** (in shader):
```hlsl
float invMass = InvMass[idx];
if (invMass < EPSILON)
    return;  // Skip kinematic
```

### 3. Multiple Collision Calls Per Substep

**Issue**: If collision is called inside constraint iteration loop, accumulation buffers might conflict

**Mitigation**:
- **Recommended**: Call collision ONCE per substep (before constraint loop)
- **Alternative**: If calling inside loop, ensure delta buffers are cleared before each iteration (already done via `ClearAccumulationBuffers()`)

### 4. Transform Update Frequency

**Issue**: `UpdateTransforms()` called every frame might be expensive with many colliders

**Mitigation**:
- **Current design**: Registration-based (not world-scanning) keeps CPU cost low
- **Future optimization**: Spatial hashing, only update colliders near active cloth instances

### 5. Existing Collision Structures

**Issue**: `FClothCollisionSphereGPU` and `FClothCollisionCapsuleGPU` already exist but unused

**Mitigation**:
- **Keep existing structures** for backward compatibility
- **New unified structure** (`FClothColliderGPU`) is separate and more flexible
- Can deprecate old structures later

---

## API Usage Example

### Example: Register Static Mesh Colliders

```cpp
// In your game code or test actor
void AMyClothActor::BeginPlay()
{
    Super::BeginPlay();
    
    // Get collision manager from cloth world
    UClothWorld* ClothWorld = GetWorld()->GetClothWorld();
    FClothCollisionManager* CollisionMgr = ClothWorld->GetBatchManager()->GetCollisionManager();
    
    // Register all static mesh actors in level
    TArray<AActor*> StaticMeshActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AStaticMeshActor::StaticClass(), StaticMeshActors);
    
    for (AActor* Actor : StaticMeshActors)
    {
        AStaticMeshActor* StaticActor = Cast<AStaticMeshActor>(Actor);
        UStaticMeshComponent* MeshComp = StaticActor->GetStaticMeshComponent();
        
        // Register colliders from this component's BodySetup
        int32 NumColliders = CollisionMgr->RegisterCollider(MeshComp);
        
        UE_LOG(ELogLevel::Display, TEXT("Registered %d colliders from %s"), 
               NumColliders, *Actor->GetName());
    }
}
```

### Example: Manually Add Sphere Collider

```cpp
// Add a sphere collider at world origin with radius 50
CollisionMgr->AddSphereCollider(FVector(0, 0, 0), 50.0f);

// Add a capsule collider
FVector Start(0, 0, 0);
FVector End(0, 0, 100);
CollisionMgr->AddCapsuleCollider(Start, End, 25.0f);
```

---

## Performance Considerations

### CPU Overhead

**Transform Updates**:
- **Cost**: O(N) where N = number of registered colliders
- **Frequency**: Once per frame (before simulation)
- **Optimization**: Only check transforms of colliders near active cloth

**GPU Upload**:
- **Cost**: O(N) memcpy + D3D11_MAP_WRITE_DISCARD
- **Frequency**: Only when dirty (transform changed)
- **Optimization**: Already minimized via dirty tracking

### GPU Overhead

**Collision Kernel**:
- **Complexity**: O(NumParticles × NumColliders)
- **Thread Groups**: `(NumParticles + 255) / 256`
- **Memory Bandwidth**: Reads collider buffer, reads/writes predicted buffer

**Optimization Strategies**:

1. **Spatial Culling** (future enhancement):
   ```cpp
   // Only test colliders within AABB of cloth instance
   for (each cloth instance)
   {
       FBox InstanceBounds = ComputeBounds(instance);
       TArray<int32> NearbyColliders = SpatialGrid.Query(InstanceBounds);
       // Pass only nearby colliders to GPU
   }
   ```

2. **Per-Instance Collider Lists** (future):
   - Store per-instance collider index lists in GPU buffer
   - Shader only tests colliders relevant to particle's instance

3. **LOD-Based Collision**:
   - Disable collision for distant cloth instances (LOD 2+)
   - Implemented via `FClothInstanceParameters::IsActive` flag

---

## Testing Strategy

### Unit Tests

1. **SDF Functions** (HLSL):
   - Test sphere SDF with known point
   - Test capsule SDF edge cases (endpoints)
   - Test box SDF corners/edges

2. **Collision Manager**:
   - Register/unregister colliders
   - Transform update triggers dirty flag
   - GPU upload writes correct data

3. **Integration**:
   - Single cloth + single sphere
   - Cloth falling onto static ground plane
   - Moving collider (kinematic object)

### Visual Tests

1. **Sphere Collision**: Cloth drapes over sphere
2. **Capsule Collision**: Cloth wraps around pole
3. **Box Collision**: Cloth rests on table
4. **Dynamic Collision**: Sphere moves through hanging cloth

### Performance Profiling

**GPU Timing** (use `ID3D11Query` for timestamps):
```cpp
// Before dispatch
Graphics->DeviceContext->End(QueryStart);

DispatchCollisionSDF(UsedParticleCount);

// After dispatch
Graphics->DeviceContext->End(QueryEnd);

// Read back timing
float CollisionTimeMS = /* compute from query results */;
```

---

## Future Enhancements

1. **SDF Textures**: Volume textures for complex shapes
2. **Mesh SDFs**: Precomputed SDF grids for arbitrary meshes
3. **Two-Way Interaction**: Cloth pushes back on colliders
4. **Self-Collision**: Particle-particle collision (separate system)
5. **Friction Modeling**: Tangential velocity damping
6. **Continuous Collision Detection**: Sweep tests for fast-moving objects

---

## Summary Checklist

### Phase 1: Collision Manager ✓
- [x] Architecture designed
- [ ] FClothCollisionManager class created
- [ ] UBodySetup extraction implemented
- [ ] Registration API complete
- [ ] Transform tracking functional

### Phase 2: GPU Structures ✓
- [x] FClothColliderGPU defined
- [ ] Buffer allocation in solver
- [ ] Upload with dirty tracking
- [ ] Constant buffer extended

### Phase 3: Integration ✓
- [x] Integration points identified
- [ ] Collision manager wired to solver
- [ ] DispatchCollisionSDF implemented
- [ ] Simulation loop updated

### Phase 4: Shader Implementation ✓
- [x] Shader architecture designed
- [ ] ClothSDFCollision.hlsl created
- [ ] SDF functions implemented
- [ ] Collision kernel tested

---

## Conclusion

This implementation plan provides a complete, actionable roadmap for adding SDF collision to the cloth simulation system. The design:

✅ **Integrates seamlessly** with existing VELVET-style PBD architecture  
✅ **Minimizes CPU overhead** via registration and dirty tracking  
✅ **Leverages existing patterns** (delta accumulation, shader binding conventions)  
✅ **Maintains modularity** (separate collision manager class)  
✅ **Scales efficiently** (GPU-parallel collision testing)  

**Estimated Implementation Time**: 6-7 days for core functionality + testing

**Next Step**: Begin with Phase 1 (Collision Manager class) and test incrementally at each phase.
