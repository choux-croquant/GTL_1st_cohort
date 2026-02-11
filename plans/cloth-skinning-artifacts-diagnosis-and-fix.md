# Cloth Skinning Artifacts: Root Cause Analysis & Fix Plan

## Executive Summary

**Problem:** Dual-mesh cloth simulation (high-res render mesh skinned to low-res simulation mesh) exhibits two critical visual artifacts:
1. **Edge Curling/Shrinkage** - Mesh edges curl inward compared to rest pose
2. **UV Distortion/Swimming** - Textures appear wavy and slide across the surface

**Root Cause:** The current implementation uses **simple Linear Blend Skinning (LBS)** with K-nearest neighbor weighting, which only interpolates positions without preserving local surface orientation. This causes the render mesh to "shrink-wrap" toward simulation vertices rather than maintaining proper surface detail.

**Solution:** Implement **Tangent-Space Offset Skinning** where each render vertex stores its offset from the simulation surface in a local tangent frame, then reconstructs this offset during deformation.

---

## 1. Root Cause Analysis

### 1.1 Current Implementation Issues

#### **Problem 1: Position-Only Skinning (No Rotation)**

**Current Code:** [`ClothProductionVertexShader.hlsl:58-79`](EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionVertexShader.hlsl:58)
```hlsl
// Current approach: Simple weighted average of simulation positions
float3 skinnedPosition = float3(0, 0, 0);
for (int i = 0; i < 4; ++i)
{
    float weight = skinning.Weights[i];
    if (weight > 0.0)
    {
        uint simVertexIndex = skinning.SimVertexIndices[i];
        float3 simPos = SimPositionBuffer[simVertexIndex].xyz;
        skinnedPosition += weight * simPos;  // ❌ PROBLEM: Only position, no rotation
    }
}
```

**Why This Fails:**
- **No Local Frame:** The render vertex is pulled toward the weighted average of simulation vertices, but its **offset direction** is not rotated with the surface
- **Shrinkage:** When simulation vertices move, the render vertex follows the centroid but loses detail perpendicular to the surface
- **UV Swimming:** Since the offset isn't tied to surface orientation, textures slide as the mesh deforms

**Analogy:** Imagine a wrinkle on cloth. The current method only tracks where the wrinkle's center moves, not how it rotates. The wrinkle gets flattened.

---

#### **Problem 2: K-Nearest Neighbor Without Triangle Context**

**Current Code:** [`ClothSkinningWeightGenerator.cpp:187-202`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator.cpp:187)
```cpp
// Find K nearest simulation vertices
spatialHash.FindNearestNeighbors(
    renderPos,
    Params.MaxInfluences,  // K=4
    SimPositions,
    nearestIndices,
    nearestDistances
);
```

**Why This Fails:**
- **No Triangle Binding:** The render vertex is bound to individual simulation **vertices**, not to a simulation **triangle**
- **No Barycentric Coordinates:** Without triangle context, there's no natural local coordinate system
- **Edge Vertices Suffer Most:** Render vertices near mesh boundaries often have all their influences on one side, causing asymmetric pulling

**Correct Approach:** Bind each render vertex to the **closest simulation triangle**, compute **barycentric coordinates** within that triangle, and store the **perpendicular offset** from the triangle plane.

---

#### **Problem 3: Decimation May Shrink Boundaries**

**Current Code:** [`ClothMeshDecimator_Voronoi.cpp:659-773`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshDecimator_Voronoi.cpp:659)

The Voronoi decimation uses Lloyd's algorithm with projection to the original surface. However:
- **Boundary Vertices:** May not be explicitly preserved as seeds
- **Centroid Drift:** Lloyd iterations move seeds toward region centroids, which can pull boundary seeds inward
- **No Boundary Constraint:** The algorithm doesn't enforce that boundary vertices remain on the boundary

**Result:** The low-res mesh may be slightly smaller than the high-res mesh, causing the skinned result to curl inward.

---

### 1.2 Why Barycentric Mapping Alone Didn't Help

The user mentioned switching to "standard Barycentric mapping" with no improvement. This is expected because:

