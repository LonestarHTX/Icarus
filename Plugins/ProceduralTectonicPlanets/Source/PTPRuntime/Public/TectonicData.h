#pragma once

#include "CoreMinimal.h"

#include "PlanetState.h"

#include "TectonicData.generated.h"

UENUM(BlueprintType)
enum class EBoundaryType : uint8
{
    None,
    Convergent,
    Divergent,
    Transform
};

UENUM(BlueprintType)
enum class EBoundaryConvergenceType : uint8
{
    None,
    OceanicSubduction,
    OceanOceanSubduction,
    ContinentalCollision
};

USTRUCT(BlueprintType)
struct PTPRUNTIME_API FTectonicData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<FVector> PointPositions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<int32> PlateIDs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<float> Elevations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<bool> ContinentalMask;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<EBoundaryType> BoundaryTypes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<EBoundaryConvergenceType> BoundaryConvergenceTypes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<float> BoundaryStress;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonic")
    TArray<FVector> Velocities;

    int32 GetNumPoints() const;
    FVector3d GetPointPosition(int32 Index) const;
    int32 GetPlateID(int32 Index) const;
    double GetElevation(int32 Index) const;
    bool IsContinental(int32 Index) const;
    EBoundaryType GetBoundaryType(int32 Index) const;
    EBoundaryConvergenceType GetBoundaryConvergenceType(int32 Index) const;
    double GetBoundaryStress(int32 Index) const;
    FVector3d GetVelocity(int32 Index) const;

    static FTectonicData FromPlanetState(const FPlanetState& State);
    static FTectonicData CreateMockData(int32 NumPoints = 120000, int32 NumPlates = 12, int32 Seed = 1337);
};
