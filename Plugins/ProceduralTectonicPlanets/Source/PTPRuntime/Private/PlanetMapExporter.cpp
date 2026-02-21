#include "PlanetMapExporter.h"

#include "Async/ParallelFor.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "MapColorRamps.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"

namespace
{
// Golden angle in radians: π * (3 - √5) ≈ 2.3999... ≈ 137.5°
constexpr double GoldenAngle = PI * (3.0 - 2.23606797749979);

// Search radius - must be large enough to cover the local Voronoi cell
// With 500k points, we need ~50-100 to ensure we find the true nearest neighbor
constexpr int32 LocalSearchRadius = 75;

FVector3d PixelToDirection(const int32 PixelX, const int32 PixelY, const int32 Width, const int32 Height)
{
    const double Lon = ((static_cast<double>(PixelX) + 0.5) / static_cast<double>(Width)) * (2.0 * PI) - PI;
    const double Lat = (PI * 0.5) - ((static_cast<double>(PixelY) + 0.5) / static_cast<double>(Height)) * PI;

    const double CosLat = FMath::Cos(Lat);
    return FVector3d(
        CosLat * FMath::Cos(Lon),
        CosLat * FMath::Sin(Lon),
        FMath::Sin(Lat));
}

// Calculate Fibonacci lattice point analytically (must match generation formula exactly)
FVector3d GetFibonacciPoint(const int32 Index, const int32 NumPoints)
{
    // z goes from ~1 (north pole) to ~-1 (south pole)
    const double Z = 1.0 - (2.0 * static_cast<double>(Index) + 1.0) / static_cast<double>(NumPoints);
    const double Lon = static_cast<double>(Index) * GoldenAngle;
    const double Radius = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
    return FVector3d(
        Radius * FMath::Cos(Lon),
        Radius * FMath::Sin(Lon),
        Z);
}

int32 FindNearestFibonacciPoint(const FVector3d& Direction, const int32 NumPoints)
{
    // Approximate index from z-coordinate
    // From: z = 1 - (2i + 1) / N, solve for i: i = (N * (1 - z) - 1) / 2
    const double T = (1.0 - Direction.Z) * static_cast<double>(NumPoints) * 0.5;
    const int32 ApproxIndex = FMath::Clamp(FMath::RoundToInt(T - 0.5), 0, NumPoints - 1);

    int32 BestIndex = ApproxIndex;
    double BestDistanceSquared = MAX_dbl;

    // Search local neighborhood - must be large enough to cover spiral turns
    const int32 MinIndex = FMath::Max(0, ApproxIndex - LocalSearchRadius);
    const int32 MaxIndex = FMath::Min(NumPoints - 1, ApproxIndex + LocalSearchRadius);

    for (int32 Index = MinIndex; Index <= MaxIndex; ++Index)
    {
        const FVector3d Point = GetFibonacciPoint(Index, NumPoints);
        const double DistanceSquared = FVector3d::DistSquared(Direction, Point);
        if (DistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            BestIndex = Index;
        }
    }

    return BestIndex;
}

FLinearColor LayerColor(const FTectonicData& Data, const int32 Index, const EMapLayer Layer)
{
    switch (Layer)
    {
    case EMapLayer::PlateID:
        return PTPMapColor::PlateIDToColor(Data.GetPlateID(Index));
    case EMapLayer::Elevation:
        return PTPMapColor::ElevationToColor(Data.GetElevation(Index));
    case EMapLayer::ContinentalMask:
        return PTPMapColor::ContinentalMaskToColor(Data.IsContinental(Index));
    case EMapLayer::BoundaryType:
        return PTPMapColor::BoundaryTypeToColor(
            Data.GetBoundaryType(Index),
            Data.GetBoundaryConvergenceType(Index),
            Data.GetBoundaryStress(Index));
    case EMapLayer::Velocity:
        return PTPMapColor::VelocityToColor(Data.GetVelocity(Index));
    case EMapLayer::Composite:
    default:
        return PTPMapColor::ElevationToColor(Data.GetElevation(Index));
    }
}

bool IsPlateBorderPixel(const FTectonicData& Data, const int32 X, const int32 Y, const int32 Width, const int32 Height, const int32 CenterPointIndex)
{
    const int32 NumPoints = Data.GetNumPoints();
    const int32 CenterPlate = Data.GetPlateID(CenterPointIndex);
    if (CenterPlate < 0)
    {
        return false;
    }

    const int32 LeftX = (X - 1 + Width) % Width;
    const int32 RightX = (X + 1) % Width;
    const int32 UpY = FMath::Clamp(Y - 1, 0, Height - 1);
    const int32 DownY = FMath::Clamp(Y + 1, 0, Height - 1);

    const int32 NeighborIndices[] = {
        FindNearestFibonacciPoint(PixelToDirection(LeftX, Y, Width, Height), NumPoints),
        FindNearestFibonacciPoint(PixelToDirection(RightX, Y, Width, Height), NumPoints),
        FindNearestFibonacciPoint(PixelToDirection(X, UpY, Width, Height), NumPoints),
        FindNearestFibonacciPoint(PixelToDirection(X, DownY, Width, Height), NumPoints)
    };

    for (const int32 NeighborIndex : NeighborIndices)
    {
        if (Data.GetPlateID(NeighborIndex) != CenterPlate)
        {
            return true;
        }
    }

    return false;
}

bool SavePixelsToPNG(const TArray<FColor>& Pixels, const int32 Width, const int32 Height, const FString& Path)
{
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
    const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid())
    {
        return false;
    }

