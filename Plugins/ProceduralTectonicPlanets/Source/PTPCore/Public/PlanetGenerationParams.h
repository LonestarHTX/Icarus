#pragma once

#include "CoreMinimal.h"

#include "PlanetGenerationParams.generated.h"

USTRUCT(BlueprintType)
struct PTPCORE_API FPlanetGenerationParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "100", ClampMax = "1000000"))
    int32 SampleCount = 500000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "1", ClampMax = "1024"))
    int32 NumPlates = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ContinentalRatio = 0.30f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Elevation")
    float ContinentalBaseElevation = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Elevation")
    float ContinentalMaxElevation = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Elevation")
    float OceanicBaseElevation = -4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Elevation")
    float OceanicMinElevation = -6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Motion", meta = (ClampMin = "0.0"))
    float MinAngularVelocity = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation|Motion", meta = (ClampMin = "0.0"))
    float MaxAngularVelocity = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
    int32 RandomSeed = 42;
};
