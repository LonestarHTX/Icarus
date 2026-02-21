#include "TectonicViewportClient.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "PlanetActor.h"
#include "UnrealEdGlobals.h"

FTectonicViewportClient::FTectonicViewportClient()
{
}

void FTectonicViewportClient::Tick(const float DeltaSeconds)
{
    FLevelEditorViewportClient* ViewportClient = ResolveLevelViewportClient();
    if (!ViewportClient)
    {
        return;
    }

    if (!bInitialized)
    {
        ConfigureCameraForPlanet(ViewportClient);
        CameraState.Reset();
        bInitialized = true;
    }

    if (bOrbitLeftHeld != bOrbitRightHeld)
    {
        CameraState.TargetAzimuthVelocity = bOrbitLeftHeld ? CameraState.OrbitSpeed : -CameraState.OrbitSpeed;
    }
    else
    {
        CameraState.TargetAzimuthVelocity = 0.0;
    }

    if (bOrbitUpHeld != bOrbitDownHeld)
    {
        CameraState.TargetElevationVelocity = bOrbitUpHeld ? CameraState.OrbitSpeed : -CameraState.OrbitSpeed;
    }
    else
    {
        CameraState.TargetElevationVelocity = 0.0;
    }

    if (bZoomInHeld != bZoomOutHeld)
    {
        CameraState.TargetZoomVelocity = bZoomInHeld ? -CameraState.ZoomSpeed : CameraState.ZoomSpeed;
    }
    else
    {
        CameraState.TargetZoomVelocity = 0.0;
    }

    const float StableDelta = FMath::Clamp(DeltaSeconds, 1.0f / 240.0f, 1.0f / 20.0f);
    CameraState.Tick(StableDelta);

    const FTransform CameraTransform = CameraState.GetCameraTransform();
    ViewportClient->SetViewLocation(CameraTransform.GetLocation());
    ViewportClient->SetViewRotation(CameraTransform.GetRotation().Rotator());
    ViewportClient->SetRealtime(true);
    ViewportClient->Invalidate();
}

void FTectonicViewportClient::StartOrbitLeft()
{
    bOrbitLeftHeld = true;
}

void FTectonicViewportClient::StopOrbitLeft()
{
    bOrbitLeftHeld = false;
}

void FTectonicViewportClient::StartOrbitRight()
{
    bOrbitRightHeld = true;
}

void FTectonicViewportClient::StopOrbitRight()
{
    bOrbitRightHeld = false;
}

void FTectonicViewportClient::StartOrbitUp()
{
    bOrbitUpHeld = true;
}

void FTectonicViewportClient::StopOrbitUp()
{
    bOrbitUpHeld = false;
}

void FTectonicViewportClient::StartOrbitDown()
{
    bOrbitDownHeld = true;
}

void FTectonicViewportClient::StopOrbitDown()
{
    bOrbitDownHeld = false;
}

void FTectonicViewportClient::StartZoomIn()
{
    bZoomInHeld = true;
}

void FTectonicViewportClient::StopZoomIn()
{
    bZoomInHeld = false;
}

void FTectonicViewportClient::StartZoomOut()
{
    bZoomOutHeld = true;
}

void FTectonicViewportClient::StopZoomOut()
{
    bZoomOutHeld = false;
}

void FTectonicViewportClient::ResetCamera()
{
    bOrbitLeftHeld = false;
    bOrbitRightHeld = false;
    bOrbitUpHeld = false;
    bOrbitDownHeld = false;
    bZoomInHeld = false;
    bZoomOutHeld = false;

    if (FLevelEditorViewportClient* ViewportClient = ResolveLevelViewportClient())
    {
        ConfigureCameraForPlanet(ViewportClient);
    }

    CameraState.Reset();
}

void FTectonicViewportClient::SetViewportViewMode(const EViewModeIndex InViewMode)
{
    if (FLevelEditorViewportClient* ViewportClient = ResolveLevelViewportClient())
    {
        ViewportClient->SetViewMode(InViewMode);
        ViewportClient->Invalidate();
    }
}

EViewModeIndex FTectonicViewportClient::GetViewportViewMode() const
{
    if (const FLevelEditorViewportClient* ViewportClient = ResolveLevelViewportClient())
    {
        return ViewportClient->GetViewMode();
    }

    return VMI_Lit;
}

FLevelEditorViewportClient* FTectonicViewportClient::ResolveLevelViewportClient() const
{
    if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsPerspective())
    {
        return GCurrentLevelEditingViewportClient;
    }

    if (GEditor)
    {
        for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
        {
            if (Candidate && Candidate->IsPerspective())
            {
                return Candidate;
            }
        }
    }

    return nullptr;
}

void FTectonicViewportClient::ConfigureCameraForPlanet(FLevelEditorViewportClient* InViewportClient)
{
    if (!InViewportClient)
    {
        return;
    }

    double PlanetRadius = 637000.0;
    UWorld* World = InViewportClient->GetWorld();
    if (!World && GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }

    if (World)
    {
        for (TActorIterator<APlanetActor> It(World); It; ++It)
        {
            const APlanetActor* PlanetActor = *It;
            if (PlanetActor && PlanetActor->GetRootComponent())
            {
                const float CandidateRadius = PlanetActor->GetRootComponent()->Bounds.SphereRadius;
                if (CandidateRadius > 10.0f)
                {
                    PlanetRadius = static_cast<double>(CandidateRadius);
                    break;
                }
            }
        }
    }

    CameraState.MinDistance = PlanetRadius * 1.05;
    CameraState.MaxDistance = PlanetRadius * 12.0;
    CameraState.DefaultDistance = PlanetRadius * 2.5;
    CameraState.ZoomSpeed = PlanetRadius * 1.2;
}