    if (!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
    {
        return false;
    }

    const TArray64<uint8>& Compressed = ImageWrapper->GetCompressed(100);
    return FFileHelper::SaveArrayToFile(Compressed, *Path);
}

FString LayerName(const EMapLayer Layer)
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

bool UPlanetMapExporter::ExportLayerToPNG(
    const FTectonicData& Data,
    const EMapLayer Layer,
    const FString& FilePath,
    const int32 Width,
    const int32 Height)
{
    if (Data.GetNumPoints() <= 0 || Width <= 0 || Height <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] ExportLayerToPNG failed: invalid data or dimensions."));
        return false;
    }

    const FString Directory = FPaths::GetPath(FilePath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    const TArray<FColor> Pixels = RenderLayer(Data, Layer, Width, Height);
    const bool bSaved = SavePixelsToPNG(Pixels, Width, Height, FilePath);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Export layer %s -> %s (%s)"),
        *LayerName(Layer),
        *FilePath,
        bSaved ? TEXT("ok") : TEXT("failed"));

    return bSaved;
}

void UPlanetMapExporter::ExportAllLayers(
    const FTectonicData& Data,
    const FString& OutputDirectory,
    const int32 Width,
    const int32 Height)
{
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);

    const EMapLayer Layers[] = {
        EMapLayer::PlateID,
        EMapLayer::Elevation,
        EMapLayer::ContinentalMask,
        EMapLayer::BoundaryType,
        EMapLayer::Velocity,
        EMapLayer::Composite
    };

    for (const EMapLayer Layer : Layers)
    {
        const FString FilePath = OutputDirectory / FString::Printf(TEXT("%s.png"), *LayerName(Layer));
        ExportLayerToPNG(Data, Layer, FilePath, Width, Height);
    }
}

TArray<FColor> UPlanetMapExporter::RenderLayer(
    const FTectonicData& Data,
    const EMapLayer Layer,
    const int32 Width,
    const int32 Height)
{
    TArray<FColor> PixelData;
    PixelData.SetNumUninitialized(FMath::Max(0, Width * Height));

    if (Data.GetNumPoints() <= 0 || Width <= 0 || Height <= 0)
    {
        return PixelData;
    }

    const int32 NumPoints = Data.GetNumPoints();

    ParallelFor(Height, [&](const int32 Row)
    {
        for (int32 Col = 0; Col < Width; ++Col)
        {
            const FVector3d Direction = PixelToDirection(Col, Row, Width, Height);
            const int32 NearestIndex = FindNearestFibonacciPoint(Direction, NumPoints);

            FLinearColor Color = LayerColor(Data, NearestIndex, Layer);

            if (Layer == EMapLayer::Composite)
            {
                const EBoundaryType BoundaryType = Data.GetBoundaryType(NearestIndex);
                if (BoundaryType != EBoundaryType::None)
                {
                    const FLinearColor BoundaryColor = PTPMapColor::BoundaryTypeToColor(
                        BoundaryType,
                        Data.GetBoundaryConvergenceType(NearestIndex),
                        Data.GetBoundaryStress(NearestIndex));
                    Color = PTPMapColor::AlphaBlend(Color, BoundaryColor, 0.6f);
                }

                if (IsPlateBorderPixel(Data, Col, Row, Width, Height, NearestIndex))
                {
                    Color = PTPMapColor::AlphaBlend(Color, FLinearColor::Black, 0.55f);
                }
            }

            PixelData[Row * Width + Col] = Color.ToFColor(true);
        }
    });

    return PixelData;
}
