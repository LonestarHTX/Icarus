#pragma once

#include "CoreMinimal.h"

#include "CrustTypes.h"

struct FCrustSample
{
    FVector Position;

    ECrustType CrustType = ECrustType::Oceanic;
    float Thickness = 0.0f;
    float Elevation = 0.0f;

    float OceanicAge = 0.0f;
    FVector RidgeDirection = FVector::ZeroVector;

    EOrogenyType OrogenyType = EOrogenyType::None;
    float OrogenyAge = 0.0f;
    FVector FoldDirection = FVector::ZeroVector;

    int32 PlateIndex = -1;
    float DistToFront = TNumericLimits<float>::Max();

    friend PTPCORE_API FArchive& operator<<(FArchive& Ar, FCrustSample& Sample);
};
