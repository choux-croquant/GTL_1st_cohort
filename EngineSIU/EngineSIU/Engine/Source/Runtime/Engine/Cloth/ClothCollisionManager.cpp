#include "ClothCollisionManager.h"
#include "Engine/UserInterface/Console.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Cloth/ClothGPUStructs.h"
#include <cstring>
#include <PxPhysicsAPI.h>

FClothCollisionManager::FClothCollisionManager()
	: ColliderBuffer(nullptr)
	, ColliderBufferSRV(nullptr)
	, MaxColliderCapacity(0)
	, bGPUDirty(false)
{
}

FClothCollisionManager::~FClothCollisionManager()
{
	Release();
}

void FClothCollisionManager::Initialize(uint32 MaxColliders)
{
	MaxColliderCapacity = MaxColliders;
	ColliderSources.Reserve(MaxColliders);
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Initialized with capacity for %d colliders"), MaxColliders);
}

void FClothCollisionManager::Release()
{
	// Release D3D11 resources
	if (ColliderBufferSRV)
	{
		ColliderBufferSRV->Release();
		ColliderBufferSRV = nullptr;
	}
	
	if (ColliderBuffer)
	{
		ColliderBuffer->Release();
		ColliderBuffer = nullptr;
	}
	
	// Clear CPU data
	ColliderSources.Empty();
	ComponentToColliderMap.Empty();
	bGPUDirty = false;
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Released"));
}

int32 FClothCollisionManager::RegisterCollider(UPrimitiveComponent* Component, bool bIncludeChildren)
{
	if (!Component)
		return 0;
	
	int32 NumRegistered = 0;
	
	// Get BodySetup from component
	UBodySetup* BodySetup = Component->GetBodySetup();
	if (BodySetup)
	{
		int32 BeforeCount = ColliderSources.Num();
		ExtractCollidersFromBodySetup(BodySetup, Component);
		NumRegistered = ColliderSources.Num() - BeforeCount;
	}
	
	// TODO: Handle bIncludeChildren if component has child components
	
	if (NumRegistered > 0)
	{
		UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Registered %d colliders from component %s"), 
			NumRegistered, *Component->GetName());
		bGPUDirty = true;
	}
	
	return NumRegistered;
}

void FClothCollisionManager::UnregisterCollider(UPrimitiveComponent* Component)
{
	if (!Component)
		return;
	
	TArray<int32>* ColliderIndices = ComponentToColliderMap.Find(Component);
	if (!ColliderIndices)
		return;
	
	// Remove colliders in reverse order to maintain indices
	ColliderIndices->Sort();
	for (int32 i = ColliderIndices->Num() - 1; i >= 0; --i)
	{
		int32 Index = (*ColliderIndices)[i];
		if (Index >= 0 && Index < ColliderSources.Num())
		{
			ColliderSources.RemoveAt(Index);
		}
	}
	
	ComponentToColliderMap.Remove(Component);
	bGPUDirty = true;
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Unregistered colliders from component"));
}

void FClothCollisionManager::ClearAllColliders()
{
	ColliderSources.Empty();
	ComponentToColliderMap.Empty();
	bGPUDirty = true;
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Cleared all colliders"));
}

void FClothCollisionManager::AddSphereCollider(const FVector& WorldCenter, float Radius)
{
	FClothColliderSource Source;
	Source.Type = EClothColliderType::Sphere;
	Source.Component = nullptr;  // Manual collider
	Source.ElementIndex = -1;
	Source.CachedTransform = FTransform::Identity;
	Source.CachedLocalCenter = WorldCenter;
	Source.CachedRadius = Radius;
	Source.bIsDirty = true;
	Source.GPUBufferIndex = ColliderSources.Num();
	
	ColliderSources.Add(Source);
	bGPUDirty = true;
}

