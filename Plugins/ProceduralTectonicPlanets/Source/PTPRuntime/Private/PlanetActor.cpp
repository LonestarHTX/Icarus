#include "PlanetActor.h"

#include "AdjacencyBuilder.h"
#include "CrustSample.h"
#include "Engine/CollisionProfile.h"
#include "FibonacciSphere.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionConstant.h"
#include "PlanetColorRamp.h"
#include "PlanetConstants.h"
#include "PlanetMapExporter.h"
#include "BoundaryDetection.h"
#include "PlateInitializer.h"
#include "PlateMotion.h"
#include "RealtimeMeshComponent.h"
#include "RealtimeMeshSimple.h"
#include "SphericalTriangulation.h"
#include "TectonicData.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/Paths.h"

using namespace RealtimeMesh;

namespace
{
const FRealtimeMeshSectionGroupKey PlanetSectionGroupKey = FRealtimeMeshSectionGroupKey::Create(0, FName("Planet"));
const FRealtimeMeshSectionKey PlanetSectionKey = FRealtimeMeshSectionKey::CreateForPolyGroup(PlanetSectionGroupKey, 0);
}

APlanetActor::APlanetActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    MeshComponent = CreateDefaultSubobject<URealtimeMeshComponent>(TEXT("RealtimeMeshComponent"));
    MeshComponent->SetMobility(EComponentMobility::Movable);
    MeshComponent->SetGenerateOverlapEvents(false);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    SetRootComponent(MeshComponent);
}

void APlanetActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bSimulationPlaybackActive)
    {
        return;
    }

    SimulateSteps(PlaybackStepsPerTick);
}

bool APlanetActor::ShouldTickIfViewportsOnly() const
{
    return true;
}

void APlanetActor::GeneratePlanet()
{
    bSimulationPlaybackActive = false;
    SetActorTickEnabled(false);

    const int32 ClampedSampleCount = FMath::Max(SampleCount, 4);
    if (SampleCount != ClampedSampleCount)
    {
        SampleCount = ClampedSampleCount;
    }
    GenerationParams.SampleCount = SampleCount;

    TArray<FVector> Positions;

    {
        const double StartTime = FPlatformTime::Seconds();
        GenerateFibonacciSphere(Positions, SampleCount);
        const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Fibonacci sampling: %.1f ms (%d points)"), ElapsedMs, SampleCount);
    }

    PlanetState = FPlanetState();
    PlanetState.SampleCount = SampleCount;
    PlanetState.Samples.SetNum(SampleCount);
    for (int32 Index = 0; Index < SampleCount; ++Index)
    {
        PlanetState.Samples[Index].Position = Positions[Index];
    }

    bool bTriangulated = false;
    {
        const double StartTime = FPlatformTime::Seconds();
        bTriangulated = ComputeSphericalDelaunay(Positions, PlanetState.TriangleIndices);
        const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Convex hull triangulation: %.1f ms (%d triangles, success=%s)"),
            ElapsedMs,
            PlanetState.TriangleIndices.Num() / 3,
            bTriangulated ? TEXT("true") : TEXT("false"));
    }

    if (!bTriangulated)
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] GeneratePlanet aborted: triangulation failed."));
        return;
    }

    {
        const double StartTime = FPlatformTime::Seconds();
        BuildAdjacencyCSR(SampleCount, PlanetState.TriangleIndices, PlanetState.AdjacencyOffsets, PlanetState.AdjacencyNeighbors);
        const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Adjacency build: %.1f ms (%d neighbors)"), ElapsedMs, PlanetState.AdjacencyNeighbors.Num());
    }

    const bool bValid = ValidatePlanetGeometry(PlanetState);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Planet validation: %s"), bValid ? TEXT("PASSED") : TEXT("FAILED"));

    {
        const double StartTime = FPlatformTime::Seconds();
        InitializePlates(PlanetState, GenerationParams);
        const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("[PTP] Plate initialization: %.1f ms (%d plates)"), ElapsedMs, PlanetState.NumPlates);
    }

    const bool bPlatesValid = ValidatePlateInitialization(PlanetState);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Plate validation: %s"), bPlatesValid ? TEXT("PASSED") : TEXT("FAILED"));

    DetectAndClassifyBoundaries(PlanetState);

    UpdateMesh();
}

