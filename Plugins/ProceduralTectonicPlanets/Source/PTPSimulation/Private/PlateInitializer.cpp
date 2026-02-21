#include "PlateInitializer.h"

#include "PlanetConstants.h"

namespace
{
constexpr float AreaTolerance = 0.02f;
constexpr float ContinentalSampleRatioTolerance = 0.20f;

int32 FindClosestSeedByDot(const FVector& Position, const TArray<int32>& SeedIndices, const TArray<FCrustSample>& Samples)
{
    int32 ClosestSeed = 0;
    float BestDot = -1.0f;

    for (int32 SeedOrder = 0; SeedOrder < SeedIndices.Num(); ++SeedOrder)
    {
        const FVector SeedPosition = Samples[SeedIndices[SeedOrder]].Position;
        const float Dot = FVector::DotProduct(Position, SeedPosition);
        if (Dot > BestDot)
        {
            BestDot = Dot;
            ClosestSeed = SeedOrder;
        }
    }

    return ClosestSeed;
}
}

void InitializePlates(FPlanetState& State, const FPlanetGenerationParams& Params)
{
    if (State.SampleCount <= 0 || State.Samples.Num() != State.SampleCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] InitializePlates skipped: invalid sample buffer."));
        State.NumPlates = 0;
        State.Plates.Reset();
        return;
    }

    const int32 NumSamples = State.SampleCount;
    const int32 RequestedPlates = FMath::Max(1, Params.NumPlates);
    const int32 NumPlates = FMath::Clamp(RequestedPlates, 1, NumSamples);

    FRandomStream RandomStream(Params.RandomSeed);

    for (FCrustSample& Sample : State.Samples)
    {
        Sample.PlateIndex = -1;
    }

    State.NumPlates = NumPlates;
    State.Plates.SetNum(NumPlates);
    for (int32 PlateIndex = 0; PlateIndex < NumPlates; ++PlateIndex)
    {
        State.Plates[PlateIndex] = FPlate();
        State.Plates[PlateIndex].PlateIndex = PlateIndex;
    }

    TArray<int32> SeedIndices;
    SeedIndices.Reserve(NumPlates);

    TArray<uint8> UsedSeedFlags;
    UsedSeedFlags.Init(0, NumSamples);

    const float MinimumSeedAngle = PI / (2.0f * FMath::Sqrt(static_cast<float>(NumPlates)));
    const float MinimumSeedDot = FMath::Cos(MinimumSeedAngle);
    const int32 MaxSeedAttempts = NumPlates * 800;

    int32 Attempts = 0;
    while (SeedIndices.Num() < NumPlates && Attempts < MaxSeedAttempts)
    {
        ++Attempts;
        const int32 Candidate = RandomStream.RandRange(0, NumSamples - 1);
        if (UsedSeedFlags[Candidate])
        {
            continue;
        }

        const FVector CandidatePosition = State.Samples[Candidate].Position;
        bool bTooClose = false;
        for (const int32 ExistingSeed : SeedIndices)
        {
            const FVector ExistingPosition = State.Samples[ExistingSeed].Position;
            if (FVector::DotProduct(CandidatePosition, ExistingPosition) > MinimumSeedDot)
            {
                bTooClose = true;
                break;
            }
        }

        if (bTooClose)
        {
            continue;
        }

        UsedSeedFlags[Candidate] = 1;
        SeedIndices.Add(Candidate);
    }

    while (SeedIndices.Num() < NumPlates)
    {
        const int32 Candidate = RandomStream.RandRange(0, NumSamples - 1);
        if (UsedSeedFlags[Candidate])
        {
            continue;
        }

        UsedSeedFlags[Candidate] = 1;
        SeedIndices.Add(Candidate);
    }

    for (int32 PlateIndex = 0; PlateIndex < NumPlates; ++PlateIndex)
    {
        const int32 SeedSampleIndex = SeedIndices[PlateIndex];
        State.Plates[PlateIndex].SeedSampleIndex = SeedSampleIndex;
        State.Samples[SeedSampleIndex].PlateIndex = PlateIndex;
    }

    TArray<int32> Queue;
    Queue.Reserve(NumSamples);
    for (const int32 SeedSampleIndex : SeedIndices)
    {
        Queue.Add(SeedSampleIndex);
    }

    for (int32 QueueHead = 0; QueueHead < Queue.Num(); ++QueueHead)
    {
        const int32 SampleIndex = Queue[QueueHead];
        const int32 PlateIndex = State.Samples[SampleIndex].PlateIndex;

        const int32 Begin = State.AdjacencyOffsets.IsValidIndex(SampleIndex) ? State.AdjacencyOffsets[SampleIndex] : 0;
        const int32 End = State.AdjacencyOffsets.IsValidIndex(SampleIndex + 1) ? State.AdjacencyOffsets[SampleIndex + 1] : Begin;
        for (int32 NeighborOffset = Begin; NeighborOffset < End; ++NeighborOffset)
        {
            const int32 NeighborIndex = State.AdjacencyNeighbors[NeighborOffset];
            if (!State.Samples.IsValidIndex(NeighborIndex))
            {
                continue;
            }

            if (State.Samples[NeighborIndex].PlateIndex >= 0)
            {
                continue;
            }

            State.Samples[NeighborIndex].PlateIndex = PlateIndex;
            Queue.Add(NeighborIndex);
        }
    }

    int32 UnassignedCount = 0;
    for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        if (State.Samples[SampleIndex].PlateIndex >= 0)
        {
            continue;
        }

        ++UnassignedCount;
        State.Samples[SampleIndex].PlateIndex = FindClosestSeedByDot(State.Samples[SampleIndex].Position, SeedIndices, State.Samples);
    }

    if (UnassignedCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] InitializePlates fallback assigned %d disconnected samples."), UnassignedCount);
    }

    TArray<int32> PlateCounts;
    PlateCounts.Init(0, NumPlates);

    for (const FCrustSample& Sample : State.Samples)
    {
        if (State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            ++PlateCounts[Sample.PlateIndex];
        }
    }

    for (int32 PlateIndex = 0; PlateIndex < NumPlates; ++PlateIndex)
    {
        State.Plates[PlateIndex].Area = static_cast<float>(PlateCounts[PlateIndex]) / static_cast<float>(NumSamples);
    }

    TArray<int32> PlateOrder;
    PlateOrder.Reserve(NumPlates);
    for (int32 PlateIndex = 0; PlateIndex < NumPlates; ++PlateIndex)
    {
        PlateOrder.Add(PlateIndex);
    }

    PlateOrder.Sort([&State](int32 A, int32 B)
    {
        return State.Plates[A].Area > State.Plates[B].Area;
    });

    const float ClampedContinentalRatio = FMath::Clamp(Params.ContinentalRatio, 0.0f, 1.0f);
    const int32 ContinentalPlateCount = FMath::Clamp(FMath::RoundToInt(static_cast<float>(NumPlates) * ClampedContinentalRatio), 0, NumPlates);

    for (int32 OrderIndex = 0; OrderIndex < PlateOrder.Num(); ++OrderIndex)
    {
        const int32 PlateIndex = PlateOrder[OrderIndex];
        State.Plates[PlateIndex].CrustType = (OrderIndex < ContinentalPlateCount) ? ECrustType::Continental : ECrustType::Oceanic;
    }

    const float MinContinental = FMath::Min(Params.ContinentalBaseElevation, Params.ContinentalMaxElevation);
    const float MaxContinental = FMath::Max(Params.ContinentalBaseElevation, Params.ContinentalMaxElevation);
    const float MinOceanic = FMath::Min(Params.OceanicMinElevation, Params.OceanicBaseElevation);
    const float MaxOceanic = FMath::Max(Params.OceanicMinElevation, Params.OceanicBaseElevation);

    for (FCrustSample& Sample : State.Samples)
    {
        const FPlate& Plate = State.Plates[Sample.PlateIndex];
        Sample.CrustType = Plate.CrustType;

        if (Sample.CrustType == ECrustType::Continental)
        {
            Sample.Elevation = FMath::Lerp(MinContinental, MaxContinental, RandomStream.FRand());
            Sample.Thickness = 35.0f;
        }
        else
        {
            Sample.Elevation = FMath::Lerp(MinOceanic, MaxOceanic, RandomStream.FRand());
            Sample.OceanicAge = 0.0f;
            Sample.Thickness = 7.0f;
        }
    }

    const float MinAngularVelocity = FMath::Min(Params.MinAngularVelocity, Params.MaxAngularVelocity);
    const float MaxAngularVelocity = FMath::Max(Params.MinAngularVelocity, Params.MaxAngularVelocity);
    for (FPlate& Plate : State.Plates)
    {
        FVector Axis = RandomStream.VRand();
        Axis.Normalize();
        Plate.RotationAxis = Axis;

        Plate.AngularVelocity = FMath::Lerp(MinAngularVelocity, MaxAngularVelocity, RandomStream.FRand());
        if (RandomStream.FRand() < 0.5f)
        {
            Plate.AngularVelocity *= -1.0f;
        }

        const float AnglePerStep = Plate.AngularVelocity * PTP::DeltaT;
        Plate.StepRotation = FQuat(Plate.RotationAxis, AnglePerStep);
    }
}

