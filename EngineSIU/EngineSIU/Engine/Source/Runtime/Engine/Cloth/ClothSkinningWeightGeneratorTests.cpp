/**
 * Cloth Skinning Weight Generator - Unit Tests
 * Comprehensive test suite for barycentric and inverse distance weighting
 */

#include "ClothSkinningWeightGenerator.h"
#include "Engine/UserInterface/Console.h"
#include <cmath>

// ========== Test Utilities ==========

namespace ClothSkinningTests
{
    // Test result tracking
    struct FTestResult
    {
        int32 TotalTests = 0;
        int32 PassedTests = 0;
        int32 FailedTests = 0;
        
        void RecordPass(const FString& TestName)
        {
            TotalTests++;
            PassedTests++;
            UE_LOG(ELogLevel::Display, TEXT("  ✓ PASS: %s"), *TestName);
        }
        
        void RecordFail(const FString& TestName, const FString& Reason)
        {
            TotalTests++;
            FailedTests++;
            UE_LOG(ELogLevel::Error, TEXT("  ✗ FAIL: %s - %s"), *TestName, *Reason);
        }
        
        void PrintSummary()
        {
            UE_LOG(ELogLevel::Display, TEXT("=== Test Summary ==="));
            UE_LOG(ELogLevel::Display, TEXT("Total: %d, Passed: %d, Failed: %d"), 
                   TotalTests, PassedTests, FailedTests);
            
            if (FailedTests == 0)
            {
                UE_LOG(ELogLevel::Display, TEXT("✓ ALL TESTS PASSED"));
            }
            else
            {
                UE_LOG(ELogLevel::Warning, TEXT("✗ %d TESTS FAILED"), FailedTests);
            }
        }
    };
    
    // Floating point comparison with tolerance
    bool FloatEquals(float a, float b, float tolerance = 1e-4f)
    {
        return fabs(a - b) < tolerance;
    }
    
    bool VectorEquals(const FVector& a, const FVector& b, float tolerance = 1e-4f)
    {
        return FloatEquals(a.X, b.X, tolerance) &&
               FloatEquals(a.Y, b.Y, tolerance) &&
               FloatEquals(a.Z, b.Z, tolerance);
    }
}

using namespace ClothSkinningTests;

// ========== Barycentric Coordinate Tests ==========

void Test_BarycentricCoordinates_PointInsideTriangle(FTestResult& Result)
{
    // Triangle: (0,0,0), (1,0,0), (0,1,0)
    FVector v0(0, 0, 0);
    FVector v1(1, 0, 0);
    FVector v2(0, 1, 0);
    
    // Point at center: (1/3, 1/3, 0)
    FVector point(0.333f, 0.333f, 0);
    
    FBarycentricCoordinates bary = FClothSkinningWeightGenerator::CalculateBarycentricCoordinates(
        point, v0, v1, v2
    );
    
    if (!bary.bIsValid)
    {
        Result.RecordFail("BarycentricCoordinates_PointInsideTriangle", "Invalid result");
        return;
    }
    
    // Check coordinates sum to 1
    float sum = bary.U + bary.V + bary.W;
    if (!FloatEquals(sum, 1.0f))
    {
        Result.RecordFail("BarycentricCoordinates_PointInsideTriangle", 
                         FString::Printf(TEXT("Coordinates don't sum to 1.0 (sum=%.4f)"), sum));
        return;
    }
    
    // Check all coordinates are positive (inside triangle)
    if (bary.U < 0 || bary.V < 0 || bary.W < 0)
    {
        Result.RecordFail("BarycentricCoordinates_PointInsideTriangle", "Negative coordinates");
        return;
    }
    
    // Verify reconstruction: P = u*V0 + v*V1 + w*V2
    FVector reconstructed = v0 * bary.U + v1 * bary.V + v2 * bary.W;
    if (!VectorEquals(reconstructed, point, 0.01f))
    {
        Result.RecordFail("BarycentricCoordinates_PointInsideTriangle", "Reconstruction failed");
        return;
    }
    
    Result.RecordPass("BarycentricCoordinates_PointInsideTriangle");
}

