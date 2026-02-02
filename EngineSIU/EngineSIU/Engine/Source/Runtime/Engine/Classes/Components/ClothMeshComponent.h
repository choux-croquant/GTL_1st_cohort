/**
 * Cloth Mesh Component - Renderable cloth component
 * Extends ClothComponent with rendering capabilities
 */

#pragma once

#include "ClothComponent.h"
#include "Material/Material.h"

class UStaticMesh;

/**
 * Cloth render data structure for passing to render pass
 * Now supports both legacy (per-instance buffers) and batched (unified buffers) rendering
 */
struct FClothRenderData
{
    // Legacy mode: Per-instance SRVs
    ID3D11ShaderResourceView *PositionBufferSRV;
    ID3D11ShaderResourceView *NormalBufferSRV;
    ID3D11ShaderResourceView *IndexBufferSRV;
    const TArray<uint32> *Indices;

    // Batched mode: Unified index buffer + rendering metadata
    ID3D11Buffer *UnifiedIndexBuffer; // NEW: Direct access to unified D3D11 buffer
    uint32 ParticleOffset;            // Offset into unified position/normal buffer
    uint32 IndexOffset;               // Offset into unified index buffer (in indices, not bytes)

    // Common
    uint32 NumVertices;
    uint32 NumTriangles;
    FMatrix WorldTransform;
    UMaterial *Material;

    // Mode flag
    bool bIsBatchedMode;

    FClothRenderData()
        : PositionBufferSRV(nullptr), NormalBufferSRV(nullptr), IndexBufferSRV(nullptr), Indices(nullptr), UnifiedIndexBuffer(nullptr), ParticleOffset(0), IndexOffset(0), NumVertices(0), NumTriangles(0), WorldTransform(FMatrix::Identity), Material(nullptr), bIsBatchedMode(false)
    {
    }
};

/**
 * Debug draw modes for cloth visualization
 */
enum class EClothDebugDrawMode : uint8
{
    None,        // No debug drawing
    Particles,   // Show simulation particles
    Constraints, // Show distance constraints as lines
    Normals,     // Show vertex normals
    Velocity,    // Color-code by velocity
    Forces       // Show external forces
};

/**
 * Renderable cloth mesh component
 * Combines simulation from ClothComponent with rendering
 */
class UClothMeshComponent : public UClothComponent
{
    DECLARE_CLASS(UClothMeshComponent, UClothComponent)

public:

    UClothMeshComponent();
    virtual ~UClothMeshComponent() override;

    // Component lifecycle
    virtual void InitializeComponent() override;
    virtual void TickComponent(float DeltaTime) override;

    // Rendering interface
    void GetRenderData(FClothRenderData &OutData) const;
    uint32 GetNumMaterials() const { return Materials.Num(); }
    UMaterial *GetMaterial(uint32 Index) const;
    void SetMaterial(uint32 Index, UMaterial *InMaterial);

    // Transform
    void SetWorldTransform(const FMatrix &Transform) { WorldTransform = Transform; }
    const FMatrix &GetWorldTransform() const { return WorldTransform; }

    // Debug visualization
    void SetDebugDrawMode(EClothDebugDrawMode Mode) { DebugDrawMode = Mode; }
    EClothDebugDrawMode GetDebugDrawMode() const { return DebugDrawMode; }

    // Visibility
    void SetVisible(bool bVisible) { bIsVisible = bVisible; }
    bool IsVisible() const { return bIsVisible; }

    // Source static mesh
    UStaticMesh* GetStaticMesh() const { return SourceStaticMesh; }
    void SetStaticMesh(UStaticMesh* Value) { SourceStaticMesh = Value; }

    UStaticMesh* SourceStaticMesh = nullptr;
public:
    // Activeness
    bool bSimulate;

protected:
    // Materials
    TArray<UMaterial *> Materials;

    // Transform
    FMatrix WorldTransform;

    // Debug
    EClothDebugDrawMode DebugDrawMode;

    // Visibility
    bool bIsVisible;
};
