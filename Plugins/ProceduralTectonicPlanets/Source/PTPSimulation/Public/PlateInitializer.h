#pragma once

#include "PlanetGenerationParams.h"
#include "PlanetState.h"

PTPSIMULATION_API void InitializePlates(FPlanetState& State, const FPlanetGenerationParams& Params);
PTPSIMULATION_API bool ValidatePlateInitialization(const FPlanetState& State);
