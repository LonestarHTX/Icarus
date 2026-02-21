#pragma once

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FTPPRuntimeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterConsoleCommands();
    void UnregisterConsoleCommands();
    void HandleExportAll(const TArray<FString>& Args);
    void HandleExportLayer(const TArray<FString>& Args);

    IConsoleObject* ExportAllCommand = nullptr;
    IConsoleObject* ExportLayerCommand = nullptr;
};
