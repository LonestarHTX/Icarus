#include "BoundaryDetection.h"

#include "Async/ParallelFor.h"
#include "BoundaryTypes.h"
#include "CrustSample.h"
#include "PlanetConstants.h"
#include "PlanetState.h"

#include <queue>
#include <vector>

namespace
{
constexpr float ConvergeThresholdMmPerYear = 5.0f;

int64 MakePlatePairKey(const int32 PlateA, const int32 PlateB)
{
    const int32 MinPlate = FMath::Min(PlateA, PlateB);
    const int32 MaxPlate = FMath::Max(PlateA, PlateB);
    return (static_cast<int64>(MinPlate) << 32) | static_cast<uint32>(MaxPlate);
}

double GreatCircleAngle(const FVector& A, const FVector& B)
{
    const FVector3d AD(A);
    const FVector3d BD(B);
    return FMath::Atan2(FVector3d::CrossProduct(AD, BD).Length(), FVector3d::DotProduct(AD, BD));
}

float GreatCircleDistanceKm(const FVector& A, const FVector& B)
{
    return static_cast<float>(GreatCircleAngle(A, B) * static_cast<double>(PTP::Radius));
}

FVector ComputeMeanDirection(const TArray<int32>& Indices, const TArray<FCrustSample>& Samples)
{
    FVector Sum = FVector::ZeroVector;
    for (const int32 SampleIndex : Indices)
    {
        if (Samples.IsValidIndex(SampleIndex))
        {
            Sum += Samples[SampleIndex].Position;
        }
    }

    return Sum.GetSafeNormal();
}

ECrustType MajorityCrustType(const TArray<int32>& Indices, const FPlanetState& State, const int32 FallbackPlateIndex)
{
    int32 Continental = 0;
    int32 Oceanic = 0;
    for (const int32 SampleIndex : Indices)
    {
        if (!State.Samples.IsValidIndex(SampleIndex))
        {
            continue;
        }

        if (State.Samples[SampleIndex].CrustType == ECrustType::Continental)
        {
            ++Continental;
        }
        else
        {
            ++Oceanic;
        }
    }

    if (Continental == Oceanic && State.Plates.IsValidIndex(FallbackPlateIndex))
    {
        return State.Plates[FallbackPlateIndex].CrustType;
    }

    return (Continental > Oceanic) ? ECrustType::Continental : ECrustType::Oceanic;
}

double AverageOceanicAge(const TArray<int32>& Indices, const FPlanetState& State)
{
    double AgeSum = 0.0;
    int32 AgeCount = 0;
    for (const int32 SampleIndex : Indices)
    {
        if (!State.Samples.IsValidIndex(SampleIndex))
        {
            continue;
        }

        const FCrustSample& Sample = State.Samples[SampleIndex];
        if (Sample.CrustType != ECrustType::Oceanic)
        {
            continue;
        }

        AgeSum += static_cast<double>(Sample.OceanicAge);
        ++AgeCount;
    }

    return AgeCount > 0 ? AgeSum / static_cast<double>(AgeCount) : 0.0;
}

FVector ComputeSurfaceVelocity(const FPlanetState& State, const int32 PlateIndex, const FVector& Position)
{
    if (!State.Plates.IsValidIndex(PlateIndex))
    {
        return FVector::ZeroVector;
    }

    const FPlate& Plate = State.Plates[PlateIndex];
    const FVector AngularVector = Plate.RotationAxis * Plate.AngularVelocity;
    return FVector::CrossProduct(AngularVector, Position);
}

void ClassifyConvergence(FBoundarySegment& Segment, const FPlanetState& State)
{
    const ECrustType CrustA = MajorityCrustType(Segment.SamplesA, State, Segment.PlateIndexA);
    const ECrustType CrustB = MajorityCrustType(Segment.SamplesB, State, Segment.PlateIndexB);

    Segment.SubductingPlateIndex = INDEX_NONE;
    Segment.OverridingPlateIndex = INDEX_NONE;

    if (CrustA == ECrustType::Oceanic && CrustB == ECrustType::Oceanic)
    {
        Segment.ConvergenceType = EPTPConvergenceType::OceanOceanSubduction;
        const double AgeA = AverageOceanicAge(Segment.SamplesA, State);
        const double AgeB = AverageOceanicAge(Segment.SamplesB, State);
        if (AgeA >= AgeB)
        {
            Segment.SubductingPlateIndex = Segment.PlateIndexA;
            Segment.OverridingPlateIndex = Segment.PlateIndexB;
        }
        else
        {
            Segment.SubductingPlateIndex = Segment.PlateIndexB;
            Segment.OverridingPlateIndex = Segment.PlateIndexA;
        }
        return;
    }

    if (CrustA == ECrustType::Oceanic && CrustB == ECrustType::Continental)
    {
        Segment.ConvergenceType = EPTPConvergenceType::OceanicSubduction;
        Segment.SubductingPlateIndex = Segment.PlateIndexA;
        Segment.OverridingPlateIndex = Segment.PlateIndexB;
        return;
    }

    if (CrustA == ECrustType::Continental && CrustB == ECrustType::Oceanic)
    {
        Segment.ConvergenceType = EPTPConvergenceType::OceanicSubduction;
        Segment.SubductingPlateIndex = Segment.PlateIndexB;
        Segment.OverridingPlateIndex = Segment.PlateIndexA;
        return;
    }

    Segment.ConvergenceType = EPTPConvergenceType::ContinentalCollision;
}

void ComputeDistanceToFront(FPlanetState& State)
{
    for (FCrustSample& Sample : State.Samples)
    {
        Sample.DistToFront = TNumericLimits<float>::Max();
    }

    struct FQueueNode
    {
        float Distance = 0.0f;
        int32 SampleIndex = INDEX_NONE;
    };

    struct FQueueNodeCompare
    {
        bool operator()(const FQueueNode& A, const FQueueNode& B) const
        {
            return A.Distance > B.Distance;
        }
    };

    ParallelFor(State.Plates.Num(), [&State](const int32 PlateIndex)
    {
        TArray<int32> Seeds;
        for (const FBoundarySegment& Segment : State.BoundaryRegistry.Segments)
        {
            if (Segment.Type != EPTPBoundaryType::Convergent)
            {
                continue;
            }

            if (Segment.OverridingPlateIndex == PlateIndex)
            {
                const TArray<int32>& SideSamples = (Segment.PlateIndexA == PlateIndex) ? Segment.SamplesA : Segment.SamplesB;
                Seeds.Append(SideSamples);
                continue;
            }

            if (Segment.ConvergenceType == EPTPConvergenceType::ContinentalCollision)
            {
                if (Segment.PlateIndexA == PlateIndex)
                {
                    Seeds.Append(Segment.SamplesA);
                }
                else if (Segment.PlateIndexB == PlateIndex)
                {
                    Seeds.Append(Segment.SamplesB);
                }
            }
        }

        if (Seeds.IsEmpty())
        {
            return;
        }

        Seeds.Sort();
        int32 WriteIndex = 0;
        for (int32 ReadIndex = 0; ReadIndex < Seeds.Num(); ++ReadIndex)
        {
            if (WriteIndex == 0 || Seeds[ReadIndex] != Seeds[WriteIndex - 1])
            {
                Seeds[WriteIndex++] = Seeds[ReadIndex];
            }
        }
        Seeds.SetNum(WriteIndex);

        std::priority_queue<FQueueNode, std::vector<FQueueNode>, FQueueNodeCompare> Queue;
        for (const int32 Seed : Seeds)
        {
            if (!State.Samples.IsValidIndex(Seed) || State.Samples[Seed].PlateIndex != PlateIndex)
            {
                continue;
            }

            State.Samples[Seed].DistToFront = 0.0f;
            Queue.push(FQueueNode{0.0f, Seed});
        }

        while (!Queue.empty())
        {
            const FQueueNode Current = Queue.top();
            Queue.pop();

            if (!State.Samples.IsValidIndex(Current.SampleIndex))
            {
                continue;
            }

            if (Current.Distance > State.Samples[Current.SampleIndex].DistToFront + KINDA_SMALL_NUMBER)
            {
                continue;
            }

            const int32 Begin = State.AdjacencyOffsets[Current.SampleIndex];
            const int32 End = State.AdjacencyOffsets[Current.SampleIndex + 1];
            for (int32 NeighborOffset = Begin; NeighborOffset < End; ++NeighborOffset)
            {
                const int32 NeighborIndex = State.AdjacencyNeighbors[NeighborOffset];
                if (!State.Samples.IsValidIndex(NeighborIndex))
                {
                    continue;
                }

                if (State.Samples[NeighborIndex].PlateIndex != PlateIndex)
                {
                    continue;
                }

                const float EdgeDistanceKm = GreatCircleDistanceKm(State.Samples[Current.SampleIndex].Position, State.Samples[NeighborIndex].Position);
                const float NewDistance = Current.Distance + EdgeDistanceKm;
                if (NewDistance + KINDA_SMALL_NUMBER < State.Samples[NeighborIndex].DistToFront)
                {
                    State.Samples[NeighborIndex].DistToFront = NewDistance;
                    Queue.push(FQueueNode{NewDistance, NeighborIndex});
                }
            }
        }
    }, EParallelForFlags::Unbalanced);
}
}