void APlanetActor::SimulateSteps(int32 StepCount)
{
    if (PlanetState.Samples.IsEmpty() || PlanetState.Plates.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] SimulateSteps skipped: planet has not been generated."));
        return;
    }

    const int32 ClampedStepCount = FMath::Max(StepCount, 1);
    for (int32 StepIndex = 0; StepIndex < ClampedStepCount; ++StepIndex)
    {
        MovePlates(PlanetState, PTP::DeltaT);
        DetectAndClassifyBoundaries(PlanetState);
    }

    UpdateMesh();

    UE_LOG(LogTemp, Log, TEXT("[PTP] Simulated %d step(s). Planet time: %.2f My"), ClampedStepCount, PlanetState.Time);

    if (bExportMapsAfterStepping)
    {
        ExportCurrentMaps();
    }
}

void APlanetActor::StartSimulationPlayback()
{
    if (PlanetState.Samples.IsEmpty() || PlanetState.Plates.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] StartSimulationPlayback skipped: planet has not been generated."));
        return;
    }

    bSimulationPlaybackActive = true;
    SetActorTickEnabled(true);
}

void APlanetActor::StopSimulationPlayback()
{
    bSimulationPlaybackActive = false;
    SetActorTickEnabled(false);
}

void APlanetActor::ResetSimulation()
{
    GeneratePlanet();
}

bool APlanetActor::ExportCurrentMaps(const int32 Width, const int32 Height) const
{
    if (PlanetState.Samples.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] ExportCurrentMaps skipped: planet state is empty."));
        return false;
    }

    const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("TectonicMaps");
    UPlanetMapExporter::ExportAllLayers(FTectonicData::FromPlanetState(PlanetState), OutputDirectory, Width, Height);
    return true;
}

void APlanetActor::UpdateMesh()
{
    if (PlanetState.SampleCount <= 0 || PlanetState.Samples.Num() != PlanetState.SampleCount || PlanetState.TriangleIndices.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PTP] UpdateMesh skipped: PlanetState is empty or invalid."));
        return;
    }

    URealtimeMeshSimple* RealtimeMesh = GetOrCreateRealtimeMesh();
    if (!RealtimeMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] UpdateMesh failed: unable to initialize URealtimeMeshSimple."));
        return;
    }

    const double StartTime = FPlatformTime::Seconds();

    FRealtimeMeshStreamSet StreamSet;
    TRealtimeMeshBuilderLocal<uint32, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);

    Builder.EnableTangents();
    Builder.EnableTexCoords();
    Builder.EnableColors();
    Builder.EnablePolyGroups();

    Builder.ReserveNumVertices(PlanetState.SampleCount);
    Builder.ReserveNumTriangles(PlanetState.TriangleIndices.Num() / 3);

    for (int32 VertexIndex = 0; VertexIndex < PlanetState.SampleCount; ++VertexIndex)
    {
        const FCrustSample& Sample = PlanetState.Samples[VertexIndex];

        const FVector3f Position = FVector3f(Sample.Position * RenderScale);
        const FVector3f Normal = FVector3f(Sample.Position);
        const FVector3f Tangent = FVector3f::UpVector.Cross(Normal).GetSafeNormal();
        const FVector2f UV(0.0f, 0.0f);

        Builder.AddVertex(Position)
            .SetNormalAndTangent(Normal, Tangent)
            .SetTexCoord(UV)
            .SetColor(ResolveSampleColor(Sample));
    }

    // Triangle indices already have correct CCW winding (fixed by EnsureOutwardWinding in SphericalTriangulation)
    for (int32 TriangleBase = 0; TriangleBase + 2 < PlanetState.TriangleIndices.Num(); TriangleBase += 3)
    {
        Builder.AddTriangle(
            PlanetState.TriangleIndices[TriangleBase],
            PlanetState.TriangleIndices[TriangleBase + 1],
            PlanetState.TriangleIndices[TriangleBase + 2],
            0);
    }

    // Use assigned material or fall back to default vertex color material
    UMaterialInterface* MaterialToUse = VertexColorMaterial.Get() ? VertexColorMaterial.Get() : GetOrCreateDefaultMaterial();
    RealtimeMesh->SetupMaterialSlot(0, FName("PlanetMaterial"), MaterialToUse);

    if (!bMeshInitialized)
    {
        RealtimeMesh->CreateSectionGroup(PlanetSectionGroupKey, StreamSet, FRealtimeMeshSectionGroupConfig(ERealtimeMeshSectionDrawType::Static));

        FRealtimeMeshSectionConfig SectionConfig(0);
        SectionConfig.bCastsShadow = false;
        RealtimeMesh->UpdateSectionConfig(PlanetSectionKey, SectionConfig, false);

        bMeshInitialized = true;
    }
    else
    {
        RealtimeMesh->UpdateSectionGroup(PlanetSectionGroupKey, StreamSet);
    }

    const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
    UE_LOG(LogTemp, Log, TEXT("[PTP] Mesh upload: %.1f ms (%d vertices, %d triangles)"),
        ElapsedMs,
        PlanetState.SampleCount,
        PlanetState.TriangleIndices.Num() / 3);
}

