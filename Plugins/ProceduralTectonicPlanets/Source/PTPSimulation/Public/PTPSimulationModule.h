#pragma once

#include "Modules/ModuleManager.h"

class FPTPSimulationModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
