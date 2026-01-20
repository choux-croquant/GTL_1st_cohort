/**
 * Cloth Instance Implementation
 */

#include "ClothInstance.h"
#include "ClothSolver.h"
#include "ClothWorld.h"
#include "Engine/ClothAsset.h"
#include "Components/ClothComponent.h"
#include "Windows/D3D11RHI/GraphicDevice.h"
#include "Windows/D3D11RHI/DXDBufferManager.h"
#include "Windows/D3D11RHI/DXDShaderManager.h"

/**
 * Helper function to calculate explosion force for a particle
 */
static FVector CalculateExplosionForce(const FClothExplosionForce &Explosion, const FVector &ParticlePos)
{
    FVector delta = ParticlePos - Explosion.WorldPosition;
    float distance = delta.Size();

    if (distance < 1e-6f || distance > Explosion.Radius)
    {
        return FVector::ZeroVector;
    }

    // Radial force, falloff with distance
    float falloff = 1.0f - (distance / Explosion.Radius);
    falloff = falloff * falloff; // Quadratic falloff

    FVector direction = delta / distance;
    return direction * Explosion.Strength * falloff;
}

FClothInstance::FClothInstance()
    : Solver(nullptr), ClothWorld(nullptr), NumParticles(0), NumConstraints(0), NumTriangles(0), bIsInitialized(false), bIsActive(true), OwnerComponent(nullptr), ExternalForceAccum(FVector::ZeroVector)
{
}

FClothInstance::~FClothInstance()
{
    Release();
}

bool FClothInstance::Initialize(UClothAsset *InAsset, const FClothConfig &InConfig, FGraphicsDevice *Graphics, FDXDBufferManager *BufferMgr, FDXDShaderManager *ShaderMgr)
{
    if (!InAsset || !InAsset->IsValid() || !Graphics || !BufferMgr || !ShaderMgr)
        return false;

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

    // Combine global and local forces
    FVector totalGravity = SimData.Gravity; // Start with local gravity
    FVector totalWind = SimData.Wind;       // Start with local wind

    if (ClothWorld)
    {
        const FClothGlobalForces &globalForces = ClothWorld->GetGlobalForces();

        // Add global forces (always in world space)
        //totalGravity += globalForces.GlobalGravity;
        totalWind += globalForces.GlobalWind;

        // Evaluate explosion forces for this instance
        // Use average particle position or first particle as reference
        FVector instancePos = FVector::ZeroVector;
        if (SimData.CurrentPositions.Num() > 0)
        {
            instancePos = SimData.CurrentPositions[0]; // Use first particle position
        }

        for (const FClothExplosionForce &explosion : globalForces.Explosions)
        {
            FVector forceToAdd = CalculateExplosionForce(explosion, instancePos);
            ExternalForceAccum += forceToAdd;
        }
    }

    // Apply accumulated external forces
    if (ExternalForceAccum.SizeSquared() > 0.0f)
    {
        Solver->AddExternalForce(ExternalForceAccum);
        ExternalForceAccum = FVector::ZeroVector;
    }

    // Apply combined forces
    Solver->SetGravity(totalGravity);
    Solver->SetWind(totalWind);

    // Run GPU simulation
    Solver->Simulate(DeltaTime);

    // Update simulation data (for readback if needed)
    SimData.CurrentTime += DeltaTime;
}

void FClothInstance::UpdateKinematicData(float DeltaTime)
{
    // Update attachment positions from skeletal bones or static positions
    // This is called before simulation

    if (Attachments.Num() == 0)
        return;

    // Transform attachments to world space based on their type
    for (FClothAttachmentData &attach : Attachments)
    {
        switch (attach.Type)
        {
        case EClothAttachmentType::SkeletalBone:
        {
            // TODO: Get skeletal mesh component reference from owner component
            // For now, skeletal bone attachments need to be updated externally
            // and passed in with WorldPosition already set
            break;
        }

        case EClothAttachmentType::ActorTransform:
        {
            // TODO: Get actor transform from owner component
            // For now, actor transform attachments need to be updated externally
            // and passed in with WorldPosition already set
            break;
        }

        case EClothAttachmentType::WorldPosition:
        default:
        {
            // WorldPosition is already in world space, no update needed
            break;
        }
        }
    }

    // Upload kinematic targets to solver
    if (Solver)
    {
        Solver->UpdateKinematicTargets(Attachments);
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
