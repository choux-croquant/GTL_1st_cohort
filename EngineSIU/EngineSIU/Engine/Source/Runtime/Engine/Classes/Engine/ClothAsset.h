/**
 * Cloth Asset
 * Stores cloth mesh data, simulation parameters, and constraints
 * Extended to support render/simulation mesh separation with skinning
 */

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Cloth/ClothSimulationData.h"
#include "Cloth/ClothSkinningWeightGenerator.h"
#include "Cloth/ClothAssetGenerator.h"

class UStaticMesh;

/**
 * Cloth Asset - Stores all data needed for cloth simulation
 * Similar to UStaticMesh but with additional physics data
 */
class UClothAsset : public UObject
{
    DECLARE_CLASS(UClothAsset, UObject)

public:
    UClothAsset();
    virtual ~UClothAsset() override;

    // Asset initialization
    void InitializeFromMesh(UStaticMesh *InSourceMesh);

    // Data access
    const TArray<FClothLODData> &GetLODData() const { return LODData; }
    const TArray<FVector> &GetRestPositions() const { return RestPositions; }
    const TArray<uint32> &GetIndices() const { return Indices; }
    
    // NEW: Base InvMass (immutable, source of truth for instances)
    const TArray<float> &GetBaseInvMasses() const { return BaseInvMasses; }
    
    // DEPRECATED: Old InvMass accessor (for backward compatibility during migration)
    const TArray<float> &GetInvMasses() const { return BaseInvMasses; }
    
    const TArray<FClothDistanceConstraint> &GetDistanceConstraints() const { return DistanceConstraints; }
    const TArray<FClothBendConstraint> &GetBendConstraints() const { return BendConstraints; }
    const TArray<FClothAreaConstraint> &GetAreaConstraints() const { return AreaConstraints; }
    const TArray<FClothEdgeCollisionConstraint> &GetEdgeCollisions() const { return EdgeCollisions; }
    
    // NEW: Attachment capabilities (asset-level metadata)
    const TArray<FClothAttachmentCapability> &GetAttachmentCapabilities() const { return AttachmentCapabilities; }
    
    // DEPRECATED: Old attachment data accessors (for backward compatibility during migration)
    //const TArray<FClothAttachmentData> &GetAttachmentData() const { return AttachmentsData; }
    //const TArray<uint32> &GetAttachmentIndices() const { return AttachmentIndices; }
    
    const TArray<FClothVertexPaintData> &GetVertexPaintData() const { return VertexPaintData; }

    // Modifiers
    void SetRestPositions(const TArray<FVector> &InPositions) { RestPositions = InPositions; }
    void SetIndices(const TArray<uint32> &InIndices) { Indices = InIndices; }
    
    // NEW: Base InvMass setter
    void SetBaseInvMasses(const TArray<float> &InInvMasses) { BaseInvMasses = InInvMasses; }
    
    // DEPRECATED: Old InvMass setter (redirects to BaseInvMasses for backward compatibility)
    void SetInvMasses(const TArray<float> &InInvMasses) { BaseInvMasses = InInvMasses; }
    
    void AddDistanceConstraint(const FClothDistanceConstraint &Constraint) { DistanceConstraints.Add(Constraint); }
    void AddBendConstraint(const FClothBendConstraint &Constraint) { BendConstraints.Add(Constraint); }
    void AddAreaConstraint(const FClothAreaConstraint &Constraint) { AreaConstraints.Add(Constraint); }
    void AddEdgeCollision(const FClothEdgeCollisionConstraint &EdgeCollision) { EdgeCollisions.Add(EdgeCollision); }
    
    // NEW: Attachment capability management
    void AddAttachmentCapability(const FClothAttachmentCapability &Capability) { AttachmentCapabilities.Add(Capability); }
    void ClearAttachmentCapabilities() { AttachmentCapabilities.Empty(); }
    
    // DEPRECATED: Old attachment data methods (for backward compatibility during migration)
    //void AddAttachmentData(const FClothAttachmentData &Data) { AttachmentsData.Add(Data); }
    //void AddAttachmentIndex(uint32 VertexIndex) { AttachmentIndices.Add(VertexIndex); }

    // Serialization
    virtual void SerializeAsset(FArchive &Ar) override;

    // Save/Load to binary file
    bool SaveToFile(const FString& FilePath);
    bool LoadFromFile(const FString& FilePath);
    
    // Get file size estimate
    uint64 GetEstimatedFileSize() const;

    // Validation
    bool IsValid() const;

public:
    // Asset data
    TArray<FClothLODData> LODData;

    // Source mesh reference (optional)
    UStaticMesh *SourceMesh;
    FString SourceMeshName;

    // Simulation mesh data (low-res, used for physics)
    TArray<FVector> RestPositions;  // Simulation mesh positions
    TArray<uint32> Indices;         // Simulation mesh indices
    
    // NEW: Base InvMass (immutable, source of truth for per-instance RuntimeInvMasses)
    TArray<float> BaseInvMasses;
    
    // DEPRECATED: Old InvMass field (kept for backward compatibility during migration)
    // Will be removed after full migration - use BaseInvMasses instead
    TArray<float> InvMasses;

    // NEW: Render mesh data (high-res, used for rendering)
    bool bUseRenderMesh = true;                        // Flag: use render/sim separation
    TArray<FVector> RenderRestPositions;                // High-detail render positions
    TArray<FVector> RenderNormals;                      // Render mesh normals
    TArray<FVector2D> RenderUVs;                        // Render mesh UVs
    TArray<uint32> RenderIndices;                       // Render mesh indices
    TArray<FClothSkinningWeight> SkinningWeights;       // Render → Sim mapping (K-nearest neighbor, legacy)
    
    // NEW: Triangle-based skinning weights (fixes edge curling and UV distortion)
    bool bUseTriangleSkinning = true;                   // Flag: use triangle-based skinning (recommended)
    TArray<FClothSkinningWeightTriangle> TriangleSkinningWeights;  // Render → Sim triangle mapping with tangent-space offsets
    
    // Generation metadata
    float QEMReductionRatio = 0.1f;                     // How much was the sim mesh reduced
    uint32 OriginalVertexCount = 0;                     // Original render mesh vertex count
    uint32 DecimatedVertexCount = 0;                    // Decimated sim mesh vertex count

    // Constraints
    TArray<FClothDistanceConstraint> DistanceConstraints;

    TArray<FClothBendConstraint> BendConstraints;

    TArray<FClothAreaConstraint> AreaConstraints;
    
    TArray<FClothEdgeCollisionConstraint> EdgeCollisions;

    // NEW: Attachment capabilities (asset-level metadata - which vertices CAN be attached)
    TArray<FClothAttachmentCapability> AttachmentCapabilities;

    // DEPRECATED: Old attachment data (instance-specific, should be in component)
    // Kept for backward compatibility during migration - will be removed
    //TArray<FClothAttachmentData> AttachmentsData;
    //TArray<uint32> AttachmentIndices;

    // Per-vertex painting data for authoring
    TArray<FClothVertexPaintData> VertexPaintData;
};
