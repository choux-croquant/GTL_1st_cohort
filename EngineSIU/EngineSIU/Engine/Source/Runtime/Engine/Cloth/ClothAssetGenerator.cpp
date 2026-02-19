/**
 * Cloth Asset Generator Implementation
 */

#include "ClothAssetGenerator.h"
#include "ClothMeshDecimator.h"
#include "ClothSkinningWeightGenerator.h"
#include "Engine/ClothAsset.h"
#include "Engine/ClothMaterial.h"
#include "Engine/StaticMesh.h"
#include "Engine/Asset/StaticMeshAsset.h"
#include "UObject/ObjectFactory.h"
#include "Engine/UserInterface/Console.h"
#include <fstream>
#include <sstream>
#include <string>

// Main generation function
bool FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
    UStaticMesh* SourceMesh,
    const FClothAssetGenerationParams& Params,
    FClothAssetGenerationResult& OutResult)
{
    OutResult.bSuccess = false;
    OutResult.Asset = nullptr;
    
    if (!SourceMesh)
    {
        OutResult.ErrorMessage = "Source mesh is null";
        return false;
    }
    
    FString error;
    
    // Step 1: Extract render mesh data
    FClothRenderMeshData renderMesh;
    if (!ExtractRenderMeshData(SourceMesh, renderMesh, error))
    {
        OutResult.ErrorMessage = error;
        return false;
    }
    
    OutResult.RenderVertexCount = renderMesh.GetVertexCount();
    OutResult.RenderTriangleCount = renderMesh.GetTriangleCount();
    
    // Step 2: Generate simulation mesh via QEM decimation
    FClothSimulationMeshData simMesh;
    if (!GenerateSimulationMesh(renderMesh, Params.DecimationParams, simMesh, error))
    {
        OutResult.ErrorMessage = error;
        return false;
    }
    
    OutResult.SimVertexCount = simMesh.GetVertexCount();
    OutResult.SimTriangleCount = simMesh.GetTriangleCount();
    
    // Step 3: Generate physics constraints
    TArray<FClothDistanceConstraint> distanceConstraints;
    TArray<FClothBendConstraint> bendConstraints;
    TArray<FClothAreaConstraint> areaConstraints;
    TArray<FClothEdgeCollisionConstraint> edgeCollisions;
    
    if (!GeneratePhysicsConstraints(simMesh, Params, distanceConstraints, 
                                     bendConstraints, areaConstraints, 
                                     edgeCollisions, error))
    {
        OutResult.ErrorMessage = error;
        return false;
    }
    
    OutResult.ConstraintCount = distanceConstraints.Num();
    OutResult.BendConstraintCount = bendConstraints.Num();
    OutResult.AreaConstraintCount = areaConstraints.Num();
    OutResult.EdgeCollisionCount = edgeCollisions.Num();
    
    // Step 4: Calculate skinning weights
    FClothSkinningData skinningData;
    if (!CalculateSkinningWeights(renderMesh, simMesh, Params.SkinningParams, 
                                   skinningData, error))
    {
        OutResult.ErrorMessage = error;
        return false;
    }
    
    // Step 5: Package into asset
    OutResult.Asset = PackageIntoAsset(renderMesh, simMesh, skinningData,
                                        distanceConstraints, bendConstraints,
                                        areaConstraints, edgeCollisions, SourceMesh);
    
    if (!OutResult.Asset)
    {
        OutResult.ErrorMessage = "Failed to create asset";
        return false;
    }
    
    OutResult.bSuccess = true;
    return true;
}

bool FClothAssetGenerator::ExtractRenderMeshData(
    UStaticMesh* SourceMesh,
    FClothRenderMeshData& OutRenderMesh,
    FString& OutError)
{
    if (!SourceMesh)
    {
        OutError = "Source mesh is null";
        return false;
    }
    
    FStaticMeshRenderData* renderData = SourceMesh->GetRenderData();
    if (!renderData)
    {
        OutError = "Source mesh has no render data";
        return false;
    }
    
    if (renderData->Vertices.Num() == 0)
    {
        OutError = "Source mesh has no vertices";
        return false;
    }
    
    if (renderData->Indices.Num() < 3)
    {
        OutError = "Source mesh has no triangles";
        return false;
    }
    
    // Extract vertex data
    OutRenderMesh.Positions.Empty();
    OutRenderMesh.Normals.Empty();
    OutRenderMesh.UVs.Empty();
    
    OutRenderMesh.Positions.Reserve(renderData->Vertices.Num());
    OutRenderMesh.Normals.Reserve(renderData->Vertices.Num());
    OutRenderMesh.UVs.Reserve(renderData->Vertices.Num());
    
    for (const FStaticMeshVertex& vertex : renderData->Vertices)
    {
        // Extract position
        FVector position(vertex.X, vertex.Y, vertex.Z);
        OutRenderMesh.Positions.Add(position);
        
        // Extract normal
        FVector normal(vertex.NormalX, vertex.NormalY, vertex.NormalZ);
        OutRenderMesh.Normals.Add(normal);
        
        // Extract UV
        FVector2D uv(vertex.U, vertex.V);
        OutRenderMesh.UVs.Add(uv);
    }
    
    // Extract indices
    OutRenderMesh.Indices.Empty();
    OutRenderMesh.Indices.Reserve(renderData->Indices.Num());
    
    for (UINT index : renderData->Indices)
    {
        OutRenderMesh.Indices.Add(static_cast<uint32>(index));
    }
    
    // Validate mesh is manifold (optional but recommended)
    uint32 numTriangles = OutRenderMesh.Indices.Num() / 3;
    if (numTriangles == 0)
    {
        OutError = "No valid triangles found";
        return false;
    }
    
    return true;
}

