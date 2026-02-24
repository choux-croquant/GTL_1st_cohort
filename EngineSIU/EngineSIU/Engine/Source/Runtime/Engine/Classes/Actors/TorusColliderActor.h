#pragma once
#include "GameFramework/Actor.h"

class UTorusComponent;

class ATorusColliderActor : public AActor
{
    DECLARE_CLASS(ATorusColliderActor, AActor)
public:
    ATorusColliderActor();

    UTorusComponent* GetTorusComponent() const;
    
protected:
    UPROPERTY
    (UTorusComponent*, TorusComponent, = nullptr);
};
