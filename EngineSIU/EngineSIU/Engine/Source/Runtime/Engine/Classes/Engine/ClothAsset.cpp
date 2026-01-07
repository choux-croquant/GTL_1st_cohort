/**
 * Cloth Asset Implementation
 */

#include "ClothAsset.h"
#include "StaticMesh.h"

//IMPLEMENT_CLASS(UClothAsset)

UClothAsset::UClothAsset()
    : SourceMesh(nullptr)
{
}

UClothAsset::~UClothAsset()
{
}

void UClothAsset::InitializeFromMesh(UStaticMesh *InSourceMesh)
{
    if (!InSourceMesh)
    {
        return;
    }

    SourceMesh = InSourceMesh;

    // TODO: Extract mesh data from static mesh
    // This would involve:
    // 1. Getting vertex positions, normals, UVs
    // 2. Getting index buffer
    // 3. Generating constraints from mesh topology
    // 4. Calculating inverse masses
}

void UClothAsset::SerializeAsset(FArchive &Ar)
{
    Super::SerializeAsset(Ar);

    // Serialize cloth data
    Ar << RestPositions;
    Ar << Indices;
    Ar << InvMasses;
    Ar << DistanceConstraints;
    Ar << BendConstraints;
    Ar << AttachmentIndices;
    Ar << VertexPaintData;

    // Serialize config
    Ar << ClothConfig.Mass;
    Ar << ClothConfig.Damping;
    Ar << ClothConfig.Friction;
    Ar << ClothConfig.StretchStiffness;
    Ar << ClothConfig.BendStiffness;
    Ar << ClothConfig.AttachStiffness;
    Ar << ClothConfig.NumIterations;
    Ar << ClothConfig.TimeStep;
    Ar << ClothConfig.bUseXPBD;
    Ar << ClothConfig.AirDrag;
    Ar << ClothConfig.WindStrength;
    Ar << ClothConfig.CollisionThickness;
    Ar << ClothConfig.bEnableSelfCollision;
}

bool UClothAsset::IsValid() const
{
    return RestPositions.Num() > 0 && Indices.Num() > 0;
}
