#include "TestClothActor.h"
#include "Math/Vector.h"
#include "UObject/ObjectFactory.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "World/World.h"
#include "Cloth/ClothInstance.h"
#include "Classes/Engine/FObjLoader.h"
#include <functional>

struct FEdge
{
    uint32 A;
    uint32 B;

    FEdge() : A(0), B(0) {}

    FEdge(uint32 InA, uint32 InB)
    {
        A = FMath::Min(InA, InB);
        B = FMath::Max(InA, InB);
    }

    bool operator==(const FEdge &Other) const
    {
        return A == Other.A && B == Other.B;
    }
};

namespace std
{
    template <>
    struct hash<FEdge>
    {
        size_t operator()(const FEdge &Edge) const noexcept
        {
            // 간단한 정수 해시 조합
            size_t h1 = std::hash<uint32>()(Edge.A);
            size_t h2 = std::hash<uint32>()(Edge.B);

            // 표준적인 hash combine 패턴
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
        }
    };
}

ATestClothActor::ATestClothActor()
{
    ClothMesh = AddComponent<UClothMeshComponent>(TEXT("ClothMesh"));
    RootComponent = ClothMesh;

    ClothAsset = FObjectFactory::ConstructObject<UClothAsset>(this, TEXT("TestClothAsset"));

    AttachmentDriver = nullptr;
    AnimationTime = 0.0f;
    DriverInitialPosition = FVector::ZeroVector;
    bDriverSpawned = false;
}

void ATestClothActor::BeginPlay()
{
    Super::BeginPlay();

    // Generate simple test cloth
    CreateTestCloth();

    // Cache attachment data from the cloth asset for runtime updates
    if (ClothAsset)
    {
        const TArray<FClothAttachmentData> &assetAttachments = ClothAsset->GetAttachmentData();
        CachedAttachments.Empty();
        for (const FClothAttachmentData &data : assetAttachments)
        {
            CachedAttachments.Add(data);
        }
    }

    // Assign to component
    ClothMesh->SetClothAsset(ClothAsset);
    ClothMesh->StartSimulation();

    // Note: Attachment driver will be spawned on first Tick to avoid iterator invalidation
    // during World::BeginPlay() actor iteration
}

void ATestClothActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Spawn attachment driver on first tick (after BeginPlay has completed)
    // This avoids iterator invalidation during World::BeginPlay()
    if (!bDriverSpawned)
    {
        bDriverSpawned = true;

        // Position driver above the cloth
        //FVector driverSpawnLocation = GetActorLocation() + FVector(0.0f, 0.0f, 0.0f);

        // Spawn the attachment driver actor
        UWorld *world = GetWorld();
        if (world)
        {
            AttachmentDriver = world->SpawnActor<AStaticMeshActor>();
        }

        if (AttachmentDriver)
        {
            // Set position after spawning
            //AttachmentDriver->SetActorLocation(driverSpawnLocation);
            AttachmentDriver->SetActorLocation(DriverInitialPosition);
            
            // Store initial position for animation
            //DriverInitialPosition = driverSpawnLocation;

            // Set up the attachment driver mesh component if it exists
            UStaticMeshComponent *meshComp = AttachmentDriver->GetStaticMeshComponent();
            FString MeshName = "Contents/pole/pole.obj";
            UStaticMesh* StaticMesh = FObjManager::GetStaticMesh(MeshName.ToWideString());
            meshComp->SetStaticMesh(StaticMesh);
            meshComp->SetRelativeScale3D(FVector(2.0f, 2.0f, 4.0f));

            if (meshComp)
            {
                // The mesh could be set here if we have a specific mesh asset
                //meshComp->SetWorldLocation(driverSpawnLocation);
            }
        }
    }

    // Accumulate animation time
    AnimationTime += DeltaTime;

    // Animate the attachment driver with a simple oscillating motion
    // This demonstrates the cloth following the moving attachment point
    if (AttachmentDriver)
    {
        // 속도와 크기 조절
        const float Speed = 1.0f;             // 전체 동작 속도
        const float MoveRadius = 150.0f;      // 위치 이동 반경
        const float SwayAngleScale = 45.0f;   // 회전 각도 (±45도)

        float Time = AnimationTime * Speed;

        // 1. 위치 이동 (Position): 8자 형태의 큰 궤적 (Lissajous)
        float OffsetY = FMath::Sin(Time) * MoveRadius;          // 좌우 이동
        float OffsetX = FMath::Cos(Time * 2.0f) * (MoveRadius * 0.4f); // 앞뒤 이동
        float OffsetZ = FMath::Sin(Time * 2.0f) * (MoveRadius * 0.2f); // 상하 반동

        FVector NewLocation = DriverInitialPosition + FVector(OffsetX, OffsetY, OffsetZ);

        // 2. 회전 (Rotation)
        float RollAngle = -FMath::Cos(Time) * SwayAngleScale;   // 좌우 이동 속도에 맞춰 기울기
        float PitchAngle = FMath::Sin(Time * 2.0f) * (SwayAngleScale * 0.3f); // 앞뒤 이동에 맞춘 기울기

        FRotator NewRotation = FRotator(PitchAngle, 0.0f, RollAngle);

        // 3. 적용
        AttachmentDriver->SetActorLocation(NewLocation);
        AttachmentDriver->SetActorRotation(NewRotation);
    }

    // Update attachment positions to follow the driver
    UpdateAttachments();
}

