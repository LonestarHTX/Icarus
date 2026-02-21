#pragma once

#include "CoreTypes.h"

struct FPlanetState;

struct FGlobalResampleStats
{
    int32 NormalCount = 0;
    int32 FallbackCount = 0;
    int32 GapCount = 0;
    int32 OverlapCount = 0;
    double BvhBuildMs = 0.0;
    double QueryMs = 0.0;
    double TransferMs = 0.0;
    double RebuildMs = 0.0;
    double TotalMs = 0.0;
};

PTPSIMULATION_API bool GlobalResample(FPlanetState& State, FGlobalResampleStats* OutStats = nullptr);
