#pragma once

#include "Modules/ModuleManager.h"

class FTPPRuntimeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
