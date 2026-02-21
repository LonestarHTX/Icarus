#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PlanetGenerationParams.h"
#include "PlanetState.h"

#include "PlanetActor.generated.h"

class UMaterialInterface;
class URealtimeMeshComponent;
class URealtimeMeshSimple;

UENUM(BlueprintType)
enum class EPlanetVisualizationMode : uint8
{
    Elevation,
    PlateIndex,
    CrustType,
    Flat
};

UCLASS()
class PTPRUNTIME_API APlanetActor : public AActor
{
    GENERATED_BODY()

public:
    APlanetActor();
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
    void GeneratePlanet();

    UFUNCTION(BlueprintCallable, Category = "Planet")
    void UpdateMesh();

    UFUNCTION(BlueprintCallable, Category = "Planet|Simulation")
    void SimulateSteps(int32 StepCount = 1);

    UFUNCTION(BlueprintCallable, Category = "Planet|Simulation")
    void StartSimulationPlayback();

    UFUNCTION(BlueprintCallable, Category = "Planet|Simulation")
    void StopSimulationPlayback();

    UFUNCTION(BlueprintCallable, Category = "Planet|Simulation")
    void ResetSimulation();

    UFUNCTION(BlueprintCallable, Category = "Planet|Simulation")
    bool ExportCurrentMaps(int32 Width = 2048, int32 Height = 1024) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    EPlanetVisualizationMode VisualizationMode = EPlanetVisualizationMode::Elevation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1.0", ClampMax = "1000000.0"))
    float RenderScale = 637000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "100", ClampMax = "1000000"))
    int32 SampleCount = 500000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    FPlanetGenerationParams GenerationParams;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Simulation", meta = (ClampMin = "1"))
    int32 PlaybackStepsPerTick = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Simulation")
    bool bExportMapsAfterStepping = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rendering")
    TObjectPtr<UMaterialInterface> VertexColorMaterial = nullptr;

    const FPlanetState& GetPlanetState() const { return PlanetState; }

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<URealtimeMeshComponent> MeshComponent;

    FPlanetState PlanetState;

private:
    bool bMeshInitialized = false;
    bool bSimulationPlaybackActive = false;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> DefaultVertexColorMaterial = nullptr;

    URealtimeMeshSimple* GetOrCreateRealtimeMesh();
    UMaterialInterface* GetOrCreateDefaultMaterial();
    FColor ResolveSampleColor(const FCrustSample& Sample) const;
};