void FClothCollisionManager::AddCapsuleCollider(const FVector& WorldStart, const FVector& WorldEnd, float Radius)
{
	FClothColliderSource Source;
	Source.Type = EClothColliderType::Capsule;
	Source.Component = nullptr;
	Source.ElementIndex = -1;
	Source.CachedTransform = FTransform::Identity;
	
	// Calculate center and axis
	FVector Center = (WorldStart + WorldEnd) * 0.5f;
	FVector Axis = (WorldEnd - WorldStart).GetSafeNormal();
	float HalfHeight = (WorldEnd - WorldStart).Size() * 0.5f;
	
	Source.CachedLocalCenter = Center;
	Source.CachedLocalAxis = Axis;
	Source.CachedRadius = Radius;
	Source.CachedExtents = FVector(HalfHeight, 0, 0);  // Store half-height in X
	Source.bIsDirty = true;
	Source.GPUBufferIndex = ColliderSources.Num();
	
	ColliderSources.Add(Source);
	bGPUDirty = true;
}

void FClothCollisionManager::AddBoxCollider(const FVector& WorldCenter, const FVector& Extents, const FRotator& Rotation)
{
	FClothColliderSource Source;
	Source.Type = EClothColliderType::Box;
	Source.Component = nullptr;
	Source.ElementIndex = -1;
	Source.CachedTransform = FTransform(Rotation, WorldCenter, FVector::OneVector);
	Source.CachedLocalCenter = WorldCenter;
	Source.CachedExtents = Extents;
	Source.bIsDirty = true;
	Source.GPUBufferIndex = ColliderSources.Num();
	
	ColliderSources.Add(Source);
	bGPUDirty = true;
}

void FClothCollisionManager::UpdateTransforms()
{
	// Iterate backwards to allow safe removal of stale entries
	for (int32 i = ColliderSources.Num() - 1; i >= 0; --i)
	{
		FClothColliderSource& Source = ColliderSources[i];
		
		// NEW: Skip skeletal mesh colliders (updated via UpdateSkeletalColliderTransforms)
		if (Source.BoneIndex >= 0 && Source.SkeletalMeshComponent.IsValid())
		{
			continue;  // Handled separately
		}
		
		// Check if this is a component-based collider
		if (Source.Component.IsValid())
		{
			// Get current world transform
			FTransform CurrentTransform = Source.Component->GetComponentTransform();
			
			// Validate transform for NaN/Inf before using
			if (!CurrentTransform.IsValid() || CurrentTransform.ContainsNaN())
			{
				// Invalid transform - remove this collider
				ColliderSources.RemoveAt(i);
				bGPUDirty = true;
				continue;
			}
			
			// Check if changed (simple equality check)
			if (!CurrentTransform.Equals(Source.CachedTransform))
			{
				Source.CachedTransform = CurrentTransform;
				Source.bIsDirty = true;
				bGPUDirty = true;
			}
		}
		else if (!Source.Component.IsValid())
		{
			// Component was valid but is now stale (destroyed) - remove it
			ColliderSources.RemoveAt(i);
			bGPUDirty = true;
		}
		// If Component.IsExplicitlyNull(), it's a manual collider (no transform to update)
	}
}

