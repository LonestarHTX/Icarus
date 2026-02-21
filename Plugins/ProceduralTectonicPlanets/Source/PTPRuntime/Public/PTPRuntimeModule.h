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
    void HandleStep(const TArray<FString>& Args);
    void HandlePlay(const TArray<FString>& Args);
    void HandleStop(const TArray<FString>& Args);
    void HandleReset(const TArray<FString>& Args);
    void HandleExportAll(const TArray<FString>& Args);
    void HandleExportLayer(const TArray<FString>& Args);

    IConsoleObject* StepCommand = nullptr;
    IConsoleObject* PlayCommand = nullptr;
    IConsoleObject* StopCommand = nullptr;
    IConsoleObject* ResetCommand = nullptr;
    IConsoleObject* ExportAllCommand = nullptr;
    IConsoleObject* ExportLayerCommand = nullptr;
};
