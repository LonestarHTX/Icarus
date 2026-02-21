#include "AdjacencyBuilder.h"

#include "Algo/Sort.h"
#include "Containers/Set.h"

namespace
{
FORCEINLINE int64 MakeEdgeKey(int32 MinIndex, int32 MaxIndex)
{
    return (static_cast<int64>(MinIndex) << 32) | static_cast<uint32>(MaxIndex);
}

void AddUndirectedEdge(const int32 A, const int32 B, TSet<int64>& UniqueUndirectedEdges, TArray<int32>& Degrees)
{
    if (A == B)
    {
        return;
    }

    const int32 MinIndex = FMath::Min(A, B);
    const int32 MaxIndex = FMath::Max(A, B);
    const int64 EdgeKey = MakeEdgeKey(MinIndex, MaxIndex);

    if (!UniqueUndirectedEdges.Contains(EdgeKey))
    {
        UniqueUndirectedEdges.Add(EdgeKey);
        ++Degrees[A];
        ++Degrees[B];
    }
}
}

void BuildAdjacencyCSR(
    int32 NumPoints,
    const TArray<int32>& TriangleIndices,
    TArray<int32>& OutOffsets,
    TArray<int32>& OutNeighbors)
{
    OutOffsets.Reset();
    OutNeighbors.Reset();

    if (NumPoints <= 0)
    {
        OutOffsets.Add(0);
        return;
    }

    TArray<int32> Degrees;
    Degrees.Init(0, NumPoints);

    TSet<int64> UniqueUndirectedEdges;
    UniqueUndirectedEdges.Reserve(TriangleIndices.Num());

    for (int32 TriangleBase = 0; TriangleBase + 2 < TriangleIndices.Num(); TriangleBase += 3)
    {
        const int32 A = TriangleIndices[TriangleBase];
        const int32 B = TriangleIndices[TriangleBase + 1];
        const int32 C = TriangleIndices[TriangleBase + 2];

        if (!Degrees.IsValidIndex(A) || !Degrees.IsValidIndex(B) || !Degrees.IsValidIndex(C))
        {
            continue;
        }

        AddUndirectedEdge(A, B, UniqueUndirectedEdges, Degrees);
        AddUndirectedEdge(B, C, UniqueUndirectedEdges, Degrees);
        AddUndirectedEdge(A, C, UniqueUndirectedEdges, Degrees);
    }

    OutOffsets.SetNumUninitialized(NumPoints + 1);
    OutOffsets[0] = 0;
    for (int32 Index = 0; Index < NumPoints; ++Index)
    {
        OutOffsets[Index + 1] = OutOffsets[Index] + Degrees[Index];
    }

    OutNeighbors.Init(INDEX_NONE, OutOffsets[NumPoints]);

    TArray<int32> WriteCursor = OutOffsets;

    for (const int64 EdgeKey : UniqueUndirectedEdges)
    {
        const int32 A = static_cast<int32>(EdgeKey >> 32);
        const int32 B = static_cast<int32>(EdgeKey & 0xFFFFFFFFu);

        OutNeighbors[WriteCursor[A]++] = B;
        OutNeighbors[WriteCursor[B]++] = A;
    }

    for (int32 Index = 0; Index < NumPoints; ++Index)
    {
        const int32 Begin = OutOffsets[Index];
        const int32 End = OutOffsets[Index + 1];
        Algo::Sort(MakeArrayView(OutNeighbors.GetData() + Begin, End - Begin));
    }
}

