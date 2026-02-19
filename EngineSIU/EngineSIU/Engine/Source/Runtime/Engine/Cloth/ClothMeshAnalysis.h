#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Math/Vector.h"

/**
 * Cloth Mesh Analysis Utilities
 * Provides functions to analyze mesh topology and compute adaptive parameters
 * for self-collision spatial hashing based on Müller et al. recommendations
 */
class FClothMeshAnalysis
{
public:
	/**
	 * Compute average edge length from mesh topology
	 * Uses hash set to avoid counting edges twice
	 * @param Positions - Vertex positions (world space)
	 * @param Indices - Triangle indices
	 * @return Average edge length in world units
	 */
	static float ComputeAverageEdgeLength(
		const TArray<FVector>& Positions,
		const TArray<uint32>& Indices
	);
	
	/**
	 * Compute axis-aligned bounding box from positions
	 * @param Positions - Vertex positions (world space)
	 * @param OutMin - Output AABB min
	 * @param OutMax - Output AABB max
	 */
	static void ComputeAABB(
		const TArray<FVector>& Positions,
		FVector& OutMin,
		FVector& OutMax
	);
	
	/**
	 * Compute adaptive spatial hash parameters for self-collision
	 * Based on Müller et al. recommendations:
	 * - Cell size: 1.0~1.5 × average edge length
	 * - Collision radius: 0.5 × average edge length
	 * 
	 * @param AvgEdgeLength - Average edge length
	 * @param BoundsMin - Mesh AABB min
	 * @param BoundsMax - Mesh AABB max
	 * @param ParticleCount - Number of particles
	 * @param OutCellSize - Output cell size (AvgEdgeLength × 1.5)
	 * @param OutCollisionRadius - Output collision radius (AvgEdgeLength × 0.5)
	 * @param OutGridMin - Output grid min (BoundsMin - margin)
	 * @param OutGridMax - Output grid max (BoundsMax + margin)
	 * @param OutGridDimX/Y/Z - Output grid dimensions
	 * @param OutMaxPerCell - Output max particles per cell
	 */
	static void ComputeAdaptiveSpatialHashParams(
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
		uint32& OutMaxPerCell
	);
	
	/**
	 * Estimate average particles per cell for capacity planning
	 * Assumes uniform distribution (conservative estimate)
	 */
	static uint32 EstimateAverageParticlesPerCell(
		uint32 ParticleCount,
		uint32 GridDimX,
		uint32 GridDimY,
		uint32 GridDimZ
	);
	
	/**
	 * Validate self-collision setup and log warnings
	 * Checks for common issues like insufficient cell size, grid coverage, etc.
	 * @return true if valid, false if critical issues detected
	 */
	static bool ValidateSelfCollisionSetup(
		float CellSize,
		float CollisionRadius,
		const FVector& GridMin,
		const FVector& GridMax,
		uint32 GridDimX,
		uint32 GridDimY,
		uint32 GridDimZ,
		uint32 MaxParticlesPerCell,
		uint32 ParticleCount
	);
};
