#include "TestClothActor.h"
#include "Math/Vector.h"
#include "UObject/ObjectFactory.h"
#include "Components/ClothMeshComponent.h"
#include "Engine/ClothAsset.h"
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

    bool operator==(const FEdge& Other) const
    {
        return A == Other.A && B == Other.B;
    }
};

namespace std
{
    template<>
    struct hash<FEdge>
    {
        size_t operator()(const FEdge& Edge) const noexcept
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
}

void ATestClothActor::BeginPlay()
{
    Super::BeginPlay();

    // Generate simple test cloth
    CreateTestCloth();

    // Assign to component
    ClothMesh->SetClothAsset(ClothAsset);
    ClothMesh->StartSimulation();
}

void ATestClothActor::Tick(float DeltaTime)
{
}

void ATestClothActor::CreateTestCloth()
{
    const int32 GridSize = 4;
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
            invMasses.Add(invMass);
            /*bool bIsTopRow = (y == 0);
            bool bIsLeftCorner = (x == 0);
            bool bIsRightCorner = (x == GridSize - 1);

            float invMass = (bIsTopRow && (bIsLeftCorner || bIsRightCorner)) ? 0.0f : 1.0f;
            invMasses.Add(invMass);*/
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

    auto FindOppositeVertex = [&](uint32 TriIdx, const FEdge& Edge) -> uint32
    {
        uint32 i0 = indices[TriIdx * 3 + 0];
        uint32 i1 = indices[TriIdx * 3 + 1];
        uint32 i2 = indices[TriIdx * 3 + 2];

        if (i0 != Edge.A && i0 != Edge.B) return i0;
        if (i1 != Edge.A && i1 != Edge.B) return i1;
        return i2;
    };

    auto CalculateDihedralAngle = [&](const FVector& A, const FVector& B,
        const FVector& C, const FVector& D) -> float
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

    for (auto& Pair : EdgeToTriangles)
    {
        const TArray<uint32>& Tris = Pair.Value;
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
            bc.Stiffness = 1.0f;     // 기본값, 또는 Config.BendStiffness 사용
            bc.Compliance = 0.0f;
            bc.Lambda = 0.0f;

            bendConstraints.Add(bc);
        }
    }

    // Attachment
    FClothAttachmentData attachment;
    attachment.Type = EClothAttachmentType::WorldPosition;
    attachment.ClothVertexIndex = 40;
    attachment.WorldPosition = FVector(0, 0, 0);
    attachment.Stiffness = 1.0f;  // Hard kinematic

    TArray<FClothAttachmentData> attachments;
    attachments.Add(attachment);
    
    // Set cloth asset data
    ClothAsset->SetRestPositions(positions);
    ClothAsset->SetIndices(indices);
    ClothAsset->SetInvMasses(invMasses);

    for (const FClothDistanceConstraint &constraint : constraints)
    {
        ClothAsset->AddDistanceConstraint(constraint);
    }
    for (const FClothBendConstraint& constraint : bendConstraints)
    {
        ClothAsset->AddBendConstraint(constraint);
    }
    for (const FClothAttachmentData& data : attachments)
    {
        ClothAsset->AddAttachmentData(data);
    }
    // Configure simulation parameters
    FClothConfig config;
    config.Mass = 1.0f;
    config.Damping = 0.2f;          // Lower damping for more dynamic motion
    config.StretchStiffness = 0.9f; // High stiffness for structural integrity
    config.BendStiffness = 0.9f;
    config.NumIterations = 2;        // Increase iterations for better convergence
    config.TimeStep = 0.016f;
    config.bUseXPBD = false;

    ClothAsset->SetConfig(config);
}