bool FClothAssetGenerator::GenerateSimulationMesh(
    const FClothRenderMeshData& RenderMesh,
    const FClothDecimationParams& Params,
    FClothSimulationMeshData& OutSimMesh,
    FString& OutError)
{
    // Run QEM decimation
    FClothDecimationResult decimationResult;
    if (!FClothMeshDecimator::DecimateMesh(
        RenderMesh.Positions,
        RenderMesh.Indices,
        RenderMesh.UVs,
        Params,
        decimationResult))
    {
        OutError = "Decimation failed: " + decimationResult.ErrorMessage;
        return false;
    }
    
    // Copy results
    OutSimMesh.Positions = decimationResult.Positions;
    OutSimMesh.Indices = decimationResult.Indices;
    
    // Calculate inverse masses
    // Using default uniform mass distribution
    OutSimMesh.InvMasses.SetNum(OutSimMesh.Positions.Num());
    float particleMass = 1.0f / static_cast<float>(OutSimMesh.Positions.Num());
    float invMass = 1.0f / particleMass;
    
    for (int32 i = 0; i < OutSimMesh.InvMasses.Num(); ++i)
    {
        OutSimMesh.InvMasses[i] = invMass;
    }
    
    return true;
}

bool FClothAssetGenerator::GeneratePhysicsConstraints(
    const FClothSimulationMeshData& SimMesh,
    const FClothAssetGenerationParams& Params,
    TArray<FClothDistanceConstraint>& OutDistanceConstraints,
    TArray<FClothBendConstraint>& OutBendConstraints,
    TArray<FClothAreaConstraint>& OutAreaConstraints,
    TArray<FClothEdgeCollisionConstraint>& OutEdgeCollisions,
    FString& OutError)
{
    OutDistanceConstraints.Empty();
    OutBendConstraints.Empty();
    OutAreaConstraints.Empty();
    OutEdgeCollisions.Empty();
    
    // Build edge map for constraint generation
    TMap<uint64, bool> edges;
    TMap<uint64, TArray<uint32>> edgeToTriangles;
    
    auto getEdgeKey = [](uint32 a, uint32 b) -> uint64
    {
        uint32 minV = (a < b) ? a : b;
        uint32 maxV = (a < b) ? b : a;
        return (static_cast<uint64>(minV) << 32) | static_cast<uint64>(maxV);
    };
    
    uint32 numTriangles = SimMesh.Indices.Num() / 3;
    
    for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
    {
        uint32 i0 = SimMesh.Indices[triIdx * 3 + 0];
        uint32 i1 = SimMesh.Indices[triIdx * 3 + 1];
        uint32 i2 = SimMesh.Indices[triIdx * 3 + 2];
        
        // Add edges
        uint64 e01 = getEdgeKey(i0, i1);
        uint64 e12 = getEdgeKey(i1, i2);
        uint64 e20 = getEdgeKey(i2, i0);
        
        edges.FindOrAdd(e01);
        edges.FindOrAdd(e12);
        edges.FindOrAdd(e20);
        
        edgeToTriangles.FindOrAdd(e01).Add(triIdx);
        edgeToTriangles.FindOrAdd(e12).Add(triIdx);
        edgeToTriangles.FindOrAdd(e20).Add(triIdx);
    }
    
    // Get material parameters if available
    float stretchStiffness = 1.0f;
    float bendStiffness = 1.0f;
    float areaStiffness = 0.1f;
    float restLengthMultiplier = 1.0f;
    
    if (Params.ClothMaterial)
    {
        stretchStiffness = Params.ClothMaterial->StretchStiffness;
        bendStiffness = Params.ClothMaterial->BendStiffness;
        areaStiffness = Params.ClothMaterial->AreaStiffness;
        restLengthMultiplier = Params.ClothMaterial->RestLengthMultiplier;
        
        UE_LOG(ELogLevel::Display, TEXT("Applying ClothMaterial parameters: Stretch=%.3f, Bend=%.3f, Area=%.3f, RestMult=%.3f"),
               stretchStiffness, bendStiffness, areaStiffness, restLengthMultiplier);
    }
    
    // Generate distance constraints
    if (Params.bGenerateDistanceConstraints)
    {
        for (const auto& pair : edges)
        {
            uint64 edgeKey = pair.Key;
            uint32 v0 = static_cast<uint32>(edgeKey >> 32);
            uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);
            
            FVector p0 = SimMesh.Positions[v0];
            FVector p1 = SimMesh.Positions[v1];
            FVector diff = p1 - p0;
            float restLength = sqrtf(diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z);
            
            FClothDistanceConstraint constraint;
            constraint.ParticleA = v0;
            constraint.ParticleB = v1;
            constraint.RestLength = restLength * restLengthMultiplier;  // Apply material multiplier
            constraint.Stiffness = stretchStiffness;  // Apply material stiffness
            
            OutDistanceConstraints.Add(constraint);
        }
    }
    
    // Generate bend constraints
    if (Params.bGenerateBendConstraints)
    {
        for (const auto& pair : edgeToTriangles)
        {
            const TArray<uint32>& tris = pair.Value;
            
            if (tris.Num() == 2)  // Internal edge with 2 adjacent triangles
            {
                uint32 tri0 = tris[0];
                uint32 tri1 = tris[1];
                
                // Get the 4 vertices forming the bend constraint
                // This is simplified - production code would need proper quad detection
                uint64 edgeKey = pair.Key;
                uint32 v0 = static_cast<uint32>(edgeKey >> 32);
                uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);
                
                // Find the other 2 vertices
                uint32 v2 = 0xFFFFFFFF;
                uint32 v3 = 0xFFFFFFFF;
                
                for (int j = 0; j < 3; ++j)
                {
                    uint32 v = SimMesh.Indices[tri0 * 3 + j];
                    if (v != v0 && v != v1)
                    {
                        v2 = v;
                        break;
                    }
                }
                
                for (int j = 0; j < 3; ++j)
                {
                    uint32 v = SimMesh.Indices[tri1 * 3 + j];
                    if (v != v0 && v != v1)
                    {
                        v3 = v;
                        break;
                    }
                }
                
                if (v2 != 0xFFFFFFFF && v3 != 0xFFFFFFFF)
                {
                    FClothBendConstraint constraint;
                    constraint.ParticleA = v0;
                    constraint.ParticleB = v1;
                    constraint.ParticleC = v2;
                    constraint.ParticleD = v3;
                    constraint.RestAngle = 0.0f;  // Flat cloth default
                    constraint.Stiffness = bendStiffness;  // Apply material stiffness
                    
                    OutBendConstraints.Add(constraint);
                }
            }
        }
    }
    
    // Generate area constraints
    if (Params.bGenerateAreaConstraints)
    {
        for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
        {
            uint32 i0 = SimMesh.Indices[triIdx * 3 + 0];
            uint32 i1 = SimMesh.Indices[triIdx * 3 + 1];
            uint32 i2 = SimMesh.Indices[triIdx * 3 + 2];
            
            FVector p0 = SimMesh.Positions[i0];
            FVector p1 = SimMesh.Positions[i1];
            FVector p2 = SimMesh.Positions[i2];
            
            FVector e1 = p1 - p0;
            FVector e2 = p2 - p0;
            FVector cross = FVector::CrossProduct(e1, e2);
            float area = cross.Size() * 0.5f;
            FVector normal = cross.Size() > 1e-6f ? cross / cross.Size() : FVector(0, 0, 1);
            
            FClothAreaConstraint constraint;
            constraint.ParticleA = i0;
            constraint.ParticleB = i1;
            constraint.ParticleC = i2;
            constraint.RestArea = area;
            constraint.RestNormal = normal;
            constraint.Stiffness = areaStiffness;  // Apply material stiffness
            
            OutAreaConstraints.Add(constraint);
        }
    }
    
    // Generate edge collisions
    if (Params.bGenerateEdgeCollisions)
    {
        for (const auto& pair : edges)
        {
            uint64 edgeKey = pair.Key;
            uint32 v0 = static_cast<uint32>(edgeKey >> 32);
            uint32 v1 = static_cast<uint32>(edgeKey & 0xFFFFFFFF);
            
            FVector p0 = SimMesh.Positions[v0];
            FVector p1 = SimMesh.Positions[v1];
            FVector diff = p1 - p0;
            float restLength = sqrtf(diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z);
            
            FClothEdgeCollisionConstraint constraint;
            constraint.ParticleA = v0;
            constraint.ParticleB = v1;
            constraint.RestLength = restLength;
            
            OutEdgeCollisions.Add(constraint);
        }
    }
    
    return true;
}

