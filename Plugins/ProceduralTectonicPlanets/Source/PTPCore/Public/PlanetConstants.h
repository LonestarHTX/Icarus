#pragma once

#include "CoreTypes.h"

namespace PTP
{
    constexpr int32 DefaultSampleCount = 500000;
    constexpr float DeltaT = 2.0f;
    constexpr float Radius = 6370.0f;
    constexpr float RidgeElevation = -1.0f;
    constexpr float AbyssalElevation = -6.0f;
    constexpr float TrenchElevation = -10.0f;
    constexpr float MaxContElevation = 10.0f;
    constexpr float SubductionDist = 1800.0f;
    constexpr float CollisionDist = 4200.0f;
    constexpr float CollisionCoeff = 1.3e-5f;
    constexpr float MaxPlateSpeed = 100.0f;
    constexpr float OceanDamping = 4e-2f;
    constexpr float ContErosion = 3e-2f;
    constexpr float SedimentAccretion = 3e-1f;
    constexpr float SubductionUplift = 6e-1f;
}
