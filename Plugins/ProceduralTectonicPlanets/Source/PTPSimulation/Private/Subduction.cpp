#include "Subduction.h"

#include "Async/ParallelFor.h"
#include "BoundaryTypes.h"
#include "CrustSample.h"
#include "PlanetConstants.h"
#include "PlanetState.h"

namespace
{
constexpr float SubductionPeakDistKm = 450.0f;
constexpr float FoldDirectionBeta = 0.01f;
constexpr float SlabPullEpsilon = 1.0e-4f;

struct FSubductionSegmentData
{
    bool bValid = false;
    int32 SegmentIndex = INDEX_NONE;
    int32 SubductingPlateIndex = INDEX_NONE;
    int32 OverridingPlateIndex = INDEX_NONE;
    float SpeedTransfer = 0.0f;
    float IncomingElevationKm = 0.0f;
};

float DistanceTransfer(const float DistanceKm)
{
    if (DistanceKm <= 0.0f || DistanceKm >= PTP::SubductionDist)
    {
        return 0.0f;
    }

    if (DistanceKm < SubductionPeakDistKm)
    {
        const float T = DistanceKm / SubductionPeakDistKm;
        return T * T * (3.0f - 2.0f * T);
    }

    const float T = (DistanceKm - SubductionPeakDistKm) / (PTP::SubductionDist - SubductionPeakDistKm);
    const float Smoothstep = T * T * (3.0f - 2.0f * T);
    return 1.0f - Smoothstep;
}

float SpeedTransfer(const float RelativeSpeedMmPerYear)
{
    return FMath::Clamp(RelativeSpeedMmPerYear / PTP::MaxPlateSpeed, 0.0f, 1.0f);
}

float HeightTransfer(const float IncomingElevationKm)
{
    const float ZTilde = FMath::Clamp(
        (IncomingElevationKm - PTP::TrenchElevation) / (PTP::MaxContElevation - PTP::TrenchElevation),
        0.0f,
        1.0f);
    return ZTilde * ZTilde;
}

float AverageElevationOfPlateSide(const FBoundarySegment& Segment, const FPlanetState& State, const int32 PlateIndex)
{
    const TArray<int32>& SideSamples = (Segment.PlateIndexA == PlateIndex) ? Segment.SamplesA : Segment.SamplesB;
    if (SideSamples.IsEmpty())
    {
        return 0.0f;
    }

    double SumElevation = 0.0;
    int32 Count = 0;
    for (const int32 SampleIndex : SideSamples)
    {
        if (!State.Samples.IsValidIndex(SampleIndex))
        {
            continue;
        }

        SumElevation += static_cast<double>(State.Samples[SampleIndex].Elevation);
        ++Count;
    }

    return Count > 0 ? static_cast<float>(SumElevation / static_cast<double>(Count)) : 0.0f;
}

FVector SurfaceVelocity(const FPlanetState& State, const int32 PlateIndex, const FVector& Position)
{
    if (!State.Plates.IsValidIndex(PlateIndex))
    {
        return FVector::ZeroVector;
    }

    const FPlate& Plate = State.Plates[PlateIndex];
    return FVector::CrossProduct(Plate.RotationAxis * Plate.AngularVelocity, Position);
}
}