bool ValidatePlateInitialization(const FPlanetState& State)
{
    if (State.SampleCount <= 0 || State.Samples.Num() != State.SampleCount || State.NumPlates <= 0 || State.Plates.Num() != State.NumPlates)
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: malformed state buffers."));
        return false;
    }

    if (State.AdjacencyOffsets.Num() != State.SampleCount + 1)
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: adjacency offsets size %d does not match sample count %d."),
            State.AdjacencyOffsets.Num(),
            State.SampleCount + 1);
        return false;
    }

    bool bValid = true;

    TArray<int32> PlateSampleCounts;
    PlateSampleCounts.Init(0, State.NumPlates);

    int32 ContinentalSamples = 0;
    int32 OceanicSamples = 0;
    for (int32 SampleIndex = 0; SampleIndex < State.SampleCount; ++SampleIndex)
    {
        const FCrustSample& Sample = State.Samples[SampleIndex];
        if (!State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: sample %d has invalid plate index %d."), SampleIndex, Sample.PlateIndex);
            bValid = false;
            continue;
        }

        ++PlateSampleCounts[Sample.PlateIndex];
        if (Sample.CrustType == ECrustType::Continental)
        {
            ++ContinentalSamples;
        }
        else
        {
            ++OceanicSamples;
        }
    }

    float AreaSum = 0.0f;
    for (int32 PlateIndex = 0; PlateIndex < State.NumPlates; ++PlateIndex)
    {
        const FPlate& Plate = State.Plates[PlateIndex];
        AreaSum += Plate.Area;

        if (PlateSampleCounts[PlateIndex] <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: plate %d has no assigned samples."), PlateIndex);
            bValid = false;
        }

        if (!Plate.RotationAxis.IsNormalized())
        {
            UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: plate %d has non-unit rotation axis (|axis|=%.6f)."),
                PlateIndex,
                Plate.RotationAxis.Length());
            bValid = false;
        }
    }

    if (!FMath::IsNearlyEqual(AreaSum, 1.0f, AreaTolerance))
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: plate areas sum to %.6f."), AreaSum);
        bValid = false;
    }

    int32 ContinentalPlateCount = 0;
    for (const FPlate& Plate : State.Plates)
    {
        if (Plate.CrustType == ECrustType::Continental)
        {
            ++ContinentalPlateCount;
        }
    }

    const float ContinentalSampleRatio = static_cast<float>(ContinentalSamples) / static_cast<float>(State.SampleCount);
    const float ExpectedContinentalRatio = FPlanetGenerationParams().ContinentalRatio;
    if (FMath::Abs(ContinentalSampleRatio - ExpectedContinentalRatio) > ContinentalSampleRatioTolerance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Plate validation warning: continental sample ratio %.3f deviates from default %.3f."),
            ContinentalSampleRatio,
            ExpectedContinentalRatio);
    }

    int32 MismatchCount = 0;
    for (int32 SampleIndex = 0; SampleIndex < State.SampleCount; ++SampleIndex)
    {
        const FCrustSample& Sample = State.Samples[SampleIndex];
        const int32 Begin = State.AdjacencyOffsets[SampleIndex];
        const int32 End = State.AdjacencyOffsets[SampleIndex + 1];
        for (int32 NeighborOffset = Begin; NeighborOffset < End; ++NeighborOffset)
        {
            const int32 NeighborIndex = State.AdjacencyNeighbors[NeighborOffset];
            const FCrustSample& Neighbor = State.Samples[NeighborIndex];

            if (Sample.PlateIndex == Neighbor.PlateIndex && Sample.CrustType != Neighbor.CrustType)
            {
                ++MismatchCount;
            }
        }
    }

    if (MismatchCount > 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] Plate validation failed: detected %d same-plate crust type mismatches."), MismatchCount);
        bValid = false;
    }

    UE_LOG(LogTemp, Log, TEXT("[PTP] Plate validation stats: plates=%d (continental=%d), samples=(continental=%d oceanic=%d), areaSum=%.6f"),
        State.NumPlates,
        ContinentalPlateCount,
        ContinentalSamples,
        OceanicSamples,
        AreaSum);

    return bValid;
}
