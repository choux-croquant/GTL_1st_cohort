# Cloth Mesh Decimation Performance Optimization

## Current Performance Analysis (O(N²) Complexity)

### Bottleneck #1: ExecuteEdgeCollapse - Global Index Updates
**Location:** [`ClothMeshDecimator.cpp:523-577`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshDecimator.cpp:523)

```cpp
// O(N) - Scans ENTIRE index buffer for every collapse
for (int32 i = 0; i < Indices.Num(); ++i)
{
    if (Indices[i] == v1)
    {
        Indices[i] = v0;
    }
}

// O(N) - Rebuilds entire index buffer
TArray<uint32> newIndices;
for (int32 i = 0; i < Indices.Num(); i += 3)
{
    // Check and copy triangles
}

// O(N) - Rebuilds ALL connectivity from scratch
Connectivity.Build(Indices, Positions.Num());
```

**Impact:** Each collapse is O(N), with K collapses → O(K×N) → O(N²) worst case

### Bottleneck #2: Static Collapse Queue
**Location:** [`ClothMeshDecimator.cpp:224-229`](../EngineSIU/EngineSIU/Engine/Source/Runtime/Engine/Cloth/ClothMeshDecimator.cpp:224)

```cpp
BuildEdgeCollapseQueue(...)  // Build once
collapseQueue.Sort(...)      // Sort once
// Then linearly scan with queueIndex, never update
```

**Problems:**
- Errors become stale after early collapses
- Process many invalid edges that reference deleted vertices
- Rely on consecutive failure detection to exit
- Cannot adapt to mesh changes

### Performance Metrics (Current)
- **Mesh:** 2,500 vertices → 250 vertices
- **Expected:** ~2,250 collapses
- **Current Complexity:** O(2,250 × 2,500) = ~5.6M operations
- **Observed Time:** Tens of seconds to minutes (pathological cases)

## Optimization Strategy

### Target Complexity: O(E log E)
- **E** = number of edges ≈ 3V for triangle meshes
- **Operations:** E edge collapse candidates × log E heap operations
- **Expected:** ~7,500 edges × log(7,500) ≈ 96K operations
- **Speedup:** ~58× improvement in big-O terms

### Architecture Changes

#### 1. Local Connectivity Updates
Replace global rebuilds with targeted updates:

```cpp
// OLD: Scan entire mesh
for (int32 i = 0; i < Indices.Num(); ++i)
    if (Indices[i] == v1) Indices[i] = v0;

// NEW: Update only affected triangles
TArray<uint32>& triangles = connectivity.VertexToTriangles[v1];
for (uint32 triIdx : triangles)
{
    // Update just this triangle's vertices
}
```

#### 2. Dynamic Priority Heap
Replace static sorted array with min-heap:

```cpp
// OLD: Static queue
TArray<FEdgeCollapse> collapseQueue;
collapseQueue.Sort();
int32 queueIndex = 0;  // Linear scan

// NEW: Dynamic heap
TArray<FEdgeCollapse> collapseHeap;
Heapify();
while (!collapseHeap.IsEmpty())
{
    FEdgeCollapse collapse = HeapPop();
    // ... process ...
    // Add updated edges back to heap
    for (affectedEdge : GetAffectedEdges(v0))
    {
        HeapPush(RecomputeEdgeCollapse(affectedEdge));
    }
}
```

## Implementation Plan

### Phase 1: Incremental Connectivity Updates

#### 1.1 Add Local Update Methods to FMeshConnectivity
```cpp
struct FMeshConnectivity
{
    // Existing
    TMap<uint64, TArray<uint32>> EdgeToTriangles;
    TMap<uint32, TArray<uint32>> VertexToTriangles;
    TMap<uint32, TArray<uint32>> VertexToVertices;
    
    // NEW: Incremental update methods
    void RemoveTriangle(uint32 TriIdx);
    void UpdateVertexInTriangle(uint32 TriIdx, uint32 OldVertex, uint32 NewVertex);
    void RemoveEdge(uint64 EdgeKey);
    void AddEdge(uint64 EdgeKey, uint32 TriIdx);
    TArray<uint64> GetVertexEdges(uint32 VertexIdx) const;
};
```

