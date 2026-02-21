#include "PTPSimulationModule.h"

#include "AdjacencyBuilder.h"
#include "BoundaryDetection.h"
#include "CrustSample.h"
#include "FibonacciSphere.h"
#include "HAL/IConsoleManager.h"
#include "PlanetConstants.h"
#include "PlanetGenerationParams.h"
#include "PlanetState.h"
#include "PlateInitializer.h"
#include "PlateMotion.h"
#include "Resampling.h"
#include "SphericalTriangulation.h"
#include "Subduction.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FPTPSimulationModule, PTPSimulation)

static FAutoConsoleCommand PTPResampleTestCommand(
    TEXT("PTP.ResampleTest"),
    TEXT("Run a headless resampling regression test. Usage: PTP.ResampleTest [NumPoints] [NumSteps]"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        int32 NumPoints = 100000;
        int32 NumSteps = 10;
        if (Args.Num() > 0)
        {
            NumPoints = FMath::Clamp(FCString::Atoi(*Args[0]), 1000, 500000);
        }
        if (Args.Num() > 1)
        {
            NumSteps = FMath::Clamp(FCString::Atoi(*Args[1]), 1, 100);
        }

        UE_LOG(LogTemp, Log, TEXT("[PTP] === ResampleTest start (points=%d, steps=%d) ==="), NumPoints, NumSteps);

        FPlanetState State;
        State.SampleCount = NumPoints;
        State.Samples.SetNum(NumPoints);

        TArray<FVector> Positions;
        GenerateFibonacciSphere(Positions, NumPoints);
        for (int32 Index = 0; Index < NumPoints; ++Index)
        {
            State.Samples[Index].Position = Positions[Index];
        }

        if (!ComputeSphericalDelaunay(Positions, State.TriangleIndices))
        {
            UE_LOG(LogTemp, Error, TEXT("[PTP] ResampleTest failed: triangulation failed."));
            return;
        }

        BuildAdjacencyCSR(NumPoints, State.TriangleIndices, State.AdjacencyOffsets, State.AdjacencyNeighbors);

        FPlanetGenerationParams Params;
        Params.SampleCount = NumPoints;
        Params.NumPlates = 40;
        InitializePlates(State, Params);
        DetectAndClassifyBoundaries(State);

        for (int32 Step = 0; Step < NumSteps; ++Step)
        {
            MovePlates(State, PTP::DeltaT);
            DetectAndClassifyBoundaries(State);
            ProcessSubduction(State, PTP::DeltaT);
        }

        FGlobalResampleStats Stats;
        if (!GlobalResample(State, &Stats))
        {
            UE_LOG(LogTemp, Error, TEXT("[PTP] ResampleTest failed: GlobalResample returned false."));
            return;
        }

        const int32 Total = FMath::Max(1, State.SampleCount);
        const double NormalRatio = static_cast<double>(Stats.NormalCount) / static_cast<double>(Total);
        const double FallbackRatio = static_cast<double>(Stats.FallbackCount) / static_cast<double>(Total);
        const double GapRatio = static_cast<double>(Stats.GapCount) / static_cast<double>(Total);
        const double OverlapRatio = static_cast<double>(Stats.OverlapCount) / static_cast<double>(Total);

        bool bPlateIndexValid = true;
        for (const FCrustSample& Sample : State.Samples)
        {
            if (!State.Plates.IsValidIndex(Sample.PlateIndex))
            {
                bPlateIndexValid = false;
                break;
            }
        }

        const bool bPassed =
            State.Samples.Num() == NumPoints
            && bPlateIndexValid
            && NormalRatio >= 0.85
            && GapRatio <= 0.12
            && OverlapRatio <= 0.001
            && FallbackRatio <= 0.001;

        UE_LOG(LogTemp, Log, TEXT("[PTP] ResampleTest ratios: normal=%.3f fallback=%.3f gap=%.3f overlap=%.3f"),
            NormalRatio, FallbackRatio, GapRatio, OverlapRatio);
        UE_LOG(LogTemp, Log, TEXT("[PTP] ResampleTest checks: sampleCount=%s plateIndexValid=%s"),
            State.Samples.Num() == NumPoints ? TEXT("true") : TEXT("false"),
            bPlateIndexValid ? TEXT("true") : TEXT("false"));
        UE_LOG(LogTemp, Log, TEXT("[PTP] ResampleTest: %s"), bPassed ? TEXT("PASSED") : TEXT("FAILED"));
    }));

void FPTPSimulationModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("[PTP] PTPSimulation module loaded. Use 'PTP.ResampleTest [NumPoints] [NumSteps]'."));
}

void FPTPSimulationModule::ShutdownModule()
{
}
