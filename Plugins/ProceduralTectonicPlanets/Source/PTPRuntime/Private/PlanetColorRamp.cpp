#include "PlanetColorRamp.h"

namespace
{
FLinearColor LerpByRange(float Value, float MinValue, float MaxValue, const FLinearColor& A, const FLinearColor& B)
{
    if (FMath::IsNearlyEqual(MinValue, MaxValue))
    {
        return B;
    }

    const float T = FMath::GetMappedRangeValueClamped(FVector2f(MinValue, MaxValue), FVector2f(0.0f, 1.0f), Value);
    return FMath::Lerp(A, B, T);
}
}

namespace PTP
{
FLinearColor ElevationToColor(float ElevationKm)
{
    const FLinearColor Trench(0.01f, 0.04f, 0.15f, 1.0f);      // -10
    const FLinearColor Abyssal(0.02f, 0.12f, 0.32f, 1.0f);     // -6
    const FLinearColor Ridge(0.08f, 0.28f, 0.65f, 1.0f);       // -1
    const FLinearColor SeaLevel(0.0f, 0.78f, 0.74f, 1.0f);     // 0
    const FLinearColor Lowland(0.15f, 0.58f, 0.20f, 1.0f);     // 2
    const FLinearColor Highland(0.58f, 0.42f, 0.24f, 1.0f);    // 5
    const FLinearColor Peak(0.96f, 0.96f, 0.96f, 1.0f);        // 10

    if (ElevationKm <= -6.0f)
    {
        return LerpByRange(ElevationKm, -10.0f, -6.0f, Trench, Abyssal);
    }

    if (ElevationKm <= -1.0f)
    {
        return LerpByRange(ElevationKm, -6.0f, -1.0f, Abyssal, Ridge);
    }

    if (ElevationKm <= 0.0f)
    {
        return LerpByRange(ElevationKm, -1.0f, 0.0f, Ridge, SeaLevel);
    }

    if (ElevationKm <= 2.0f)
    {
        return LerpByRange(ElevationKm, 0.0f, 2.0f, SeaLevel, Lowland);
    }

    if (ElevationKm <= 5.0f)
    {
        return LerpByRange(ElevationKm, 2.0f, 5.0f, Lowland, Highland);
    }

    return LerpByRange(ElevationKm, 5.0f, 10.0f, Highland, Peak);
}

FLinearColor PlateIndexToColor(int32 PlateIndex)
{
    if (PlateIndex < 0)
    {
        return FLinearColor::Black;
    }

    const float Hue = FMath::Fmod(static_cast<float>(PlateIndex) * 137.508f, 360.0f);
    return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue / 360.0f * 255.0f), 200, 220);
}

FLinearColor CrustTypeToColor(ECrustType CrustType)
{
    return (CrustType == ECrustType::Continental)
        ? FLinearColor(0.10f, 0.65f, 0.18f, 1.0f)
        : FLinearColor(0.05f, 0.22f, 0.72f, 1.0f);
}
}
