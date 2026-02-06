#include "ClothMeshComponent.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothBatchManager.h"
#include "Cloth/ClothBatchedSolver.h"
#include "Cloth/ClothWorld.h"
#include "Classes/Engine/StaticMesh.h"
#include "Classes/Engine/ClothAsset.h"
#include "Classes/Engine/ClothMaterial.h"
#include "Engine/Asset/StaticMeshAsset.h"
#include "UObject/ObjectFactory.h"

UClothMeshComponent::UClothMeshComponent()
    : WorldTransform(FMatrix::Identity), DebugDrawMode(EClothDebugDrawMode::None), bIsVisible(true), bSimulate(true)
{
    // WorldTransform will be updated from component hierarchy in InitializeComponent and TickComponent
}

UClothMeshComponent::~UClothMeshComponent()
{
}

void UClothMeshComponent::InitializeComponent()
{
    Super::InitializeComponent();

    // Initialize world transform from component hierarchy
    // GetWorldMatrix() is inherited from USceneComponent
    WorldTransform = GetWorldMatrix();
}

void UClothMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // Update world transform from component hierarchy every frame
    // This ensures the cloth follows its parent component/actor transforms
    /*WorldTransform = GetWorldMatrix();
    FVector gravity = FTransform(WorldTransform).InverseTransformDirection(ClothInstance->GetClothWorld()->GetGlobalForces().GlobalGravity);
    ClothInstance->SetGravity(gravity);*/
}

void UClothMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // Auto-start simulation
    if (GeneratedClothAsset)
    {
        // TODO? : Register in begin play now
        RegisterWithClothWorld();
    }
}

UObject* UClothMeshComponent::Duplicate(UObject* InOuter)
{
    ThisClass* NewComponent = Cast<ThisClass>(Super::Duplicate(InOuter));
    NewComponent->GeneratedClothAsset = GeneratedClothAsset;
    NewComponent->Materials = Materials;
    NewComponent->bIsSimulating = false;

    /*NewComponent->RegisterWithClothWorld();*/
    return NewComponent;
}

void UClothMeshComponent::GetRenderData(FClothRenderData &OutData) const
{
    // Initialize to safe defaults
    OutData = FClothRenderData(); // Use default constructor
    OutData.Material = Materials.Num() > 0 ? Materials[0] : nullptr;

    // Check which mode we're in
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // Batched mode - get data from batch manager
        FClothBatchManager *batchMgr = ClothInstanceHandle->GetBatchManager();
        if (!batchMgr || !batchMgr->GetSolver())
            return;

        FClothBatchedSolver *batchedSolver = batchMgr->GetSolver();
        const FClothInstanceMetadata &metadata = ClothInstanceHandle->GetMetadata();

        // Set unified simulation buffer SRVs (shared by all instances in batch)
        OutData.PositionBufferSRV = batchedSolver->GetPositionBufferSRV();
        OutData.NormalBufferSRV = batchedSolver->GetNormalBufferSRV();

        // Set unified index buffer (shared by all instances)
        OutData.UnifiedIndexBuffer = batchedSolver->GetUnifiedIndexBuffer();

        // Set instance-specific offsets and counts (simulation mesh)
        // CRITICAL FIX: ParticleOffset = 0 because indices in unified buffer are ALREADY global
        // They were converted to global during upload (localIdx + ParticleOffset)
        // Adding offset again in shader would cause double offset bug
        OutData.ParticleOffset = 0;
        OutData.NumVertices = metadata.ParticleCount;
        OutData.IndexOffset = metadata.TriangleOffset * 3; // Convert triangle offset to index offset
        OutData.NumTriangles = metadata.TriangleCount;

        // CRITICAL FIX: Use Identity transform for batched mode
        // Particles are already in WORLD space (transformed during upload)
        // No additional transform needed in vertex shader
        OutData.WorldTransform = FMatrix::Identity;

        // Mark as batched mode
        OutData.bIsBatchedMode = true;

        // No per-instance index buffer in batched mode (indices are in unified buffer)
        OutData.Indices = nullptr;
        OutData.IndexBufferSRV = nullptr;
        
        // NEW: Production rendering - populate render mesh data if available
        if (GeneratedClothAsset && GeneratedClothAsset->bUseRenderMesh)
        {
            OutData.bUseProductionRendering = true;
            OutData.UnifiedRenderVertexBuffer = batchedSolver->GetUnifiedRenderVertexBuffer();  // NEW: Actual vertex buffer
            OutData.UnifiedRenderIndexBuffer = batchedSolver->GetUnifiedRenderIndexBuffer();
            OutData.SkinningWeightBufferSRV = batchedSolver->GetSkinningWeightBufferSRV();
            OutData.RenderVertexOffset = metadata.RenderVertexOffset;
            OutData.RenderVertexCount = metadata.RenderVertexCount;
            OutData.RenderIndexOffset = metadata.RenderIndexOffset;
            OutData.RenderIndexCount = metadata.RenderIndexCount;
        }
        else
        {
            OutData.bUseProductionRendering = false;
        }
    }
}

