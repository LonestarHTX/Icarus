#pragma once

#include "Modules/ModuleManager.h"

class FPTPRuntimeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void OpenViewerTab();
    TSharedRef<class SDockTab> SpawnViewerTab(const class FSpawnTabArgs& SpawnTabArgs);
};