void FClothCollisionManager::UploadToGPU(ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	if (!bGPUDirty || ColliderSources.Num() == 0)
		return;
	
	if (!Device || !Context)
		return;
	
	// Create buffer if needed
	if (!ColliderBuffer || ColliderSources.Num() > (int32)MaxColliderCapacity)
	{
		// Release old buffer
		if (ColliderBufferSRV)
		{
			ColliderBufferSRV->Release();
			ColliderBufferSRV = nullptr;
		}
		if (ColliderBuffer)
		{
			ColliderBuffer->Release();
			ColliderBuffer = nullptr;
		}
		
		// Create new buffer
		uint32 BufferSize = ColliderSources.Num();
		if (BufferSize < MaxColliderCapacity)
			BufferSize = MaxColliderCapacity;
		
		D3D11_BUFFER_DESC bufferDesc = {};
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.ByteWidth = BufferSize * sizeof(FClothColliderGPU);
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(FClothColliderGPU);
		
		HRESULT hr = Device->CreateBuffer(&bufferDesc, nullptr, &ColliderBuffer);
		if (FAILED(hr))
		{
			UE_LOG(ELogLevel::Error, TEXT("ClothCollisionManager: Failed to create collider buffer"));
			return;
		}
		
		// Create SRV
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = BufferSize;
		
		hr = Device->CreateShaderResourceView(ColliderBuffer, &srvDesc, &ColliderBufferSRV);
		if (FAILED(hr))
		{
			UE_LOG(ELogLevel::Error, TEXT("ClothCollisionManager: Failed to create collider SRV"));
			ColliderBuffer->Release();
			ColliderBuffer = nullptr;
			return;
		}
	}
	
	// P3 OPTIMIZATION: Reuse pre-allocated buffer (NO allocation in hot path)
	// Clear array but keep allocated capacity to avoid reallocations
	int32 CurrentCapacity = StagingColliders.Max();
	StagingColliders.Empty(ColliderSources.Num());  // Clear and reserve in one call
	
	// Fill buffer
	for (const FClothColliderSource& Source : ColliderSources)
	{
		StagingColliders.Add(ConvertToGPU(Source));
	}
	
	// Upload to GPU
	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hr = Context->Map(ColliderBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if (SUCCEEDED(hr))
	{
		memcpy(msr.pData, StagingColliders.GetData(), StagingColliders.Num() * sizeof(FClothColliderGPU));
		Context->Unmap(ColliderBuffer, 0);
		bGPUDirty = false;
	}
	else
	{
		UE_LOG(ELogLevel::Warning, TEXT("ClothCollisionManager: Failed to map collider buffer for upload"));
	}
}

void FClothCollisionManager::DebugDraw()
{
	// TODO: Implement debug visualization
	// Could use line drawing to visualize sphere/capsule/box colliders
}

void FClothCollisionManager::ExtractCollidersFromBodySetup(UBodySetup* BodySetup, UPrimitiveComponent* Component)
{
	if (!BodySetup || !Component)
		return;
	
	FKAggregateGeom& AggGeom = BodySetup->AggGeom;
	TArray<int32> NewColliderIndices;
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Extracting colliders from %s"), *Component->GetName());
	UE_LOG(ELogLevel::Display, TEXT("  - Spheres: %d, Capsules: %d, Boxes: %d"),
		AggGeom.SphereElems.Num(), AggGeom.CapsuleElems.Num(), AggGeom.BoxElems.Num());
	
	// Extract spheres from PhysX shapes
	for (int32 i = 0; i < AggGeom.SphereElems.Num(); ++i)
	{
		ExtractSphereFromShape(AggGeom.SphereElems[i], Component, i);
		NewColliderIndices.Add(ColliderSources.Num() - 1);
	}
	
	// Extract capsules from PhysX shapes
	for (int32 i = 0; i < AggGeom.CapsuleElems.Num(); ++i)
	{
		int32 BeforeCount = ColliderSources.Num();
		ExtractCapsuleFromShape(AggGeom.CapsuleElems[i], Component, i);
		if (ColliderSources.Num() > BeforeCount)
		{
			NewColliderIndices.Add(ColliderSources.Num() - 1);
			UE_LOG(ELogLevel::Display, TEXT("  - Capsule %d extracted successfully"), i);
		}
		else
		{
			UE_LOG(ELogLevel::Warning, TEXT("  - Capsule %d extraction FAILED"), i);
		}
	}
	
	// Extract boxes from PhysX shapes
	for (int32 i = 0; i < AggGeom.BoxElems.Num(); ++i)
	{
		ExtractBoxFromShape(AggGeom.BoxElems[i], Component, i);
		NewColliderIndices.Add(ColliderSources.Num() - 1);
	}
	
	// Track component to colliders mapping
	if (NewColliderIndices.Num() > 0)
	{
		ComponentToColliderMap.Add(Component, NewColliderIndices);
	}
}

void FClothCollisionManager::ExtractSphereFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex)
{
	if (!Shape)
		return;
	
	// Get sphere geometry from PhysX shape
	physx::PxSphereGeometry sphereGeom;
	if (!Shape->getSphereGeometry(sphereGeom))
		return;
	
	// Get local pose from shape
	physx::PxTransform localPose = Shape->getLocalPose();
	
	FClothColliderSource Source;
	Source.Type = EClothColliderType::Sphere;
	Source.Component = Component;
	Source.ElementIndex = ElementIndex;
	Source.CachedTransform = Component->GetComponentTransform();
	Source.CachedLocalCenter = FVector(localPose.p.x, localPose.p.y, localPose.p.z);
	Source.CachedRadius = sphereGeom.radius;
	Source.bIsDirty = true;
	Source.GPUBufferIndex = ColliderSources.Num();
	
	ColliderSources.Add(Source);
}

