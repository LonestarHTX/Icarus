#include "PlanetState.h"

FArchive& operator<<(FArchive& Ar, FPlanetState& State)
{
    Ar << State.Time;
    Ar << State.SampleCount;
    Ar << State.Samples;
    Ar << State.TriangleIndices;
    Ar << State.AdjacencyOffsets;
    Ar << State.AdjacencyNeighbors;
    Ar << State.NumPlates;
    Ar << State.Plates;
    return Ar;
}