void ATestClothActor::CreateTestCloth()
{
    const int32 GridSize = 8;
    const float Spacing = 5.0f; // 10 cm spacing between particles

    TArray<FVector> positions;
    TArray<uint32> indices;
    TArray<float> invMasses;

    // Generate grid vertices
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            FVector pos;

            pos.X = 0.0f;
            pos.Y = Spacing * (x - GridSize / 2.0f);
            pos.Z = Spacing * (GridSize / 2.0f - y); // Top row at top, bottom row at bottom

            positions.Add(pos);

            // Top row is fixed (pinned)
            float invMass = (y == 0) ? 0.0f : 1.0f;
            //float invMass = 1.0f;
            //if (y == 0 && (x == 0 || x == GridSize - 1)) invMass = 0.0f;
            invMasses.Add(invMass);
        }
    }

    // Generate triangle indices
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);

            // Triangle 1
            indices.Add(i0);
            indices.Add(i1);
            indices.Add(i2);

            // Triangle 2
            indices.Add(i1);
            indices.Add(i3);
            indices.Add(i2);
        }
    }

    // Generate distance constraints
    TArray<FClothDistanceConstraint> constraints;

    // Structural springs (horizontal and vertical)
    for (int32 y = 0; y < GridSize; ++y)
    {
        for (int32 x = 0; x < GridSize; ++x)
        {
            int32 idx = y * GridSize + x;

            // Horizontal constraint (structural)
            if (x < GridSize - 1)
            {
                int32 neighborIdx = y * GridSize + (x + 1);
                float restLength = (positions[neighborIdx] - positions[idx]).Length();
                constraints.Add(FClothDistanceConstraint(idx, neighborIdx, restLength, 1.0f));
            }

            // Vertical constraint (structural)
            if (y < GridSize - 1)
            {
                int32 neighborIdx = (y + 1) * GridSize + x;
                float restLength = (positions[neighborIdx] - positions[idx]).Length();
                constraints.Add(FClothDistanceConstraint(idx, neighborIdx, restLength, 1.0f));
            }
        }
    }

    // Shear springs (diagonals for triangle stability)
    for (int32 y = 0; y < GridSize - 1; ++y)
    {
        for (int32 x = 0; x < GridSize - 1; ++x)
        {
            int32 i0 = y * GridSize + x;
            int32 i3 = (y + 1) * GridSize + (x + 1);

            int32 i1 = y * GridSize + (x + 1);
            int32 i2 = (y + 1) * GridSize + x;

            float restLength03 = (positions[i3] - positions[i0]).Length();
            float restLength12 = (positions[i2] - positions[i1]).Length();
            constraints.Add(FClothDistanceConstraint(i0, i3, restLength03, 1.0f));
            constraints.Add(FClothDistanceConstraint(i1, i2, restLength12, 1.0f));
        }
    }

    TArray<FClothBendConstraint> bendConstraints;

    const uint32 NumTriangles = indices.Num() / 3;

    auto FindOppositeVertex = [&](uint32 TriIdx, const FEdge &Edge) -> uint32
    {
        uint32 i0 = indices[TriIdx * 3 + 0];
        uint32 i1 = indices[TriIdx * 3 + 1];
        uint32 i2 = indices[TriIdx * 3 + 2];

        if (i0 != Edge.A && i0 != Edge.B)
            return i0;
        if (i1 != Edge.A && i1 != Edge.B)
            return i1;
        return i2;
    };

    auto CalculateDihedralAngle = [&](const FVector &A, const FVector &B,
                                      const FVector &C, const FVector &D) -> float
    {
        FVector e = B - A;
        FVector n1 = FVector::CrossProduct(C - A, e);
        FVector n2 = FVector::CrossProduct(e, D - A);

        float n1Len = n1.Length();
        float n2Len = n2.Length();
        if (n1Len < KINDA_SMALL_NUMBER || n2Len < KINDA_SMALL_NUMBER)
            return 0.0f;

        n1 /= n1Len;
        n2 /= n2Len;

        float c = FVector::DotProduct(n1, n2);
        c = FMath::Clamp(c, -1.0f, 1.0f);
        return FMath::Acos(c);
    };

    TMap<FEdge, TArray<uint32>> EdgeToTriangles;

    for (uint32 triIdx = 0; triIdx < NumTriangles; ++triIdx)
    {
        uint32 i0 = indices[triIdx * 3 + 0];
        uint32 i1 = indices[triIdx * 3 + 1];
        uint32 i2 = indices[triIdx * 3 + 2];

        EdgeToTriangles.FindOrAdd(FEdge(i0, i1)).Add(triIdx);
        EdgeToTriangles.FindOrAdd(FEdge(i1, i2)).Add(triIdx);
        EdgeToTriangles.FindOrAdd(FEdge(i2, i0)).Add(triIdx);
    }

    for (auto &Pair : EdgeToTriangles)
    {
        const TArray<uint32> &Tris = Pair.Value;
        if (Tris.Num() == 2)
        {
            FEdge edge = Pair.Key;
            uint32 tri0 = Tris[0];
            uint32 tri1 = Tris[1];

            uint32 opp0 = FindOppositeVertex(tri0, edge);
            uint32 opp1 = FindOppositeVertex(tri1, edge);

            float restAngle = CalculateDihedralAngle(
                positions[edge.A],
                positions[edge.B],
                positions[opp0],
                positions[opp1]);

            FClothBendConstraint bc;
            bc.ParticleA = edge.A;
            bc.ParticleB = edge.B;
            bc.ParticleC = opp0;
            bc.ParticleD = opp1;
            bc.RestAngle = restAngle;
            bc.Stiffness = 1.0f;
            bc.Compliance = 0.0f;
            bc.Lambda = 0.0f;

            bendConstraints.Add(bc);
        }
    }

    // Set up attachment points for the cloth
    // Attach the top-left corner (vertex 0) to follow the attachment driver
    TArray<FClothAttachmentData> attachments;

    for (int32 x = 0; x < GridSize; ++x)
    {
        FClothAttachmentData attachment;
        attachment.Type = EClothAttachmentType::ActorTransform;
        attachment.ClothVertexIndex = x;

        // 0.0 ~ 22.5 - 10
        // 0.0 ~ 17.5 - 10
        float Offset = (15.0f / float(GridSize - 1)) * x;
        attachment.LocalOffset = FTransform(FVector(0.0f, 0.0f, Offset));

        attachment.Stiffness = 0.98f; // Hard kinematic constraint
        attachment.bIsKinematic = true;

        attachments.Add(attachment);
    }

    // Set cloth asset data
    ClothAsset->SetRestPositions(positions);
    ClothAsset->SetIndices(indices);
    ClothAsset->SetInvMasses(invMasses);

    for (const FClothDistanceConstraint &constraint : constraints)
    {
        ClothAsset->AddDistanceConstraint(constraint);
    }
    for (const FClothBendConstraint &constraint : bendConstraints)
    {
        ClothAsset->AddBendConstraint(constraint);
    }
    for (const FClothAttachmentData &data : attachments)
    {
        ClothAsset->AddAttachmentData(data);
    }

    // Configure simulation parameters
    FClothConfig config;
    config.Mass = 1.0f;
    config.Damping = 0.7f;
    config.StretchStiffness = 1.0f;
    config.BendStiffness = 0.2f;
    config.NumIterations = 5;
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;

    ClothAsset->SetConfig(config);
}

