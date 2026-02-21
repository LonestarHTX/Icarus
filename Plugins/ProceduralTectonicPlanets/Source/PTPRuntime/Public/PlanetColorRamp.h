#pragma once

#include "CoreMinimal.h"

#include "CrustTypes.h"

namespace PTP
{
    PTPRUNTIME_API FLinearColor ElevationToColor(float ElevationKm);
    PTPRUNTIME_API FLinearColor PlateIndexToColor(int32 PlateIndex);
    PTPRUNTIME_API FLinearColor CrustTypeToColor(ECrustType CrustType);
}