void FClothCollisionManager::ExtractCapsuleFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex)
{
	if (!Shape)
	{
		UE_LOG(ELogLevel::Warning, TEXT("ClothCollisionManager: ExtractCapsuleFromShape - Shape is null"));
		return;
	}
	
	// Verify shape type first
	physx::PxGeometryType::Enum geomType = Shape->getGeometryType();
	if (geomType != physx::PxGeometryType::eCAPSULE)
	{
		UE_LOG(ELogLevel::Warning, TEXT("ClothCollisionManager: Shape is not a capsule (type=%d)"), (int)geomType);
		return;
	}
	
	// Get capsule geometry from PhysX shape
	physx::PxCapsuleGeometry capsuleGeom;
	if (!Shape->getCapsuleGeometry(capsuleGeom))
	{
		UE_LOG(ELogLevel::Error, TEXT("ClothCollisionManager: Failed to get capsule geometry from shape"));
		return;
	}
	
	// Get local pose from shape
	physx::PxTransform localPose = Shape->getLocalPose();
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Extracting capsule - Radius=%.2f, HalfHeight=%.2f, Pos=(%.2f,%.2f,%.2f)"),
		capsuleGeom.radius, capsuleGeom.halfHeight,
		localPose.p.x, localPose.p.y, localPose.p.z);
	
	FClothColliderSource Source;
	Source.Type = EClothColliderType::Capsule;
	Source.Component = Component;
	Source.ElementIndex = ElementIndex;
	Source.CachedTransform = Component->GetComponentTransform();
	Source.CachedLocalCenter = FVector(localPose.p.x, localPose.p.y, localPose.p.z);
	Source.CachedLocalRotation = FQuat(localPose.q.x, localPose.q.y, localPose.q.z, localPose.q.w);  // Store local rotation!
	
	// PhysX capsule axis is along X by default
	physx::PxQuat quat = localPose.q;
	FVector axis = FVector(1, 0, 0); // Default axis along X
	// Rotate by local pose quaternion
	physx::PxVec3 pxAxis = quat.rotate(physx::PxVec3(1, 0, 0));
	Source.CachedLocalAxis = FVector(pxAxis.x, pxAxis.y, pxAxis.z).GetSafeNormal();
	Source.CachedRadius = capsuleGeom.radius;
	Source.CachedExtents = FVector(capsuleGeom.halfHeight, 0, 0);  // Store half-height in X
	Source.bIsDirty = true;
	Source.GPUBufferIndex = ColliderSources.Num();
	
	ColliderSources.Add(Source);
	
	UE_LOG(ELogLevel::Display, TEXT("ClothCollisionManager: Capsule added to ColliderSources (index=%d)"), Source.GPUBufferIndex);
}

void FClothCollisionManager::ExtractBoxFromShape(physx::PxShape* Shape, UPrimitiveComponent* Component, int32 ElementIndex)
{
	if (!Shape)
		return;
	
	// Get box geometry from PhysX shape
	physx::PxBoxGeometry boxGeom;
	if (!Shape->getBoxGeometry(boxGeom))
		return;
	
	// Get local pose from shape
	physx::PxTransform localPose = Shape->getLocalPose();
	
	FClothColliderSource Source;
	Source.Type = EClothColliderType::Box;
	Source.Component = Component;
	Source.ElementIndex = ElementIndex;
	Source.CachedTransform = Component->GetComponentTransform();
	Source.CachedLocalCenter = FVector(localPose.p.x, localPose.p.y, localPose.p.z);
	Source.CachedLocalRotation = FQuat(localPose.q.x, localPose.q.y, localPose.q.z, localPose.q.w);  // Store local rotation!
    Source.CachedExtents = FVector(boxGeom.halfExtents.x * 0.5, boxGeom.halfExtents.y * 0.5, boxGeom.halfExtents.z * 0.5);
	Source.bIsDirty = true;
	Source.GPUBufferIndex = ColliderSources.Num();
	
	ColliderSources.Add(Source);
}

