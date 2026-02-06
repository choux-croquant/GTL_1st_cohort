/**
 * Cloth Mesh Component - Renderable cloth component
 * Extends ClothComponent with rendering capabilities
 */

#pragma once

#include "ClothComponent.h"
#include "Material/Material.h"
#include "GameFramework/Actor.h"
#include "Cloth/ClothSimulationData.h"
#include "Cloth/ClothBatchTypes.h"
#include "Cloth/ClothAssetGenerator.h"
#include "UObject/ObjectMacros.h"

class UClothMeshComponent;
class UClothAsset;
class UStaticMesh;
class FClothInstanceHandle;
class UStaticMesh;

/**
 * Cloth render data structure for passing to render pass
 */
struct FClothRenderData
{
    // Legacy mode: Per-instance SRVs
    ID3D11ShaderResourceView *PositionBufferSRV;
    ID3D11ShaderResourceView *NormalBufferSRV;
    ID3D11ShaderResourceView *IndexBufferSRV;
    const TArray<uint32> *Indices;

    // Batched mode: Unified index buffer + rendering metadata (simulation mesh)
    ID3D11Buffer *UnifiedIndexBuffer; // Direct access to unified D3D11 buffer
    uint32 ParticleOffset;            // Offset into unified position/normal buffer
    uint32 IndexOffset;               // Offset into unified index buffer (in indices, not bytes)

    // NEW: Production rendering - render mesh data
    ID3D11Buffer *UnifiedRenderVertexBuffer;        // Unified render vertex buffer (position, normal, UV)
    ID3D11Buffer *UnifiedRenderIndexBuffer;         // Unified render index buffer
    ID3D11ShaderResourceView *SkinningWeightBufferSRV;  // Skinning weight buffer SRV
    uint32 RenderVertexOffset;                      // Offset into unified render vertex buffers
    uint32 RenderVertexCount;                       // Number of render vertices
    uint32 RenderIndexOffset;                       // Offset into unified render index buffer
    uint32 RenderIndexCount;                        // Number of render indices
    bool bUseProductionRendering;                   // Toggle production vs debug rendering

    // Common
    uint32 NumVertices;
    uint32 NumTriangles;
    FMatrix WorldTransform;
    UMaterial *Material;

    // Mode flag
    bool bIsBatchedMode;

    FClothRenderData()
        : PositionBufferSRV(nullptr), NormalBufferSRV(nullptr), IndexBufferSRV(nullptr), Indices(nullptr), UnifiedIndexBuffer(nullptr), ParticleOffset(0), IndexOffset(0), UnifiedRenderVertexBuffer(nullptr), UnifiedRenderIndexBuffer(nullptr), SkinningWeightBufferSRV(nullptr), RenderVertexOffset(0), RenderVertexCount(0), RenderIndexOffset(0), RenderIndexCount(0), bUseProductionRendering(false), NumVertices(0), NumTriangles(0), WorldTransform(FMatrix::Identity), Material(nullptr), bIsBatchedMode(false)
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
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime) override;


    UObject* Duplicate(UObject* InOuter);

    // Rendering interface
    void GetRenderData(FClothRenderData &OutData) const;
    uint32 GetNumMaterials() const { return Materials.Num(); }
    UMaterial *GetMaterial(uint32 Index) const;
    void SetMaterial(uint32 Index, UMaterial *InMaterial);

    void GenerateClothAsset();

    void ClearClothAsset();

    bool ValidateSetup(FString& OutErrorMessage);

    void RegisterWithClothWorld();

    void UnregisterFromClothWorld();

    FClothAssetGenerationParams BuildGenerationParams() const;

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

public:
    // ===== EDITOR PROPERTIES (exposed in Detail Panel) =====

    // Source mesh (assigned by artist)
    //UPROPERTY(EditAnywhere, UStaticMesh*, SourceStaticMesh, = nullptr)
    UStaticMesh* GetStaticMesh() const { return SourceStaticMesh; }
    void SetStaticMesh(UStaticMesh* Value) { SourceStaticMesh = Value; }

    UStaticMesh* SourceStaticMesh = nullptr;

    // Generated cloth asset (visible but not editable)
    UPROPERTY(VisibleAnywhere, UClothAsset*, GeneratedClothAsset, = nullptr)

    // === Generation Parameters ===

    UPROPERTY(EditAnywhere, float, SimulationMeshReductionRatio, = 0.1f)  // 0.1 = 10% of original vertices

    UPROPERTY(EditAnywhere, bool, bPreserveBoundaryEdges, = true)

    UPROPERTY(EditAnywhere, bool, bPreserveUVSeams, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateDistanceConstraints, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateBendConstraints, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateAreaConstraints, = true)

    UPROPERTY(EditAnywhere, bool, bGenerateEdgeCollisions, = true)

    UPROPERTY(EditAnywhere, EClothDecimationMethod, DecimationMethod, = EClothDecimationMethod::Voronoi)
        
    // === Simulation Parameters per cloth instance===

    UPROPERTY(EditAnywhere, float, RestLengthMultiplier, = 1.0f)

    UPROPERTY(EditAnywhere, float, StretchStiffness, = 0.9f)

    UPROPERTY(EditAnywhere, float, BendStiffness, = 0.1f)

    UPROPERTY(EditAnywhere, float, AreaStiffness, = 0.001f)

    UPROPERTY(EditAnywhere, float, TotalMass, = 1.0f)

    // === Status Display (read-only) ===

    UPROPERTY(VisibleAnywhere, bool, bAssetGenerated, = false)

    UPROPERTY(VisibleAnywhere, FString, LastErrorMessage, = "")

protected:
    // Materials
    TArray<UMaterial *> Materials;

    // Transform
    FMatrix WorldTransform;

    // Debug
    EClothDebugDrawMode DebugDrawMode;

    // Visibility
    bool bIsVisible;
    bool bClothInitialized;
    bool bRegisteredWithWorld;
};
