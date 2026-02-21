#pragma once

#include "CoreMinimal.h"

#include "CrustSample.h"

struct FPlanetState
{
    float Time = 0.0f;
    int32 SampleCount = 0;

    TArray<FCrustSample> Samples;
    TArray<int32> TriangleIndices;

    TArray<int32> AdjacencyOffsets;
    TArray<int32> AdjacencyNeighbors;

    friend PTPCORE_API FArchive& operator<<(FArchive& Ar, FPlanetState& State);
};
