#include "PTPRuntimeModule.h"

#include "PlanetActor.h"
#include "PlanetMapExporter.h"
#include "TectonicData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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

APlanetActor* FindPlanetActor()
{
    if (!GEngine)
    {
        return nullptr;
    }

    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (!World || !World->PersistentLevel)
        {
            continue;
        }

        for (TActorIterator<APlanetActor> It(World); It; ++It)
        {
            return *It;
        }
    }

    return nullptr;
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
    StepCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Tectonic.Step"),
        TEXT("Advance simulation by N steps. Usage: Tectonic.Step [N]"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FTPPRuntimeModule::HandleStep),
        ECVF_Default);

    PlayCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Tectonic.Play"),
        TEXT("Start simulation playback."),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FTPPRuntimeModule::HandlePlay),
        ECVF_Default);

    StopCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Tectonic.Stop"),
        TEXT("Stop simulation playback."),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FTPPRuntimeModule::HandleStop),
        ECVF_Default);

    ResetCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("Tectonic.Reset"),
        TEXT("Regenerate planet from current generation params."),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FTPPRuntimeModule::HandleReset),
        ECVF_Default);

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
    if (StepCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(StepCommand, false);
        StepCommand = nullptr;
    }

    if (PlayCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(PlayCommand, false);
        PlayCommand = nullptr;
    }

    if (StopCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(StopCommand, false);
        StopCommand = nullptr;
    }

    if (ResetCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(ResetCommand, false);
        ResetCommand = nullptr;
    }

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

void FTPPRuntimeModule::HandleStep(const TArray<FString>& Args)
{
    APlanetActor* PlanetActor = FindPlanetActor();
    if (!PlanetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Tectonic.Step failed: no APlanetActor found."));
        return;
    }

    int32 StepCount = 1;
    if (Args.Num() >= 1)
    {
        StepCount = FMath::Max(1, FCString::Atoi(*Args[0]));
    }

    PlanetActor->SimulateSteps(StepCount);
}

void FTPPRuntimeModule::HandlePlay(const TArray<FString>& Args)
{
    APlanetActor* PlanetActor = FindPlanetActor();
    if (!PlanetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Tectonic.Play failed: no APlanetActor found."));
        return;
    }

    PlanetActor->StartSimulationPlayback();
}

void FTPPRuntimeModule::HandleStop(const TArray<FString>& Args)
{
    APlanetActor* PlanetActor = FindPlanetActor();
    if (!PlanetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Tectonic.Stop failed: no APlanetActor found."));
        return;
    }

    PlanetActor->StopSimulationPlayback();
}

void FTPPRuntimeModule::HandleReset(const TArray<FString>& Args)
{
    APlanetActor* PlanetActor = FindPlanetActor();
    if (!PlanetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] Tectonic.Reset failed: no APlanetActor found."));
        return;
    }

    PlanetActor->ResetSimulation();
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

    const APlanetActor* PlanetActor = FindPlanetActor();
    if (!PlanetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] TectonicExport.All failed: no APlanetActor found."));
        return;
    }

    const FTectonicData Data = FTectonicData::FromPlanetState(PlanetActor->GetPlanetState());
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

    const APlanetActor* PlanetActor = FindPlanetActor();
    if (!PlanetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] TectonicExport.Layer failed: no APlanetActor found."));
        return;
    }

    const FTectonicData Data = FTectonicData::FromPlanetState(PlanetActor->GetPlanetState());
    const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("TectonicMaps");
    const FString OutPath = OutputDirectory / FString::Printf(TEXT("%s.png"), *LayerToName(Layer));
    UPlanetMapExporter::ExportLayerToPNG(Data, Layer, OutPath, Width, Height);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Exported tectonic map layer to: %s"), *OutPath);
}