#### 1.2 Refactor ExecuteEdgeCollapse
```cpp
bool ExecuteEdgeCollapse(...)
{
    // Step 1: Get affected triangles (local, not global)
    TArray<uint32> affectedTriangles;
    GetTrianglesAdjacentToVertex(v1, affectedTriangles);
    
    // Step 2: Update positions and quadrics
    Positions[v0] = Collapse.OptimalPosition;
    Quadrics[v0] = Quadrics[v0] + Quadrics[v1];
    ValidVertices[v1] = false;
    
    // Step 3: Local index updates (only affected triangles)
    TArray<uint32> trianglesToRemove;
    for (uint32 triIdx : affectedTriangles)
    {
        uint32* tri = &Indices[triIdx * 3];
        
        // Replace v1 with v0
        for (int i = 0; i < 3; i++)
        {
            if (tri[i] == v1) tri[i] = v0;
        }
        
        // Check for degeneracy
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0])
        {
            trianglesToRemove.Add(triIdx);
        }
    }
    
    // Step 4: Remove degenerate triangles (mark as invalid)
    for (uint32 triIdx : trianglesToRemove)
    {
        RemoveTriangleFromConnectivity(triIdx);
    }
    
    // Step 5: Update connectivity incrementally
    MergeVertexConnectivity(v0, v1);
    
    return true;
}
```

### Phase 2: Dynamic Priority Heap

#### 2.1 Heap Helper Functions
```cpp
// Binary heap operations on TArray<FEdgeCollapse>
void HeapPush(TArray<FEdgeCollapse>& Heap, const FEdgeCollapse& Collapse);
FEdgeCollapse HeapPop(TArray<FEdgeCollapse>& Heap);
void HeapifyDown(TArray<FEdgeCollapse>& Heap, int32 Index);
void HeapifyUp(TArray<FEdgeCollapse>& Heap, int32 Index);
bool HeapIsEmpty(const TArray<FEdgeCollapse>& Heap);
```

#### 2.2 Edge Recomputation After Collapse
```cpp
TArray<uint64> GetAffectedEdges(uint32 VertexIdx, const FMeshConnectivity& Connectivity)
{
    TArray<uint64> edges;
    
    // All edges incident to this vertex
    const TArray<uint32>* neighbors = Connectivity.VertexToVertices.Find(VertexIdx);
    if (neighbors)
    {
        for (uint32 neighbor : *neighbors)
        {
            edges.Add(Connectivity.GetEdgeKey(VertexIdx, neighbor));
        }
    }
    
    return edges;
}
```

#### 2.3 Main Loop Refactor
```cpp
// Initialize heap
TArray<FEdgeCollapse> collapseHeap;
BuildInitialHeap(positions, connectivity, quadrics, isBoundary, isUVSeam, Params, collapseHeap);

while (currentVertexCount > targetVertexCount && !HeapIsEmpty(collapseHeap))
{
    // Pop best candidate
    FEdgeCollapse collapse = HeapPop(collapseHeap);
    
    // Validate
    if (!validVertices[collapse.V0] || !validVertices[collapse.V1])
        continue;
    if (Params.bPreserveBoundaryEdges && collapse.bIsBoundary)
        continue;
    if (Params.bPreserveUVSeams && collapse.bIsUVSeam)
        continue;
    
    // Execute collapse
    if (ExecuteEdgeCollapse(collapse, positions, indices, quadrics, validVertices, connectivity))
    {
        currentVertexCount--;
        
        // Recompute affected edges and push to heap
        TArray<uint64> affectedEdges = GetAffectedEdges(collapse.V0, connectivity);
        for (uint64 edgeKey : affectedEdges)
        {
            uint32 v0 = static_cast<uint32>(edgeKey >> 32);
            uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);
            
            if (validVertices[v0] && validVertices[v1])
            {
                FEdgeCollapse newCollapse = ComputeEdgeCollapse(
                    v0, v1, positions, quadrics, isBoundary, isUVSeam, Params);
                HeapPush(collapseHeap, newCollapse);
            }
        }
    }
}
```

