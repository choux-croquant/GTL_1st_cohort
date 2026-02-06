#include "ClothAsset.h"
#include "StaticMesh.h"
#include "Serialization/ArchiveFileWriter.h"
#include "Serialization/ArchiveFileReader.h"
#include "Engine/UserInterface/Console.h"


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

    // Flags
    uint32 flags = 0;
    if (Ar.IsSaving())
    {
        flags |= (bUseRenderMesh ? 0x01 : 0);
        flags |= (BendConstraints.Num() > 0 ? 0x02 : 0);
        flags |= (AreaConstraints.Num() > 0 ? 0x04 : 0);
        flags |= (EdgeCollisions.Num() > 0 ? 0x08 : 0);
    }
    Ar << flags;
    
    if (Ar.IsLoading())
    {
        bUseRenderMesh = (flags & 0x01) != 0;
    }

    // Simulation mesh data
    Ar << RestPositions;
    Ar << Indices;
    Ar << InvMasses;

    // Render mesh data (if enabled)
    if (bUseRenderMesh)
    {
        Ar << RenderRestPositions;
        Ar << RenderNormals;
        Ar << RenderUVs;
        Ar << RenderIndices;
        Ar << SkinningWeights;
    }

    // Constraints
    Ar << DistanceConstraints;
    
    if (flags & 0x02)
        Ar << BendConstraints;
    
    if (flags & 0x04)
        Ar << AreaConstraints;
    
    if (flags & 0x08)
        Ar << EdgeCollisions;

    // Attachments
    Ar << AttachmentsData;
    Ar << AttachmentIndices;

    // Vertex paint data
    Ar << VertexPaintData;

    // Metadata
    Ar << QEMReductionRatio;
    Ar << OriginalVertexCount;
    Ar << DecimatedVertexCount;
}

bool UClothAsset::SaveToFile(const FString& FilePath)
{
    FArchiveFileWriter Ar(FilePath);
    if (!Ar.IsValid())
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to create file for writing: %s"), *FilePath);
        return false;
    }
    
    // Write magic number "CAST"
    uint32 magic = 0x54534143; // "CAST" in little-endian
    Ar << magic;
    
    // Write version
    uint32 version = 1;
    Ar << version;
    
    // Serialize asset data
    SerializeAsset(Ar);
    
    UE_LOG(ELogLevel::Display, TEXT("ClothAsset saved successfully: %s (Size: %lld bytes)"), 
        *FilePath, Ar.Tell());
    return true;
}

bool UClothAsset::LoadFromFile(const FString& FilePath)
{
    FArchiveFileReader Ar(FilePath);
    if (!Ar.IsValid())
    {
        UE_LOG(ELogLevel::Error, TEXT("Failed to open file for reading: %s"), *FilePath);
        return false;
    }
    
    // Read and verify magic number
    uint32 magic = 0;
    Ar << magic;
    if (magic != 0x54534143)
    {
        UE_LOG(ELogLevel::Error, TEXT("Invalid ClothAsset file format: %s"), *FilePath);
        return false;
    }
    
    // Read version
    uint32 version = 0;
    Ar << version;
    
    if (version != 1)
    {
        UE_LOG(ELogLevel::Warning, TEXT("ClothAsset version mismatch: %d (expected 1)"), version);
    }
    
    // Deserialize asset data
    SerializeAsset(Ar);
    
    UE_LOG(ELogLevel::Display, TEXT("ClothAsset loaded successfully: %s"), *FilePath);
    return true;
}

uint64 UClothAsset::GetEstimatedFileSize() const
{
    uint64 size = 0;
    
    // Header
    size += sizeof(uint32) * 2; // magic + version
    size += sizeof(uint32); // flags
    
    // Simulation mesh
    size += sizeof(uint32) * 2; // vertex count + triangle count
    size += RestPositions.Num() * sizeof(FVector);
    size += Indices.Num() * sizeof(uint32);
    size += InvMasses.Num() * sizeof(float);
    
    // Render mesh (if enabled)
    if (bUseRenderMesh)
    {
        size += sizeof(uint32) * 2; // vertex count + triangle count
        size += RenderRestPositions.Num() * sizeof(FVector);
        size += RenderNormals.Num() * sizeof(FVector);
        size += RenderUVs.Num() * sizeof(FVector2D);
        size += RenderIndices.Num() * sizeof(uint32);
        size += SkinningWeights.Num() * sizeof(FClothSkinningWeight);
    }
    
    // Constraints
    size += sizeof(uint32); // distance constraint count
    size += DistanceConstraints.Num() * sizeof(FClothDistanceConstraint);
    
    if (BendConstraints.Num() > 0)
    {
        size += sizeof(uint32); // bend constraint count
        size += BendConstraints.Num() * sizeof(FClothBendConstraint);
    }
    
    if (AreaConstraints.Num() > 0)
    {
        size += sizeof(uint32); // area constraint count
        size += AreaConstraints.Num() * sizeof(FClothAreaConstraint);
    }
    
    if (EdgeCollisions.Num() > 0)
    {
        size += sizeof(uint32); // edge collision count
        size += EdgeCollisions.Num() * sizeof(FClothEdgeCollisionConstraint);
    }
    
    // Attachments
    size += sizeof(uint32); // attachment data count
    size += AttachmentsData.Num() * sizeof(FClothAttachmentData);
    size += sizeof(uint32); // attachment indices count
    size += AttachmentIndices.Num() * sizeof(uint32);
    
    // Vertex paint data
    size += sizeof(uint32); // vertex paint data count
    size += VertexPaintData.Num() * sizeof(FClothVertexPaintData);
    
    // Metadata
    size += sizeof(float); // QEMReductionRatio
    size += sizeof(uint32) * 2; // OriginalVertexCount + DecimatedVertexCount
    
    return size;
}

bool UClothAsset::IsValid() const
{
    return RestPositions.Num() > 0 && Indices.Num() > 0;
}