UMaterial *UClothMeshComponent::GetMaterial(uint32 Index) const
{
    if (Index < static_cast<uint32>(Materials.Num()))
    {
        return Materials[Index];
    }
    return nullptr;
}

void UClothMeshComponent::SetMaterial(uint32 Index, UMaterial *InMaterial)
{
    if (Index >= static_cast<uint32>(Materials.Num()))
    {
        Materials.SetNum(Index + 1);
    }
    Materials[Index] = InMaterial;
}


void UClothMeshComponent::GenerateClothAsset()
{
    // Validate setup
    FString errorMessage;
    if (!ValidateSetup(errorMessage))
    {
        LastErrorMessage = "Validation failed: " + errorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Starting cloth asset generation..."));

    // Build generation parameters from editor properties
    FClothAssetGenerationParams params = BuildGenerationParams();

    // Generate asset
    FClothAssetGenerationResult result;

    bool success = FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
        SourceStaticMesh,
        params,
        result
    );

    if (success)
    {
        // Store generated asset
        if (GeneratedClothAsset)
        {
            // Clean up old asset
            GeneratedClothAsset = nullptr;
        }

        GeneratedClothAsset = result.Asset;
        bAssetGenerated = true;

        // CRITICAL FIX: Extract materials from SourceStaticMesh
        // This ensures cloth rendering uses the same materials as the source mesh
        Materials.Empty();
        if (SourceStaticMesh)
        {
            const TArray<FStaticMaterial*>& sourceMaterials = SourceStaticMesh->GetMaterials();
            Materials.Reserve(sourceMaterials.Num());
            
            for (FStaticMaterial* staticMat : sourceMaterials)
            {
                if (staticMat && staticMat->Material)
                {
                    Materials.Add(staticMat->Material);
                }
                else
                {
                    Materials.Add(nullptr); // Maintain array indexing
                }
            }
            
            UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Extracted %d materials from SourceStaticMesh"),
                   Materials.Num());
        }

        // Update status display
        LastErrorMessage = "";

        // TEST
        this->UnregisterFromClothWorld();
        this->RegisterWithClothWorld();
    }
    else
    {
        // Generation failed
        bAssetGenerated = false;
        LastErrorMessage = "Generation failed: " + result.ErrorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
    }
}

void UClothMeshComponent::ClearClothAsset()
{
    // Unregister from simulation if active
    UnregisterFromClothWorld();

    // Clear asset
    GeneratedClothAsset = nullptr;
    bAssetGenerated = false;

    LastErrorMessage = "";

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Cloth asset cleared"));
}

bool UClothMeshComponent::ValidateSetup(FString& OutErrorMessage)
{
    if (!SourceStaticMesh)
    {
        OutErrorMessage = "Source Static Mesh is not assigned";
        return false;
    }

    FStaticMeshRenderData* renderData = SourceStaticMesh->GetRenderData();
    if (!renderData)
    {
        OutErrorMessage = "Source Static Mesh has no render data";
        return false;
    }

    if (renderData->Vertices.Num() < 3)
    {
        OutErrorMessage = "Source Static Mesh has too few vertices (minimum 3)";
        return false;
    }

    if (renderData->Indices.Num() < 3)
    {
        OutErrorMessage = "Source Static Mesh has no triangles";
        return false;
    }

    if (SimulationMeshReductionRatio <= 0.0f || SimulationMeshReductionRatio > 1.0f)
    {
        OutErrorMessage = "Reduction ratio must be between 0.01 and 1.0";
        return false;
    }

    return true;
}

void UClothMeshComponent::RegisterWithClothWorld()
{
    if (!GeneratedClothAsset)
    {
        return;
    }

    // TODO Cloth Simulation world validation check

    // Set asset to component
    this->SetClothAsset(GeneratedClothAsset);

    // Start simulation
    this->StartSimulation();

    // Get instance handle
    ClothInstanceHandle = this->GetClothInstanceHandle();

    if (ClothInstanceHandle)
    {
        bRegisteredWithWorld = true;
        UE_LOG(ELogLevel::Display, TEXT("ClothActor: Registered with ClothWorld - MetadataIndex=%d"),
            ClothInstanceHandle->GetMetadataIndex());
    }
    else
    {
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: Failed to get ClothInstanceHandle"));
    }
}