bool FClothAssetGenerator::CalculateSkinningWeights(
    const FClothRenderMeshData& RenderMesh,
    const FClothSimulationMeshData& SimMesh,
    const FClothSkinningParams& Params,
    FClothSkinningData& OutSkinningData,
    FString& OutError)
{
    // NEW: Use triangle-based skinning weights (fixes edge curling and UV distortion)
    TArray<FClothSkinningWeightTriangle> triangleWeights;
    FString error;
    
    if (!FClothSkinningWeightGenerator::GenerateTriangleSkinningWeights(
        RenderMesh.Positions,
        SimMesh.Positions,
        SimMesh.Indices,
        Params,
        triangleWeights,
        error))
    {
        OutError = "Triangle skinning weight generation failed: " + error;
        return false;
    }
    
    OutSkinningData.TriangleWeights = triangleWeights;
    
    // Also generate legacy K-nearest neighbor weights for backward compatibility
    FClothSkinningResult result;
    if (!FClothSkinningWeightGenerator::GenerateSkinningWeights(
        RenderMesh.Positions,
        SimMesh.Positions,
        Params,
        result))
    {
        OutError = "Legacy skinning weight generation failed: " + result.ErrorMessage;
        return false;
    }
    
    OutSkinningData.Weights = result.Weights;
    return true;
}