void ATestClothActor::UpdateAttachments()
{
    // Update attachment positions to follow the driver mesh
    // This is called every frame to keep the cloth attached to moving objects

    if (!AttachmentDriver || !ClothMesh || CachedAttachments.Num() == 0)
        return;

    // Get the current driver position
    FVector driverPosition = AttachmentDriver->GetActorLocation();

    // Update all attachment world positions to follow the driver
    for (int32 i = 0; i < CachedAttachments.Num(); ++i)
    {
        FClothAttachmentData &attachment = CachedAttachments[i];

        // For ActorTransform type, use the driver's world position
        if (attachment.Type == EClothAttachmentType::ActorTransform)
        {
            //WorldTransform = GetWorldMatrix();
            /*FMatrix Mat = AttachmentDriver->GetStaticMeshComponent()->GetWorldMatrix();
            FVector Pos = FTransform(Mat).InverseTransformDirection(driverPosition + attachment.LocalOffset.GetTranslation());
            attachment.WorldPosition = Pos;*/
            const FTransform AttachmentWorldTransform = FTransform(AttachmentDriver->GetStaticMeshComponent()->GetWorldMatrix()) * attachment.LocalOffset;

            // 위치만 필요하므로 Translation만 사용
            attachment.WorldPosition = AttachmentWorldTransform.GetTranslation();
        }
        else if (attachment.Type == EClothAttachmentType::WorldPosition)
        {
            // For world position attachments to the driver, update to follow it
            // In this test case, we'll make them follow the driver position
            attachment.WorldPosition = driverPosition;
        }
    }

    // Send updated attachments to the cloth component's instance
    // The component will forward them to the solver for GPU processing
    FClothInstance *instance = ClothMesh->GetClothInstance();
    if (instance)
    {
        instance->UpdateAttachments(CachedAttachments);
    }
}
