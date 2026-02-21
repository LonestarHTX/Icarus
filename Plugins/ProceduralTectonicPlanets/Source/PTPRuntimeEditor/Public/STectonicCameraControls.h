#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSpinBox.h"

class APlanetActor;
class FTectonicViewportClient;
enum class EPlanetVisualizationMode : uint8;

class STectonicCameraControls : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(STectonicCameraControls) {}
        SLATE_ARGUMENT(TWeakPtr<FTectonicViewportClient>, ViewportClient)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    EActiveTimerReturnType HandleActiveTick(double InCurrentTime, float InDeltaTime);

    // View mode options
    struct FViewModeOption
    {
        FText Label;
        EViewModeIndex ViewMode = VMI_Lit;
    };

    // Visualization mode options
    struct FVisualizationOption
    {
        FText Label;
        EPlanetVisualizationMode Mode;
    };

    TWeakPtr<FTectonicViewportClient> ViewportClient;
    TWeakObjectPtr<APlanetActor> CachedPlanetActor;

    // View mode
    TArray<TSharedPtr<FViewModeOption>> ViewModeOptions;
    TSharedPtr<FViewModeOption> SelectedViewMode;

    // Visualization mode
    TArray<TSharedPtr<FVisualizationOption>> VisualizationOptions;
    TSharedPtr<FVisualizationOption> SelectedVisualization;

    // Planet parameter widgets (for dynamic updates)
    TSharedPtr<SSpinBox<int32>> SampleCountSpinBox;
    TSharedPtr<SSpinBox<int32>> NumPlatesSpinBox;
    TSharedPtr<SSpinBox<float>> ContinentalRatioSpinBox;
    TSharedPtr<SSpinBox<int32>> RandomSeedSpinBox;
    TSharedPtr<SSpinBox<float>> RenderScaleSpinBox;
    TSharedPtr<SSpinBox<int32>> StepCountSpinBox;
    int32 StepCount = 1;

    // UI Builder helpers
    TSharedRef<SWidget> BuildCameraSection();
    TSharedRef<SWidget> BuildPlanetSection();
    TSharedRef<SWidget> MakeOrbitControls();
    TSharedRef<SWidget> MakeZoomControls();

    TSharedRef<SWidget> MakeHoldButton(
        const FString& Label,
        const FString& Tooltip,
        const FSimpleDelegate& OnPressed,
        const FSimpleDelegate& OnReleased,
        float Width = 40.0f,
        float Height = 32.0f,
        bool bAccent = false) const;

    TSharedRef<SWidget> MakeSectionHeader(const FString& Label) const;
    TSharedRef<SWidget> MakeSubsectionLabel(const FString& Label) const;
    TSharedRef<SWidget> MakeParameterRow(const FString& Label, TSharedRef<SWidget> Widget) const;

    // View mode
    TSharedRef<SWidget> OnGenerateViewModeWidget(TSharedPtr<FViewModeOption> Option) const;
    void OnViewModeSelected(TSharedPtr<FViewModeOption> Option, ESelectInfo::Type SelectInfo);
    FText GetSelectedViewModeText() const;

    // Visualization mode
    TSharedRef<SWidget> OnGenerateVisualizationWidget(TSharedPtr<FVisualizationOption> Option) const;
    void OnVisualizationSelected(TSharedPtr<FVisualizationOption> Option, ESelectInfo::Type SelectInfo);
    FText GetSelectedVisualizationText() const;

    // Planet actor helpers
    APlanetActor* FindPlanetActor();
    void RefreshFromPlanetActor();

    // Camera controls
    FReply OnResetClicked();
    void OnOrbitLeftPressed();
    void OnOrbitLeftReleased();
    void OnOrbitRightPressed();
    void OnOrbitRightReleased();
    void OnOrbitUpPressed();
    void OnOrbitUpReleased();
    void OnOrbitDownPressed();
    void OnOrbitDownReleased();
    void OnZoomInPressed();
    void OnZoomInReleased();
    void OnZoomOutPressed();
    void OnZoomOutReleased();

    // Planet parameter callbacks
    void OnSampleCountChanged(int32 NewValue);
    void OnSampleCountCommitted(int32 NewValue, ETextCommit::Type CommitType);
    void OnNumPlatesChanged(int32 NewValue);
    void OnNumPlatesCommitted(int32 NewValue, ETextCommit::Type CommitType);
    void OnContinentalRatioChanged(float NewValue);
    void OnContinentalRatioCommitted(float NewValue, ETextCommit::Type CommitType);
    void OnRandomSeedChanged(int32 NewValue);
    void OnRandomSeedCommitted(int32 NewValue, ETextCommit::Type CommitType);
    void OnRenderScaleChanged(float NewValue);
    void OnRenderScaleCommitted(float NewValue, ETextCommit::Type CommitType);

    // Actions
    FReply OnGeneratePlanetClicked();
    FReply OnRandomizeSeedClicked();
    FReply OnExportMapClicked();
    FReply OnStepClicked();
    FReply OnPlayClicked();
    FReply OnStopClicked();
    FReply OnResetSimulationClicked();
    void OnStepCountChanged(int32 NewValue);
    void OnStepCountCommitted(int32 NewValue, ETextCommit::Type CommitType);
};
