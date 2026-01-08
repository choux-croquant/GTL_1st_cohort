/**
 * Cloth Mesh Component - Renderable cloth component
 * Extends ClothComponent with rendering capabilities
 */

#pragma once

#include "ClothComponent.h"
#include "Material/Material.h"

/**
 * Cloth render data structure for passing to render pass
 */
struct FClothRenderData
{
    ID3D11ShaderResourceView *PositionBufferSRV;
    ID3D11ShaderResourceView *NormalBufferSRV;
    const TArray<uint32> *Indices;
    uint32 NumVertices;
    uint32 NumTriangles;
    FMatrix WorldTransform;
    UMaterial *Material;
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
