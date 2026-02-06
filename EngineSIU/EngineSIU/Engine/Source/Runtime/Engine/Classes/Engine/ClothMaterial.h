/**
 * ClothMaterial.h
 * Stores reusable cloth simulation parameters
 */

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Core/Math/Vector.h"
#include "Core/Container/String.h"

class FArchive;

/**
 * ClothMaterial - Reusable cloth simulation parameters
 * Can be shared across multiple ClothMeshComponents
 */
class UClothMaterial : public UObject
{
	DECLARE_CLASS(UClothMaterial, UObject)

public:
	UClothMaterial();
	virtual ~UClothMaterial() override;

	// Serialization
	virtual void SerializeAsset(FArchive& Ar) override;

	// Save/Load to binary file
	bool SaveToFile(const FString& FilePath);
	bool LoadFromFile(const FString& FilePath);

	// Validation
	bool IsValid() const;

public:
	// === Core Simulation Parameters ===
	
	// Material name for identification
	UPROPERTY(EditAnywhere, FString, MaterialName, = TEXT("DefaultClothMaterial"))
	
	// Constraint stiffness parameters
	UPROPERTY(EditAnywhere, float, StretchStiffness, = 0.9f)  // Range: 0.0 - 1.0
	UPROPERTY(EditAnywhere, float, BendStiffness, = 0.1f)     // Range: 0.0 - 1.0
	UPROPERTY(EditAnywhere, float, AreaStiffness, = 0.001f)   // Range: 0.0 - 1.0
	
	// Mass properties
	UPROPERTY(EditAnywhere, float, TotalMass, = 1.0f)         // Total mass in kg
	UPROPERTY(EditAnywhere, float, Density, = 0.2f)           // Density for area-based mass
	
	// Distance constraint properties
	UPROPERTY(EditAnywhere, float, RestLengthMultiplier, = 1.0f)  // Scale rest lengths
	
	// === Additional Physical Properties ===
	
	// Damping (energy dissipation)
	UPROPERTY(EditAnywhere, float, Damping, = 0.01f)          // Range: 0.0 - 1.0
	
	// Friction
	UPROPERTY(EditAnywhere, float, Friction, = 0.2f)          // Range: 0.0 - 1.0
	
	// Air resistance
	UPROPERTY(EditAnywhere, float, AirResistance, = 0.01f)    // Range: 0.0 - 1.0
	UPROPERTY(EditAnywhere, float, Drag, = 0.05f)             // Drag coefficient
	
	// Thickness for collision
	UPROPERTY(EditAnywhere, float, Thickness, = 0.01f)        // Cloth thickness in meters
	
	// === Solver Parameters ===
	
	// Iteration counts (per-material overrides)
	UPROPERTY(EditAnywhere, int32, SolverIterations, = 5)     // Constraint solver iterations
	
	// Self-collision
	UPROPERTY(EditAnywhere, bool, bEnableSelfCollision, = false)
	UPROPERTY(EditAnywhere, float, SelfCollisionThickness, = 0.02f)
	
	// === Metadata ===
	
	// Version for serialization compatibility
	uint32 Version = 1;
	
	// Creation timestamp
	FString CreationDate;
	
	// Description
	UPROPERTY(EditAnywhere, FString, Description, = TEXT(""))
};
