#include "PlateMotion.h"

#include "Async/ParallelFor.h"
#include "CrustSample.h"
#include "PlanetConstants.h"
#include "PlanetState.h"
#include "Plate.h"

namespace
{
constexpr double MmPerYearToKmPerMy = 1.0e-6;
constexpr double AverageEulerPoleSinFactor = 0.7;

FVector RandomUniformUnitVector(FRandomStream& RandomStream)
{
    const double Z = RandomStream.FRandRange(-1.0f, 1.0f);
    const double Phi = RandomStream.FRandRange(0.0f, 2.0f * PI);
    const double XY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));

    return FVector(
        XY * FMath::Cos(Phi),
        XY * FMath::Sin(Phi),
        Z);
}

double ComputeMaxAngularSpeedRadPerMy()
{
    const double MaxSurfaceSpeedKmPerMy = static_cast<double>(PTP::MaxPlateSpeed) * MmPerYearToKmPerMy;
    const double RadiusKm = static_cast<double>(PTP::Radius);
    return MaxSurfaceSpeedKmPerMy / (RadiusKm * AverageEulerPoleSinFactor);
}

FVector ProjectToTangent(const FVector& Vector, const FVector& Position)
{
    return Vector - (FVector::DotProduct(Vector, Position) * Position);
}
}

void InitializePlateMotions(TArray<FPlate>& Plates, FRandomStream& RandomStream)
{
    const double MaxAngularSpeed = ComputeMaxAngularSpeedRadPerMy();
    constexpr double MinSpeedFactor = 0.2;

    for (FPlate& Plate : Plates)
    {
        FVector Axis = RandomUniformUnitVector(RandomStream).GetSafeNormal();
        if (Axis.IsNearlyZero())
        {
            Axis = FVector::UpVector;
        }
        Plate.RotationAxis = Axis;

        const double SpeedFactor = RandomStream.FRandRange(MinSpeedFactor, 1.0);
        Plate.AngularVelocity = static_cast<float>(SpeedFactor * MaxAngularSpeed);
        Plate.StepRotation = FQuat(Plate.RotationAxis, Plate.AngularVelocity * PTP::DeltaT);
    }
}

void MovePlates(FPlanetState& State, const float DeltaTime)
{
    if (State.Samples.IsEmpty() || State.Plates.IsEmpty())
    {
        return;
    }

    TArray<FQuat4d> PlateRotations;
    PlateRotations.SetNum(State.Plates.Num());

    for (int32 PlateIndex = 0; PlateIndex < State.Plates.Num(); ++PlateIndex)
    {
        FPlate& Plate = State.Plates[PlateIndex];
        FVector Axis = Plate.RotationAxis.GetSafeNormal();
        if (Axis.IsNearlyZero())
        {
            Axis = FVector::UpVector;
        }
        Plate.RotationAxis = Axis;

        const double StepAngle = static_cast<double>(Plate.AngularVelocity) * static_cast<double>(DeltaTime);
        const FQuat4d Rotation(FVector3d(Axis), StepAngle);
        PlateRotations[PlateIndex] = Rotation;
        Plate.StepRotation = FQuat(Rotation);
    }

    ParallelFor(State.Samples.Num(), [&State, &PlateRotations](const int32 SampleIndex)
    {
        FCrustSample& Sample = State.Samples[SampleIndex];
        if (!PlateRotations.IsValidIndex(Sample.PlateIndex))
        {
            return;
        }

        const FQuat4d& Rotation = PlateRotations[Sample.PlateIndex];

        const FVector3d RotatedPosition = Rotation.RotateVector(FVector3d(Sample.Position));
        Sample.Position = FVector(RotatedPosition.GetSafeNormal());

        const float RidgeLength = Sample.RidgeDirection.Length();
        const FVector RidgeRotated = FVector(Rotation.RotateVector(FVector3d(Sample.RidgeDirection)));
        const FVector RidgeTangent = ProjectToTangent(RidgeRotated, Sample.Position).GetSafeNormal();
        Sample.RidgeDirection = RidgeLength > KINDA_SMALL_NUMBER ? RidgeTangent * RidgeLength : FVector::ZeroVector;

        const float FoldLength = Sample.FoldDirection.Length();
        const FVector FoldRotated = FVector(Rotation.RotateVector(FVector3d(Sample.FoldDirection)));
        const FVector FoldTangent = ProjectToTangent(FoldRotated, Sample.Position).GetSafeNormal();
        Sample.FoldDirection = FoldLength > KINDA_SMALL_NUMBER ? FoldTangent * FoldLength : FVector::ZeroVector;
    });

    State.Time += DeltaTime;
}
