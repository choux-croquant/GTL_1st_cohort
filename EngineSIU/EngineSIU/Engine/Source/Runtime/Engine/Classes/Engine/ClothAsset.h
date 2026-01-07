/**
 * Cloth Asset
 * Stores cloth mesh data, simulation parameters, and constraints
 */

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "../../Cloth/ClothSimulationData.h"

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

    // Configuration
    void SetConfig(const FClothConfig &InConfig) { ClothConfig = InConfig; }
    const FClothConfig &GetConfig() const { return ClothConfig; }

    // Data access
    const TArray<FClothLODData> &GetLODData() const { return LODData; }
    const TArray<FVector> &GetRestPositions() const { return RestPositions; }
    const TArray<uint32> &GetIndices() const { return Indices; }
    const TArray<float> &GetInvMasses() const { return InvMasses; }
    const TArray<FClothConstraint> &GetDistanceConstraints() const { return DistanceConstraints; }
    const TArray<FClothConstraint> &GetBendConstraints() const { return BendConstraints; }
    const TArray<uint32> &GetAttachmentIndices() const { return AttachmentIndices; }
    const TArray<FClothVertexPaintData> &GetVertexPaintData() const { return VertexPaintData; }

    // Modifiers
    void SetRestPositions(const TArray<FVector> &InPositions) { RestPositions = InPositions; }
    void SetIndices(const TArray<uint32> &InIndices) { Indices = InIndices; }
    void SetInvMasses(const TArray<float> &InInvMasses) { InvMasses = InInvMasses; }
    void AddDistanceConstraint(const FClothConstraint &Constraint) { DistanceConstraints.Add(Constraint); }
    void AddBendConstraint(const FClothConstraint &Constraint) { BendConstraints.Add(Constraint); }
    void AddAttachmentIndex(uint32 VertexIndex) { AttachmentIndices.Add(VertexIndex); }

    // Serialization
    virtual void SerializeAsset(FArchive &Ar) override;

    // Validation
    bool IsValid() const;

public:
    // Asset data
    TArray<FClothLODData> LODData;

    FClothConfig ClothConfig;

    // Source mesh reference (optional)
    UStaticMesh *SourceMesh;

    // Physics simulation data
    TArray<FVector> RestPositions;

    TArray<uint32> Indices;

    TArray<float> InvMasses;

    // Constraints
    TArray<FClothConstraint> DistanceConstraints;

    TArray<FClothConstraint> BendConstraints;

    TArray<uint32> AttachmentIndices;

    // Per-vertex painting data for authoring
    TArray<FClothVertexPaintData> VertexPaintData;
};