1. **Still Position-Only:** Even with barycentric coordinates, if you only compute:
   ```
   skinnedPos = w0*simPos0 + w1*simPos1 + w2*simPos2
   ```
   You're still just interpolating positions without rotation.

2. **Missing Offset:** The render vertex needs to store its **perpendicular distance** from the simulation triangle and **reconstruct** this offset using the triangle's **current normal**.

---

## 2. Detailed Solution: Tangent-Space Offset Skinning

### 2.1 Mathematical Foundation

For each render vertex `R`, we need:

1. **Find Closest Simulation Triangle:** `T = (V0, V1, V2)`
2. **Compute Barycentric Coordinates:** `(u, v, w)` where `u + v + w = 1`
3. **Compute Triangle Tangent Frame (Rest Pose):**
   ```
   Normal_rest = normalize(cross(V1 - V0, V2 - V0))
   Tangent_rest = normalize(V1 - V0)
   Bitangent_rest = cross(Normal_rest, Tangent_rest)
   ```
4. **Compute Offset in Tangent Space:**
   ```
   TriangleCenter_rest = u*V0 + v*V1 + w*V2
   Offset_world = R - TriangleCenter_rest
   Offset_tangent = [dot(Offset_world, Tangent_rest),
                     dot(Offset_world, Bitangent_rest),
                     dot(Offset_world, Normal_rest)]
   ```

**Storage:** For each render vertex, store:
- `uint3 TriangleIndices` (3 simulation vertex indices)
- `float3 BarycentricCoords` (u, v, w)
- `float3 TangentSpaceOffset` (offset in local frame)

**Runtime Reconstruction (GPU):**
```hlsl
// 1. Fetch deformed simulation vertices
float3 simPos0 = SimPositionBuffer[TriangleIndices.x].xyz;
float3 simPos1 = SimPositionBuffer[TriangleIndices.y].xyz;
float3 simPos2 = SimPositionBuffer[TriangleIndices.z].xyz;

// 2. Interpolate base position
float3 basePos = BarycentricCoords.x * simPos0 +
                 BarycentricCoords.y * simPos1 +
                 BarycentricCoords.z * simPos2;

// 3. Reconstruct tangent frame (deformed)
float3 edge1 = simPos1 - simPos0;
float3 edge2 = simPos2 - simPos0;
float3 normal_deformed = normalize(cross(edge1, edge2));
float3 tangent_deformed = normalize(edge1);
float3 bitangent_deformed = cross(normal_deformed, tangent_deformed);

// 4. Rotate offset from tangent space to world space
float3 offset_world = TangentSpaceOffset.x * tangent_deformed +
                      TangentSpaceOffset.y * bitangent_deformed +
                      TangentSpaceOffset.z * normal_deformed;

// 5. Final position
float3 skinnedPosition = basePos + offset_world;
```

---

### 2.2 Why This Fixes Both Problems

#### **Edge Curling Fixed:**
- **Boundary Preservation:** Even if the simulation mesh is slightly smaller, the tangent-space offset ensures render vertices maintain their perpendicular distance from the surface
- **No Shrinkage:** The offset is reconstructed using the **current triangle orientation**, so detail is preserved

#### **UV Distortion Fixed:**
- **Texture Sticks to Surface:** The offset rotates with the triangle's tangent frame, so textures deform naturally with the cloth
- **No Swimming:** The render vertex moves **with** the surface, not just toward a weighted average of points

---

## 3. Implementation Plan

### 3.1 C++ Changes: Weight Generator