UClothAsset* FClothAssetGenerator::PackageIntoAsset(
    const FClothRenderMeshData& RenderMesh,
    const FClothSimulationMeshData& SimMesh,
    const FClothSkinningData& SkinningData,
    const TArray<FClothDistanceConstraint>& DistanceConstraints,
    const TArray<FClothBendConstraint>& BendConstraints,
    const TArray<FClothAreaConstraint>& AreaConstraints,
    const TArray<FClothEdgeCollisionConstraint>& EdgeCollisions,
    UStaticMesh* SourceMesh)
{
    UClothAsset* asset = FObjectFactory::ConstructObject<UClothAsset>(nullptr);
    
    // Set simulation mesh data
    asset->SetRestPositions(SimMesh.Positions);
    asset->SetIndices(SimMesh.Indices);
    asset->SetInvMasses(SimMesh.InvMasses);
    
    // Set constraints
    for (const FClothDistanceConstraint& constraint : DistanceConstraints)
    {
        asset->AddDistanceConstraint(constraint);
    }
    
    for (const FClothBendConstraint& constraint : BendConstraints)
    {
        asset->AddBendConstraint(constraint);
    }
    
    for (const FClothAreaConstraint& constraint : AreaConstraints)
    {
        asset->AddAreaConstraint(constraint);
    }
    
    for (const FClothEdgeCollisionConstraint& constraint : EdgeCollisions)
    {
        asset->AddEdgeCollision(constraint);
    }
    
    // TEST: Attachment points - store in LOCAL space
    // WorldPosition attachments will be transformed during BuildKinematicAttachmentData
    // based on the component's world transform at registration time
    
    //FClothAttachmentData attachment1;
    //attachment1.ClothVertexIndex = 0;
    //attachment1.Type = EClothAttachmentType::WorldPosition;
    //attachment1.Stiffness = 1.0f;
    //attachment1.bIsKinematic = true;
    //attachment1.AttachDistance = 0.0f;
    //// Store in LOCAL space - will be transformed to world space during registration
    //attachment1.WorldPosition = FVector(10, -10, 0);
    //asset->AddAttachmentData(attachment1);

    //// Attach second endpoint
    //FClothAttachmentData attachment2;
    //attachment2.ClothVertexIndex = 3;
    //attachment2.Type = EClothAttachmentType::WorldPosition;
    //attachment2.Stiffness = 1.0f;
    //attachment2.bIsKinematic = true;
    //attachment2.AttachDistance = 0.0f;
    //// Store in LOCAL space - will be transformed to world space during registration
    //attachment1.WorldPosition = FVector(0, 0, 0);
    //asset->AddAttachmentData(attachment2);

    // Set source mesh reference
    asset->SourceMesh = SourceMesh;
    
    // CRITICAL FIX: Store render mesh data and skinning weights
    // This populates the render mesh arrays that are checked in ClothBatchManager::AddInstance()
    asset->bUseRenderMesh = true;
    asset->RenderRestPositions = RenderMesh.Positions;
    asset->RenderNormals = RenderMesh.Normals;
    asset->RenderUVs = RenderMesh.UVs;
    asset->RenderIndices = RenderMesh.Indices;
    asset->SkinningWeights = SkinningData.Weights;  // Legacy K-nearest neighbor weights
    
    // NEW: Store triangle-based skinning weights (fixes edge curling and UV distortion)
    asset->bUseTriangleSkinning = true;
    asset->TriangleSkinningWeights = SkinningData.TriangleWeights;
    
    // Store generation metadata
    asset->OriginalVertexCount = RenderMesh.GetVertexCount();
    asset->DecimatedVertexCount = SimMesh.GetVertexCount();
    
    UE_LOG(ELogLevel::Display, TEXT("ClothAssetGenerator: Packaged asset with render mesh - RenderVerts: %d, SimVerts: %d, LegacyWeights: %d, TriangleWeights: %d"),
           asset->RenderRestPositions.Num(), asset->RestPositions.Num(), asset->SkinningWeights.Num(), asset->TriangleSkinningWeights.Num());
    
    return asset;
}

