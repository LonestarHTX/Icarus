#include "STectonicCameraControls.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "PlanetActor.h"
#include "PlanetMapExporter.h"
#include "TectonicData.h"
#include "Styling/AppStyle.h"
#include "TectonicViewportClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace TectonicUI
{
    // Color palette
    const FLinearColor BackgroundDark(0.014f, 0.016f, 0.022f, 0.96f);
    const FLinearColor SectionBackground(0.025f, 0.028f, 0.035f, 0.90f);
    const FLinearColor HeaderText(0.92f, 0.94f, 0.98f, 1.0f);
    const FLinearColor SubheaderText(0.55f, 0.62f, 0.75f, 1.0f);
    const FLinearColor LabelText(0.65f, 0.70f, 0.78f, 1.0f);
    const FLinearColor AccentBlue(0.20f, 0.45f, 0.72f, 1.0f);
    const FLinearColor AccentGreen(0.22f, 0.65f, 0.38f, 1.0f);
    const FLinearColor ButtonNormal(0.10f, 0.11f, 0.14f, 0.95f);
    const FLinearColor ButtonHover(0.14f, 0.16f, 0.20f, 0.95f);
    const FLinearColor ButtonAccent(0.16f, 0.32f, 0.52f, 0.95f);
    const FLinearColor Separator(0.18f, 0.20f, 0.25f, 0.6f);
}

