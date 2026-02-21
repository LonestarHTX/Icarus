#include "TectonicData.h"

#include "BoundaryTypes.h"
#include "FibonacciSphere.h"
#include "PlateInitializer.h"
#include "PlanetConstants.h"
#include "PlanetGenerationParams.h"

int32 FTectonicData::GetNumPoints() const
{
    return PointPositions.Num();
}

FVector3d FTectonicData::GetPointPosition(const int32 Index) const
{
    return PointPositions.IsValidIndex(Index) ? PointPositions[Index] : FVector3d::ZeroVector;
}

int32 FTectonicData::GetPlateID(const int32 Index) const
{
    return PlateIDs.IsValidIndex(Index) ? PlateIDs[Index] : -1;
}

double FTectonicData::GetElevation(const int32 Index) const
{
    return Elevations.IsValidIndex(Index) ? Elevations[Index] : 0.0;
}

bool FTectonicData::IsContinental(const int32 Index) const
{
    return ContinentalMask.IsValidIndex(Index) ? ContinentalMask[Index] : false;
}

EBoundaryType FTectonicData::GetBoundaryType(const int32 Index) const
{
    return BoundaryTypes.IsValidIndex(Index) ? BoundaryTypes[Index] : EBoundaryType::None;
}

EBoundaryConvergenceType FTectonicData::GetBoundaryConvergenceType(const int32 Index) const
{
    return BoundaryConvergenceTypes.IsValidIndex(Index) ? BoundaryConvergenceTypes[Index] : EBoundaryConvergenceType::None;
}

double FTectonicData::GetBoundaryStress(const int32 Index) const
{
    return BoundaryStress.IsValidIndex(Index) ? BoundaryStress[Index] : 0.0;
}

FVector3d FTectonicData::GetVelocity(const int32 Index) const
{
    return Velocities.IsValidIndex(Index) ? Velocities[Index] : FVector3d::ZeroVector;
}

FTectonicData FTectonicData::FromPlanetState(const FPlanetState& State)
{
    const auto ConvertBoundaryType = [](const EPTPBoundaryType InType)
    {
        switch (InType)
        {
        case EPTPBoundaryType::Convergent:
            return EBoundaryType::Convergent;
        case EPTPBoundaryType::Divergent:
            return EBoundaryType::Divergent;
        case EPTPBoundaryType::Transform:
            return EBoundaryType::Transform;
        case EPTPBoundaryType::None:
        default:
            return EBoundaryType::None;
        }
    };

    const auto ConvertConvergenceType = [](const EPTPConvergenceType InType)
    {
        switch (InType)
        {
        case EPTPConvergenceType::OceanicSubduction:
            return EBoundaryConvergenceType::OceanicSubduction;
        case EPTPConvergenceType::OceanOceanSubduction:
            return EBoundaryConvergenceType::OceanOceanSubduction;
        case EPTPConvergenceType::ContinentalCollision:
            return EBoundaryConvergenceType::ContinentalCollision;
        case EPTPConvergenceType::None:
        default:
            return EBoundaryConvergenceType::None;
        }
    };

    FTectonicData Out;
    const int32 NumPoints = State.Samples.Num();
    Out.PointPositions.SetNum(NumPoints);
    Out.PlateIDs.SetNum(NumPoints);
    Out.Elevations.SetNum(NumPoints);
    Out.ContinentalMask.SetNum(NumPoints);
    Out.BoundaryTypes.SetNum(NumPoints);
    Out.BoundaryConvergenceTypes.SetNum(NumPoints);
    Out.BoundaryStress.SetNum(NumPoints);
    Out.Velocities.SetNum(NumPoints);

    for (int32 Index = 0; Index < NumPoints; ++Index)
    {
        const FCrustSample& Sample = State.Samples[Index];
        Out.PointPositions[Index] = Sample.Position;
        Out.PlateIDs[Index] = Sample.PlateIndex;
        Out.Elevations[Index] = Sample.Elevation / 10.0;
        Out.ContinentalMask[Index] = Sample.CrustType == ECrustType::Continental;
        EBoundaryType BoundaryType = EBoundaryType::None;
        EBoundaryConvergenceType ConvergenceType = EBoundaryConvergenceType::None;
        float BoundaryStress = 0.0f;
        if (State.SampleBoundaryInfo.IsValidIndex(Index))
        {
            const FSampleBoundaryInfo& BoundaryInfo = State.SampleBoundaryInfo[Index];
            BoundaryType = ConvertBoundaryType(BoundaryInfo.BoundaryType);
            ConvergenceType = ConvertConvergenceType(BoundaryInfo.ConvergenceType);
            BoundaryStress = BoundaryInfo.BoundaryStress;
        }
        Out.BoundaryTypes[Index] = BoundaryType;
        Out.BoundaryConvergenceTypes[Index] = ConvergenceType;
        Out.BoundaryStress[Index] = BoundaryStress;

        FVector3d Velocity = FVector3d::ZeroVector;
        if (State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            const FPlate& Plate = State.Plates[Sample.PlateIndex];
            const FVector3d AngularVector = FVector3d(Plate.RotationAxis) * Plate.AngularVelocity;
            Velocity = FVector3d::CrossProduct(AngularVector, FVector3d(Sample.Position));
        }

        Out.Velocities[Index] = FVector(Velocity);
    }

    return Out;
}

