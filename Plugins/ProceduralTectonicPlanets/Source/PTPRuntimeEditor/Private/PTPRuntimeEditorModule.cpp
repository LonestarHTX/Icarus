#include "PTPRuntimeEditorModule.h"

#include "STectonicViewportPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

namespace
{
const FName TectonicViewerTabId(TEXT("TectonicPlanetViewer"));
}

IMPLEMENT_MODULE(FPTPRuntimeEditorModule, PTPRuntimeEditor)

void FPTPRuntimeEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TectonicViewerTabId,
        FOnSpawnTab::CreateRaw(this, &FPTPRuntimeEditorModule::SpawnViewerTab))
        .SetDisplayName(FText::FromString(TEXT("Tectonic Camera Controls")))
        .SetTooltipText(FText::FromString(TEXT("Open orbital camera controls for the active level viewport.")));

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPTPRuntimeEditorModule::RegisterMenus));
}

void FPTPRuntimeEditorModule::ShutdownModule()
{
    if (UToolMenus::TryGet())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TectonicViewerTabId);
}

void FPTPRuntimeEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    FToolMenuSection& Section = WindowMenu->FindOrAddSection("WindowLayout");
    Section.AddMenuEntry(
        "OpenTectonicPlanetViewer",
        FText::FromString(TEXT("Tectonic Camera Controls")),
        FText::FromString(TEXT("Open orbital camera controls for the active level viewport.")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FPTPRuntimeEditorModule::OpenViewerTab)));
}

void FPTPRuntimeEditorModule::OpenViewerTab()
{
    FGlobalTabmanager::Get()->TryInvokeTab(TectonicViewerTabId);
}

TSharedRef<SDockTab> FPTPRuntimeEditorModule::SpawnViewerTab(const FSpawnTabArgs& SpawnTabArgs)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(STectonicViewportPanel)
        ];
}
