#include "TorusComponent.h"

#include "UObject/Casts.h"

UTorusComponent::UTorusComponent()
{
    ShapeType = EShapeType::Torus;
}

UObject* UTorusComponent::Duplicate(UObject* InOuter)
{
    ThisClass* NewComponent = Cast<ThisClass>(Super::Duplicate(InOuter));

    NewComponent->MajorRadius = MajorRadius;
    NewComponent->MinorRadius = MinorRadius;
    NewComponent->TorusAxis = TorusAxis;

    return NewComponent;
}

void UTorusComponent::SetProperties(const TMap<FString, FString>& InProperties)
{
    Super::SetProperties(InProperties);
    const FString* TempStr = nullptr;
    
    TempStr = InProperties.Find(TEXT("MajorRadius"));
    if (TempStr)
    {
        MajorRadius = FCString::Atof(**TempStr);
    }
    
    TempStr = InProperties.Find(TEXT("MinorRadius"));
    if (TempStr)
    {
        MinorRadius = FCString::Atof(**TempStr);
    }
    
    TempStr = InProperties.Find(TEXT("TorusAxisX"));
    if (TempStr)
    {
        TorusAxis.X = FCString::Atof(**TempStr);
    }
    
    TempStr = InProperties.Find(TEXT("TorusAxisY"));
    if (TempStr)
    {
        TorusAxis.Y = FCString::Atof(**TempStr);
    }
    
    TempStr = InProperties.Find(TEXT("TorusAxisZ"));
    if (TempStr)
    {
        TorusAxis.Z = FCString::Atof(**TempStr);
    }
    
    TorusAxis = TorusAxis.GetSafeNormal();
}

void UTorusComponent::GetProperties(TMap<FString, FString>& OutProperties) const
{
    Super::GetProperties(OutProperties);
    OutProperties.Add(TEXT("MajorRadius"), FString::SanitizeFloat(MajorRadius));
    OutProperties.Add(TEXT("MinorRadius"), FString::SanitizeFloat(MinorRadius));
    OutProperties.Add(TEXT("TorusAxisX"), FString::SanitizeFloat(TorusAxis.X));
    OutProperties.Add(TEXT("TorusAxisY"), FString::SanitizeFloat(TorusAxis.Y));
    OutProperties.Add(TEXT("TorusAxisZ"), FString::SanitizeFloat(TorusAxis.Z));
}