### Phase 3: Optimizations

#### 3.1 Lazy Heap Cleanup
```cpp
// Instead of removing stale entries, mark them invalid
struct FEdgeCollapse
{
    uint32 V0, V1;
    double Error;
    FVector OptimalPosition;
    bool bIsBoundary;
    bool bIsUVSeam;
    uint32 Timestamp;  // NEW: Detect stale entries
};

// Global timestamp counter
uint32 currentTimestamp = 0;
TMap<uint64, uint32> edgeTimestamps;

// When popping:
FEdgeCollapse collapse = HeapPop(collapseHeap);
uint64 edgeKey = GetEdgeKey(collapse.V0, collapse.V1);
if (edgeTimestamps[edgeKey] != collapse.Timestamp)
{
    continue;  // Stale entry, skip
}
```

#### 3.2 Triangle Removal Strategy
Option A: Mark as invalid (fast, requires compaction at end)
```cpp
TArray<bool> validTriangles;
// Mark invalid, compact later
```

Option B: Swap-and-pop (immediate removal)
```cpp
// Swap with last triangle, adjust connectivity
```

**Recommendation:** Option A (mark invalid) - simpler, cleaner

### Phase 4: Profiling and Validation

#### 4.1 Add Timing
```cpp
#include <chrono>

auto startTime = std::chrono::high_resolution_clock::now();
// ... decimation ...
auto endTime = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

// Log in debug builds
#if !UE_BUILD_SHIPPING
    UE_LOG(LogCloth, Log, TEXT("Decimation: %d->%d vertices in %lld ms, %d collapses"),
        OriginalVertexCount, OutputVertexCount, duration.count(), successfulCollapses);
#endif
```

#### 4.2 Validation
- Ensure output mesh is identical to old algorithm (same QEM decisions)
- Verify no degenerate triangles
- Confirm manifold topology preserved
- Check that boundary/UV seam constraints respected

## Expected Performance Gains

### Before Optimization (O(N²))
- **2,500 → 250 vertices:** 30-60 seconds (pathological cases: minutes)
- **10,000 → 1,000 vertices:** Several minutes to hours

### After Optimization (O(E log E))
- **2,500 → 250 vertices:** < 100ms expected
- **10,000 → 1,000 vertices:** < 500ms expected
- **Speedup:** 300-1000× for typical meshes

## Implementation Checklist

- [ ] Add incremental connectivity update methods
- [ ] Refactor ExecuteEdgeCollapse for local updates
- [ ] Implement heap push/pop/heapify functions
- [ ] Create GetAffectedEdges function
- [ ] Build initial heap from all edges
- [ ] Update main loop to use heap
- [ ] Add edge recomputation after collapse
- [ ] Implement lazy heap cleanup with timestamps
- [ ] Add performance timing
- [ ] Validate correctness against old implementation
- [ ] Test on various mesh sizes
- [ ] Update documentation and comments

## Backwards Compatibility

**Public API:** No changes to `DecimateMeshQEM` signature or `FClothDecimationResult`

**Behavior:** QEM decisions remain identical, only execution speed changes

**Testing:** All existing decimation tests should pass unchanged

## Risk Mitigation

1. **Correctness:** Keep old implementation as fallback during testing
2. **Complexity:** Implement incrementally, test each phase
3. **Memory:** Monitor heap size, consider max heap size limit
4. **Edge Cases:** Test with degenerate meshes, high constraint density

## References

- Garland & Heckbert 1997: "Surface Simplification Using Quadric Error Metrics"
- Hoppe 1996: "Progressive Meshes"
- Standard binary heap algorithms for priority queue implementation