bool ValidatePlanetGeometry(const FPlanetState& State)
{
    bool bIsValid = true;

    const int32 ActualPointCount = State.Samples.Num();
    const int32 ExpectedPointCount = State.SampleCount;
    const bool bPointCountValid = (ActualPointCount == ExpectedPointCount);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Point count: actual=%d expected=%d valid=%s"),
        ActualPointCount, ExpectedPointCount, bPointCountValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bPointCountValid;

    const int32 ActualTriangleCount = State.TriangleIndices.Num() / 3;
    const int32 ExpectedTriangleCount = (State.SampleCount > 0) ? (2 * State.SampleCount - 4) : 0;
    const bool bTriangleCountValid = (State.TriangleIndices.Num() % 3 == 0) && (ActualTriangleCount == ExpectedTriangleCount);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Triangle count: actual=%d expected=%d valid=%s"),
        ActualTriangleCount, ExpectedTriangleCount, bTriangleCountValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bTriangleCountValid;

    bool bIndexRangeValid = true;
    for (const int32 Index : State.TriangleIndices)
    {
        if (Index < 0 || Index >= State.SampleCount)
        {
            bIndexRangeValid = false;
            break;
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[PTP] Triangle index range valid=%s"), bIndexRangeValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bIndexRangeValid;

    bool bUnitSphereValid = true;
    for (const FCrustSample& Sample : State.Samples)
    {
        const float Length = Sample.Position.Length();
        if (Length < 0.999f || Length > 1.001f)
        {
            bUnitSphereValid = false;
            break;
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[PTP] Unit sphere check valid=%s"), bUnitSphereValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bUnitSphereValid;

    bool bNeighborCountValid = true;
    const bool bOffsetsShapeValid = (State.AdjacencyOffsets.Num() == State.SampleCount + 1);
    if (bOffsetsShapeValid)
    {
        bNeighborCountValid = (State.AdjacencyOffsets[State.SampleCount] == State.AdjacencyNeighbors.Num());
        for (int32 PointIndex = 0; PointIndex < State.SampleCount; ++PointIndex)
        {
            const int32 Begin = State.AdjacencyOffsets[PointIndex];
            const int32 End = State.AdjacencyOffsets[PointIndex + 1];
            const int32 NeighborCount = End - Begin;
            if (NeighborCount < 3 || NeighborCount > 10)
            {
                bNeighborCountValid = false;
                break;
            }
        }
    }
    else
    {
        bNeighborCountValid = false;
    }
    UE_LOG(LogTemp, Log, TEXT("[PTP] Neighbor count bounds valid=%s"), bNeighborCountValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bNeighborCountValid;

    bool bAdjacencySymmetryValid = bOffsetsShapeValid;
    if (bAdjacencySymmetryValid)
    {
        for (int32 A = 0; A < State.SampleCount && bAdjacencySymmetryValid; ++A)
        {
            const int32 Begin = State.AdjacencyOffsets[A];
            const int32 End = State.AdjacencyOffsets[A + 1];

            if (Begin < 0 || End < Begin || End > State.AdjacencyNeighbors.Num())
            {
                bAdjacencySymmetryValid = false;
                break;
            }

            for (int32 NbrIdx = Begin; NbrIdx < End; ++NbrIdx)
            {
                const int32 B = State.AdjacencyNeighbors[NbrIdx];
                if (B < 0 || B >= State.SampleCount)
                {
                    bAdjacencySymmetryValid = false;
                    break;
                }

                const int32 OtherBegin = State.AdjacencyOffsets[B];
                const int32 OtherEnd = State.AdjacencyOffsets[B + 1];
                bool bFoundBackEdge = false;
                for (int32 BackIdx = OtherBegin; BackIdx < OtherEnd; ++BackIdx)
                {
                    if (State.AdjacencyNeighbors[BackIdx] == A)
                    {
                        bFoundBackEdge = true;
                        break;
                    }
                }

                if (!bFoundBackEdge)
                {
                    bAdjacencySymmetryValid = false;
                    break;
                }
            }
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[PTP] Adjacency symmetry valid=%s"), bAdjacencySymmetryValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bAdjacencySymmetryValid;

    bool bWindingValid = true;
    for (int32 TriangleBase = 0; TriangleBase + 2 < State.TriangleIndices.Num(); TriangleBase += 3)
    {
        const int32 Idx0 = State.TriangleIndices[TriangleBase];
        const int32 Idx1 = State.TriangleIndices[TriangleBase + 1];
        const int32 Idx2 = State.TriangleIndices[TriangleBase + 2];

        if (!State.Samples.IsValidIndex(Idx0) || !State.Samples.IsValidIndex(Idx1) || !State.Samples.IsValidIndex(Idx2))
        {
            bWindingValid = false;
            break;
        }

        const FVector A = State.Samples[Idx0].Position;
        const FVector B = State.Samples[Idx1].Position;
        const FVector C = State.Samples[Idx2].Position;
        const FVector Normal = FVector::CrossProduct(B - A, C - A);
        const FVector Centroid = (A + B + C) / 3.0f;

        if (FVector::DotProduct(Normal, Centroid) <= 0.0f)
        {
            bWindingValid = false;
            break;
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[PTP] Winding consistency valid=%s"), bWindingValid ? TEXT("true") : TEXT("false"));
    bIsValid &= bWindingValid;

    UE_LOG(LogTemp, Log, TEXT("[PTP] Geometry validation result=%s"), bIsValid ? TEXT("true") : TEXT("false"));
    return bIsValid;
}
