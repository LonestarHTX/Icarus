#pragma once

#include "CoreMinimal.h"

#include "CrustTypes.h"

struct FPlate
{
    int32 PlateIndex = -1;

    ECrustType CrustType = ECrustType::Oceanic;

    FVector RotationAxis = FVector::UpVector;
    float AngularVelocity = 0.0f;

    FQuat StepRotation = FQuat::Identity;

    int32 SeedSampleIndex = -1;
    float Area = 0.0f;
    TArray<int32> BoundarySamples;

    friend PTPCORE_API FArchive& operator<<(FArchive& Ar, FPlate& Plate);
};
