/**
 * Cloth Instance Implementation
 */

#include "ClothInstance.h"
#include "ClothSolver.h"
#include "Engine/ClothAsset.h"
#include "Components/ClothComponent.h"
#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"

FClothInstance::FClothInstance()
    : Solver(nullptr), NumParticles(0), NumConstraints(0), NumTriangles(0), bIsInitialized(false), bIsActive(true), OwnerComponent(nullptr), ExternalForceAccum(FVector::ZeroVector)
{
}

FClothInstance::~FClothInstance()
{
    Release();
}

bool FClothInstance::Initialize(UClothAsset *InAsset, const FClothConfig &InConfig,
                                FGraphicsDevice *Graphics, FDXDBufferManager *BufferMgr, FDXDShaderManager *ShaderMgr)
{
    if (!InAsset || !InAsset->IsValid() || !Graphics || !BufferMgr || !ShaderMgr)
    {
        return false;
    }

    // Store configuration
    Config = InConfig;

    // Copy asset data
    RestPositions = InAsset->GetRestPositions();
    InvMasses = InAsset->GetInvMasses();
    Constraints = InAsset->GetDistanceConstraints();
    Indices = InAsset->GetIndices();

    NumParticles = RestPositions.Num();
    NumConstraints = Constraints.Num();
    NumTriangles = Indices.Num() / 3;

    // Initialize simulation data
    SimData.NumParticles = NumParticles;
    SimData.NumConstraints = NumConstraints;
    SimData.CurrentPositions = RestPositions;
    SimData.CurrentVelocities.SetNum(NumParticles);

    // Zero out velocities
    for (int32 i = 0; i < NumParticles; ++i)
    {
        SimData.CurrentVelocities[i] = FVector::ZeroVector;
    }

    // Create solver for this instance
    Solver = new FClothSolver();
    Solver->Initialize(Graphics, BufferMgr, ShaderMgr);
    Solver->SetupFromAsset(InAsset, Config);

    bIsInitialized = true;
    bIsActive = true;

    return true;
}

void FClothInstance::Release()
{
    // Release solver
    if (Solver)
    {
        Solver->Release();
        delete Solver;
        Solver = nullptr;
    }

    RestPositions.Empty();
    InvMasses.Empty();
    Constraints.Empty();
    Indices.Empty();
    Attachments.Empty();

    SimData.CurrentPositions.Empty();
    SimData.CurrentVelocities.Empty();

    NumParticles = 0;
    NumConstraints = 0;
    NumTriangles = 0;
    bIsInitialized = false;
}

void FClothInstance::Simulate(float DeltaTime)
{
    if (!Solver || !bIsActive || !bIsInitialized)
        return;

    // Apply accumulated external forces
    if (ExternalForceAccum.SizeSquared() > 0.0f)
    {
        Solver->AddExternalForce(ExternalForceAccum);
        ExternalForceAccum = FVector::ZeroVector;
    }

    // Apply instance-specific forces
    Solver->SetGravity(SimData.Gravity);
    Solver->SetWind(SimData.Wind);

    // Run GPU simulation
    Solver->Simulate(DeltaTime);

    // Update simulation data (for readback if needed)
    SimData.CurrentTime += DeltaTime;
}

void FClothInstance::UpdateKinematicData(float DeltaTime)
{
    // Update attachment positions from skeletal bones or static positions
    // This is called before simulation
    // TODO: Transform attachment positions from bone transforms

    if (Solver && Attachments.Num() > 0)
    {
        Solver->UpdateAttachmentConstraints(Attachments);
    }
}

void FClothInstance::AddExternalForce(const FVector &Force)
{
    ExternalForceAccum += Force;
}

void FClothInstance::SetWind(const FVector &WindVelocity)
{
    SimData.Wind = WindVelocity;
}

void FClothInstance::SetGravity(const FVector &InGravity)
{
    SimData.Gravity = InGravity;
}

void FClothInstance::UpdateAttachments(const TArray<FClothAttachmentData> &InAttachments)
{
    Attachments = InAttachments;
}

void FClothInstance::Reset()
{
    if (RestPositions.Num() > 0)
    {
        SimData.CurrentPositions = RestPositions;

        for (int32 i = 0; i < SimData.CurrentVelocities.Num(); ++i)
        {
            SimData.CurrentVelocities[i] = FVector::ZeroVector;
        }
    }

    SimData.CurrentTime = 0.0f;
    ExternalForceAccum = FVector::ZeroVector;

    if (Solver)
    {
        Solver->ResetSimulation();
    }
}
