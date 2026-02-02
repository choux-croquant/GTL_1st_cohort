/**
 * Cloth Actor Implementation
 * Automated cloth asset generation from Static Mesh with editor integration
 */

#include "ClothActor.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/Asset/StaticMeshAsset.h"
#include "World/World.h"
#include "Cloth/ClothInstanceHandle.h"
#include "Cloth/ClothWorld.h"
#include "Cloth/ClothAssetGenerator.h"
#include "UObject/ObjectFactory.h"

AClothActor::AClothActor()
    : SourceStaticMesh(nullptr)
    , GeneratedClothAsset(nullptr)
    , ClothMeshComponent(nullptr)
    , ClothInstanceHandle(nullptr)
    , bClothInitialized(false)
    , bRegisteredWithWorld(false)
{
    // Create cloth mesh component
    ClothMeshComponent = AddComponent<UClothMeshComponent>(TEXT("ClothMeshComponent"));
    RootComponent = ClothMeshComponent;
    
    // Initialize status
    bAssetGenerated = false;
    LastErrorMessage = "";
}

AClothActor::~AClothActor()
{
    UnregisterFromClothWorld();
}

void AClothActor::BeginPlay()
{
    Super::BeginPlay();
    
    // Guard against double initialization
    if (bClothInitialized)
    {
        return;
    }
    
    // Auto-generate if not already generated and source mesh is assigned
    if (!bAssetGenerated && SourceStaticMesh)
    {
        GenerateClothAsset();
    }
    
    // Register with cloth world for simulation
    if (bAssetGenerated && GeneratedClothAsset)
    {
        RegisterWithClothWorld();
    }
    
    bClothInitialized = true;
}

void AClothActor::PostSpawnInitialize()
{
    // Called after actor spawned in world
    // Can be used for additional setup
}

void AClothActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    // Cloth simulation runs in ClothWorld::Update()
    // This actor just needs to exist - no per-frame logic needed
}

void AClothActor::GenerateClothAsset()
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
        
        // Update status display
        LastErrorMessage = "";
    }
    else
    {
        // Generation failed
        bAssetGenerated = false;
        LastErrorMessage = "Generation failed: " + result.ErrorMessage;
        UE_LOG(ELogLevel::Error, TEXT("ClothActor: %s"), *LastErrorMessage);
    }
    
    UpdateStatusDisplay();
}

void AClothActor::ClearClothAsset()
{
    // Unregister from simulation if active
    UnregisterFromClothWorld();
    
    // Clear asset
    GeneratedClothAsset = nullptr;
    bAssetGenerated = false;
    
    LastErrorMessage = "";
    
    UpdateStatusDisplay();
    
    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Cloth asset cleared"));
}

bool AClothActor::ValidateSetup(FString& OutErrorMessage)
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

void AClothActor::RegisterWithClothWorld()
{
    if (bRegisteredWithWorld || !GeneratedClothAsset || !ClothMeshComponent)
    {
        return;
    }
    
    // TODO Cloth Simulation world validation check
    
    // Set asset to component
    ClothMeshComponent->SetClothAsset(GeneratedClothAsset);
    
    // Start simulation
    ClothMeshComponent->StartSimulation();
    
    // Get instance handle
    ClothInstanceHandle = ClothMeshComponent->GetClothInstanceHandle();
    
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

void AClothActor::UnregisterFromClothWorld()
{
    if (!bRegisteredWithWorld)
    {
        return;
    }
    
    if (ClothMeshComponent)
    {
        ClothMeshComponent->StopSimulation();
    }
    
    ClothInstanceHandle = nullptr;
    bRegisteredWithWorld = false;
    
    UE_LOG(ELogLevel::Display, TEXT("ClothActor: Unregistered from ClothWorld"));
}

void AClothActor::UpdateStatusDisplay()
{
    // Status fields are automatically displayed in Detail Panel
    // This method can trigger UI refresh if needed
}

FClothAssetGenerationParams AClothActor::BuildGenerationParams() const
{
    FClothAssetGenerationParams params;
    
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
    
    // Mass parameters
    params.UniformMass = TotalMass;
    params.bUseUniformMass = true;
    
    return params;
}
