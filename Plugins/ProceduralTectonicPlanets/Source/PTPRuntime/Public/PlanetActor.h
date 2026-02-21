#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

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

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
    void GeneratePlanet();

    UFUNCTION(BlueprintCallable, Category = "Planet")
    void UpdateMesh();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
    EPlanetVisualizationMode VisualizationMode = EPlanetVisualizationMode::Elevation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1.0", ClampMax = "1000000.0"))
    float RenderScale = 637000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "100", ClampMax = "1000000"))
    int32 SampleCount = 500000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rendering")
    TObjectPtr<UMaterialInterface> VertexColorMaterial = nullptr;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<URealtimeMeshComponent> MeshComponent;

    FPlanetState PlanetState;

private:
    bool bMeshInitialized = false;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> DefaultVertexColorMaterial = nullptr;

    URealtimeMeshSimple* GetOrCreateRealtimeMesh();
    UMaterialInterface* GetOrCreateDefaultMaterial();
    FColor ResolveSampleColor(const FCrustSample& Sample) const;
};
