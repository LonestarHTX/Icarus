#pragma once

#include "Modules/ModuleManager.h"

class FPTPCoreModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