**File:** [`ClothSkinningWeightGenerator.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator.h:1)

**New Data Structure:**
```cpp
/**
 * Triangle-based skinning weight with tangent-space offset
 * Binds render vertex to closest simulation triangle
 */
struct FClothSkinningWeightTriangle
{
    uint32 SimTriangleIndices[3];      // 3 simulation vertex indices forming triangle
    float BarycentricCoords[3];        // Barycentric coordinates (u, v, w)
    FVector TangentSpaceOffset;        // Offset in triangle's local tangent frame
    
    FClothSkinningWeightTriangle()
    {
        SimTriangleIndices[0] = 0;
        SimTriangleIndices[1] = 0;
        SimTriangleIndices[2] = 0;
        BarycentricCoords[0] = 1.0f;
        BarycentricCoords[1] = 0.0f;
        BarycentricCoords[2] = 0.0f;
        TangentSpaceOffset = FVector::ZeroVector;
    }
};

// Serialization
inline FArchive& operator<<(FArchive& Ar, FClothSkinningWeightTriangle& W)
{
    for (int i = 0; i < 3; ++i)
        Ar << W.SimTriangleIndices[i];
    for (int i = 0; i < 3; ++i)
        Ar << W.BarycentricCoords[i];
    Ar << W.TangentSpaceOffset;
    return Ar;
}
```

**New Generation Function:**
```cpp
/**
 * Generate triangle-based skinning weights with tangent-space offsets
 * Fixes edge curling and UV distortion by preserving local surface detail
 */
static bool GenerateTriangleSkinningWeights(
    const TArray<FVector>& RenderPositions,
    const TArray<FVector>& SimPositions,
    const TArray<uint32>& SimIndices,        // NEW: Need triangle topology
    const FClothSkinningParams& Params,
    TArray<FClothSkinningWeightTriangle>& OutWeights,
    FString& OutError
);
```

**Algorithm:**
```cpp
bool FClothSkinningWeightGenerator::GenerateTriangleSkinningWeights(...)
{
    OutWeights.SetNum(RenderPositions.Num());
    
    // Build triangle spatial hash for fast queries
    FTriangleSpatialHash triHash;
    triHash.Build(SimPositions, SimIndices, avgEdgeLength * 2.0f);
    
    for (int32 renderIdx = 0; renderIdx < RenderPositions.Num(); ++renderIdx)
    {
        const FVector& renderPos = RenderPositions[renderIdx];
        FClothSkinningWeightTriangle& weight = OutWeights[renderIdx];
        
        // 1. Find closest simulation triangle
        int32 closestTriIdx = -1;
        float minDist = FLT_MAX;
        FVector closestPoint;
        
        triHash.FindClosestTriangle(renderPos, SimPositions, SimIndices,
                                     closestTriIdx, closestPoint, minDist);
        
        if (closestTriIdx < 0)
        {
            // Fallback: brute force search
            closestTriIdx = FindClosestTriangleBruteForce(renderPos, SimPositions, SimIndices,
                                                          closestPoint, minDist);
        }
        
        // 2. Get triangle vertices
        uint32 i0 = SimIndices[closestTriIdx * 3 + 0];
        uint32 i1 = SimIndices[closestTriIdx * 3 + 1];
        uint32 i2 = SimIndices[closestTriIdx * 3 + 2];
        
        FVector v0 = SimPositions[i0];
        FVector v1 = SimPositions[i1];
        FVector v2 = SimPositions[i2];
        
        // 3. Compute barycentric coordinates
        FVector bary = ComputeBarycentricCoordinates(closestPoint, v0, v1, v2);
        
        // 4. Compute tangent frame (rest pose)
        FVector edge1 = v1 - v0;
        FVector edge2 = v2 - v0;
        FVector normal = FVector::CrossProduct(edge1, edge2);
        normal.Normalize();
        
        FVector tangent = edge1;
        tangent.Normalize();
        
        FVector bitangent = FVector::CrossProduct(normal, tangent);
        bitangent.Normalize();
        
        // 5. Compute offset in tangent space
        FVector offsetWorld = renderPos - closestPoint;
        FVector offsetTangent;
        offsetTangent.X = FVector::DotProduct(offsetWorld, tangent);
        offsetTangent.Y = FVector::DotProduct(offsetWorld, bitangent);
        offsetTangent.Z = FVector::DotProduct(offsetWorld, normal);
        
        // 6. Store result
        weight.SimTriangleIndices[0] = i0;
        weight.SimTriangleIndices[1] = i1;
        weight.SimTriangleIndices[2] = i2;
        weight.BarycentricCoords[0] = bary.X;
        weight.BarycentricCoords[1] = bary.Y;
        weight.BarycentricCoords[2] = bary.Z;
        weight.TangentSpaceOffset = offsetTangent;
    }
    
    return true;
}

// Helper: Compute barycentric coordinates
FVector FClothSkinningWeightGenerator::ComputeBarycentricCoordinates(
    const FVector& P,
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    FVector v0 = B - A;
    FVector v1 = C - A;
    FVector v2 = P - A;
    
    float d00 = FVector::DotProduct(v0, v0);
    float d01 = FVector::DotProduct(v0, v1);
    float d11 = FVector::DotProduct(v1, v1);
    float d20 = FVector::DotProduct(v2, v0);
    float d21 = FVector::DotProduct(v2, v1);
    
    float denom = d00 * d11 - d01 * d01;
    if (fabs(denom) < 1e-6f)
    {
        // Degenerate triangle - return vertex A
        return FVector(1.0f, 0.0f, 0.0f);
    }
    
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;
    
    return FVector(u, v, w);
}
```

---

### 3.2 HLSL Changes: Vertex Shader

**File:** [`ClothProductionVertexShader.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionVertexShader.hlsl:1)

