/**
 * ClothMaterial.cpp
 * Implementation of ClothMaterial class
 */

#include "ClothMaterial.h"
#include "Core/Serialization/Archive.h"
#include "Core/Serialization/ArchiveFileWriter.h"
#include "Core/Serialization/ArchiveFileReader.h"
#include "Engine/UserInterface/Console.h"
#include <chrono>
#include <ctime>


UClothMaterial::UClothMaterial()
{
	// Set creation date
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	char buffer[100];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
	CreationDate = FString(buffer);
}
//
UClothMaterial::~UClothMaterial()
{
}

void UClothMaterial::SerializeAsset(FArchive& Ar)
{
	Super::SerializeAsset(Ar);
	
	// Version
	Ar << Version;
	
	// Material name
	Ar << MaterialName;
	
	// Core parameters
	Ar << StretchStiffness;
	Ar << BendStiffness;
	Ar << AreaStiffness;
	Ar << TotalMass;
	Ar << Density;
	Ar << RestLengthMultiplier;
	
	// Additional parameters
	Ar << Damping;
	Ar << Friction;
	Ar << AirResistance;
	Ar << Drag;
	Ar << Thickness;
	
	// Solver parameters
	Ar << SolverIterations;
	Ar << bEnableSelfCollision;
	Ar << SelfCollisionThickness;
	
	// Metadata
	Ar << CreationDate;
	Ar << Description;
}

bool UClothMaterial::SaveToFile(const FString& FilePath)
{
	// Create file archive
	FArchiveFileWriter Ar(FilePath);
	if (!Ar.IsValid())
	{
		UE_LOG(ELogLevel::Error, TEXT("Failed to create file for writing: %s"), *FilePath);
		return false;
	}
	
	// Write magic number "CLMT"
	uint32 magic = 0x544D4C43; // "CLMT" in little-endian
	Ar << magic;
	
	// Serialize
	SerializeAsset(Ar);
	
	UE_LOG(ELogLevel::Display, TEXT("ClothMaterial saved successfully: %s"), *FilePath);
	return true;
}

bool UClothMaterial::LoadFromFile(const FString& FilePath)
{
	// Create file archive
	FArchiveFileReader Ar(FilePath);
	if (!Ar.IsValid())
	{
		UE_LOG(ELogLevel::Error, TEXT("Failed to open file for reading: %s"), *FilePath);
		return false;
	}
	
	// Read and verify magic number
	uint32 magic = 0;
	Ar << magic;
	if (magic != 0x544D4C43)
	{
		UE_LOG(ELogLevel::Error, TEXT("Invalid ClothMaterial file format: %s"), *FilePath);
		return false;
	}
	
	// Deserialize
	SerializeAsset(Ar);
	
	UE_LOG(ELogLevel::Display, TEXT("ClothMaterial loaded successfully: %s"), *FilePath);
	return true;
}

bool UClothMaterial::IsValid() const
{
	// Validate parameter ranges
	if (StretchStiffness < 0.0f || StretchStiffness > 1.0f)
		return false;
	
	if (BendStiffness < 0.0f || BendStiffness > 1.0f)
		return false;
	
	if (AreaStiffness < 0.0f || AreaStiffness > 1.0f)
		return false;
	
	if (TotalMass <= 0.0f)
		return false;
	
	if (Density < 0.0f)
		return false;
	
	if (RestLengthMultiplier <= 0.0f)
		return false;
	
	if (Damping < 0.0f || Damping > 1.0f)
		return false;
	
	if (Friction < 0.0f || Friction > 1.0f)
		return false;
	
	if (AirResistance < 0.0f || AirResistance > 1.0f)
		return false;
	
	if (Drag < 0.0f)
		return false;
	
	if (Thickness <= 0.0f)
		return false;
	
	if (SolverIterations < 1)
		return false;
	
	if (SelfCollisionThickness <= 0.0f)
		return false;
	
	return true;
}
