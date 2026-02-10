#include "ClothMeshAnalysis.h"
#include "Core/Math/MathUtility.h"
#include "Engine/UserInterface/Console.h"
#include <unordered_set>

float FClothMeshAnalysis::ComputeAverageEdgeLength(
	const TArray<FVector>& Positions,
	const TArray<uint32>& Indices)
{
	if (Positions.Num() == 0 || Indices.Num() < 3)
		return 0.0f;
	
	// Use hash set to avoid counting edges twice
	std::unordered_set<uint64> uniqueEdges;
	float totalLength = 0.0f;
	uint32 edgeCount = 0;
	
	// Iterate through triangles
	for (int32 i = 0; i < Indices.Num(); i += 3)
	{
		uint32 idx0 = Indices[i];
		uint32 idx1 = Indices[i + 1];
		uint32 idx2 = Indices[i + 2];
		
		// Process three edges of triangle
		uint32 edges[3][2] = {
			{idx0, idx1},
			{idx1, idx2},
			{idx2, idx0}
		};
		
		for (int32 e = 0; e < 3; ++e)
		{
			uint32 a = edges[e][0];
			uint32 b = edges[e][1];
			
			// Ensure consistent ordering (a < b)
			if (a > b) std::swap(a, b);
			
			// Create unique edge key
			uint64 edgeKey = (static_cast<uint64>(a) << 32) | b;
			
			// Skip if already processed
			if (uniqueEdges.find(edgeKey) != uniqueEdges.end())
				continue;
			
			uniqueEdges.insert(edgeKey);
			
			// Compute edge length
			if (a < static_cast<uint32>(Positions.Num()) && 
				b < static_cast<uint32>(Positions.Num()))
			{
				float length = (Positions[b] - Positions[a]).Size();
				totalLength += length;
				edgeCount++;
			}
		}
	}
	
	if (edgeCount == 0)
		return 0.0f;
	
	return totalLength / edgeCount;
}

void FClothMeshAnalysis::ComputeAABB(
	const TArray<FVector>& Positions,
	FVector& OutMin,
	FVector& OutMax)
{
	if (Positions.Num() == 0)
	{
		OutMin = FVector::ZeroVector;
		OutMax = FVector::ZeroVector;
		return;
	}
	
	OutMin = Positions[0];
	OutMax = Positions[0];
	
	for (int32 i = 1; i < Positions.Num(); ++i)
	{
		OutMin.X = FMath::Min(OutMin.X, Positions[i].X);
		OutMin.Y = FMath::Min(OutMin.Y, Positions[i].Y);
		OutMin.Z = FMath::Min(OutMin.Z, Positions[i].Z);
		
		OutMax.X = FMath::Max(OutMax.X, Positions[i].X);
		OutMax.Y = FMath::Max(OutMax.Y, Positions[i].Y);
		OutMax.Z = FMath::Max(OutMax.Z, Positions[i].Z);
	}
}

void FClothMeshAnalysis::ComputeAdaptiveSpatialHashParams(
	float AvgEdgeLength,
	const FVector& BoundsMin,
	const FVector& BoundsMax,
	uint32 ParticleCount,
	float& OutCellSize,
	float& OutCollisionRadius,
	FVector& OutGridMin,
	FVector& OutGridMax,
	uint32& OutGridDimX,
	uint32& OutGridDimY,
	uint32& OutGridDimZ,
	uint32& OutMaxPerCell)
{
	// Müller et al. recommendations:
	// - Cell size: 1.0~1.5 × average edge length
	// - Collision radius: 0.5 × average edge length
	
	OutCellSize = AvgEdgeLength * 1.5f;
	OutCollisionRadius = AvgEdgeLength * 0.5f;
	
	// Add margin to bounds (2× cell size for safety)
	float margin = OutCellSize * 2.0f;
	OutGridMin = BoundsMin - FVector(margin, margin, margin);
	OutGridMax = BoundsMax + FVector(margin, margin, margin);
	
	// Compute grid dimensions
	FVector extent = OutGridMax - OutGridMin;
	OutGridDimX = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.X / OutCellSize)));
	OutGridDimY = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Y / OutCellSize)));
	OutGridDimZ = FMath::Max(1u, static_cast<uint32>(FMath::CeilToInt(extent.Z / OutCellSize)));
	
	// Clamp grid dimensions to reasonable limits (prevent excessive memory)
	const uint32 MaxGridDim = 128;
	OutGridDimX = FMath::Min(OutGridDimX, MaxGridDim);
	OutGridDimY = FMath::Min(OutGridDimY, MaxGridDim);
	OutGridDimZ = FMath::Min(OutGridDimZ, MaxGridDim);
	
	// Compute max particles per cell
	uint32 avgPerCell = EstimateAverageParticlesPerCell(
		ParticleCount, OutGridDimX, OutGridDimY, OutGridDimZ);
	
	// Use 3× average as max (with minimum of 32)
	OutMaxPerCell = FMath::Max(32u, avgPerCell * 3);
}