FClothColliderGPU FClothCollisionManager::ConvertToGPU(const FClothColliderSource& Source) const
{
	FClothColliderGPU GPU;
	GPU.Type = static_cast<uint32>(Source.Type);
	GPU.Radius = Source.CachedRadius;
	GPU.Padding0 = 0.0f;
	GPU.Padding1 = 0.0f;
	GPU.Padding2 = 0.0f;
	GPU.Padding3 = 0.0f;
	
	if (Source.Type == EClothColliderType::Sphere)
	{
		// Transform local center to world space
		FVector WorldCenter = Source.CachedTransform.TransformPosition(Source.CachedLocalCenter);
		GPU.Center = WorldCenter;
		GPU.HalfHeight = 0.0f;
		GPU.Axis = FVector::ZeroVector;
		GPU.Extents = FVector::ZeroVector;
	}
	else if (Source.Type == EClothColliderType::Capsule)
	{
		// Transform center and axis to world space
		FVector WorldCenter = Source.CachedTransform.TransformPosition(Source.CachedLocalCenter);
		FVector WorldAxis = Source.CachedTransform.TransformVector(Source.CachedLocalAxis).GetSafeNormal();
		
		GPU.Center = WorldCenter;
		GPU.Axis = WorldAxis;
		GPU.HalfHeight = Source.CachedExtents.X;  // Half-height stored in X
		GPU.Extents = FVector::ZeroVector;
	}
	else if (Source.Type == EClothColliderType::Box)
	{
		// Transform center to world space
		FVector WorldCenter = Source.CachedTransform.TransformPosition(Source.CachedLocalCenter);
		
		// For box, we need to store rotation somehow
		// For now, store as oriented box with local axes
		GPU.Center = WorldCenter;
		GPU.Extents = Source.CachedExtents;
		GPU.HalfHeight = 0.0f;
		
		// TODO: Store rotation properly - may need to extend structure
		// For now, axis represents forward vector
		GPU.Axis = Source.CachedTransform.GetRotation().GetForwardVector();
	}
	
	return GPU;
}