void STectonicCameraControls::Construct(const FArguments& InArgs)
{
    ViewportClient = InArgs._ViewportClient;
    RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &STectonicCameraControls::HandleActiveTick));

    // View mode options
    ViewModeOptions = {
        MakeShared<FViewModeOption>(FViewModeOption{FText::FromString(TEXT("Lit")), VMI_Lit}),
        MakeShared<FViewModeOption>(FViewModeOption{FText::FromString(TEXT("Unlit")), VMI_Unlit}),
        MakeShared<FViewModeOption>(FViewModeOption{FText::FromString(TEXT("Wireframe")), VMI_Wireframe}),
        MakeShared<FViewModeOption>(FViewModeOption{FText::FromString(TEXT("Detail Lighting")), VMI_Lit_DetailLighting}),
        MakeShared<FViewModeOption>(FViewModeOption{FText::FromString(TEXT("Lighting Only")), VMI_LightingOnly})
    };

    SelectedViewMode = ViewModeOptions[1]; // Default to Unlit for planet viewing
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        const EViewModeIndex CurrentMode = Pinned->GetViewportViewMode();
        for (const TSharedPtr<FViewModeOption>& Option : ViewModeOptions)
        {
            if (Option->ViewMode == CurrentMode)
            {
                SelectedViewMode = Option;
                break;
            }
        }
    }

    // Visualization mode options
    VisualizationOptions = {
        MakeShared<FVisualizationOption>(FVisualizationOption{FText::FromString(TEXT("Elevation")), EPlanetVisualizationMode::Elevation}),
        MakeShared<FVisualizationOption>(FVisualizationOption{FText::FromString(TEXT("Plate Index")), EPlanetVisualizationMode::PlateIndex}),
        MakeShared<FVisualizationOption>(FVisualizationOption{FText::FromString(TEXT("Crust Type")), EPlanetVisualizationMode::CrustType}),
        MakeShared<FVisualizationOption>(FVisualizationOption{FText::FromString(TEXT("Flat")), EPlanetVisualizationMode::Flat})
    };
    SelectedVisualization = VisualizationOptions[0];

    // Try to sync with existing planet actor
    RefreshFromPlanetActor();

    ChildSlot
    [
        SNew(SBorder)
        .Padding(0.0f)
        .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(TectonicUI::BackgroundDark)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)

                // Header
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 14.0f, 16.0f, 4.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("TECTONIC PLANET")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    .ColorAndOpacity(TectonicUI::HeaderText)
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 0.0f, 16.0f, 12.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Camera & generation controls")))
                    .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                    .ColorAndOpacity(TectonicUI::SubheaderText)
                ]

                // Camera Section
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    BuildCameraSection()
                ]

                // Planet Section
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    BuildPlanetSection()
                ]

                // Bottom padding
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 8.0f)
                [
                    SNullWidget::NullWidget
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> STectonicCameraControls::BuildCameraSection()
{
    return SNew(SBorder)
        .Padding(FMargin(12.0f, 8.0f))
        .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(TectonicUI::SectionBackground)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSectionHeader(TEXT("CAMERA"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 10.0f, 0.0f, 6.0f)
            [
                MakeParameterRow(TEXT("View Mode"),
                    SNew(SBox)
                    .WidthOverride(140.0f)
                    [
                        SNew(SComboBox<TSharedPtr<FViewModeOption>>)
                        .OptionsSource(&ViewModeOptions)
                        .InitiallySelectedItem(SelectedViewMode)
                        .OnGenerateWidget(this, &STectonicCameraControls::OnGenerateViewModeWidget)
                        .OnSelectionChanged(this, &STectonicCameraControls::OnViewModeSelected)
                        [
                            SNew(STextBlock)
                            .Text(this, &STectonicCameraControls::GetSelectedViewModeText)
                            .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                        ]
                    ]
                )
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(SHorizontalBox)

                // Orbit controls
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                    [
                        MakeSubsectionLabel(TEXT("ORBIT"))
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        MakeOrbitControls()
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(20.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                    [
                        MakeSubsectionLabel(TEXT("ZOOM"))
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        MakeZoomControls()
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> STectonicCameraControls::BuildPlanetSection()
{
    return SNew(SBorder)
        .Padding(FMargin(12.0f, 8.0f))
        .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(TectonicUI::SectionBackground)
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                MakeSectionHeader(TEXT("PLANET"))
            ]

            // Visualization Mode
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 10.0f, 0.0f, 4.0f)
            [
                MakeParameterRow(TEXT("Visualization"),
                    SNew(SBox)
                    .WidthOverride(140.0f)
                    [
                        SNew(SComboBox<TSharedPtr<FVisualizationOption>>)
                        .OptionsSource(&VisualizationOptions)
                        .InitiallySelectedItem(SelectedVisualization)
                        .OnGenerateWidget(this, &STectonicCameraControls::OnGenerateVisualizationWidget)
                        .OnSelectionChanged(this, &STectonicCameraControls::OnVisualizationSelected)
                        [
                            SNew(STextBlock)
                            .Text(this, &STectonicCameraControls::GetSelectedVisualizationText)
                            .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                        ]
                    ]
                )
            ]

            // Sample Count
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            [
                MakeParameterRow(TEXT("Samples"),
                    SAssignNew(SampleCountSpinBox, SSpinBox<int32>)
                    .MinValue(100)
                    .MaxValue(1000000)
                    .MinSliderValue(1000)
                    .MaxSliderValue(500000)
                    .Value(500000)
                    .Delta(1000)
                    .OnValueChanged(this, &STectonicCameraControls::OnSampleCountChanged)
                    .OnValueCommitted(this, &STectonicCameraControls::OnSampleCountCommitted)
                )
            ]

            // Num Plates
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            [
                MakeParameterRow(TEXT("Plates"),
                    SAssignNew(NumPlatesSpinBox, SSpinBox<int32>)
                    .MinValue(1)
                    .MaxValue(200)
                    .Value(40)
                    .Delta(1)
                    .OnValueChanged(this, &STectonicCameraControls::OnNumPlatesChanged)
                    .OnValueCommitted(this, &STectonicCameraControls::OnNumPlatesCommitted)
                )
            ]

            // Continental Ratio
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            [
                MakeParameterRow(TEXT("Continental %"),
                    SAssignNew(ContinentalRatioSpinBox, SSpinBox<float>)
                    .MinValue(0.0f)
                    .MaxValue(1.0f)
                    .Value(0.30f)
                    .Delta(0.05f)
                    .OnValueChanged(this, &STectonicCameraControls::OnContinentalRatioChanged)
                    .OnValueCommitted(this, &STectonicCameraControls::OnContinentalRatioCommitted)
                )
            ]

            // Render Scale
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            [
                MakeParameterRow(TEXT("Scale (cm)"),
                    SAssignNew(RenderScaleSpinBox, SSpinBox<float>)
                    .MinValue(1000.0f)
                    .MaxValue(10000000.0f)
                    .MinSliderValue(100000.0f)
                    .MaxSliderValue(2000000.0f)
                    .Value(637000.0f)
                    .Delta(10000.0f)
                    .OnValueChanged(this, &STectonicCameraControls::OnRenderScaleChanged)
                    .OnValueCommitted(this, &STectonicCameraControls::OnRenderScaleCommitted)
                )
            ]

            // Random Seed with Randomize button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 8.0f)
            [
                MakeParameterRow(TEXT("Seed"),
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SAssignNew(RandomSeedSpinBox, SSpinBox<int32>)
                        .MinValue(0)
                        .MaxValue(999999)
                        .Value(42)
                        .Delta(1)
                        .OnValueChanged(this, &STectonicCameraControls::OnRandomSeedChanged)
                        .OnValueCommitted(this, &STectonicCameraControls::OnRandomSeedCommitted)
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity(TectonicUI::ButtonNormal)
                        .OnClicked(this, &STectonicCameraControls::OnRandomizeSeedClicked)
                        .ToolTipText(FText::FromString(TEXT("Generate random seed")))
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Rand")))
                            .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                            .ColorAndOpacity(FLinearColor::White)
                        ]
                    ]
                )
            ]

            // Separator
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SSeparator)
                .Orientation(Orient_Horizontal)
                .ColorAndOpacity(TectonicUI::Separator)
            ]

            // Generate Button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 4.0f)
            .HAlign(HAlign_Fill)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .ButtonColorAndOpacity(TectonicUI::AccentGreen)
                .OnClicked(this, &STectonicCameraControls::OnGeneratePlanetClicked)
                [
                    SNew(SBox)
                    .Padding(FMargin(24.0f, 6.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Generate Planet")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                        .ColorAndOpacity(FLinearColor::White)
                        .Justification(ETextJustify::Center)
                    ]
                ]
            ]

            // Export Map Button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 4.0f)
            .HAlign(HAlign_Fill)
            [
                SNew(SButton)
                .HAlign(HAlign_Center)
                .ButtonColorAndOpacity(TectonicUI::AccentBlue)
                .OnClicked(this, &STectonicCameraControls::OnExportMapClicked)
                .ToolTipText(FText::FromString(TEXT("Export equirectangular map images to Saved/TectonicMaps/")))
                [
                    SNew(SBox)
                    .Padding(FMargin(24.0f, 6.0f))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Export Map")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                        .ColorAndOpacity(FLinearColor::White)
                        .Justification(ETextJustify::Center)
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> STectonicCameraControls::MakeOrbitControls()
{
    return SNew(SGridPanel)
        .FillColumn(0, 1.0f)
        .FillColumn(1, 1.0f)
        .FillColumn(2, 1.0f)

        + SGridPanel::Slot(1, 0)
        .HAlign(HAlign_Center)
        [
            MakeHoldButton(TEXT("\u25B2"), TEXT("Orbit Up"),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitUpPressed),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitUpReleased),
                36.0f, 28.0f)
        ]

        + SGridPanel::Slot(0, 1)
        .HAlign(HAlign_Center)
        [
            MakeHoldButton(TEXT("\u25C0"), TEXT("Orbit Left"),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitLeftPressed),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitLeftReleased),
                36.0f, 28.0f)
        ]

        + SGridPanel::Slot(1, 1)
        .HAlign(HAlign_Center)
        [
            SNew(SBox)
            .WidthOverride(44.0f)
            .HeightOverride(28.0f)
            [
                SNew(SButton)
                .OnClicked(this, &STectonicCameraControls::OnResetClicked)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .ButtonColorAndOpacity(TectonicUI::ButtonAccent)
                .ToolTipText(FText::FromString(TEXT("Reset camera")))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u25CE")))
                    .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                    .ColorAndOpacity(FLinearColor::White)
                ]
            ]
        ]

        + SGridPanel::Slot(2, 1)
        .HAlign(HAlign_Center)
        [
            MakeHoldButton(TEXT("\u25B6"), TEXT("Orbit Right"),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitRightPressed),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitRightReleased),
                36.0f, 28.0f)
        ]

        + SGridPanel::Slot(1, 2)
        .HAlign(HAlign_Center)
        [
            MakeHoldButton(TEXT("\u25BC"), TEXT("Orbit Down"),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitDownPressed),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnOrbitDownReleased),
                36.0f, 28.0f)
        ];
}

