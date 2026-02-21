#pragma once

#include "CoreMinimal.h"

struct FPlanetState;
struct FPlate;

PTPSIMULATION_API void InitializePlateMotions(TArray<FPlate>& Plates, FRandomStream& RandomStream);
PTPSIMULATION_API void MovePlates(FPlanetState& State, float DeltaTime);