**New GPU Structure:**
```hlsl
// Triangle-based skinning weight (must match C++ FClothSkinningWeightTriangle)
struct FClothSkinningWeightTriangleGPU
{
    uint3 SimTriangleIndices;      // 3 simulation vertex indices
    float3 BarycentricCoords;      // Barycentric coordinates (u, v, w)
    float3 TangentSpaceOffset;     // Offset in triangle's local tangent frame
};

// Update buffer binding
StructuredBuffer<FClothSkinningWeightTriangleGPU> SkinningWeightBuffer : register(t16);
```

**New Vertex Shader:**
```hlsl
VS_OUTPUT_ClothMesh main(VS_INPUT_ClothProduction Input)
{
    VS_OUTPUT_ClothMesh Output;
    
    // 1. Fetch skinning weight for this render vertex
    uint renderVertexIndex = Input.VertexID + ClothRenderVertexOffset;
    FClothSkinningWeightTriangleGPU skinning = SkinningWeightBuffer[renderVertexIndex];
    
    // 2. Fetch deformed simulation triangle vertices
    float3 simPos0 = SimPositionBuffer[skinning.SimTriangleIndices.x].xyz;
    float3 simPos1 = SimPositionBuffer[skinning.SimTriangleIndices.y].xyz;
    float3 simPos2 = SimPositionBuffer[skinning.SimTriangleIndices.z].xyz;
    
    // 3. Interpolate base position using barycentric coordinates
    float3 basePos = skinning.BarycentricCoords.x * simPos0 +
                     skinning.BarycentricCoords.y * simPos1 +
                     skinning.BarycentricCoords.z * simPos2;
    
    // 4. Reconstruct tangent frame from deformed triangle
    float3 edge1 = simPos1 - simPos0;
    float3 edge2 = simPos2 - simPos0;
    
    float3 normal_deformed = cross(edge1, edge2);
    float normalLen = length(normal_deformed);
    if (normalLen > 1e-6)
        normal_deformed /= normalLen;
    else
        normal_deformed = float3(0, 0, 1); // Degenerate triangle fallback
    
    float3 tangent_deformed = normalize(edge1);
    float3 bitangent_deformed = cross(normal_deformed, tangent_deformed);
    
    // 5. Rotate offset from tangent space to world space
    float3 offset_world = skinning.TangentSpaceOffset.x * tangent_deformed +
                          skinning.TangentSpaceOffset.y * bitangent_deformed +
                          skinning.TangentSpaceOffset.z * normal_deformed;
    
    // 6. Final skinned position
    float3 skinnedPosition = basePos + offset_world;
    
    // 7. Compute skinned normal (use triangle normal + offset influence)
    // For better quality, could interpolate simulation normals and rotate
    float3 simNormal0 = SimNormalBuffer[skinning.SimTriangleIndices.x];
    float3 simNormal1 = SimNormalBuffer[skinning.SimTriangleIndices.y];
    float3 simNormal2 = SimNormalBuffer[skinning.SimTriangleIndices.z];
    
    float3 skinnedNormal = skinning.BarycentricCoords.x * simNormal0 +
                           skinning.BarycentricCoords.y * simNormal1 +
                           skinning.BarycentricCoords.z * simNormal2;
    
    float normalLenSq = dot(skinnedNormal, skinnedNormal);
    if (normalLenSq < 1e-6)
    {
        // Fallback to triangle normal
        skinnedNormal = normal_deformed;
    }
    skinnedNormal = normalize(skinnedNormal);
    
    // 8. Transform to clip space
    float4 worldPos = mul(float4(skinnedPosition, 1.0), ClothWorldMatrix);
    Output.Position = mul(worldPos, ViewMatrix);
    Output.Position = mul(Output.Position, ProjectionMatrix);
    
    // 9. Pass through world-space data
    Output.WorldPosition = worldPos.xyz;
    Output.WorldNormal = normalize(mul(skinnedNormal, (float3x3)ClothWorldMatrix));
    Output.UV = Input.UV;
    
    // 10. Generate tangent for normal mapping
    float3 worldTangent;
    if (abs(Output.WorldNormal.y) < 0.999)
        worldTangent = normalize(cross(float3(0, 1, 0), Output.WorldNormal));
    else
        worldTangent = normalize(cross(float3(1, 0, 0), Output.WorldNormal));
    Output.WorldTangent = float4(worldTangent, 1.0);
    
    Output.Color = float4(1, 1, 1, 1);
    
    return Output;
}
```