URealtimeMeshSimple* APlanetActor::GetOrCreateRealtimeMesh()
{
    if (!MeshComponent)
    {
        return nullptr;
    }

    if (URealtimeMeshSimple* Existing = MeshComponent->GetRealtimeMeshAs<URealtimeMeshSimple>())
    {
        return Existing;
    }

    return MeshComponent->InitializeRealtimeMesh<URealtimeMeshSimple>();
}

UMaterialInterface* APlanetActor::GetOrCreateDefaultMaterial()
{
    if (DefaultVertexColorMaterial)
    {
        return DefaultVertexColorMaterial;
    }

    // Create a simple unlit material that displays vertex colors
    UMaterial* Material = NewObject<UMaterial>(this, FName("DefaultPlanetMaterial"), RF_Transient);
    Material->MaterialDomain = EMaterialDomain::MD_Surface;
    Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
    Material->TwoSided = true;  // Two-sided to ensure visibility regardless of winding

    // Create vertex color expression
    UMaterialExpressionVertexColor* VertexColorExpr = NewObject<UMaterialExpressionVertexColor>(Material);
    VertexColorExpr->MaterialExpressionEditorX = -300;
    VertexColorExpr->MaterialExpressionEditorY = 0;
    Material->GetExpressionCollection().AddExpression(VertexColorExpr);

    // Connect vertex color RGB to emissive (unlit output)
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, VertexColorExpr);

    // Compile the material
    Material->PreEditChange(nullptr);
    Material->PostEditChange();

    DefaultVertexColorMaterial = Material;
    UE_LOG(LogTemp, Log, TEXT("[PTP] Created default unlit vertex color material."));

    return DefaultVertexColorMaterial;
}

FColor APlanetActor::ResolveSampleColor(const FCrustSample& Sample) const
{
    switch (VisualizationMode)
    {
    case EPlanetVisualizationMode::Elevation:
        return PTP::ElevationToColor(Sample.Elevation).ToFColor(true);

    case EPlanetVisualizationMode::PlateIndex:
        return PTP::PlateIndexToColor(Sample.PlateIndex).ToFColor(true);

    case EPlanetVisualizationMode::CrustType:
        return PTP::CrustTypeToColor(Sample.CrustType).ToFColor(true);

    case EPlanetVisualizationMode::Flat:
    default:
        return FColor(128, 128, 128, 255);
    }
}

#if WITH_EDITOR
void APlanetActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    if (PropertyName == GET_MEMBER_NAME_CHECKED(APlanetActor, VisualizationMode)
        || PropertyName == GET_MEMBER_NAME_CHECKED(APlanetActor, RenderScale)
        || PropertyName == GET_MEMBER_NAME_CHECKED(APlanetActor, VertexColorMaterial))
    {
        UpdateMesh();
    }
}
#endif