TSharedRef<SWidget> STectonicCameraControls::MakeZoomControls()
{
    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            MakeHoldButton(TEXT("+"), TEXT("Zoom In"),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnZoomInPressed),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnZoomInReleased),
                52.0f, 32.0f, true)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            MakeHoldButton(TEXT("-"), TEXT("Zoom Out"),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnZoomOutPressed),
                FSimpleDelegate::CreateSP(this, &STectonicCameraControls::OnZoomOutReleased),
                52.0f, 32.0f, true)
        ];
}

EActiveTimerReturnType STectonicCameraControls::HandleActiveTick(double InCurrentTime, float InDeltaTime)
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->Tick(InDeltaTime);
    }

    return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> STectonicCameraControls::MakeHoldButton(
    const FString& Label,
    const FString& Tooltip,
    const FSimpleDelegate& OnPressed,
    const FSimpleDelegate& OnReleased,
    float Width,
    float Height,
    bool bAccent) const
{
    const FLinearColor ButtonTint = bAccent ? TectonicUI::ButtonAccent : TectonicUI::ButtonNormal;

    return SNew(SBox)
        .WidthOverride(Width)
        .HeightOverride(Height)
        [
            SNew(SButton)
            .OnPressed(OnPressed)
            .OnReleased(OnReleased)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .ButtonColorAndOpacity(ButtonTint)
            .ToolTipText(FText::FromString(Tooltip))
            [
                SNew(STextBlock)
                .Text(FText::FromString(Label))
                .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                .ColorAndOpacity(FLinearColor::White)
            ]
        ];
}