uint32 FClothMeshAnalysis::EstimateAverageParticlesPerCell(
	uint32 ParticleCount,
	uint32 GridDimX,
	uint32 GridDimY,
	uint32 GridDimZ)
{
	uint32 totalCells = GridDimX * GridDimY * GridDimZ;
	if (totalCells == 0)
		return 0;
	
	// Assume uniform distribution (conservative estimate)
	return (ParticleCount + totalCells - 1) / totalCells;
}

bool FClothMeshAnalysis::ValidateSelfCollisionSetup(
	float CellSize,
	float CollisionRadius,
	const FVector& GridMin,
	const FVector& GridMax,
	uint32 GridDimX,
	uint32 GridDimY,
	uint32 GridDimZ,
	uint32 MaxParticlesPerCell,
	uint32 ParticleCount)
{
	bool bValid = true;
	
	// Check 1: Cell size should be >= 1.5× collision radius
	float minCellSize = CollisionRadius * 1.5f;
	if (CellSize < minCellSize)
	{
		UE_LOG(ELogLevel::Warning,
			TEXT("Self-Collision: CellSize (%.3f) < 1.5×CollisionRadius (%.3f). May miss collisions!"),
			CellSize, minCellSize);
		bValid = false;
	}
	
	// Check 2: Grid should cover mesh bounds
	FVector extent = GridMax - GridMin;
	FVector computedExtent = FVector(
		GridDimX * CellSize,
		GridDimY * CellSize,
		GridDimZ * CellSize);
	
	if (computedExtent.X < extent.X ||
		computedExtent.Y < extent.Y ||
		computedExtent.Z < extent.Z)
	{
		UE_LOG(ELogLevel::Error,
			TEXT("Self-Collision: Grid doesn't cover mesh bounds! Extent=(%.1f,%.1f,%.1f), Grid=(%.1f,%.1f,%.1f)"),
			extent.X, extent.Y, extent.Z,
			computedExtent.X, computedExtent.Y, computedExtent.Z);
		bValid = false;
	}
	
	// Check 3: Estimate if MaxParticlesPerCell is sufficient
	uint32 totalCells = GridDimX * GridDimY * GridDimZ;
	uint32 avgPerCell = EstimateAverageParticlesPerCell(
		ParticleCount, GridDimX, GridDimY, GridDimZ);
	
	if (MaxParticlesPerCell < avgPerCell * 2)
	{
		UE_LOG(ELogLevel::Warning,
			TEXT("Self-Collision: MaxParticlesPerCell (%u) may be insufficient. Avg=%u, recommend %u"),
			MaxParticlesPerCell, avgPerCell, avgPerCell * 3);
	}
	
	// Check 4: Grid dimensions reasonable
	if (GridDimX > 128 || GridDimY > 128 || GridDimZ > 128)
	{
		float memoryMB = (totalCells * sizeof(uint32) + totalCells * MaxParticlesPerCell * sizeof(uint32)) / (1024.0f * 1024.0f);
		UE_LOG(ELogLevel::Warning,
			TEXT("Self-Collision: Very large grid (%ux%ux%u). Memory usage: ~%.2f MB"),
			GridDimX, GridDimY, GridDimZ, memoryMB);
	}
	
	return bValid;
}
