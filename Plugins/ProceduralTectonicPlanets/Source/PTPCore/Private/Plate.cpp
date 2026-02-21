#include "Plate.h"

FArchive& operator<<(FArchive& Ar, FPlate& Plate)
{
    uint8 CrustTypeByte = static_cast<uint8>(Plate.CrustType);

    Ar << Plate.PlateIndex;
    Ar << CrustTypeByte;
    Ar << Plate.RotationAxis;
    Ar << Plate.AngularVelocity;
    Ar << Plate.StepRotation;
    Ar << Plate.SeedSampleIndex;
    Ar << Plate.Area;
    Ar << Plate.BoundarySamples;

    if (Ar.IsLoading())
    {
        Plate.CrustType = static_cast<ECrustType>(CrustTypeByte);
    }

    return Ar;
}