TSharedRef<SWidget> STectonicCameraControls::MakeSectionHeader(const FString& Label) const
{
    return SNew(STextBlock)
        .Text(FText::FromString(Label))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        .ColorAndOpacity(TectonicUI::AccentBlue);
}

TSharedRef<SWidget> STectonicCameraControls::MakeSubsectionLabel(const FString& Label) const
{
    return SNew(STextBlock)
        .Text(FText::FromString(Label))
        .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
        .ColorAndOpacity(TectonicUI::SubheaderText);
}

TSharedRef<SWidget> STectonicCameraControls::MakeParameterRow(const FString& Label, TSharedRef<SWidget> Widget) const
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SBox)
            .WidthOverride(90.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Label))
                .Font(FAppStyle::Get().GetFontStyle("SmallFont"))
                .ColorAndOpacity(TectonicUI::LabelText)
            ]
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        [
            Widget
        ];
}

APlanetActor* STectonicCameraControls::FindPlanetActor()
{
    if (CachedPlanetActor.IsValid())
    {
        return CachedPlanetActor.Get();
    }

    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        return nullptr;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    for (TActorIterator<APlanetActor> It(World); It; ++It)
    {
        CachedPlanetActor = *It;
        return *It;
    }

    return nullptr;
}

void STectonicCameraControls::RefreshFromPlanetActor()
{
    APlanetActor* Planet = FindPlanetActor();
    if (!Planet)
    {
        return;
    }

    // Sync visualization mode
    for (const TSharedPtr<FVisualizationOption>& Option : VisualizationOptions)
    {
        if (Option->Mode == Planet->VisualizationMode)
        {
            SelectedVisualization = Option;
            break;
        }
    }

    // Sync parameters (will be set when spinboxes are created)
    if (SampleCountSpinBox.IsValid())
    {
        SampleCountSpinBox->SetValue(Planet->SampleCount);
    }
    if (NumPlatesSpinBox.IsValid())
    {
        NumPlatesSpinBox->SetValue(Planet->GenerationParams.NumPlates);
    }
    if (ContinentalRatioSpinBox.IsValid())
    {
        ContinentalRatioSpinBox->SetValue(Planet->GenerationParams.ContinentalRatio);
    }
    if (RandomSeedSpinBox.IsValid())
    {
        RandomSeedSpinBox->SetValue(Planet->GenerationParams.RandomSeed);
    }
    if (RenderScaleSpinBox.IsValid())
    {
        RenderScaleSpinBox->SetValue(Planet->RenderScale);
    }
}

// View Mode
TSharedRef<SWidget> STectonicCameraControls::OnGenerateViewModeWidget(TSharedPtr<FViewModeOption> Option) const
{
    return SNew(STextBlock)
        .Text(Option.IsValid() ? Option->Label : FText::FromString(TEXT("Unknown")))
        .Font(FAppStyle::Get().GetFontStyle("SmallFont"));
}

void STectonicCameraControls::OnViewModeSelected(TSharedPtr<FViewModeOption> Option, ESelectInfo::Type SelectInfo)
{
    if (!Option.IsValid())
    {
        return;
    }

    SelectedViewMode = Option;
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->SetViewportViewMode(Option->ViewMode);
    }
}

FText STectonicCameraControls::GetSelectedViewModeText() const
{
    return SelectedViewMode.IsValid() ? SelectedViewMode->Label : FText::FromString(TEXT("View Mode"));
}

// Visualization Mode
TSharedRef<SWidget> STectonicCameraControls::OnGenerateVisualizationWidget(TSharedPtr<FVisualizationOption> Option) const
{
    return SNew(STextBlock)
        .Text(Option.IsValid() ? Option->Label : FText::FromString(TEXT("Unknown")))
        .Font(FAppStyle::Get().GetFontStyle("SmallFont"));
}

void STectonicCameraControls::OnVisualizationSelected(TSharedPtr<FVisualizationOption> Option, ESelectInfo::Type SelectInfo)
{
    if (!Option.IsValid())
    {
        return;
    }

    SelectedVisualization = Option;

    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->VisualizationMode = Option->Mode;
        Planet->UpdateMesh();
    }
}

