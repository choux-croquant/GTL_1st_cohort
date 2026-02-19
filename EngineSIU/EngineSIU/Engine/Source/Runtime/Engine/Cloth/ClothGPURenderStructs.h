/**
 * Cloth GPU Render Structures
 * GPU-compatible structures for production cloth rendering with skinning
 */

#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Math/Vector.h"

// Forward declaration
struct FMatrix;

/**
 * GPU-compatible skinning weight structure
 * Maps a render vertex to up to 4 simulation vertices
 * Must match HLSL struct layout exactly
 */
struct FClothSkinningWeightGPU
{
    uint32 SimVertexIndices[4];  // Global simulation vertex indices in unified buffer
    float Weights[4];             // Normalized weights (sum to 1.0)

    FClothSkinningWeightGPU()
    {
        for (int i = 0; i < 4; ++i)
        {
            SimVertexIndices[i] = 0;
            Weights[i] = 0.0f;
        }
    }
};

// Verify structure size for GPU compatibility (32 bytes = 8 floats)
static_assert(sizeof(FClothSkinningWeightGPU) == 32, "FClothSkinningWeightGPU must be 32 bytes for GPU alignment");

/**
 * GPU-compatible triangle-based skinning weight structure
 * Maps a render vertex to a simulation triangle with tangent-space offset
 * Must match HLSL struct layout exactly
 */
struct FClothSkinningWeightTriangleGPU
{
    uint32 SimTriangleIndices[3];  // 3 simulation vertex indices forming triangle (12 bytes)
    uint32 Padding1;                // 4 bytes padding for alignment
    float BarycentricCoords[3];     // Barycentric coordinates (u, v, w) (12 bytes)
    float Padding2;                 // 4 bytes padding for alignment
    FVector TangentSpaceOffset;     // Offset in triangle's local tangent frame (12 bytes)
    float Padding3;                 // 4 bytes padding for alignment
    
    FClothSkinningWeightTriangleGPU()
        : Padding1(0), Padding2(0.0f), Padding3(0.0f)
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

// Verify structure size for GPU compatibility (48 bytes with padding for float3 alignment)
static_assert(sizeof(FClothSkinningWeightTriangleGPU) == 48, "FClothSkinningWeightTriangleGPU must be 48 bytes for GPU alignment");

/**
 * Per-instance constant buffer for cloth rendering
 * Provides offsets and metadata for GPU skinning
 * Must be 16-byte aligned for D3D11
 */
struct FClothInstanceConstants
{
    float ClothWorldMatrix[16];         // 64 bytes (offset 0) - World transform as 4x4 matrix (row-major)
    
    uint32 ClothRenderVertexOffset;     // 4 bytes (offset 64) - Offset into unified render vertex buffers
    uint32 ClothRenderIndexOffset;      // 4 bytes (offset 68) - Offset into unified render index buffer
    uint32 ClothSimParticleOffset;      // 4 bytes (offset 72) - Offset into unified sim particle buffers
    uint32 ClothNumRenderVertices;      // 4 bytes (offset 76) - Number of render vertices
    
    uint32 ClothNumSimParticles;        // 4 bytes (offset 80) - Number of sim particles
    uint32 Padding[3];                  // 12 bytes (offset 84) - Padding to 96 bytes (multiple of 16)

    FClothInstanceConstants()
        : ClothRenderVertexOffset(0)
        , ClothRenderIndexOffset(0)
        , ClothSimParticleOffset(0)
        , ClothNumRenderVertices(0)
        , ClothNumSimParticles(0)
    {
        // Initialize to identity matrix
        for (int i = 0; i < 16; ++i)
            ClothWorldMatrix[i] = 0.0f;
        ClothWorldMatrix[0] = ClothWorldMatrix[5] = ClothWorldMatrix[10] = ClothWorldMatrix[15] = 1.0f;
        
        Padding[0] = Padding[1] = Padding[2] = 0;
    }
    
    // Helper to set from FMatrix (implemented in .cpp to avoid including Matrix.h)
    void SetWorldMatrix(const FMatrix& Matrix);
};

// Verify structure size for constant buffer alignment
static_assert(sizeof(FClothInstanceConstants) == 96, "FClothInstanceConstants must be 96 bytes (multiple of 16)");

/**
 * Render mesh vertex structure (for vertex buffer)
 * Matches the input layout expected by the vertex shader
 */
struct FClothRenderVertex
{
    FVector Position;   // 12 bytes - Rest position
    FVector Normal;     // 12 bytes - Rest normal
    FVector2D UV;       // 8 bytes - Texture coordinates
    
    FClothRenderVertex()
        : Position(FVector::ZeroVector)
        , Normal(FVector::ZeroVector)
        , UV(FVector2D::ZeroVector)
    {
    }
    
    FClothRenderVertex(const FVector& InPos, const FVector& InNormal, const FVector2D& InUV)
        : Position(InPos)
        , Normal(InNormal)
        , UV(InUV)
    {
    }
};

// Verify structure size (32 bytes)
static_assert(sizeof(FClothRenderVertex) == 32, "FClothRenderVertex must be 32 bytes");