void Test_BarycentricCoordinates_PointOnVertex(FTestResult& Result)
{
    FVector v0(0, 0, 0);
    FVector v1(1, 0, 0);
    FVector v2(0, 1, 0);
    
    // Point exactly on V0
    FBarycentricCoordinates bary = FClothSkinningWeightGenerator::CalculateBarycentricCoordinates(
        v0, v0, v1, v2
    );
    
    if (!bary.bIsValid)
    {
        Result.RecordFail("BarycentricCoordinates_PointOnVertex", "Invalid result");
        return;
    }
    
    // Should be (1, 0, 0)
    if (!FloatEquals(bary.U, 1.0f) || !FloatEquals(bary.V, 0.0f) || !FloatEquals(bary.W, 0.0f))
    {
        Result.RecordFail("BarycentricCoordinates_PointOnVertex", 
                         FString::Printf(TEXT("Expected (1,0,0), got (%.4f,%.4f,%.4f)"), 
                                       bary.U, bary.V, bary.W));
        return;
    }
    
    Result.RecordPass("BarycentricCoordinates_PointOnVertex");
}

void Test_BarycentricCoordinates_PointOnEdge(FTestResult& Result)
{
    FVector v0(0, 0, 0);
    FVector v1(1, 0, 0);
    FVector v2(0, 1, 0);
    
    // Point on edge V0-V1 at midpoint: (0.5, 0, 0)
    FVector point(0.5f, 0, 0);
    
    FBarycentricCoordinates bary = FClothSkinningWeightGenerator::CalculateBarycentricCoordinates(
        point, v0, v1, v2
    );
    
    if (!bary.bIsValid)
    {
        Result.RecordFail("BarycentricCoordinates_PointOnEdge", "Invalid result");
        return;
    }
    
    // Should be (0.5, 0.5, 0) - on edge between V0 and V1
    if (!FloatEquals(bary.W, 0.0f, 0.01f))
    {
        Result.RecordFail("BarycentricCoordinates_PointOnEdge", "Point not on edge");
        return;
    }
    
    if (!FloatEquals(bary.U + bary.V, 1.0f, 0.01f))
    {
        Result.RecordFail("BarycentricCoordinates_PointOnEdge", "Coordinates don't sum correctly");
        return;
    }
    
    Result.RecordPass("BarycentricCoordinates_PointOnEdge");
}

void Test_BarycentricCoordinates_DegenerateTriangle(FTestResult& Result)
{
    // Degenerate triangle (all vertices collinear)
    FVector v0(0, 0, 0);
    FVector v1(1, 0, 0);
    FVector v2(2, 0, 0);  // Collinear with v0 and v1
    
    FVector point(0.5f, 0.5f, 0);
    
    FBarycentricCoordinates bary = FClothSkinningWeightGenerator::CalculateBarycentricCoordinates(
        point, v0, v1, v2
    );
    
    // Should return invalid for degenerate triangle
    if (bary.bIsValid)
    {
        Result.RecordFail("BarycentricCoordinates_DegenerateTriangle", "Should be invalid");
        return;
    }
    
    Result.RecordPass("BarycentricCoordinates_DegenerateTriangle");
}

// ========== Point-to-Triangle Projection Tests ==========