FText STectonicCameraControls::GetSelectedVisualizationText() const
{
    return SelectedVisualization.IsValid() ? SelectedVisualization->Label : FText::FromString(TEXT("Visualization"));
}

// Planet Parameters
void STectonicCameraControls::OnSampleCountChanged(int32 NewValue)
{
    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->SampleCount = NewValue;
    }
}

void STectonicCameraControls::OnSampleCountCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    OnSampleCountChanged(NewValue);
}

void STectonicCameraControls::OnNumPlatesChanged(int32 NewValue)
{
    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->GenerationParams.NumPlates = NewValue;
    }
}

void STectonicCameraControls::OnNumPlatesCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    OnNumPlatesChanged(NewValue);
}

void STectonicCameraControls::OnContinentalRatioChanged(float NewValue)
{
    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->GenerationParams.ContinentalRatio = NewValue;
    }
}

void STectonicCameraControls::OnContinentalRatioCommitted(float NewValue, ETextCommit::Type CommitType)
{
    OnContinentalRatioChanged(NewValue);
}

void STectonicCameraControls::OnRandomSeedChanged(int32 NewValue)
{
    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->GenerationParams.RandomSeed = NewValue;
    }
}

void STectonicCameraControls::OnRandomSeedCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
    OnRandomSeedChanged(NewValue);
}

void STectonicCameraControls::OnRenderScaleChanged(float NewValue)
{
    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->RenderScale = NewValue;
        Planet->UpdateMesh();
    }
}

void STectonicCameraControls::OnRenderScaleCommitted(float NewValue, ETextCommit::Type CommitType)
{
    OnRenderScaleChanged(NewValue);
}

// Actions
FReply STectonicCameraControls::OnGeneratePlanetClicked()
{
    if (APlanetActor* Planet = FindPlanetActor())
    {
        Planet->GeneratePlanet();

        // Reconfigure camera for potentially new planet size
        if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
        {
            Pinned->ResetCamera();
        }
    }

    return FReply::Handled();
}

FReply STectonicCameraControls::OnRandomizeSeedClicked()
{
    const int32 NewSeed = FMath::RandRange(0, 999999);
    if (RandomSeedSpinBox.IsValid())
    {
        RandomSeedSpinBox->SetValue(NewSeed);
    }
    OnRandomSeedChanged(NewSeed);

    return FReply::Handled();
}

FReply STectonicCameraControls::OnExportMapClicked()
{
    APlanetActor* Planet = FindPlanetActor();
    if (!Planet)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Export failed: No PlanetActor found in level."));
        return FReply::Handled();
    }

    // Get planet state and convert to TectonicData
    const FPlanetState& PlanetState = Planet->GetPlanetState();
    if (PlanetState.Samples.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Export failed: Planet has no generated data. Generate the planet first."));
        return FReply::Handled();
    }

    const FTectonicData Data = FTectonicData::FromPlanetState(PlanetState);

    // Export to Saved/TectonicMaps/ folder
    const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("TectonicMaps");
    UPlanetMapExporter::ExportAllLayers(Data, OutputDirectory, 2048, 1024);

    UE_LOG(LogTemp, Log, TEXT("[PTP] Exported all map layers to: %s"), *OutputDirectory);

    return FReply::Handled();
}

// Camera Controls
FReply STectonicCameraControls::OnResetClicked()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->ResetCamera();
    }

    return FReply::Handled();
}

void STectonicCameraControls::OnOrbitLeftPressed()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StartOrbitLeft();
    }
}

void STectonicCameraControls::OnOrbitLeftReleased()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StopOrbitLeft();
    }
}

void STectonicCameraControls::OnOrbitRightPressed()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StartOrbitRight();
    }
}

void STectonicCameraControls::OnOrbitRightReleased()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StopOrbitRight();
    }
}

void STectonicCameraControls::OnOrbitUpPressed()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StartOrbitUp();
    }
}

void STectonicCameraControls::OnOrbitUpReleased()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StopOrbitUp();
    }
}

void STectonicCameraControls::OnOrbitDownPressed()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StartOrbitDown();
    }
}

void STectonicCameraControls::OnOrbitDownReleased()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StopOrbitDown();
    }
}

void STectonicCameraControls::OnZoomInPressed()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StartZoomIn();
    }
}

void STectonicCameraControls::OnZoomInReleased()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StopZoomIn();
    }
}

void STectonicCameraControls::OnZoomOutPressed()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StartZoomOut();
    }
}

void STectonicCameraControls::OnZoomOutReleased()
{
    if (const TSharedPtr<FTectonicViewportClient> Pinned = ViewportClient.Pin())
    {
        Pinned->StopZoomOut();
    }
}
