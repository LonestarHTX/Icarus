#pragma once

#include "CoreMinimal.h"

enum class EPTPBoundaryType : uint8
{
    None,
    Convergent,
    Divergent,
    Transform
};

enum class EPTPConvergenceType : uint8
{
    None,
    OceanicSubduction,
    OceanOceanSubduction,
    ContinentalCollision
};

struct FSampleBoundaryInfo
{
    bool bIsBoundary = false;
    EPTPBoundaryType BoundaryType = EPTPBoundaryType::None;
    EPTPConvergenceType ConvergenceType = EPTPConvergenceType::None;
    int32 AdjacentPlateIndex = INDEX_NONE;
    int32 NearestConvergentSegmentIndex = INDEX_NONE;
    float BoundaryStress = 0.0f;

    friend PTPCORE_API FArchive& operator<<(FArchive& Ar, FSampleBoundaryInfo& Info);
};

struct FBoundarySegment
{
    int32 PlateIndexA = INDEX_NONE;
    int32 PlateIndexB = INDEX_NONE;
    EPTPBoundaryType Type = EPTPBoundaryType::None;
    EPTPConvergenceType ConvergenceType = EPTPConvergenceType::None;
    int32 SubductingPlateIndex = INDEX_NONE;
    int32 OverridingPlateIndex = INDEX_NONE;

    TArray<int32> SamplesA;
    TArray<int32> SamplesB;

    float RelativeSpeed = 0.0f;

    friend PTPCORE_API FArchive& operator<<(FArchive& Ar, FBoundarySegment& Segment);
};

struct FBoundaryRegistry
{
    TArray<FBoundarySegment> Segments;
    TMap<int64, int32> PlatePairToSegmentIndex;

    void Reset()
    {
        Segments.Reset();
        PlatePairToSegmentIndex.Reset();
    }

    friend PTPCORE_API FArchive& operator<<(FArchive& Ar, FBoundaryRegistry& Registry);
};
