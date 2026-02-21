#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "TectonicData.h"

#include "PlanetMapExporter.generated.h"

UENUM(BlueprintType)
enum class EMapLayer : uint8
{
    PlateID,
    Elevation,
    ContinentalMask,
    BoundaryType,
    Velocity,
    Composite
};

UCLASS()
class PTPRUNTIME_API UPlanetMapExporter : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Tectonic|Export")
    static bool ExportLayerToPNG(
        const FTectonicData& Data,
        EMapLayer Layer,
        const FString& FilePath,
        int32 Width = 2048,
        int32 Height = 1024);

    UFUNCTION(BlueprintCallable, Category = "Tectonic|Export")
    static void ExportAllLayers(
        const FTectonicData& Data,
        const FString& OutputDirectory,
        int32 Width = 2048,
        int32 Height = 1024);

    UFUNCTION(BlueprintCallable, Category = "Tectonic|Export")
    static TArray<FColor> RenderLayer(
        const FTectonicData& Data,
        EMapLayer Layer,
        int32 Width,
        int32 Height);
};
