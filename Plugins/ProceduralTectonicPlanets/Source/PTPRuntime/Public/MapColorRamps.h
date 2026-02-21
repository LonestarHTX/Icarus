#pragma once

#include "CoreMinimal.h"

#include "TectonicData.h"

namespace PTPMapColor
{
inline FLinearColor LerpRange(
    double Value,
    double MinValue,
    double MaxValue,
    const FLinearColor& A,
    const FLinearColor& B)
{
    if (FMath::IsNearlyEqual(MinValue, MaxValue))
    {
        return B;
    }

    const double T = FMath::GetMappedRangeValueClamped(FVector2d(MinValue, MaxValue), FVector2d(0.0, 1.0), Value);
    return FMath::Lerp(A, B, static_cast<float>(T));
}

inline FLinearColor PlateIDToColor(int32 PlateID)
{
    if (PlateID < 0)
    {
        return FLinearColor(0.04f, 0.04f, 0.04f, 1.0f);
    }

    const double Hue01 = FMath::Fmod(static_cast<double>(PlateID) * 0.618033988749895, 1.0);
    return FLinearColor::MakeFromHSV8(
        static_cast<uint8>(Hue01 * 255.0),
        255,
        255);
}

inline FLinearColor ElevationToColor(double Elevation)
{
    const double E = FMath::Clamp(Elevation, -1.0, 1.0);

    const FLinearColor DeepOcean(0.02f, 0.08f, 0.28f, 1.0f);
    const FLinearColor ShallowOcean(0.05f, 0.30f, 0.62f, 1.0f);
    const FLinearColor Coastal(0.10f, 0.72f, 0.80f, 1.0f);
    const FLinearColor Lowland(0.18f, 0.64f, 0.22f, 1.0f);
    const FLinearColor Highland(0.52f, 0.42f, 0.24f, 1.0f);
    const FLinearColor Mountain(0.56f, 0.56f, 0.56f, 1.0f);
    const FLinearColor Snow(0.96f, 0.96f, 0.96f, 1.0f);

    if (E <= -0.2)
    {
        return LerpRange(E, -1.0, -0.2, DeepOcean, ShallowOcean);
    }
    if (E <= 0.0)
    {
        return LerpRange(E, -0.2, 0.0, ShallowOcean, Coastal);
    }
    if (E <= 0.1)
    {
        return LerpRange(E, 0.0, 0.1, Coastal, Lowland);
    }
    if (E <= 0.4)
    {
        return LerpRange(E, 0.1, 0.4, Lowland, Highland);
    }
    if (E <= 0.7)
    {
        return LerpRange(E, 0.4, 0.7, Highland, Mountain);
    }

    return LerpRange(E, 0.7, 1.0, Mountain, Snow);
}

inline FLinearColor ContinentalMaskToColor(bool bIsContinental)
{
    return bIsContinental
        ? FLinearColor(0.76f, 0.70f, 0.50f, 1.0f)
        : FLinearColor(0.15f, 0.25f, 0.55f, 1.0f);
}

inline FLinearColor BoundaryTypeToColor(EBoundaryType BoundaryType, EBoundaryConvergenceType ConvergenceType, double Stress)
{
    const float Brightness = static_cast<float>(FMath::Clamp(0.35 + Stress * 0.65, 0.2, 1.0));
    switch (BoundaryType)
    {
    case EBoundaryType::Convergent:
        if (ConvergenceType == EBoundaryConvergenceType::ContinentalCollision)
        {
            return FLinearColor(0.90f * Brightness, 0.50f * Brightness, 0.10f * Brightness, 1.0f);
        }
        return FLinearColor(0.90f * Brightness, 0.10f * Brightness, 0.10f * Brightness, 1.0f);
    case EBoundaryType::Divergent:
        return FLinearColor(0.10f * Brightness, 0.30f * Brightness, 0.90f * Brightness, 1.0f);
    case EBoundaryType::Transform:
        return FLinearColor(0.10f * Brightness, 0.80f * Brightness, 0.20f * Brightness, 1.0f);
    case EBoundaryType::None:
    default:
        return FLinearColor(0.15f, 0.15f, 0.15f, 1.0f);
    }
}

inline FLinearColor VelocityToColor(const FVector3d& Velocity)
{
    const double Mag = Velocity.Length();
    if (Mag <= KINDA_SMALL_NUMBER)
    {
        return FLinearColor::Black;
    }

    const FVector3d Normalized = Velocity / Mag;
    const double Angle = FMath::Atan2(Normalized.Y, Normalized.X);
    const double Hue01 = (Angle + PI) / (2.0 * PI);
    const uint8 Value = static_cast<uint8>(FMath::Clamp(Mag * 255.0, 0.0, 255.0));
    return FLinearColor::MakeFromHSV8(
        static_cast<uint8>(Hue01 * 255.0),
        230,
        Value);
}

inline FLinearColor AlphaBlend(const FLinearColor& Base, const FLinearColor& Overlay, float Alpha)
{
    return FMath::Lerp(Base, Overlay, FMath::Clamp(Alpha, 0.0f, 1.0f));
}
}
