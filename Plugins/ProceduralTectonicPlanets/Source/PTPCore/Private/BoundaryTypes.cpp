#include "BoundaryTypes.h"

FArchive& operator<<(FArchive& Ar, FSampleBoundaryInfo& Info)
{
    uint8 BoundaryTypeByte = static_cast<uint8>(Info.BoundaryType);
    uint8 ConvergenceTypeByte = static_cast<uint8>(Info.ConvergenceType);

    Ar << Info.bIsBoundary;
    Ar << BoundaryTypeByte;
    Ar << ConvergenceTypeByte;
    Ar << Info.AdjacentPlateIndex;
    Ar << Info.NearestConvergentSegmentIndex;
    Ar << Info.BoundaryStress;

    if (Ar.IsLoading())
    {
        Info.BoundaryType = static_cast<EPTPBoundaryType>(BoundaryTypeByte);
        Info.ConvergenceType = static_cast<EPTPConvergenceType>(ConvergenceTypeByte);
    }

    return Ar;
}

FArchive& operator<<(FArchive& Ar, FBoundarySegment& Segment)
{
    uint8 TypeByte = static_cast<uint8>(Segment.Type);
    uint8 ConvergenceTypeByte = static_cast<uint8>(Segment.ConvergenceType);

    Ar << Segment.PlateIndexA;
    Ar << Segment.PlateIndexB;
    Ar << TypeByte;
    Ar << ConvergenceTypeByte;
    Ar << Segment.SubductingPlateIndex;
    Ar << Segment.OverridingPlateIndex;
    Ar << Segment.SamplesA;
    Ar << Segment.SamplesB;
    Ar << Segment.RelativeSpeed;

    if (Ar.IsLoading())
    {
        Segment.Type = static_cast<EPTPBoundaryType>(TypeByte);
        Segment.ConvergenceType = static_cast<EPTPConvergenceType>(ConvergenceTypeByte);
    }

    return Ar;
}

FArchive& operator<<(FArchive& Ar, FBoundaryRegistry& Registry)
{
    Ar << Registry.Segments;
    Ar << Registry.PlatePairToSegmentIndex;
    return Ar;
}
