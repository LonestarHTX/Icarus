#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#include "TectonicCameraState.h"

class FLevelEditorViewportClient;

class FTectonicViewportClient : public TSharedFromThis<FTectonicViewportClient>
{
public:
    FTectonicViewportClient();

    void Tick(float DeltaSeconds);

    void StartOrbitLeft();
    void StopOrbitLeft();
    void StartOrbitRight();
    void StopOrbitRight();
    void StartOrbitUp();
    void StopOrbitUp();
    void StartOrbitDown();
    void StopOrbitDown();
    void StartZoomIn();
    void StopZoomIn();
    void StartZoomOut();
    void StopZoomOut();
    void ResetCamera();

    void SetViewportViewMode(EViewModeIndex InViewMode);
    EViewModeIndex GetViewportViewMode() const;

private:
    FLevelEditorViewportClient* ResolveLevelViewportClient() const;
    void ConfigureCameraForPlanet(FLevelEditorViewportClient* InViewportClient);

    FTectonicCameraState CameraState;
    bool bInitialized = false;

    bool bOrbitLeftHeld = false;
    bool bOrbitRightHeld = false;
    bool bOrbitUpHeld = false;
    bool bOrbitDownHeld = false;
    bool bZoomInHeld = false;
    bool bZoomOutHeld = false;
};
