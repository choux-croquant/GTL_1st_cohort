#pragma once
#include "Quat.h"
#include "Vector.h"
#include "Container/String.h"
#include "Serialization/Archive.h"

struct FTransform
{
    // 기존 멤버 변수
    FVector Translation;
    FQuat Rotation;
    FVector Scale3D;
    
    // 기존 생성자들
    FTransform();
    FTransform(const FQuat& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector(1.0f, 1.0f, 1.0f));
    FTransform(const FRotator& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector(1.0f, 1.0f, 1.0f));
    
    // 추가 생성자들
    explicit FTransform(const FVector& InTranslation);
    explicit FTransform(const FQuat& InRotation);
    explicit FTransform(const FRotator& InRotation);
    explicit FTransform(const FMatrix& InMatrix);
    
    // 변환 함수들
    FVector TransformPosition(const FVector& V) const;
    FVector InverseTransformPosition(const FVector& V) const;
    FVector TransformVector(const FVector& V) const;
    FVector TransformVectorWithoutScale(const FVector& V) const;
    FVector InverseTransformVector(const FVector& V) const;
    FVector TransformDirection(const FVector& V) const;
    FVector InverseTransformDirection(const FVector& V) const;
    
    // 회전 관련 추가 함수들
    FQuat GetRotation() const { return Rotation; }
    FRotator Rotator() const { return Rotation.Rotator(); }
    void SetRotation(const FQuat& InRotation) { Rotation = InRotation; }
    
    // 위치 관련 함수들
    FVector GetTranslation() const { return Translation; }
    void SetTranslation(const FVector& InTranslation) { Translation = InTranslation; }
    void AddToTranslation(const FVector& InTranslation) { Translation += InTranslation; }
    
    // 스케일 관련 함수들
    FVector GetScale3D() const { return Scale3D; }
    void SetScale3D(const FVector& InScale) { Scale3D = InScale; }
    bool IsUniform() const;
    float GetMaximumAxisScale() const;
    float GetMinimumAxisScale() const;
    
    // 누적 변환 함수들
    void Accumulate(const FTransform& Other);
    void MultiplyScale3D(const FVector& InMultiplyScale);
    
    // 정규화 및 유효성 검사
    bool IsValid() const;
    bool IsRotationNormalized() const;
    void NormalizeRotation();
    
    // 변환 연산자들과 함수들
    FTransform operator*(const FTransform& Other) const;
    FTransform operator*(float Scale) const;
    
    void Blend(const FTransform& Atom1, const FTransform& Atom2, float Alpha);
    FTransform BlendWith(const FTransform& Other, float Alpha) const;
    
    // 선형 보간 함수
    static FTransform LerpTransform(const FTransform& A, const FTransform& B, float Alpha);
    
    // 항등 변환 관련
    bool IsIdentity() const;
    void SetIdentity();
    void SetIdentityZeroScale();
    static const FTransform Identity;
    
    // 역변환 계산
    FTransform Inverse() const;
    
    // 행렬 변환
    FMatrix ToMatrixWithScale() const;
    FMatrix ToMatrixNoScale() const;
    void SetFromMatrix(const FMatrix& InMatrix);
    
    // 상대 변환 계산
    FTransform GetRelativeTransform(const FTransform& Other) const;
    FTransform GetRelativeTransformReverse(const FTransform& Other) const;
    
    // 디버깅 및 직렬화 관련
    FString ToString() const;
    bool Serialize(FArchive& Ar);
    
    // 메모리 레이아웃 최적화 관련 (SIMD 연산용)
    bool ContainsNaN() const;
    
    // 추가적인 변환 유틸리티
    FVector4 TransformFVector4(const FVector4& V) const;
    FVector4 TransformFVector4NoScale(const FVector4& V) const;
    
    // 애니메이션 관련 함수들
    void ScaleTranslation(const FVector& InScale3D);
    void RemoveScaling(float Tolerance = SMALL_NUMBER);
    FVector GetSafeScaleReciprocal(const FVector& InScale, float Tolerance = SMALL_NUMBER) const;
    
    // 보간 함수들
    static void BlendFromIdentityAndAccumulate(FTransform& OutTransform, const FTransform& InTransform, float Alpha);
    
    void AccumulateWithShortestRotation(const FTransform& DeltaAtom, float BlendWeight);

    inline friend FArchive& operator<<(FArchive& Ar, FTransform& Transform)
    {
        return Ar << Transform.Translation
                  << Transform.Rotation
                  << Transform.Scale3D;
    }

    // 비교 연산
    bool Equals(const FTransform& Other, float Tolerance = KINDA_SMALL_NUMBER) const
    {
        return Translation.Equals(Other.Translation, Tolerance) &&
            Rotation.Equals(Other.Rotation, Tolerance) &&
            Scale3D.Equals(Other.Scale3D, Tolerance);
    }

    // 엄격한 비교 (매우 작은 tolerance)
    bool ExactEquals(const FTransform& Other) const
    {
        return Translation.Equals(Other.Translation, SMALL_NUMBER) &&
            Rotation.Equals(Other.Rotation, SMALL_NUMBER) &&
            Scale3D.Equals(Other.Scale3D, SMALL_NUMBER);
    }

    // 회전만 무시하고 비교
    bool EqualsNoRotation(const FTransform& Other, float Tolerance = KINDA_SMALL_NUMBER) const
    {
        return Translation.Equals(Other.Translation, Tolerance) &&
            Scale3D.Equals(Other.Scale3D, Tolerance);
    }

    // 스케일만 무시하고 비교
    bool EqualsNoScale(const FTransform& Other, float Tolerance = KINDA_SMALL_NUMBER) const
    {
        return Translation.Equals(Other.Translation, Tolerance) &&
            Rotation.Equals(Other.Rotation, Tolerance);
    }

    // operator== (정확한 비교)
    bool operator==(const FTransform& Other) const
    {
        return Translation == Other.Translation &&
            Rotation == Other.Rotation &&
            Scale3D == Other.Scale3D;
    }

    // operator!= (정확한 비교)
    bool operator!=(const FTransform& Other) const
    {
        return !(*this == Other);
    }
};