void DetectAndClassifyBoundaries(FPlanetState& State)
{
    if (State.Samples.IsEmpty() || State.Plates.IsEmpty() || State.AdjacencyOffsets.Num() != State.Samples.Num() + 1)
    {
        return;
    }

    const double BoundaryStartTime = FPlatformTime::Seconds();

    for (FPlate& Plate : State.Plates)
    {
        Plate.BoundarySamples.Reset();
    }
    State.BoundaryRegistry.Reset();
    State.SampleBoundaryInfo.Init(FSampleBoundaryInfo(), State.Samples.Num());

    TArray<uint8> BoundaryFlags;
    BoundaryFlags.Init(0, State.Samples.Num());

    ParallelFor(State.Samples.Num(), [&State, &BoundaryFlags](const int32 SampleIndex)
    {
        const int32 PlateIndex = State.Samples[SampleIndex].PlateIndex;
        const int32 Begin = State.AdjacencyOffsets[SampleIndex];
        const int32 End = State.AdjacencyOffsets[SampleIndex + 1];
        for (int32 NeighborOffset = Begin; NeighborOffset < End; ++NeighborOffset)
        {
            const int32 NeighborIndex = State.AdjacencyNeighbors[NeighborOffset];
            if (State.Samples.IsValidIndex(NeighborIndex) && State.Samples[NeighborIndex].PlateIndex != PlateIndex)
            {
                BoundaryFlags[SampleIndex] = 1;
                return;
            }
        }
    });

    for (int32 SampleIndex = 0; SampleIndex < State.Samples.Num(); ++SampleIndex)
    {
        if (BoundaryFlags[SampleIndex] == 0)
        {
            continue;
        }

        const int32 PlateIndex = State.Samples[SampleIndex].PlateIndex;
        if (!State.Plates.IsValidIndex(PlateIndex))
        {
            continue;
        }

        State.Plates[PlateIndex].BoundarySamples.Add(SampleIndex);
        State.SampleBoundaryInfo[SampleIndex].bIsBoundary = true;

        int32 UniqueNeighborPlates[8];
        int32 UniqueNeighborCount = 0;

        const int32 Begin = State.AdjacencyOffsets[SampleIndex];
        const int32 End = State.AdjacencyOffsets[SampleIndex + 1];
        for (int32 NeighborOffset = Begin; NeighborOffset < End; ++NeighborOffset)
        {
            const int32 NeighborIndex = State.AdjacencyNeighbors[NeighborOffset];
            if (!State.Samples.IsValidIndex(NeighborIndex))
            {
                continue;
            }

            const int32 NeighborPlate = State.Samples[NeighborIndex].PlateIndex;
            if (NeighborPlate == PlateIndex || NeighborPlate == INDEX_NONE)
            {
                continue;
            }

            bool bSeen = false;
            for (int32 Entry = 0; Entry < UniqueNeighborCount; ++Entry)
            {
                if (UniqueNeighborPlates[Entry] == NeighborPlate)
                {
                    bSeen = true;
                    break;
                }
            }

            if (!bSeen && UniqueNeighborCount < UE_ARRAY_COUNT(UniqueNeighborPlates))
            {
                UniqueNeighborPlates[UniqueNeighborCount++] = NeighborPlate;
            }
        }

        for (int32 Entry = 0; Entry < UniqueNeighborCount; ++Entry)
        {
            const int32 NeighborPlate = UniqueNeighborPlates[Entry];
            const int64 PairKey = MakePlatePairKey(PlateIndex, NeighborPlate);

            int32 SegmentIndex = INDEX_NONE;
            if (const int32* ExistingIndex = State.BoundaryRegistry.PlatePairToSegmentIndex.Find(PairKey))
            {
                SegmentIndex = *ExistingIndex;
            }
            else
            {
                FBoundarySegment Segment;
                Segment.PlateIndexA = FMath::Min(PlateIndex, NeighborPlate);
                Segment.PlateIndexB = FMath::Max(PlateIndex, NeighborPlate);
                SegmentIndex = State.BoundaryRegistry.Segments.Add(MoveTemp(Segment));
                State.BoundaryRegistry.PlatePairToSegmentIndex.Add(PairKey, SegmentIndex);
            }

            FBoundarySegment& Segment = State.BoundaryRegistry.Segments[SegmentIndex];
            if (PlateIndex == Segment.PlateIndexA)
            {
                Segment.SamplesA.Add(SampleIndex);
            }
            else
            {
                Segment.SamplesB.Add(SampleIndex);
            }
        }
    }

    TArray<FVector> PlateCentroids;
    TArray<int32> PlateSampleCounts;
    PlateCentroids.Init(FVector::ZeroVector, State.Plates.Num());
    PlateSampleCounts.Init(0, State.Plates.Num());
    for (const FCrustSample& Sample : State.Samples)
    {
        if (State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            PlateCentroids[Sample.PlateIndex] += Sample.Position;
            ++PlateSampleCounts[Sample.PlateIndex];
        }
    }
    for (int32 PlateIndex = 0; PlateIndex < State.Plates.Num(); ++PlateIndex)
    {
        if (PlateSampleCounts[PlateIndex] > 0)
        {
            PlateCentroids[PlateIndex].Normalize();
        }
        else
        {
            PlateCentroids[PlateIndex] = FVector::UpVector;
        }
    }

    int32 ConvergentCount = 0;
    int32 DivergentCount = 0;
    int32 TransformCount = 0;
    int32 OceanicSubductionCount = 0;
    int32 OceanOceanSubductionCount = 0;
    int32 CollisionCount = 0;

    TArray<float> BestStress;
    BestStress.Init(-1.0f, State.Samples.Num());

    for (FBoundarySegment& Segment : State.BoundaryRegistry.Segments)
    {
        const FVector MidA = ComputeMeanDirection(Segment.SamplesA, State.Samples);
        const FVector MidB = ComputeMeanDirection(Segment.SamplesB, State.Samples);
        FVector Midpoint = (MidA + MidB).GetSafeNormal();
        if (Midpoint.IsNearlyZero())
        {
            Midpoint = (PlateCentroids[Segment.PlateIndexA] + PlateCentroids[Segment.PlateIndexB]).GetSafeNormal();
        }

        const FVector VelocityA = ComputeSurfaceVelocity(State, Segment.PlateIndexA, Midpoint);
        const FVector VelocityB = ComputeSurfaceVelocity(State, Segment.PlateIndexB, Midpoint);
        const FVector RelativeVelocity = VelocityA - VelocityB;

        FVector BoundaryNormal = PlateCentroids[Segment.PlateIndexB] - PlateCentroids[Segment.PlateIndexA];
        BoundaryNormal -= Midpoint * FVector::DotProduct(BoundaryNormal, Midpoint);
        BoundaryNormal.Normalize();
        if (BoundaryNormal.IsNearlyZero())
        {
            BoundaryNormal = FVector::CrossProduct(Midpoint, FVector::UpVector).GetSafeNormal();
        }

        const float ApproachSpeedRadPerMy = FVector::DotProduct(RelativeVelocity, BoundaryNormal);
        const float ApproachSpeedMmPerYear = ApproachSpeedRadPerMy * PTP::Radius;

        Segment.RelativeSpeed = FMath::Abs(ApproachSpeedMmPerYear);
        Segment.ConvergenceType = EPTPConvergenceType::None;
        Segment.SubductingPlateIndex = INDEX_NONE;
        Segment.OverridingPlateIndex = INDEX_NONE;

        if (ApproachSpeedMmPerYear > ConvergeThresholdMmPerYear)
        {
            Segment.Type = EPTPBoundaryType::Convergent;
            ClassifyConvergence(Segment, State);
            ++ConvergentCount;
            if (Segment.ConvergenceType == EPTPConvergenceType::OceanicSubduction)
            {
                ++OceanicSubductionCount;
            }
            else if (Segment.ConvergenceType == EPTPConvergenceType::OceanOceanSubduction)
            {
                ++OceanOceanSubductionCount;
            }
            else if (Segment.ConvergenceType == EPTPConvergenceType::ContinentalCollision)
            {
                ++CollisionCount;
            }
        }
        else if (ApproachSpeedMmPerYear < -ConvergeThresholdMmPerYear)
        {
            Segment.Type = EPTPBoundaryType::Divergent;
            ++DivergentCount;
        }
        else
        {
            Segment.Type = EPTPBoundaryType::Transform;
            ++TransformCount;
        }

        const auto ApplySampleClassification = [&State, &Segment, &BestStress](const TArray<int32>& SideSamples, const int32 AdjacentPlateIndex)
        {
            for (const int32 SampleIndex : SideSamples)
            {
                if (!State.SampleBoundaryInfo.IsValidIndex(SampleIndex))
                {
                    continue;
                }

                if (Segment.RelativeSpeed < BestStress[SampleIndex])
                {
                    continue;
                }

                BestStress[SampleIndex] = Segment.RelativeSpeed;
                FSampleBoundaryInfo& Info = State.SampleBoundaryInfo[SampleIndex];
                Info.bIsBoundary = true;
                Info.BoundaryType = Segment.Type;
                Info.ConvergenceType = Segment.ConvergenceType;
                Info.AdjacentPlateIndex = AdjacentPlateIndex;
                Info.BoundaryStress = Segment.RelativeSpeed;
            }
        };

        ApplySampleClassification(Segment.SamplesA, Segment.PlateIndexB);
        ApplySampleClassification(Segment.SamplesB, Segment.PlateIndexA);
    }

    const double BoundaryElapsedMs = (FPlatformTime::Seconds() - BoundaryStartTime) * 1000.0;
    const double DistanceStartTime = FPlatformTime::Seconds();
    ComputeDistanceToFront(State);
    const double DistanceElapsedMs = (FPlatformTime::Seconds() - DistanceStartTime) * 1000.0;

    int32 BoundarySampleCount = 0;
    double DistSum = 0.0;
    float DistMin = FLT_MAX;
    float DistMax = 0.0f;
    int32 DistCount = 0;

    for (const FSampleBoundaryInfo& Info : State.SampleBoundaryInfo)
    {
        if (Info.bIsBoundary)
        {
            ++BoundarySampleCount;
        }
    }

    for (const FCrustSample& Sample : State.Samples)
    {
        if (!FMath::IsFinite(Sample.DistToFront) || Sample.DistToFront >= TNumericLimits<float>::Max() * 0.5f)
        {
            continue;
        }

        DistMin = FMath::Min(DistMin, Sample.DistToFront);
        DistMax = FMath::Max(DistMax, Sample.DistToFront);
        DistSum += Sample.DistToFront;
        ++DistCount;
    }

    const double DistMean = DistCount > 0 ? DistSum / static_cast<double>(DistCount) : 0.0;

    UE_LOG(LogTemp, Log, TEXT("[PTP] Boundaries: %d segments (%d convergent, %d divergent, %d transform)"),
        State.BoundaryRegistry.Segments.Num(),
        ConvergentCount,
        DivergentCount,
        TransformCount);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Convergent: %d oceanic-subduction, %d ocean-ocean, %d collision"),
        OceanicSubductionCount,
        OceanOceanSubductionCount,
        CollisionCount);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Boundary samples: %d / %d (%.2f%%)"),
        BoundarySampleCount,
        State.Samples.Num(),
        State.Samples.Num() > 0 ? (100.0 * static_cast<double>(BoundarySampleCount) / static_cast<double>(State.Samples.Num())) : 0.0);
    UE_LOG(LogTemp, Log, TEXT("[PTP] DistToFront: min=%.1f km, max=%.1f km, mean=%.1f km"),
        DistCount > 0 ? DistMin : 0.0f,
        DistMax,
        DistMean);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Boundary detection: %.1f ms, Distance BFS: %.1f ms"), BoundaryElapsedMs, DistanceElapsedMs);
}
