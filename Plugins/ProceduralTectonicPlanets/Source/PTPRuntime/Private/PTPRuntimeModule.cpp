#include "PTPRuntimeModule.h"

#include "PlanetMapExporter.h"
#include "TectonicData.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FTPPRuntimeModule, PTPRuntime)

namespace
{
bool TryParseLayer(const FString& LayerName, EMapLayer& OutLayer)
{
    if (LayerName.Equals(TEXT("PlateID"), ESearchCase::IgnoreCase))
    {
        OutLayer = EMapLayer::PlateID;
        return true;
    }
    if (LayerName.Equals(TEXT("Elevation"), ESearchCase::IgnoreCase))
    {
        OutLayer = EMapLayer::Elevation;
        return true;
    }
    if (LayerName.Equals(TEXT("ContinentalMask"), ESearchCase::IgnoreCase))
    {
        OutLayer = EMapLayer::ContinentalMask;
        return true;
    }
    if (LayerName.Equals(TEXT("BoundaryType"), ESearchCase::IgnoreCase))
    {
        OutLayer = EMapLayer::BoundaryType;
        return true;
    }
    if (LayerName.Equals(TEXT("Velocity"), ESearchCase::IgnoreCase))
    {
        OutLayer = EMapLayer::Velocity;
        return true;
    }
    if (LayerName.Equals(TEXT("Composite"), ESearchCase::IgnoreCase))
    {
        OutLayer = EMapLayer::Composite;
        return true;
    }

    return false;
}

FString LayerToName(const EMapLayer Layer)
{
    switch (Layer)
    {
    case EMapLayer::PlateID:
        return TEXT("PlateID");
    case EMapLayer::Elevation:
        return TEXT("Elevation");
    case EMapLayer::ContinentalMask:
        return TEXT("ContinentalMask");
    case EMapLayer::BoundaryType:
        return TEXT("BoundaryType");
    case EMapLayer::Velocity:
        return TEXT("Velocity");
    case EMapLayer::Composite:
    default:
        return TEXT("Composite");
    }
}
}

void FTPPRuntimeModule::StartupModule()
{
    RegisterConsoleCommands();
}

void FTPPRuntimeModule::ShutdownModule()
{
    UnregisterConsoleCommands();
}

void FTPPRuntimeModule::RegisterConsoleCommands()
{
    ExportAllCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("TectonicExport.All"),
        TEXT("Export all tectonic map layers. Optional args: Width Height"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FTPPRuntimeModule::HandleExportAll),
        ECVF_Default);

    ExportLayerCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("TectonicExport.Layer"),
        TEXT("Export one layer. Usage: TectonicExport.Layer <LayerName> [Width] [Height]"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FTPPRuntimeModule::HandleExportLayer),
        ECVF_Default);
}

void FTPPRuntimeModule::UnregisterConsoleCommands()
{
    if (ExportAllCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(ExportAllCommand, false);
        ExportAllCommand = nullptr;
    }

    if (ExportLayerCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(ExportLayerCommand, false);
        ExportLayerCommand = nullptr;
    }
}

void FTPPRuntimeModule::HandleExportAll(const TArray<FString>& Args)
{
    int32 Width = 2048;
    int32 Height = 1024;
    if (Args.Num() >= 1)
    {
        Width = FMath::Max(128, FCString::Atoi(*Args[0]));
    }
    if (Args.Num() >= 2)
    {
        Height = FMath::Max(64, FCString::Atoi(*Args[1]));
    }

    const FTectonicData Data = FTectonicData::CreateMockData();
    const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("TectonicMaps");
    UPlanetMapExporter::ExportAllLayers(Data, OutputDirectory, Width, Height);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Exported all tectonic map layers to: %s"), *OutputDirectory);
}

void FTPPRuntimeModule::HandleExportLayer(const TArray<FString>& Args)
{
    if (Args.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Usage: TectonicExport.Layer <PlateID|Elevation|ContinentalMask|BoundaryType|Velocity|Composite> [Width] [Height]"));
        return;
    }

    EMapLayer Layer = EMapLayer::Elevation;
    if (!TryParseLayer(Args[0], Layer))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Unknown layer '%s'."), *Args[0]);
        return;
    }

    int32 Width = 2048;
    int32 Height = 1024;
    if (Args.Num() >= 2)
    {
        Width = FMath::Max(128, FCString::Atoi(*Args[1]));
    }
    if (Args.Num() >= 3)
    {
        Height = FMath::Max(64, FCString::Atoi(*Args[2]));
    }

    const FTectonicData Data = FTectonicData::CreateMockData();
    const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("TectonicMaps");
    const FString OutPath = OutputDirectory / FString::Printf(TEXT("%s.png"), *LayerToName(Layer));
    UPlanetMapExporter::ExportLayerToPNG(Data, Layer, OutPath, Width, Height);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Exported tectonic map layer to: %s"), *OutPath);
}
