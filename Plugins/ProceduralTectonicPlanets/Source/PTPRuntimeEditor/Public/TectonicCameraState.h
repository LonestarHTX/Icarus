#pragma once

#include "CoreMinimal.h"

struct FTectonicCameraState
{
    double Azimuth = 0.0;
    double Elevation = FMath::DegreesToRadians(30.0);
    double Distance = 2000000.0;
    double DefaultDistance = 2000000.0;

    double MinDistance = 1000.0;
    double MaxDistance = 20000000.0;

    double AzimuthVelocity = 0.0;
    double ElevationVelocity = 0.0;
    double ZoomVelocity = 0.0;
    double TargetAzimuthVelocity = 0.0;
    double TargetElevationVelocity = 0.0;
    double TargetZoomVelocity = 0.0;

    double Damping = 4.0;
    double VelocityResponse = 6.0;
    double OrbitSpeed = 1.5;
    double ZoomSpeed = 250000.0;

    float SurfaceBlendAlpha = 0.0f;
    double SurfaceBlendTransitionDistance = 0.5;

    FTransform GetCameraTransform() const;
    void Tick(float DeltaTime);
    void Reset();
};