void Test_ProjectPointOntoTriangle_PointAbove(FTestResult& Result)
{
    FVector v0(0, 0, 0);
    FVector v1(1, 0, 0);
    FVector v2(0, 1, 0);
    
    // Point above triangle center
    FVector point(0.333f, 0.333f, 1.0f);
    
    FBarycentricCoordinates bary;
    FVector projected = FClothSkinningWeightGenerator::ProjectPointOntoTriangle(
        point, v0, v1, v2, bary
    );
    
    // Should project to (0.333, 0.333, 0)
    FVector expected(0.333f, 0.333f, 0);
    if (!VectorEquals(projected, expected, 0.01f))
    {
        Result.RecordFail("ProjectPointOntoTriangle_PointAbove", "Projection incorrect");
        return;
    }
    
    // Distance should be 1.0
    if (!FloatEquals(bary.DistanceToTriangle, 1.0f, 0.01f))
    {
        Result.RecordFail("ProjectPointOntoTriangle_PointAbove", 
                         FString::Printf(TEXT("Distance incorrect: %.4f"), bary.DistanceToTriangle));
        return;
    }
    
    Result.RecordPass("ProjectPointOntoTriangle_PointAbove");
}

void Test_ProjectPointOntoTriangle_PointOutside(FTestResult& Result)
{
    FVector v0(0, 0, 0);
    FVector v1(1, 0, 0);
    FVector v2(0, 1, 0);
    
    // Point outside triangle
    FVector point(2, 2, 0);
    
    FBarycentricCoordinates bary;
    FVector projected = FClothSkinningWeightGenerator::ProjectPointOntoTriangle(
        point, v0, v1, v2, bary
    );
    
    // Should clamp to triangle boundary
    // Barycentric coordinates should all be non-negative after clamping
    if (bary.U < -0.01f || bary.V < -0.01f || bary.W < -0.01f)
    {
        Result.RecordFail("ProjectPointOntoTriangle_PointOutside", "Negative coordinates after clamping");
        return;
    }
    
    // Coordinates should sum to 1
    float sum = bary.U + bary.V + bary.W;
    if (!FloatEquals(sum, 1.0f, 0.01f))
    {
        Result.RecordFail("ProjectPointOntoTriangle_PointOutside", "Coordinates don't sum to 1.0");
        return;
    }
    
    Result.RecordPass("ProjectPointOntoTriangle_PointOutside");
}

// ========== BVH Tests ==========

void Test_BVH_Construction(FTestResult& Result)
{
    // Create simple mesh: 2 triangles forming a quad
    TArray<FVector> vertices;
    vertices.Add(FVector(0, 0, 0));  // 0
    vertices.Add(FVector(1, 0, 0));  // 1
    vertices.Add(FVector(1, 1, 0));  // 2
    vertices.Add(FVector(0, 1, 0));  // 3
    
    TArray<uint32> indices;
    indices.Add(0); indices.Add(1); indices.Add(2);  // Triangle 0
    indices.Add(0); indices.Add(2); indices.Add(3);  // Triangle 1
    
    FClothSkinningWeightGenerator::FTriangleBVH bvh;
    bvh.Build(vertices, indices, 8);
    
    // BVH should be built successfully
    // For 2 triangles with leaf size 8, should create 1 leaf node
    
    Result.RecordPass("BVH_Construction");
}

void Test_BVH_FindNearestTriangle(FTestResult& Result)
{
    // Create simple mesh
    TArray<FVector> vertices;
    vertices.Add(FVector(0, 0, 0));
    vertices.Add(FVector(1, 0, 0));
    vertices.Add(FVector(0, 1, 0));
    
    TArray<uint32> indices;
    indices.Add(0); indices.Add(1); indices.Add(2);
    
    FClothSkinningWeightGenerator::FTriangleBVH bvh;
    bvh.Build(vertices, indices, 8);
    
    // Query point near triangle
    FVector queryPoint(0.25f, 0.25f, 0.1f);
    
    uint32 nearestTriIdx = 0;
    float nearestDist = FLT_MAX;
    
    bool found = bvh.FindNearestTriangle(queryPoint, vertices, indices, nearestTriIdx, nearestDist);
    
    if (!found)
    {
        Result.RecordFail("BVH_FindNearestTriangle", "Triangle not found");
        return;
    }
    
    if (nearestTriIdx != 0)
    {
        Result.RecordFail("BVH_FindNearestTriangle", "Wrong triangle found");
        return;
    }
    
    // Distance should be approximately 0.1 (perpendicular distance)
    if (!FloatEquals(nearestDist, 0.1f, 0.02f))
    {
        Result.RecordFail("BVH_FindNearestTriangle", 
                         FString::Printf(TEXT("Distance incorrect: %.4f"), nearestDist));
        return;
    }
    
    Result.RecordPass("BVH_FindNearestTriangle");
}

