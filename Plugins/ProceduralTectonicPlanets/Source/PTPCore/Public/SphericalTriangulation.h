#pragma once

#include "CoreMinimal.h"

PTPCORE_API bool ComputeSphericalDelaunay(const TArray<FVector>& Positions, TArray<int32>& OutTriangleIndices);
