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
    : WorldTransform(FMatrix::Identity),
      DebugDrawMode(EClothDebugDrawMode::None),
      bIsVisible(true),
      bSimulate(true),
      SpawnTransform(FMatrix::Identity),
      LastEditorTransform(FMatrix::Identity),
      bIsSimulationActive(false)
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
}

void UClothMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // Capture spawn transform
    SpawnTransform = GetWorldMatrix();
    LastEditorTransform = SpawnTransform;

    // Auto-start simulation
    if (GeneratedClothAsset)
    {
        RegisterWithClothWorld();
        bIsSimulationActive = true;  // Lock transform during simulation
    }
}

UObject* UClothMeshComponent::Duplicate(UObject* InOuter)
{
    ThisClass* NewComponent = Cast<ThisClass>(Super::Duplicate(InOuter));
    
    // Copy cloth asset and materials
    NewComponent->GeneratedClothAsset = GeneratedClothAsset;
    NewComponent->Materials = Materials;
    
    // Copy transform state - use CURRENT transform as spawn for duplicate
    NewComponent->SpawnTransform = GetWorldMatrix();
    NewComponent->LastEditorTransform = NewComponent->SpawnTransform;
    
    // Reset simulation state (duplicates start in editor mode)
    NewComponent->bIsSimulationActive = false;
    NewComponent->bIsSimulating = false;
    
    return NewComponent;
}

void UClothMeshComponent::GetRenderData(FClothRenderData &OutData) const
{
    // Initialize to safe defaults
    OutData = FClothRenderData(); // Use default constructor
    OutData.Material = Materials.Num() > 0 ? Materials[0] : nullptr;

    // Check if we're in PIE simulation mode or Edit mode
    if (bUseBatchedMode && ClothInstanceHandle)
    {
        // PIE MODE: Use simulated particle data from GPU
        // Particles are already in world space (transformed during upload)
        
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
        OutData.ParticleOffset = 0;
        OutData.NumVertices = metadata.ParticleCount;
        OutData.IndexOffset = metadata.TriangleOffset * 3;
        OutData.NumTriangles = metadata.TriangleCount;

        // Particles are already in world space - use identity transform
        if (bIsSimulationActive) {
            OutData.WorldTransform = FMatrix::Identity;
        }
        else {
            OutData.WorldTransform = GetWorldMatrix();
        }

        // Mark as batched mode
        OutData.bIsBatchedMode = true;

        // No per-instance index buffer in batched mode
        OutData.Indices = nullptr;
        OutData.IndexBufferSRV = nullptr;
        
        // Production rendering data if available
        if (GeneratedClothAsset && GeneratedClothAsset->bUseRenderMesh)
        {
            OutData.bUseProductionRendering = true;
            OutData.UnifiedRenderVertexBuffer = batchedSolver->GetUnifiedRenderVertexBuffer();
            OutData.UnifiedRenderIndexBuffer = batchedSolver->GetUnifiedRenderIndexBuffer();
            OutData.SkinningWeightBufferSRV = batchedSolver->GetSkinningWeightBufferSRV();
            OutData.TriangleSkinningWeightBufferSRV = batchedSolver->GetTriangleSkinningWeightBufferSRV();
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
    else if (GeneratedClothAsset)
    {
        // EDIT MODE: Use rest positions from asset (in local space)
        // Apply current WorldMatrix to transform to world space for rendering
        // This allows transform manipulation to be reflected immediately
        
        //OutData.bIsBatchedMode = false;
        //OutData.bUseProductionRendering = false;
        
        // Note: In Edit Mode, the renderer will need to apply WorldTransform
        // to the asset's RestPositions (which are in local space)
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

void UClothMeshComponent::ClearMaterial()
{
    Materials.Empty();
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
    // STEP 1: Unregister old instance FIRST (before generating new asset)
    bool bWasRegistered = false;
    if (bAssetGenerated && GeneratedClothAsset)
    {
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Unregistering old cloth instance before regeneration"));
        
        // Check if currently registered
        bWasRegistered = (ClothInstanceHandle != nullptr);
        
        // Unregister from simulation (removes from unified buffers)
        UnregisterFromClothWorld();
        
        // Clear old asset reference
        GeneratedClothAsset = nullptr;
        bAssetGenerated = false;
        
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Old cloth instance unregistered successfully"));
    }
    
    // STEP 2: Validate setup
    FString errorMessage;
    if (!ValidateSetup(errorMessage))
    {
        LastErrorMessage = "Validation failed: " + errorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
        return;
    }

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Starting cloth asset generation..."));

    // STEP 3: Build generation parameters from current component settings
    FClothAssetGenerationParams params = BuildGenerationParams();

    // STEP 4: Generate new asset
    FClothAssetGenerationResult result;

    bool success = FClothAssetGenerator::GenerateClothAssetFromStaticMesh(
        SourceStaticMesh,
        params,
        result
    );

    if (success)
    {
        // STEP 5: Store new generated asset
        GeneratedClothAsset = result.Asset;
        GeneratedClothAsset->SourceMeshName = SourceStaticMesh->GetRenderData()->ObjectName;

        bAssetGenerated = true;

        // STEP 6: Setup materials
        Materials.Empty();
        // SourceStaticMesh path를 가지고 ClothAsset를 불러오기 한 경우 아래의 로직으로 Material세팅
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

        // STEP 7: Register new instance with ClothWorld
        // This uploads new simulation and render data to unified buffers
        RegisterWithClothWorld();
        
        // Update status display
        LastErrorMessage = "";
        
        UE_LOG(ELogLevel::Display, TEXT("ClothMeshComponent: Asset generation and registration complete"));
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
        
        // Initialize transform tracking
        LastEditorTransform = GetWorldMatrix();
        
        UE_LOG(ELogLevel::Display, TEXT("ClothActor: Registered with ClothWorld - MetadataIndex=%d, SimulationActive=%d"),
            ClothInstanceHandle->GetMetadataIndex(), bIsSimulationActive);
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

    // Return to editor placement mode
    bIsSimulationActive = false;

    ClothInstanceHandle = nullptr;
    bRegisteredWithWorld = false;

    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Unregistered - Returned to EDITOR PLACEMENT mode"));
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
