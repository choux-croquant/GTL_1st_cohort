#pragma once
#include "ShapeComponent.h"

class UTorusComponent : public UShapeComponent
{
    DECLARE_CLASS(UTorusComponent, UShapeComponent)

public:
    UTorusComponent();

    virtual UObject* Duplicate(UObject* InOuter) override;

    virtual void SetProperties(const TMap<FString, FString>& InProperties) override;
    virtual void GetProperties(TMap<FString, FString>& OutProperties) const override;

    void SetMajorRadius(float InMajorRadius) { MajorRadius = InMajorRadius; }
    float GetMajorRadius() const { return MajorRadius; }
    
    void SetMinorRadius(float InMinorRadius) { MinorRadius = InMinorRadius; }
    float GetMinorRadius() const { return MinorRadius; }
    
    void SetTorusAxis(const FVector& InAxis) { TorusAxis = InAxis.GetSafeNormal(); }
    FVector GetTorusAxis() const { return TorusAxis; }
    
private:
    float MajorRadius = 8.f;  // Ring radius
    float MinorRadius = 4.f;   // Tube radius
    FVector TorusAxis = FVector::UpVector;  // Torus up vector (default: horizontal torus)
};
