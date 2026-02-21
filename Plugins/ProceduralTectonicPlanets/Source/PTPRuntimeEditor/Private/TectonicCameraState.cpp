#include "TectonicCameraState.h"

FTransform FTectonicCameraState::GetCameraTransform() const
{
    const double CosElevation = FMath::Cos(Elevation);
    const FVector Location(
        Distance * CosElevation * FMath::Cos(Azimuth),
        Distance * CosElevation * FMath::Sin(Azimuth),
        Distance * FMath::Sin(Elevation));

    // Phase 2 hook: blend between center-looking orbit orientation and surface-relative orientation using SurfaceBlendAlpha.
    const FVector Forward = (-Location).GetSafeNormal();
    const FRotator Rotation = Forward.Rotation();
    return FTransform(Rotation, Location);
}

void FTectonicCameraState::Tick(const float DeltaTime)
{
    const auto Approach = [DeltaTime, this](const double Current, const double Target)
    {
        const double Alpha = 1.0 - FMath::Exp(-VelocityResponse * DeltaTime);
        return FMath::Lerp(Current, Target, Alpha);
    };

    AzimuthVelocity = Approach(AzimuthVelocity, TargetAzimuthVelocity);
    ElevationVelocity = Approach(ElevationVelocity, TargetElevationVelocity);
    ZoomVelocity = Approach(ZoomVelocity, TargetZoomVelocity);

    if (FMath::IsNearlyZero(TargetAzimuthVelocity))
    {
        AzimuthVelocity *= FMath::Exp(-Damping * DeltaTime);
    }
    if (FMath::IsNearlyZero(TargetElevationVelocity))
    {
        ElevationVelocity *= FMath::Exp(-Damping * DeltaTime);
    }
    if (FMath::IsNearlyZero(TargetZoomVelocity))
    {
        ZoomVelocity *= FMath::Exp(-Damping * DeltaTime);
    }

    Azimuth += AzimuthVelocity * DeltaTime;
    Elevation += ElevationVelocity * DeltaTime;
    Distance += ZoomVelocity * DeltaTime;

    const double MinElevation = FMath::DegreesToRadians(-85.0);
    const double MaxElevation = FMath::DegreesToRadians(85.0);
    Elevation = FMath::Clamp(Elevation, MinElevation, MaxElevation);
    Distance = FMath::Clamp(Distance, MinDistance, MaxDistance);

    const double Alpha = 1.0 - FMath::Clamp((Distance - MinDistance) / SurfaceBlendTransitionDistance, 0.0, 1.0);
    SurfaceBlendAlpha = static_cast<float>(Alpha);
}

void FTectonicCameraState::Reset()
{
    Azimuth = 0.0;
    Elevation = FMath::DegreesToRadians(30.0);
    Distance = FMath::Clamp(DefaultDistance, MinDistance, MaxDistance);
    AzimuthVelocity = 0.0;
    ElevationVelocity = 0.0;
    ZoomVelocity = 0.0;
    TargetAzimuthVelocity = 0.0;
    TargetElevationVelocity = 0.0;
    TargetZoomVelocity = 0.0;
    SurfaceBlendAlpha = 0.0f;
}
