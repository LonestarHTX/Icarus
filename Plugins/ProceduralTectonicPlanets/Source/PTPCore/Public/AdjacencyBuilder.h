#pragma once

#include "CoreMinimal.h"

#include "PlanetState.h"

PTPCORE_API void BuildAdjacencyCSR(
    int32 NumPoints,
    const TArray<int32>& TriangleIndices,
    TArray<int32>& OutOffsets,
    TArray<int32>& OutNeighbors);

PTPCORE_API bool ValidatePlanetGeometry(const FPlanetState& State);