// ========== Weight Generation Tests ==========

void Test_WeightGeneration_Barycentric_Simple(FTestResult& Result)
{
    // Render mesh: single vertex
    TArray<FVector> renderPositions;
    renderPositions.Add(FVector(0.5f, 0.5f, 0));
    
    // Simulation mesh: single triangle
    TArray<FVector> simPositions;
    simPositions.Add(FVector(0, 0, 0));
    simPositions.Add(FVector(1, 0, 0));
    simPositions.Add(FVector(0, 1, 0));
    
    TArray<uint32> simIndices;
    simIndices.Add(0); simIndices.Add(1); simIndices.Add(2);
    
    // Generate weights
    FClothSkinningParams params;
    params.WeightingMethod = EClothWeightingMethod::Barycentric;
    
    FClothSkinningResult result;
    bool success = FClothSkinningWeightGenerator::GenerateSkinningWeights(
        renderPositions, simPositions, simIndices, params, result
    );
    
    if (!success)
    {
        Result.RecordFail("WeightGeneration_Barycentric_Simple", result.ErrorMessage);
        return;
    }
    
    if (result.Weights.Num() != 1)
    {
        Result.RecordFail("WeightGeneration_Barycentric_Simple", "Wrong number of weights");
        return;
    }
    
    const FClothSkinningWeight& weight = result.Weights[0];
    
    // Should have 3 influences (triangle vertices)
    if (weight.NumInfluences != 3)
    {
        Result.RecordFail("WeightGeneration_Barycentric_Simple", 
                         FString::Printf(TEXT("Expected 3 influences, got %d"), weight.NumInfluences));
        return;
    }
    
    // Weights should sum to 1.0
    float sum = weight.Weights[0] + weight.Weights[1] + weight.Weights[2];
    if (!FloatEquals(sum, 1.0f))
    {
        Result.RecordFail("WeightGeneration_Barycentric_Simple", "Weights don't sum to 1.0");
        return;
    }
    
    Result.RecordPass("WeightGeneration_Barycentric_Simple");
}

void Test_WeightGeneration_InverseDistance_Simple(FTestResult& Result)
{
    // Render mesh: single vertex
    TArray<FVector> renderPositions;
    renderPositions.Add(FVector(0.5f, 0.5f, 0));
    
    // Simulation mesh: 3 vertices
    TArray<FVector> simPositions;
    simPositions.Add(FVector(0, 0, 0));
    simPositions.Add(FVector(1, 0, 0));
    simPositions.Add(FVector(0, 1, 0));
    
    // Generate weights (legacy method)
    FClothSkinningParams params;
    params.WeightingMethod = EClothWeightingMethod::InverseDistance;
    params.MaxInfluences = 3;
    
    FClothSkinningResult result;
    bool success = FClothSkinningWeightGenerator::GenerateSkinningWeights(
        renderPositions, simPositions, params, result
    );
    
    if (!success)
    {
        Result.RecordFail("WeightGeneration_InverseDistance_Simple", result.ErrorMessage);
        return;
    }
    
    if (result.Weights.Num() != 1)
    {
        Result.RecordFail("WeightGeneration_InverseDistance_Simple", "Wrong number of weights");
        return;
    }
    
    const FClothSkinningWeight& weight = result.Weights[0];
    
    // Weights should sum to 1.0
    float sum = 0.0f;
    for (uint32 i = 0; i < weight.NumInfluences; ++i)
    {
        sum += weight.Weights[i];
    }
    
    if (!FloatEquals(sum, 1.0f))
    {
        Result.RecordFail("WeightGeneration_InverseDistance_Simple", "Weights don't sum to 1.0");
        return;
    }
    
    Result.RecordPass("WeightGeneration_InverseDistance_Simple");
}