void UClothMeshComponent::UnregisterFromClothWorld()
{
    if (!bRegisteredWithWorld)
    {
        return;
    }

    if (this)
    {
        this->StopSimulation();
    }

    ClothInstanceHandle = nullptr;
    bRegisteredWithWorld = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Unregistered from ClothWorld"));
}

FClothAssetGenerationParams UClothMeshComponent::BuildGenerationParams() const
{
    FClothAssetGenerationParams params;

    // Decimation Method
    params.DecimationParams.Method = DecimationMethod;

    // Decimation parameters
    params.DecimationParams.ReductionRatio = SimulationMeshReductionRatio;
    params.DecimationParams.bPreserveBoundaryEdges = bPreserveBoundaryEdges;
    params.DecimationParams.bPreserveUVSeams = bPreserveUVSeams;
    params.DecimationParams.BoundaryWeight = 1000.0f;
    params.DecimationParams.UVSeamWeight = 100.0f;
    params.DecimationParams.MinTriangleArea = 0.001f;
    params.DecimationParams.bValidateResult = true;

    // Skinning parameters
    params.SkinningParams.MaxInfluences = 4;
    params.SkinningParams.MaxDistance = 1000.0f;
    params.SkinningParams.bNormalizeWeights = true;
    params.SkinningParams.bUseInverseDistanceWeighting = true;
    params.SkinningParams.WeightPower = 2.0f;

    // Constraint generation flags
    params.bGenerateDistanceConstraints = bGenerateDistanceConstraints;
    params.bGenerateBendConstraints = bGenerateBendConstraints;
    params.bGenerateAreaConstraints = bGenerateAreaConstraints;
    params.bGenerateEdgeCollisions = bGenerateEdgeCollisions;

    // NEW: Use ClothMaterial if available
    if (ClothMaterial)
    {
        params.ClothMaterial = ClothMaterial;
        params.UniformMass = ClothMaterial->TotalMass;
    }
    else
    {
        // Use component parameters
        params.UniformMass = TotalMass;
    }
    params.bUseUniformMass = true;

    return params;
}

// ClothMaterial helper methods
void UClothMeshComponent::ApplyClothMaterial(UClothMaterial* Material)
{
    if (!Material)
        return;
    
    ClothMaterial = Material;
    
    // Apply material parameters to component
    StretchStiffness = Material->StretchStiffness;
    BendStiffness = Material->BendStiffness;
    AreaStiffness = Material->AreaStiffness;
    TotalMass = Material->TotalMass;
    RestLengthMultiplier = Material->RestLengthMultiplier;
    
    UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Applied ClothMaterial '%s'"), *Material->MaterialName);
}

UClothMaterial* UClothMeshComponent::CreateClothMaterialFromSettings()
{
    UClothMaterial* material = FObjectFactory::ConstructObject<UClothMaterial>(nullptr);
    
    if (material)
    {
        material->MaterialName = TEXT("GeneratedMaterial");
        material->StretchStiffness = StretchStiffness;
        material->BendStiffness = BendStiffness;
        material->AreaStiffness = AreaStiffness;
        material->TotalMass = TotalMass;
        material->RestLengthMultiplier = RestLengthMultiplier;
        
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Created ClothMaterial from settings"));
    }
    
    return material;
}

float UClothMeshComponent::GetEffectiveStretchStiffness() const
{
    return ClothMaterial ? ClothMaterial->StretchStiffness : StretchStiffness;
}

float UClothMeshComponent::GetEffectiveBendStiffness() const
{
    return ClothMaterial ? ClothMaterial->BendStiffness : BendStiffness;
}

float UClothMeshComponent::GetEffectiveAreaStiffness() const
{
    return ClothMaterial ? ClothMaterial->AreaStiffness : AreaStiffness;
}

float UClothMeshComponent::GetEffectiveTotalMass() const
{
    return ClothMaterial ? ClothMaterial->TotalMass : TotalMass;
}

float UClothMeshComponent::GetEffectiveRestLengthMultiplier() const
{
    return ClothMaterial ? ClothMaterial->RestLengthMultiplier : RestLengthMultiplier;
}
