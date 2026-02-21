#include "CrustSample.h"

FArchive& operator<<(FArchive& Ar, FCrustSample& Sample)
{
    uint8 CrustTypeByte = static_cast<uint8>(Sample.CrustType);
    uint8 OrogenyTypeByte = static_cast<uint8>(Sample.OrogenyType);

    Ar << Sample.Position;
    Ar << CrustTypeByte;
    Ar << Sample.Thickness;
    Ar << Sample.Elevation;
    Ar << Sample.OceanicAge;
    Ar << Sample.RidgeDirection;
    Ar << OrogenyTypeByte;
    Ar << Sample.OrogenyAge;
    Ar << Sample.FoldDirection;
    Ar << Sample.PlateIndex;
    Ar << Sample.DistToFront;

    if (Ar.IsLoading())
    {
        Sample.CrustType = static_cast<ECrustType>(CrustTypeByte);
        Sample.OrogenyType = static_cast<EOrogenyType>(OrogenyTypeByte);
    }

    return Ar;
}