void FClothAssetGenerator::CalculateInverseMasses(
    const TArray<FVector>& Positions,
    const TArray<uint32>& Indices,
    float TotalMass,
    bool bUseUniformMass,
    TArray<float>& OutInvMasses)
{
    OutInvMasses.SetNum(Positions.Num());
    
    if (bUseUniformMass)
    {
        // Uniform mass distribution
        float particleMass = TotalMass / static_cast<float>(Positions.Num());
        float invMass = (particleMass > 1e-6f) ? (1.0f / particleMass) : 0.0f;
        
        for (int32 i = 0; i < OutInvMasses.Num(); ++i)
        {
            OutInvMasses[i] = invMass;
        }
    }
    else
    {
        // Area-weighted mass distribution
        TArray<float> vertexAreas;
        vertexAreas.SetNum(Positions.Num());
        
        for (int32 i = 0; i < vertexAreas.Num(); ++i)
        {
            vertexAreas[i] = 0.0f;
        }
        
        // Distribute triangle areas to vertices
        uint32 numTriangles = Indices.Num() / 3;
        for (uint32 triIdx = 0; triIdx < numTriangles; ++triIdx)
        {
            uint32 i0 = Indices[triIdx * 3 + 0];
            uint32 i1 = Indices[triIdx * 3 + 1];
            uint32 i2 = Indices[triIdx * 3 + 2];
            
            FVector p0 = Positions[i0];
            FVector p1 = Positions[i1];
            FVector p2 = Positions[i2];
            
            FVector e1 = p1 - p0;
            FVector e2 = p2 - p0;
            FVector cross = FVector::CrossProduct(e1, e2);
            float area = cross.Size() * 0.5f;
            
            // Distribute equally to 3 vertices
            vertexAreas[i0] += area / 3.0f;
            vertexAreas[i1] += area / 3.0f;
            vertexAreas[i2] += area / 3.0f;
        }
        
        // Convert areas to masses
        float totalArea = 0.0f;
        for (float area : vertexAreas)
        {
            totalArea += area;
        }
        
        if (totalArea > 1e-6f)
        {
            for (int32 i = 0; i < OutInvMasses.Num(); ++i)
            {
                float particleMass = (vertexAreas[i] / totalArea) * TotalMass;
                OutInvMasses[i] = (particleMass > 1e-6f) ? (1.0f / particleMass) : 0.0f;
            }
        }
        else
        {
            // Fallback to uniform
            float invMass = static_cast<float>(Positions.Num()) / TotalMass;
            for (int32 i = 0; i < OutInvMasses.Num(); ++i)
            {
                OutInvMasses[i] = invMass;
            }
        }
    }
}

// Helper: Build generation params from ClothMaterial
FClothAssetGenerationParams FClothAssetGenerationParams::FromClothMaterial(
    UClothMaterial* Material,
    const FClothDecimationParams& DecimationParams,
    const FClothSkinningParams& SkinningParams)
{
    FClothAssetGenerationParams params;
    
    // Copy decimation and skinning params
    params.DecimationParams = DecimationParams;
    params.SkinningParams = SkinningParams;
    
    // Set ClothMaterial reference
    params.ClothMaterial = Material;
    
    // Use material's mass if available
    if (Material)
    {
        params.UniformMass = Material->TotalMass;
        params.bUseUniformMass = true;
    }
    
    return params;
}