// NEW: Register colliders from skeletal mesh bone
int32 FClothCollisionManager::RegisterSkeletalCollider(
	USkeletalMeshComponent* SkeletalMesh,
	int32 BoneIndex,
	UBodySetup* BodySetup)
{
	if (!SkeletalMesh || !BodySetup || BoneIndex < 0)
		return 0;
	
	int32 NumRegistered = 0;
	
	// Extract spheres
	for (int32 i = 0; i < BodySetup->AggGeom.SphereElems.Num(); ++i)
	{
		physx::PxShape* Shape = BodySetup->AggGeom.SphereElems[i];
		if (!Shape)
			continue;
		
		FClothColliderSource Source;
		Source.Type = EClothColliderType::Sphere;
		Source.Component = SkeletalMesh;
		Source.SkeletalMeshComponent = SkeletalMesh;
		Source.BoneIndex = BoneIndex;
		Source.ElementIndex = i;
		
		// Extract local offset from PhysX shape
		physx::PxVec3 LocalPos = Shape->getLocalPose().p;
		Source.CachedLocalOffset = FTransform(
			FQuat::Identity,
			FVector(LocalPos.x, LocalPos.y, LocalPos.z)
		);
		
		// Extract radius
		physx::PxSphereGeometry SphereGeom;
		if (Shape->getSphereGeometry(SphereGeom))
		{
			Source.CachedRadius = SphereGeom.radius;
			Source.CachedLocalCenter = FVector(LocalPos.x, LocalPos.y, LocalPos.z);
		}
		
		Source.bIsDirty = true;
		ColliderSources.Add(Source);
		NumRegistered++;
	}
	
	// Extract capsules
	for (int32 i = 0; i < BodySetup->AggGeom.CapsuleElems.Num(); ++i)
	{
		physx::PxShape* Shape = BodySetup->AggGeom.CapsuleElems[i];
		if (!Shape)
			continue;
		
		FClothColliderSource Source;
		Source.Type = EClothColliderType::Capsule;
		Source.Component = SkeletalMesh;
		Source.SkeletalMeshComponent = SkeletalMesh;
		Source.BoneIndex = BoneIndex;
		Source.ElementIndex = i;
		
		// Extract local offset from PhysX shape
		physx::PxVec3 LocalPos = Shape->getLocalPose().p;
		physx::PxQuat LocalRot = Shape->getLocalPose().q;
		Source.CachedLocalOffset = FTransform(
			FQuat(LocalRot.x, LocalRot.y, LocalRot.z, LocalRot.w),
			FVector(LocalPos.x, LocalPos.y, LocalPos.z)
		);
		
		// Extract capsule geometry
		physx::PxCapsuleGeometry CapsuleGeom;
		if (Shape->getCapsuleGeometry(CapsuleGeom))
		{
			Source.CachedRadius = CapsuleGeom.radius;
			float HalfHeight = CapsuleGeom.halfHeight;
			
			// Capsule axis in local space (PhysX capsules are along X-axis)
			FVector LocalAxis = Source.CachedLocalOffset.GetRotation().GetRightVector();
			Source.CachedLocalCenter = FVector(LocalPos.x, LocalPos.y, LocalPos.z);
			Source.CachedLocalAxis = LocalAxis;
			Source.CachedExtents = FVector(HalfHeight, 0.0f, 0.0f);
		}
		
		Source.bIsDirty = true;
		ColliderSources.Add(Source);
		NumRegistered++;
	}
	
	// Extract boxes
	for (int32 i = 0; i < BodySetup->AggGeom.BoxElems.Num(); ++i)
	{
		physx::PxShape* Shape = BodySetup->AggGeom.BoxElems[i];
		if (!Shape)
			continue;
		
		FClothColliderSource Source;
		Source.Type = EClothColliderType::Box;
		Source.Component = SkeletalMesh;
		Source.SkeletalMeshComponent = SkeletalMesh;
		Source.BoneIndex = BoneIndex;
		Source.ElementIndex = i;
		
		// Extract local offset from PhysX shape
		physx::PxVec3 LocalPos = Shape->getLocalPose().p;
		physx::PxQuat LocalRot = Shape->getLocalPose().q;
		Source.CachedLocalOffset = FTransform(
			FQuat(LocalRot.x, LocalRot.y, LocalRot.z, LocalRot.w),
			FVector(LocalPos.x, LocalPos.y, LocalPos.z)
		);
		Source.CachedLocalRotation = FQuat(LocalRot.x, LocalRot.y, LocalRot.z, LocalRot.w);
		
		// Extract box geometry
		physx::PxBoxGeometry BoxGeom;
		if (Shape->getBoxGeometry(BoxGeom))
		{
			Source.CachedLocalCenter = FVector(LocalPos.x, LocalPos.y, LocalPos.z);
			Source.CachedExtents = FVector(BoxGeom.halfExtents.x, BoxGeom.halfExtents.y, BoxGeom.halfExtents.z);
		}
		
		Source.bIsDirty = true;
		ColliderSources.Add(Source);
		NumRegistered++;
	}
	
	if (NumRegistered > 0)
	{
		bGPUDirty = true;
		UE_LOG(ELogLevel::Display,
			TEXT("ClothCollisionManager: Registered %d skeletal colliders for bone index %d"),
			NumRegistered, BoneIndex);
	}
	
	return NumRegistered;
}

// NEW: Update transforms for skeletal mesh colliders
void FClothCollisionManager::UpdateSkeletalColliderTransforms(
	USkeletalMeshComponent* SkeletalMesh,
	const TArray<FMatrix>& BoneWorldTransforms)
{
	if (!SkeletalMesh)
		return;
	
	// Iterate through all colliders
	for (FClothColliderSource& Source : ColliderSources)
	{
		// Check if this is a collider from this skeletal mesh
		if (Source.SkeletalMeshComponent.Get() == SkeletalMesh &&
			Source.BoneIndex >= 0 &&
			Source.BoneIndex < BoneWorldTransforms.Num())
		{
			// Compute new world transform: BoneWorld * LocalOffset
			FTransform BoneWorldTransform(BoneWorldTransforms[Source.BoneIndex]);
            FTransform NewWorldTransform = Source.CachedLocalOffset * BoneWorldTransform;
			
			// Validate transform for NaN/Inf before using
			if (!NewWorldTransform.IsValid() || NewWorldTransform.ContainsNaN())
			{
				UE_LOG(ELogLevel::Warning,
					TEXT("ClothCollisionManager: Invalid transform for skeletal collider bone %d"),
					Source.BoneIndex);
				continue;
			}
			
			// Check if changed
			if (!NewWorldTransform.Equals(Source.CachedTransform))
			{
				Source.CachedTransform = NewWorldTransform;
				Source.bIsDirty = true;
				bGPUDirty = true;
			}
		}
	}
}
