#pragma once

#include "Core/HAL/PlatformType.h"
#include "Core/Container/Array.h"
#include "Core/Container/Map.h"
#include "Core/Math/Vector.h"
#include "Core/Math/Transform.h"
#include "UObject/WeakObjectPtr.h"
#include <d3d11.h>

class UPrimitiveComponent;
class UBodySetup;
struct FKAggregateGeom;
struct FClothColliderGPU;

namespace physx
{
	class PxShape;
}

/**
 * Collider type enumeration
 */
enum class EClothColliderType : uint32
{
	Sphere = 0,
	Capsule = 1,
	Box = 2,
	Torus = 3,
	Count
};

/**
 * CPU-side collider source tracking
 * Tracks a registered collider and its dirty state
 */
struct FClothColliderSource
{
	EClothColliderType Type;
	TWeakObjectPtr<UPrimitiveComponent> Component;  // Component providing the collider (SAFE weak pointer)
	int32 ElementIndex;                              // Index in BodySetup->AggGeom array
	
	FTransform CachedTransform;             // Last uploaded component transform
	FVector CachedLocalCenter;              // Local-space center/start point
	FVector CachedLocalAxis;                // Local-space axis/end point (for capsules)
	FQuat CachedLocalRotation;              // Local-space rotation (for boxes, from PhysX shape)
	float CachedRadius;
	FVector CachedExtents;                  // For boxes
	
	bool bIsDirty;                          // Transform changed since last upload
	uint32 GPUBufferIndex;                  // Index in unified GPU buffer
	
	// NEW: Skeletal mesh support for per-bone colliders
	int32 BoneIndex;                        // -1 for regular component, >= 0 for bone-based collider
	FTransform CachedLocalOffset;           // Local offset from bone (for geometry offset)
	TWeakObjectPtr<class USkeletalMeshComponent> SkeletalMeshComponent;  // For bone lookup
	
	FClothColliderSource()
		: Type(EClothColliderType::Sphere)
		, Component(nullptr)
		, ElementIndex(0)
		, CachedTransform(FTransform::Identity)
		, CachedLocalCenter(FVector::ZeroVector)
		, CachedLocalAxis(FVector::ZeroVector)
		, CachedLocalRotation(FQuat::Identity)
		, CachedRadius(0.0f)
		, CachedExtents(FVector::ZeroVector)
		, bIsDirty(true)
		, GPUBufferIndex(0)
		, BoneIndex(-1)
		, CachedLocalOffset(FTransform::Identity)
		, SkeletalMeshComponent(nullptr)
	{}
};

/**
 * Collision Manager
 * Manages world-space colliders for cloth collision
 */
class FClothCollisionManager
{
public:
	FClothCollisionManager();
	~FClothCollisionManager();
	
	// Initialization
	void Initialize(uint32 MaxColliders);
	void Release();
	
	// Registration API
	/**
	 * Register a collider from a UPrimitiveComponent's BodySetup
	 * @param Component - Component with physics shapes
	 * @param bIncludeChildren - Recursively register child components
	 * @return Number of colliders registered
	 */
	int32 RegisterCollider(UPrimitiveComponent* Component, bool bIncludeChildren = false);
	
	/**
	 * Unregister all colliders from a component
	 */
	void UnregisterCollider(UPrimitiveComponent* Component);
	
	/**
	 * Clear all registered colliders (useful for world transitions)
	 */
	void ClearAllColliders();
	
	/**
	 * Manually add a sphere collider
	 */
	void AddSphereCollider(const FVector& WorldCenter, float Radius);
	
	/**
	 * Manually add a capsule collider
	 */
	void AddCapsuleCollider(const FVector& WorldStart, const FVector& WorldEnd, float Radius);
	
	/**
	 * Manually add a box collider
	 */
	void AddBoxCollider(const FVector& WorldCenter, const FVector& Extents, const FRotator& Rotation);
	
	/**
	 * @param WorldCenter - Torus center in world space
	 * @param Axis - Torus up vector (will be normalized)
	 * @param MajorRadius - Distance from center to tube center
	 * @param MinorRadius - Tube radius
	 */
	void AddTorusCollider(const FVector& WorldCenter, const FVector& Axis, float MajorRadius, float MinorRadius);
	
	/**
	 * @param SkeletalMesh - The skeletal mesh component
	 * @param BoneIndex - Index of the bone
	 * @param BodySetup - Physics geometry for this bone
	 * @return Number of colliders registered
	 */
	int32 RegisterSkeletalCollider(
		class USkeletalMeshComponent* SkeletalMesh,
		int32 BoneIndex,
		class UBodySetup* BodySetup
	);
	
	/**
	 * @param SkeletalMesh - The skeletal mesh component
	 * @param BoneWorldTransforms - World-space transforms for each bone
	 */
	void UpdateSkeletalColliderTransforms(
		class USkeletalMeshComponent* SkeletalMesh,
		const TArray<FMatrix>& BoneWorldTransforms
	);
	
	// Update
	/**
	 * Update transforms of registered colliders and mark dirty
	 * Call once per frame before simulation
	 */
	void UpdateTransforms();
	
	/**
	 * Upload dirty colliders to GPU
	 * Only uploads colliders marked dirty
	 */
	void UploadToGPU(ID3D11Device* Device, ID3D11DeviceContext* Context);
	
	// GPU Resource Access
	ID3D11ShaderResourceView* GetColliderBufferSRV() const { return ColliderBufferSRV; }
	uint32 GetColliderCount() const { return ColliderSources.Num(); }
	
	// Debug
	void DebugDraw();
	
	// Public access for rendering (read-only)
	const TArray<FClothColliderSource>& GetColliderSources() const { return ColliderSources; }

private:
	// Extract colliders from BodySetup
	void ExtractCollidersFromBodySetup(UBodySetup* BodySetup, UPrimitiveComponent* Component);
	void ExtractSphereFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex);
	void ExtractCapsuleFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex);
	void ExtractBoxFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex);
	
	// Convert to GPU format
	FClothColliderGPU ConvertToGPU(const FClothColliderSource& Source) const;
	
	// GPU Resources
	ID3D11Buffer* ColliderBuffer;
	ID3D11ShaderResourceView* ColliderBufferSRV;
	uint32 MaxColliderCapacity;
	
	// CPU Tracking
	TArray<FClothColliderSource> ColliderSources;
	TMap<UPrimitiveComponent*, TArray<int32>> ComponentToColliderMap;  // Component -> ColliderSource indices
	
	TArray<FClothColliderGPU> StagingColliders;
	
	bool bGPUDirty;  // Global dirty flag
};
