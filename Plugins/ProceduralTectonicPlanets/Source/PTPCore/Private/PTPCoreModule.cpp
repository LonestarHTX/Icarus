#include "PTPCoreModule.h"

#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"

#include "PlanetConstants.h"
#include "PlanetState.h"
#include "FibonacciSphere.h"
#include "SphericalTriangulation.h"
#include "AdjacencyBuilder.h"

IMPLEMENT_MODULE(FPTPCoreModule, PTPCore)

static FAutoConsoleCommand PTPTestCommand(
    TEXT("PTP.Test"),
    TEXT("Run PTP geometry generation and validation. Usage: PTP.Test [NumPoints] (default 10000, max 500000)"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        int32 NumPoints = 10000; // Default to 10k for quick test
        if (Args.Num() > 0)
        {
            NumPoints = FCString::Atoi(*Args[0]);
            NumPoints = FMath::Clamp(NumPoints, 100, PTP::DefaultSampleCount);
        }

        UE_LOG(LogTemp, Log, TEXT("========================================"));
        UE_LOG(LogTemp, Log, TEXT("[PTP] Starting geometry test with %d points"), NumPoints);
        UE_LOG(LogTemp, Log, TEXT("========================================"));

        // Time the whole process
        double TotalStartTime = FPlatformTime::Seconds();

        // Step 1: Generate Fibonacci sphere
        double StepStartTime = FPlatformTime::Seconds();
        TArray<FVector> Positions;
        GenerateFibonacciSphere(Positions, NumPoints);
        double FibonacciTime = FPlatformTime::Seconds() - StepStartTime;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Fibonacci sphere: %.3f seconds"), FibonacciTime);

        // Step 2: Compute Delaunay triangulation
        StepStartTime = FPlatformTime::Seconds();
        TArray<int32> TriangleIndices;
        bool bTriangulationSuccess = ComputeSphericalDelaunay(Positions, TriangleIndices);
        double TriangulationTime = FPlatformTime::Seconds() - StepStartTime;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Triangulation: %.3f seconds (success=%s)"),
            TriangulationTime, bTriangulationSuccess ? TEXT("true") : TEXT("false"));

        if (!bTriangulationSuccess)
        {
            UE_LOG(LogTemp, Error, TEXT("[PTP] Triangulation failed! Aborting test."));
            return;
        }

        // Step 3: Build adjacency
        StepStartTime = FPlatformTime::Seconds();
        TArray<int32> AdjacencyOffsets;
        TArray<int32> AdjacencyNeighbors;
        BuildAdjacencyCSR(NumPoints, TriangleIndices, AdjacencyOffsets, AdjacencyNeighbors);
        double AdjacencyTime = FPlatformTime::Seconds() - StepStartTime;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Adjacency CSR: %.3f seconds"), AdjacencyTime);

        // Step 4: Assemble state and validate
        FPlanetState State;
        State.SampleCount = NumPoints;
        State.Samples.SetNum(NumPoints);
        for (int32 i = 0; i < NumPoints; i++)
        {
            State.Samples[i].Position = Positions[i];
        }
        State.TriangleIndices = MoveTemp(TriangleIndices);
        State.AdjacencyOffsets = MoveTemp(AdjacencyOffsets);
        State.AdjacencyNeighbors = MoveTemp(AdjacencyNeighbors);

        StepStartTime = FPlatformTime::Seconds();
        bool bValid = ValidatePlanetGeometry(State);
        double ValidationTime = FPlatformTime::Seconds() - StepStartTime;

        double TotalTime = FPlatformTime::Seconds() - TotalStartTime;

        UE_LOG(LogTemp, Log, TEXT("========================================"));
        UE_LOG(LogTemp, Log, TEXT("[PTP] RESULTS"));
        UE_LOG(LogTemp, Log, TEXT("========================================"));
        UE_LOG(LogTemp, Log, TEXT("[PTP] Points: %d"), State.SampleCount);
        UE_LOG(LogTemp, Log, TEXT("[PTP] Triangles: %d (expected %d)"), State.TriangleIndices.Num() / 3, 2 * NumPoints - 4);
        UE_LOG(LogTemp, Log, TEXT("[PTP] Edges (adjacency entries): %d"), State.AdjacencyNeighbors.Num());
        UE_LOG(LogTemp, Log, TEXT("[PTP] Avg neighbors per point: %.2f"),
            NumPoints > 0 ? (float)State.AdjacencyNeighbors.Num() / NumPoints : 0.0f);
        UE_LOG(LogTemp, Log, TEXT("----------------------------------------"));
        UE_LOG(LogTemp, Log, TEXT("[PTP] Total time: %.3f seconds"), TotalTime);
        UE_LOG(LogTemp, Log, TEXT("[PTP] Validation: %s"), bValid ? TEXT("PASSED") : TEXT("FAILED"));
        UE_LOG(LogTemp, Log, TEXT("========================================"));
    })
);

void FPTPCoreModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("[PTP] PTPCore module loaded. Use 'PTP.Test [NumPoints]' in console to test."));
}

void FPTPCoreModule::ShutdownModule()
{
}