FTectonicData FTectonicData::CreateMockData(int32 NumPoints, int32 NumPlates, const int32 Seed)
{
    NumPoints = FMath::Clamp(NumPoints, 64, 1000000);
    NumPlates = FMath::Clamp(NumPlates, 2, 128);

    FPlanetState State;
    State.SampleCount = NumPoints;
    State.Samples.SetNum(NumPoints);

    TArray<FVector> Positions;
    GenerateFibonacciSphere(Positions, NumPoints);
    for (int32 Index = 0; Index < NumPoints; ++Index)
    {
        State.Samples[Index].Position = Positions[Index];
    }

    FPlanetGenerationParams Params;
    Params.SampleCount = NumPoints;
    Params.NumPlates = NumPlates;
    Params.RandomSeed = Seed;
    Params.ContinentalRatio = 0.4f;
    Params.MinAngularVelocity = 0.008f;
    Params.MaxAngularVelocity = 0.025f;

    InitializePlates(State, Params);

    FTectonicData Out = FromPlanetState(State);
    Out.BoundaryTypes.Init(EBoundaryType::None, NumPoints);
    Out.BoundaryConvergenceTypes.Init(EBoundaryConvergenceType::None, NumPoints);
    Out.BoundaryStress.Init(0.0, NumPoints);

    for (int32 Index = 0; Index < NumPoints; ++Index)
    {
        const int32 PlateID = Out.PlateIDs[Index];
        if (!State.Plates.IsValidIndex(PlateID))
        {
            continue;
        }

        const FPlate& Plate = State.Plates[PlateID];
        const FVector3d Position = FVector3d(Out.PointPositions[Index]);
        const FVector3d AngularVector = FVector3d(Plate.RotationAxis) * Plate.AngularVelocity;
        Out.Velocities[Index] = FVector(FVector3d::CrossProduct(AngularVector, Position));

        const double Noise =
            0.42 * FMath::Sin(Position.X * 7.5 + 0.7) +
            0.30 * FMath::Sin(Position.Y * 11.1 + 1.9) +
            0.22 * FMath::Cos(Position.Z * 13.2 - 0.4);
        Out.Elevations[Index] = FMath::Clamp(Out.Elevations[Index] + Noise * 0.35, -1.0, 1.0);
    }

    if (State.AdjacencyOffsets.Num() == NumPoints + 1)
    {
        for (int32 SampleIndex = 0; SampleIndex < NumPoints; ++SampleIndex)
        {
            const int32 Begin = State.AdjacencyOffsets[SampleIndex];
            const int32 End = State.AdjacencyOffsets[SampleIndex + 1];

            const int32 PlateA = Out.PlateIDs[SampleIndex];
            EBoundaryType BoundaryType = EBoundaryType::None;
            double Stress = 0.0;

            for (int32 NeighborOffset = Begin; NeighborOffset < End; ++NeighborOffset)
            {
                const int32 NeighborIndex = State.AdjacencyNeighbors[NeighborOffset];
                const int32 PlateB = Out.PlateIDs[NeighborIndex];
                if (PlateA == PlateB || !State.Plates.IsValidIndex(PlateB))
                {
                    continue;
                }

                const FVector3d VRel = FVector3d(Out.Velocities[SampleIndex] - Out.Velocities[NeighborIndex]);
                const FVector3d Delta = FVector3d(Out.PointPositions[NeighborIndex] - Out.PointPositions[SampleIndex]);
                const double SignedMotion = FVector3d::DotProduct(VRel, Delta.GetSafeNormal());

                if (SignedMotion > 0.02)
                {
                    BoundaryType = EBoundaryType::Convergent;
                }
                else if (SignedMotion < -0.02)
                {
                    BoundaryType = EBoundaryType::Divergent;
                }
                else
                {
                    BoundaryType = EBoundaryType::Transform;
                }

                Stress = FMath::Max(Stress, FMath::Clamp(FMath::Abs(SignedMotion) * 4.0, 0.0, 1.0));
            }

            Out.BoundaryTypes[SampleIndex] = BoundaryType;
            Out.BoundaryStress[SampleIndex] = Stress;
        }
    }

    return Out;
}