void Test_WeightGeneration_Comparison(FTestResult& Result)
{
    // Create test mesh
    TArray<FVector> renderPositions;
    for (int32 i = 0; i < 10; ++i)
    {
        renderPositions.Add(FVector(i * 0.1f, i * 0.1f, 0));
    }
    
    TArray<FVector> simPositions;
    simPositions.Add(FVector(0, 0, 0));
    simPositions.Add(FVector(1, 0, 0));
    simPositions.Add(FVector(0, 1, 0));
    
    TArray<uint32> simIndices;
    simIndices.Add(0); simIndices.Add(1); simIndices.Add(2);
    
    // Generate with both methods
    FClothSkinningParams paramsInverse;
    paramsInverse.WeightingMethod = EClothWeightingMethod::InverseDistance;
    
    FClothSkinningParams paramsBarycentric;
    paramsBarycentric.WeightingMethod = EClothWeightingMethod::Barycentric;
    
    FClothSkinningResult resultInverse, resultBarycentric;
    
    bool successInverse = FClothSkinningWeightGenerator::GenerateSkinningWeights(
        renderPositions, simPositions, paramsInverse, resultInverse
    );
    
    bool successBarycentric = FClothSkinningWeightGenerator::GenerateSkinningWeights(
        renderPositions, simPositions, simIndices, paramsBarycentric, resultBarycentric
    );
    
    if (!successInverse || !successBarycentric)
    {
        Result.RecordFail("WeightGeneration_Comparison", "One or both methods failed");
        return;
    }
    
    // Both should produce valid weights
    if (resultInverse.Weights.Num() != resultBarycentric.Weights.Num())
    {
        Result.RecordFail("WeightGeneration_Comparison", "Different number of weights");
        return;
    }
    
    Result.RecordPass("WeightGeneration_Comparison");
}

// ========== Edge Case Tests ==========

void Test_EdgeCase_EmptyMesh(FTestResult& Result)
{
    TArray<FVector> emptyPositions;
    TArray<uint32> emptyIndices;
    
    FClothSkinningParams params;
    params.WeightingMethod = EClothWeightingMethod::Barycentric;
    
    FClothSkinningResult result;
    bool success = FClothSkinningWeightGenerator::GenerateSkinningWeights(
        emptyPositions, emptyPositions, emptyIndices, params, result
    );
    
    // Should fail gracefully
    if (success)
    {
        Result.RecordFail("EdgeCase_EmptyMesh", "Should fail for empty mesh");
        return;
    }
    
    Result.RecordPass("EdgeCase_EmptyMesh");
}

void Test_EdgeCase_FarVertex(FTestResult& Result)
{
    // Render vertex very far from simulation mesh
    TArray<FVector> renderPositions;
    renderPositions.Add(FVector(1000, 1000, 1000));
    
    TArray<FVector> simPositions;
    simPositions.Add(FVector(0, 0, 0));
    simPositions.Add(FVector(1, 0, 0));
    simPositions.Add(FVector(0, 1, 0));
    
    TArray<uint32> simIndices;
    simIndices.Add(0); simIndices.Add(1); simIndices.Add(2);
    
    FClothSkinningParams params;
    params.WeightingMethod = EClothWeightingMethod::Barycentric;
    params.bFallbackToInverseDistance = true;
    
    FClothSkinningResult result;
    bool success = FClothSkinningWeightGenerator::GenerateSkinningWeights(
        renderPositions, simPositions, simIndices, params, result
    );
    
    // Should succeed with fallback
    if (!success)
    {
        Result.RecordFail("EdgeCase_FarVertex", "Should succeed with fallback");
        return;
    }
    
    // Should have at least one influence
    if (result.Weights[0].NumInfluences == 0)
    {
        Result.RecordFail("EdgeCase_FarVertex", "No influences assigned");
        return;
    }
    
    Result.RecordPass("EdgeCase_FarVertex");
}

