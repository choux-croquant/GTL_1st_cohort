#include "TorusColliderActor.h"

#include "Components/TorusComponent.h"
#include "Cloth/ClothPhysicsManager.h"
#include "Cloth/ClothWorld.h"
#include "Cloth/ClothCollisionManager.h"
#include "Engine/Engine.h"

ATorusColliderActor::ATorusColliderActor()
{
    TorusComponent = AddComponent<UTorusComponent>();
    RootComponent = TorusComponent;
}

UTorusComponent* ATorusColliderActor::GetTorusComponent() const
{
    return TorusComponent;
}