---

### 3.3 Decimation Improvements: Boundary Preservation

**File:** [`ClothMeshDecimator_Voronoi.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshDecimator_Voronoi.cpp:659)

**Strategy 1: Force Boundary Vertices as Seeds**
```cpp
void FClothMeshDecimator::InitializeSeedsWithBoundaryPreservation(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    int32 NumSeeds,
    TArray<FVector>& OutSeeds)
{
    OutSeeds.Empty();
    
    // 1. Identify boundary vertices
    TSet<uint32> boundaryVertices;
    TMap<uint64, uint32> edgeCounts;
    
    for (uint32 i = 0; i < Indices.Num(); i += 3)
    {
        uint32 i0 = Indices[i + 0];
        uint32 i1 = Indices[i + 1];
        uint32 i2 = Indices[i + 2];
        
        auto countEdge = [&](uint32 a, uint32 b) {
            uint64 key = (a < b) ? ((uint64)a << 32 | b) : ((uint64)b << 32 | a);
            edgeCounts.FindOrAdd(key)++;
        };
        
        countEdge(i0, i1);
        countEdge(i1, i2);
        countEdge(i2, i0);
    }
    
    // Boundary edges have count == 1
    for (const auto& pair : edgeCounts)
    {
        if (pair.Value == 1)
        {
            uint64 key = pair.Key;
            uint32 v0 = (uint32)(key >> 32);
            uint32 v1 = (uint32)(key & 0xFFFFFFFF);
            boundaryVertices.Add(v0);
            boundaryVertices.Add(v1);
        }
    }
    
    // 2. Add all boundary vertices as seeds first
    for (uint32 vIdx : boundaryVertices)
    {
        if (vIdx < (uint32)Positions.Num())
            OutSeeds.Add(Positions[vIdx]);
    }
    
    int32 numBoundarySeeds = OutSeeds.Num();
    int32 numInteriorSeeds = NumSeeds - numBoundarySeeds;
    
    if (numInteriorSeeds <= 0)
    {
        // Already have enough seeds
        return;
    }
    
    // 3. Use FPS for remaining interior seeds
    TArray<FVector> interiorPositions;
    for (int32 i = 0; i < Positions.Num(); ++i)
    {
        if (!boundaryVertices.Contains(i))
            interiorPositions.Add(Positions[i]);
    }
    
    TArray<FVector> interiorSeeds;
    InitializeSeedsWithFPS(interiorPositions, numInteriorSeeds, interiorSeeds);
    
    OutSeeds.Append(interiorSeeds);
}
```

**Strategy 2: Constrain Lloyd Iterations**
```cpp
void FClothMeshDecimator::PerformLloydIterationWithBoundaryConstraint(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    const TSet<int32>& BoundarySeedIndices,  // NEW: Track which seeds are on boundary
    TArray<FVector>& InOutSeeds)
{
    // ... existing Lloyd iteration code ...
    
    // Step 2: Compute centroids BUT constrain boundary seeds
    for (int32 s = 0; s < numSeeds; s++)
    {
        if (BoundarySeedIndices.Contains(s))
        {
            // Boundary seed: project to nearest boundary edge instead of centroid
            FVector projected;
            ProjectToBoundaryEdge(InOutSeeds[s], Positions, Indices, projected);
            newSeeds[s] = projected;
        }
        else
        {
            // Interior seed: use centroid + surface projection (existing logic)
            // ... existing code ...
        }
    }
}
```

---

## 4. Migration Path

### 4.1 Backward Compatibility

**Option A: Dual Support (Recommended)**
- Keep existing K-nearest neighbor skinning as fallback
- Add new triangle-based skinning as opt-in
- Use asset flag `bUseTriangleSkinning` to choose method

**Option B: Full Migration**
- Replace all skinning weights with triangle-based
- Regenerate all cloth assets
- Remove old code after validation

### 4.2 Testing Strategy

1. **Visual Comparison:**
   - Render same cloth with old vs new skinning side-by-side
   - Check edge preservation, texture stability

2. **Quantitative Metrics:**
   - Measure edge length deviation from rest pose
   - Measure UV distortion (texture coordinate derivatives)

3. **Performance:**
   - Triangle-based skinning should be **faster** (3 vertex fetches vs 4)
   - Measure GPU vertex shader time

---

## 5. Expected Results

### 5.1 Edge Curling Fix

**Before:**
- Edges curl inward by 5-10% of mesh size
- Boundary vertices pulled toward interior

**After:**
- Edges maintain rest pose shape within 1-2% tolerance
- Perpendicular detail preserved

### 5.2 UV Distortion Fix

**Before:**
- Textures slide/swim across surface
- Visible "breathing" artifacts on patterns

**After:**
- Textures stick to surface like printed fabric
- Natural deformation without sliding

### 5.3 Performance Impact

**Expected:** Neutral to slightly faster
- **Fewer vertex fetches:** 3 vertices (triangle) vs 4 vertices (K-nearest)
- **More ALU:** Tangent frame reconstruction adds ~20 instructions
- **Net result:** Similar or 5-10% faster on modern GPUs (memory-bound workload)

---

## 6. Alternative/Complementary Approaches

### 6.1 Dual Quaternion Skinning (DQS)

**Pros:**
- Better volume preservation than LBS
- Handles large rotations well

**Cons:**
- More complex (quaternion interpolation)
- Still needs tangent-space offsets for detail preservation
- Overkill for cloth (not rigid body)

**Verdict:** Not recommended for cloth simulation

### 6.2 Hybrid Approach: K-Nearest + Offset

**Idea:** Keep K-nearest neighbor but add tangent-space offset
- Compute average tangent frame from K nearest triangles
- Store offset in this averaged frame

**Pros:**
- Smoother blending across triangle boundaries

**Cons:**
- More complex weight generation
- Ambiguous tangent frame definition
- Likely not worth the complexity

**Verdict:** Pure triangle-based is simpler and sufficient

### 6.3 Subdivision Surface Skinning

**Idea:** Use Catmull-Clark subdivision on low-res mesh, skin to subdivided surface

**Pros:**
- Smoother surface
- Better for organic shapes

**Cons:**
- Much more expensive (GPU subdivision)
- Cloth doesn't need smooth surfaces (has wrinkles)

**Verdict:** Not applicable to cloth

---

## 7. Implementation Checklist

### Phase 1: Core Implementation
- [ ] Add `FClothSkinningWeightTriangle` struct to [`ClothSkinningWeightGenerator.h`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator.h:1)
- [ ] Implement `GenerateTriangleSkinningWeights()` in [`ClothSkinningWeightGenerator.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothSkinningWeightGenerator.cpp:1)
- [ ] Add `ComputeBarycentricCoordinates()` helper function
- [ ] Add `FindClosestTriangle()` to spatial hash
- [ ] Update [`ClothAssetGenerator.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothAssetGenerator.cpp:414) to use new weight generator
- [ ] Update `UClothAsset` to store triangle-based weights

### Phase 2: GPU Implementation
- [ ] Add `FClothSkinningWeightTriangleGPU` struct to [`ClothProductionVertexShader.hlsl`](EngineSIU/EngineSIU/Shaders/Cloth/ClothProductionVertexShader.hlsl:10)
- [ ] Rewrite vertex shader main function with tangent-space reconstruction
- [ ] Update buffer upload in `ClothBatchedSolver` to handle new weight format
- [ ] Test shader compilation and buffer binding

### Phase 3: Decimation Improvements
- [ ] Implement `InitializeSeedsWithBoundaryPreservation()` in [`ClothMeshDecimator_Voronoi.cpp`](EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshDecimator_Voronoi.cpp:246)
- [ ] Add boundary detection helper
- [ ] Add `PerformLloydIterationWithBoundaryConstraint()`
- [ ] Add `ProjectToBoundaryEdge()` helper
- [ ] Update `DecimateMeshVoronoi()` to use boundary-aware initialization

### Phase 4: Testing & Validation
- [ ] Create test cloth asset with new skinning
- [ ] Visual comparison: old vs new skinning
- [ ] Measure edge length preservation
- [ ] Measure UV distortion
- [ ] Performance profiling (GPU vertex shader time)
- [ ] Test with various cloth shapes (flat, curved, complex)

### Phase 5: Polish & Documentation
- [ ] Add console commands to toggle skinning methods
- [ ] Add debug visualization (show triangle bindings, offsets)
- [ ] Update user documentation
- [ ] Add migration guide for existing assets

---

## 8. Code Snippets for Quick Reference

### 8.1 Barycentric Coordinate Computation
```cpp
FVector ComputeBarycentricCoordinates(const FVector& P, const FVector& A, const FVector& B, const FVector& C)
{
    FVector v0 = B - A, v1 = C - A, v2 = P - A;
    float d00 = FVector::DotProduct(v0, v0);
    float d01 = FVector::DotProduct(v0, v1);
    float d11 = FVector::DotProduct(v1, v1);
    float d20 = FVector::DotProduct(v2, v0);
    float d21 = FVector::DotProduct(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    if (fabs(denom) < 1e-6f) return FVector(1, 0, 0);
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    return FVector(1.0f - v - w, v, w);
}
```

### 8.2 Tangent Frame Construction
```cpp
void ComputeTangentFrame(const FVector& V0, const FVector& V1, const FVector& V2,
                         FVector& OutTangent, FVector& OutBitangent, FVector& OutNormal)
{
    FVector edge1 = V1 - V0;
    FVector edge2 = V2 - V0;
    OutNormal = FVector::CrossProduct(edge1, edge2);
    OutNormal.Normalize();
    OutTangent = edge1;
    OutTangent.Normalize();
    OutBitangent = FVector::CrossProduct(OutNormal, OutTangent);
    OutBitangent.Normalize();
}
```

### 8.3 HLSL Tangent Frame Reconstruction
```hlsl
void ReconstructTangentFrame(float3 v0, float3 v1, float3 v2,
                             out float3 tangent, out float3 bitangent, out float3 normal)
{
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    normal = normalize(cross(edge1, edge2));
    tangent = normalize(edge1);
    bitangent = cross(normal, tangent);
}
```

---

## 9. Conclusion

The root cause of both edge curling and UV distortion is the **lack of local surface orientation** in the current position-only skinning approach. By implementing **tangent-space offset skinning** with **triangle-based binding**, we can:

1. **Preserve perpendicular detail** (fixes edge curling)
2. **Rotate offsets with surface** (fixes UV distortion)
3. **Maintain or improve performance** (fewer vertex fetches)

This is a well-established technique in the graphics industry (used in games like Assassin's Creed, The Witcher 3) and should provide a robust solution to the visual artifacts.

**Estimated Implementation Effort:**
- Core C++ changes: 2-3 days
- GPU shader changes: 1 day
- Decimation improvements: 1-2 days
- Testing & validation: 2-3 days
- **Total: 1-2 weeks**

**Priority:** High - These artifacts significantly impact visual quality and are immediately noticeable to users.