void ProcessSubduction(FPlanetState& State, const float DeltaTime)
{
    if (State.Samples.IsEmpty() || State.BoundaryRegistry.Segments.IsEmpty() || State.SampleBoundaryInfo.Num() != State.Samples.Num())
    {
        return;
    }

    const double StartTime = FPlatformTime::Seconds();

    TArray<FSubductionSegmentData> SegmentData;
    SegmentData.SetNum(State.BoundaryRegistry.Segments.Num());

    int32 ProcessedSegments = 0;
    for (int32 SegmentIndex = 0; SegmentIndex < State.BoundaryRegistry.Segments.Num(); ++SegmentIndex)
    {
        const FBoundarySegment& Segment = State.BoundaryRegistry.Segments[SegmentIndex];
        if (Segment.Type != EPTPBoundaryType::Convergent)
        {
            continue;
        }

        if (Segment.ConvergenceType != EPTPConvergenceType::OceanicSubduction
            && Segment.ConvergenceType != EPTPConvergenceType::OceanOceanSubduction)
        {
            continue;
        }

        if (Segment.SubductingPlateIndex == INDEX_NONE || Segment.OverridingPlateIndex == INDEX_NONE)
        {
            continue;
        }

        FSubductionSegmentData Data;
        Data.bValid = true;
        Data.SegmentIndex = SegmentIndex;
        Data.SubductingPlateIndex = Segment.SubductingPlateIndex;
        Data.OverridingPlateIndex = Segment.OverridingPlateIndex;
        Data.SpeedTransfer = SpeedTransfer(Segment.RelativeSpeed);
        Data.IncomingElevationKm = AverageElevationOfPlateSide(Segment, State, Segment.SubductingPlateIndex);
        SegmentData[SegmentIndex] = Data;
        ++ProcessedSegments;
    }

    TArray<float> AppliedUpliftKm;
    AppliedUpliftKm.Init(0.0f, State.Samples.Num());

    ParallelFor(State.Samples.Num(), [&State, &SegmentData, &AppliedUpliftKm, DeltaTime](const int32 SampleIndex)
    {
        FCrustSample& Sample = State.Samples[SampleIndex];
        const FSampleBoundaryInfo& BoundaryInfo = State.SampleBoundaryInfo[SampleIndex];

        const float DistanceKm = Sample.DistToFront;
        if (DistanceKm <= 0.0f || DistanceKm >= PTP::SubductionDist)
        {
            return;
        }

        if (!SegmentData.IsValidIndex(BoundaryInfo.NearestConvergentSegmentIndex))
        {
            return;
        }

        const FSubductionSegmentData& Segment = SegmentData[BoundaryInfo.NearestConvergentSegmentIndex];
        if (!Segment.bValid || Segment.OverridingPlateIndex != Sample.PlateIndex)
        {
            return;
        }

        const float FD = DistanceTransfer(DistanceKm);
        if (FD <= 0.0f)
        {
            return;
        }

        const float GV = Segment.SpeedTransfer;
        const float HZ = HeightTransfer(Segment.IncomingElevationKm);
        const float UpliftKm = PTP::SubductionUplift * FD * GV * HZ * DeltaTime;
        if (UpliftKm <= 0.0f)
        {
            return;
        }

        AppliedUpliftKm[SampleIndex] = UpliftKm;
        Sample.Elevation = FMath::Min(Sample.Elevation + UpliftKm, PTP::MaxContElevation);
        Sample.OrogenyType = EOrogenyType::Andean;
        Sample.OrogenyAge = State.Time;

        const FVector VelSub = SurfaceVelocity(State, Segment.SubductingPlateIndex, Sample.Position);
        const FVector VelOver = SurfaceVelocity(State, Segment.OverridingPlateIndex, Sample.Position);
        FVector RelativeVelocity = VelSub - VelOver;
        RelativeVelocity -= Sample.Position * FVector::DotProduct(RelativeVelocity, Sample.Position);

        Sample.FoldDirection += FoldDirectionBeta * RelativeVelocity * DeltaTime;
        Sample.FoldDirection -= Sample.Position * FVector::DotProduct(Sample.FoldDirection, Sample.Position);
        if (Sample.FoldDirection.SizeSquared() < 1.0e-10f)
        {
            Sample.FoldDirection = RelativeVelocity.GetSafeNormal();
        }
    });

    TArray<FVector> PlateCentroids;
    TArray<int32> PlateCounts;
    PlateCentroids.Init(FVector::ZeroVector, State.Plates.Num());
    PlateCounts.Init(0, State.Plates.Num());
    for (const FCrustSample& Sample : State.Samples)
    {
        if (State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            PlateCentroids[Sample.PlateIndex] += Sample.Position;
            ++PlateCounts[Sample.PlateIndex];
        }
    }
    for (int32 PlateIndex = 0; PlateIndex < State.Plates.Num(); ++PlateIndex)
    {
        PlateCentroids[PlateIndex] = PlateCounts[PlateIndex] > 0 ? PlateCentroids[PlateIndex].GetSafeNormal() : FVector::UpVector;
    }

    TArray<FVector> SlabCorrections;
    TArray<int32> SlabCorrectionCounts;
    SlabCorrections.Init(FVector::ZeroVector, State.Plates.Num());
    SlabCorrectionCounts.Init(0, State.Plates.Num());

    for (const FBoundarySegment& Segment : State.BoundaryRegistry.Segments)
    {
        if (Segment.Type != EPTPBoundaryType::Convergent
            || (Segment.ConvergenceType != EPTPConvergenceType::OceanicSubduction
                && Segment.ConvergenceType != EPTPConvergenceType::OceanOceanSubduction)
            || !State.Plates.IsValidIndex(Segment.SubductingPlateIndex))
        {
            continue;
        }

        const int32 SubductingPlate = Segment.SubductingPlateIndex;
        const TArray<int32>& FrontSamples = (Segment.PlateIndexA == SubductingPlate) ? Segment.SamplesA : Segment.SamplesB;
        const FVector PlateCentroid = PlateCentroids[SubductingPlate];

        for (const int32 SampleIndex : FrontSamples)
        {
            if (!State.Samples.IsValidIndex(SampleIndex))
            {
                continue;
            }

            const FVector Cross = FVector::CrossProduct(PlateCentroid, State.Samples[SampleIndex].Position);
            const float CrossLen = Cross.Length();
            if (CrossLen > KINDA_SMALL_NUMBER)
            {
                SlabCorrections[SubductingPlate] += Cross / CrossLen;
                ++SlabCorrectionCounts[SubductingPlate];
            }
        }
    }

    int32 AdjustedAxes = 0;
    float MaxAxisCorrection = 0.0f;
    for (int32 PlateIndex = 0; PlateIndex < State.Plates.Num(); ++PlateIndex)
    {
        if (SlabCorrectionCounts[PlateIndex] <= 0)
        {
            continue;
        }

        FVector Correction = SlabCorrections[PlateIndex] / static_cast<float>(SlabCorrectionCounts[PlateIndex]);
        const FVector AxisDelta = SlabPullEpsilon * Correction * DeltaTime;
        if (AxisDelta.IsNearlyZero())
        {
            continue;
        }

        FPlate& Plate = State.Plates[PlateIndex];
        Plate.RotationAxis = (Plate.RotationAxis + AxisDelta).GetSafeNormal();
        ++AdjustedAxes;
        MaxAxisCorrection = FMath::Max(MaxAxisCorrection, AxisDelta.Size());
    }

    int32 AffectedSamples = 0;
    float MinUplift = FLT_MAX;
    float MaxUplift = 0.0f;
    double UpliftSum = 0.0;
    float MaxElevation = -FLT_MAX;
    int32 MaxElevationPlate = INDEX_NONE;

    for (int32 SampleIndex = 0; SampleIndex < State.Samples.Num(); ++SampleIndex)
    {
        MaxElevation = FMath::Max(MaxElevation, State.Samples[SampleIndex].Elevation);
        if (State.Samples[SampleIndex].Elevation == MaxElevation)
        {
            MaxElevationPlate = State.Samples[SampleIndex].PlateIndex;
        }

        const float Uplift = AppliedUpliftKm[SampleIndex];
        if (Uplift <= 0.0f)
        {
            continue;
        }

        ++AffectedSamples;
        MinUplift = FMath::Min(MinUplift, Uplift);
        MaxUplift = FMath::Max(MaxUplift, Uplift);
        UpliftSum += Uplift;
    }

    const float MeanUplift = AffectedSamples > 0 ? static_cast<float>(UpliftSum / static_cast<double>(AffectedSamples)) : 0.0f;
    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    UE_LOG(LogTemp, Log, TEXT("[PTP] Subduction: processed %d segments, affected %d samples"), ProcessedSegments, AffectedSamples);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Uplift stats: min=%.3f km, max=%.3f km, mean=%.3f km (this step)"),
        AffectedSamples > 0 ? MinUplift : 0.0f, MaxUplift, MeanUplift);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Max elevation: %.2f km (plate %d, Andean orogeny)"), MaxElevation, MaxElevationPlate);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Slab pull: adjusted %d plate axes, max correction=%.6f rad"), AdjustedAxes, MaxAxisCorrection);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Subduction processing: %.1f ms"), ElapsedMs);
}
