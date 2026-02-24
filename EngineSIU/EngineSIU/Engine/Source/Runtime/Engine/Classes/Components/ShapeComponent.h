
#pragma once
#include "PrimitiveComponent.h"

enum class EShapeType : uint8
{
    Box,
    Sphere,
    Capsule,
    Torus,

    Max,
};

class UShapeComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(UShapeComponent, UPrimitiveComponent)    

public:
    UShapeComponent();

    virtual void TickComponent(float DeltaTime) override;
    
    FColor ShapeColor = FColor(180, 180, 180, 255);
    bool bDrawOnlyIfSelected = true;

    EShapeType GetShapeType() const { return ShapeType; }

protected:
    EShapeType ShapeType = EShapeType::Max;
};