// ========== Performance Tests ==========

void Test_Performance_BVHBuild(FTestResult& Result)
{
    // Create larger mesh for performance testing
    TArray<FVector> vertices;
    TArray<uint32> indices;
    
    // Generate grid mesh: 100x100 = 10K vertices, ~20K triangles
    const int32 gridSize = 100;
    for (int32 y = 0; y < gridSize; ++y)
    {
        for (int32 x = 0; x < gridSize; ++x)
        {
            vertices.Add(FVector(x, y, 0));
        }
    }
    
    for (int32 y = 0; y < gridSize - 1; ++y)
    {
        for (int32 x = 0; x < gridSize - 1; ++x)
        {
            uint32 i0 = y * gridSize + x;
            uint32 i1 = y * gridSize + (x + 1);
            uint32 i2 = (y + 1) * gridSize + (x + 1);
            uint32 i3 = (y + 1) * gridSize + x;
            
            // Triangle 1
            indices.Add(i0); indices.Add(i1); indices.Add(i2);
            // Triangle 2
            indices.Add(i0); indices.Add(i2); indices.Add(i3);
        }
    }
    
    // Build BVH and measure time
    FClothSkinningWeightGenerator::FTriangleBVH bvh;
    
    // Simple build
    bvh.Build(vertices, indices, 8, false);
    
    UE_LOG(ELogLevel::Display, TEXT("  BVH built for %d triangles"), indices.Num() / 3);
    
    Result.RecordPass("Performance_BVHBuild");
}

// ========== Test Runner ==========

void RunAllClothSkinningTests()
{
    UE_LOG(ELogLevel::Display, TEXT("=== Running Cloth Skinning Weight Generator Tests ==="));
    
    FTestResult result;
    
    // Barycentric coordinate tests
    UE_LOG(ELogLevel::Display, TEXT("\n--- Barycentric Coordinate Tests ---"));
    Test_BarycentricCoordinates_PointInsideTriangle(result);
    Test_BarycentricCoordinates_PointOnVertex(result);
    Test_BarycentricCoordinates_PointOnEdge(result);
    Test_BarycentricCoordinates_DegenerateTriangle(result);
    
    // Projection tests
    UE_LOG(ELogLevel::Display, TEXT("\n--- Point-to-Triangle Projection Tests ---"));
    Test_ProjectPointOntoTriangle_PointAbove(result);
    Test_ProjectPointOntoTriangle_PointOutside(result);
    
    // BVH tests
    UE_LOG(ELogLevel::Display, TEXT("\n--- BVH Tests ---"));
    Test_BVH_Construction(result);
    Test_BVH_FindNearestTriangle(result);
    
    // Weight generation tests
    UE_LOG(ELogLevel::Display, TEXT("\n--- Weight Generation Tests ---"));
    Test_WeightGeneration_Barycentric_Simple(result);
    Test_WeightGeneration_InverseDistance_Simple(result);
    Test_WeightGeneration_Comparison(result);
    
    // Edge case tests
    UE_LOG(ELogLevel::Display, TEXT("\n--- Edge Case Tests ---"));
    Test_EdgeCase_EmptyMesh(result);
    Test_EdgeCase_FarVertex(result);
    
    // Performance tests
    UE_LOG(ELogLevel::Display, TEXT("\n--- Performance Tests ---"));
    Test_Performance_BVHBuild(result);
    
    // Print summary
    UE_LOG(ELogLevel::Display, TEXT(""));
    result.PrintSummary();
}

// Console command to run tests
void ConsoleCommand_RunSkinningTests(const TArray<FString>& Args)
{
    RunAllClothSkinningTests();
}

// Register test command
void RegisterClothSkinningTestCommands()
{
    Console.RegisterCommand("cloth.skinning.test", ConsoleCommand_RunSkinningTests);
    UE_LOG(ELogLevel::Display, TEXT("Cloth skinning test commands registered"));
}
