#include "Resampling.h"

#include "AdjacencyBuilder.h"
#include "Async/ParallelFor.h"
#include "BoundaryDetection.h"
#include "CrustSample.h"
#include "FibonacciSphere.h"
#include "PlanetConstants.h"
#include "PlanetState.h"
#include "Plate.h"
#include "SphericalTriangulation.h"
#include "HAL/ThreadSafeCounter.h"

#include <algorithm>

namespace
{
constexpr int32 LeafTriangleCount = 8;
constexpr float TriangleInsideEpsilon = 1.0e-8f;
constexpr int32 GridResolution = 64;
constexpr int32 TargetNearestCandidates = 32;
constexpr int32 MaxSearchRing = GridResolution;
constexpr int32 FallbackNearestSampleCount = 6;
constexpr int32 FallbackCandidateCount = 128;
constexpr float GapThresholdSpacingFactor = 6.0f;
constexpr float GapThresholdMinKm = 220.0f;

enum class EPointClassification : uint8
{
    Normal,
    Gap,
    Overlap
};

struct FCachedFibonacciLattice
{
    int32 SampleCount = 0;
    TArray<FVector> Positions;
    TArray<int32> TriangleIndices;
    TArray<int32> AdjacencyOffsets;
    TArray<int32> AdjacencyNeighbors;
    bool bInitialized = false;
};

struct FPlateTriangle
{
    int32 A = INDEX_NONE;
    int32 B = INDEX_NONE;
    int32 C = INDEX_NONE;
    FVector Centroid = FVector::ZeroVector;
    FBox Bounds;
};

struct FBVHNode
{
    FBox Bounds;
    int32 LeftChild = INDEX_NONE;
    int32 RightChild = INDEX_NONE;
    int32 Start = 0;
    int32 Count = 0;

    bool IsLeaf() const
    {
        return LeftChild == INDEX_NONE && RightChild == INDEX_NONE;
    }
};

struct FPlateBVH
{
    int32 PlateIndex = INDEX_NONE;
    FBox RootBounds;
    TArray<FPlateTriangle> Triangles;
    TArray<int32> TriangleOrder;
    TArray<FBVHNode> Nodes;
    int32 RootNode = INDEX_NONE;
};

struct FPointHit
{
    int32 PlateIndex = INDEX_NONE;
    int32 TriangleIndex = INDEX_NONE;
};

struct FPointQueryResult
{
    EPointClassification Type = EPointClassification::Gap;
    FPointHit PrimaryHit;
    FPointHit SecondaryHit;
};

struct FSpatialGrid
{
    TArray<TArray<int32>> Cells;
};

FCachedFibonacciLattice GCachedLattice;

bool EnsureCachedLattice(const int32 SampleCount, FCachedFibonacciLattice& Cache)
{
    if (Cache.bInitialized && Cache.SampleCount == SampleCount)
    {
        return true;
    }

    TArray<FVector> Positions;
    GenerateFibonacciSphere(Positions, SampleCount);

    TArray<int32> TriangleIndices;
    if (!ComputeSphericalDelaunay(Positions, TriangleIndices))
    {
        return false;
    }

    TArray<int32> Offsets;
    TArray<int32> Neighbors;
    BuildAdjacencyCSR(SampleCount, TriangleIndices, Offsets, Neighbors);

    Cache.SampleCount = SampleCount;
    Cache.Positions = MoveTemp(Positions);
    Cache.TriangleIndices = MoveTemp(TriangleIndices);
    Cache.AdjacencyOffsets = MoveTemp(Offsets);
    Cache.AdjacencyNeighbors = MoveTemp(Neighbors);
    Cache.bInitialized = true;
    return true;
}

FORCEINLINE int32 CellCoord(const float Value)
{
    const float Normalized = FMath::Clamp((Value + 1.0f) * 0.5f, 0.0f, 0.999999f);
    return FMath::Clamp(static_cast<int32>(Normalized * static_cast<float>(GridResolution)), 0, GridResolution - 1);
}

FORCEINLINE int32 CellKey(const int32 X, const int32 Y, const int32 Z)
{
    return X + GridResolution * (Y + GridResolution * Z);
}

FSpatialGrid BuildSpatialGrid(const FPlanetState& State)
{
    FSpatialGrid Grid;
    Grid.Cells.SetNum(GridResolution * GridResolution * GridResolution);

    for (int32 Index = 0; Index < State.Samples.Num(); ++Index)
    {
        const FVector& P = State.Samples[Index].Position;
        const int32 Key = CellKey(CellCoord(P.X), CellCoord(P.Y), CellCoord(P.Z));
        Grid.Cells[Key].Add(Index);
    }

    return Grid;
}

void CollectNearestCandidates(
    const FVector& QueryPoint,
    const FSpatialGrid& Grid,
    TArray<int32>& OutCandidates,
    const int32 DesiredCount = TargetNearestCandidates)
{
    OutCandidates.Reset();
    const int32 CellX = CellCoord(QueryPoint.X);
    const int32 CellY = CellCoord(QueryPoint.Y);
    const int32 CellZ = CellCoord(QueryPoint.Z);

    for (int32 Ring = 0; Ring <= MaxSearchRing; ++Ring)
    {
        const int32 MinX = FMath::Max(0, CellX - Ring);
        const int32 MaxX = FMath::Min(GridResolution - 1, CellX + Ring);
        const int32 MinY = FMath::Max(0, CellY - Ring);
        const int32 MaxY = FMath::Min(GridResolution - 1, CellY + Ring);
        const int32 MinZ = FMath::Max(0, CellZ - Ring);
        const int32 MaxZ = FMath::Min(GridResolution - 1, CellZ + Ring);

        for (int32 X = MinX; X <= MaxX; ++X)
        {
            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 Z = MinZ; Z <= MaxZ; ++Z)
                {
                    if (Ring > 0
                        && X > MinX && X < MaxX
                        && Y > MinY && Y < MaxY
                        && Z > MinZ && Z < MaxZ)
                    {
                        continue;
                    }

                    const TArray<int32>& CellSamples = Grid.Cells[CellKey(X, Y, Z)];
                    OutCandidates.Append(CellSamples);
                }
            }
        }

        if (OutCandidates.Num() >= DesiredCount)
        {
            return;
        }
    }
}

void GatherNearestSamples(
    const FVector& QueryPoint,
    const FPlanetState& State,
    const FSpatialGrid& Grid,
    const int32 K,
    TArray<int32>& OutSortedNearest)
{
    OutSortedNearest.Reset();
    if (K <= 0 || State.Samples.IsEmpty())
    {
        return;
    }

    TArray<int32> Candidates;
    CollectNearestCandidates(QueryPoint, Grid, Candidates, FMath::Max(FallbackCandidateCount, K * 4));
    if (Candidates.IsEmpty())
    {
        return;
    }

    Candidates.Sort([&State, &QueryPoint](const int32 A, const int32 B)
    {
        if (!State.Samples.IsValidIndex(A))
        {
            return false;
        }
        if (!State.Samples.IsValidIndex(B))
        {
            return true;
        }

        const float DistA = FVector::DistSquared(QueryPoint, State.Samples[A].Position);
        const float DistB = FVector::DistSquared(QueryPoint, State.Samples[B].Position);
        return DistA < DistB;
    });

    OutSortedNearest.Reserve(K);
    for (const int32 Candidate : Candidates)
    {
        if (State.Samples.IsValidIndex(Candidate))
        {
            OutSortedNearest.Add(Candidate);
            if (OutSortedNearest.Num() >= K)
            {
                break;
            }
        }
    }
}

void FindNearestSamplesDistinctPlates(
    const FVector& QueryPoint,
    const FPlanetState& State,
    const FSpatialGrid& Grid,
    int32& OutNearestA,
    int32& OutNearestB)
{
    OutNearestA = INDEX_NONE;
    OutNearestB = INDEX_NONE;

    TArray<int32> Candidates;
    CollectNearestCandidates(QueryPoint, Grid, Candidates, FallbackCandidateCount);

    double BestA = MAX_dbl;
    double BestB = MAX_dbl;

    for (const int32 SampleIndex : Candidates)
    {
        if (!State.Samples.IsValidIndex(SampleIndex))
        {
            continue;
        }

        const FCrustSample& Sample = State.Samples[SampleIndex];
        if (!State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            continue;
        }

        const double DistSq = FVector::DistSquared(QueryPoint, Sample.Position);
        if (DistSq < BestA)
        {
            if (OutNearestA != INDEX_NONE
                && State.Samples[OutNearestA].PlateIndex != Sample.PlateIndex
                && BestA < BestB)
            {
                OutNearestB = OutNearestA;
                BestB = BestA;
            }

            OutNearestA = SampleIndex;
            BestA = DistSq;
        }
        else if (State.Samples.IsValidIndex(OutNearestA)
            && Sample.PlateIndex != State.Samples[OutNearestA].PlateIndex
            && DistSq < BestB)
        {
            OutNearestB = SampleIndex;
            BestB = DistSq;
        }
    }

    if (OutNearestA == INDEX_NONE && !Candidates.IsEmpty())
    {
        OutNearestA = Candidates[0];
    }
}

float ResampleGreatCircleDistanceKm(const FVector& A, const FVector& B)
{
    const double Dot = FMath::Clamp(static_cast<double>(FVector::DotProduct(A, B)), -1.0, 1.0);
    return static_cast<float>(FMath::Acos(Dot) * static_cast<double>(PTP::Radius));
}

bool PointInSphericalTriangle(const FVector& P, const FVector& V1, const FVector& V2, const FVector& V3)
{
    const double D1 = FVector::DotProduct(V1, FVector::CrossProduct(V2, P));
    const double D2 = FVector::DotProduct(V2, FVector::CrossProduct(V3, P));
    const double D3 = FVector::DotProduct(V3, FVector::CrossProduct(V1, P));

    const bool bAllPos = D1 >= -TriangleInsideEpsilon && D2 >= -TriangleInsideEpsilon && D3 >= -TriangleInsideEpsilon;
    const bool bAllNeg = D1 <= TriangleInsideEpsilon && D2 <= TriangleInsideEpsilon && D3 <= TriangleInsideEpsilon;
    return bAllPos || bAllNeg;
}

FVector ComputeBarycentricWeights(const FVector& P, const FVector& V1, const FVector& V2, const FVector& V3)
{
    const double Det123 = FVector::DotProduct(V1, FVector::CrossProduct(V2, V3));
    if (FMath::Abs(Det123) < 1.0e-12)
    {
        return FVector(1.0f / 3.0f);
    }

    double W1 = FVector::DotProduct(P, FVector::CrossProduct(V2, V3)) / Det123;
    double W2 = FVector::DotProduct(P, FVector::CrossProduct(V3, V1)) / Det123;
    double W3 = 1.0 - W1 - W2;

    W1 = FMath::Max(0.0, W1);
    W2 = FMath::Max(0.0, W2);
    W3 = FMath::Max(0.0, W3);
    const double Sum = FMath::Max(1.0e-12, W1 + W2 + W3);

    return FVector(static_cast<float>(W1 / Sum), static_cast<float>(W2 / Sum), static_cast<float>(W3 / Sum));
}

FVector ResampleProjectToTangent(const FVector& V, const FVector& SurfaceNormal)
{
    return V - SurfaceNormal * FVector::DotProduct(V, SurfaceNormal);
}

int32 BuildBVHNode(FPlateBVH& BVH, const int32 Start, const int32 Count)
{
    const int32 NodeIndex = BVH.Nodes.AddDefaulted();
    FBVHNode& Node = BVH.Nodes[NodeIndex];
    Node.Start = Start;
    Node.Count = Count;

    FBox Bounds(EForceInit::ForceInitToZero);
    bool bBoundsValid = false;
    for (int32 LocalIndex = Start; LocalIndex < Start + Count; ++LocalIndex)
    {
        const FPlateTriangle& Tri = BVH.Triangles[BVH.TriangleOrder[LocalIndex]];
        if (!bBoundsValid)
        {
            Bounds = Tri.Bounds;
            bBoundsValid = true;
        }
        else
        {
            Bounds += Tri.Bounds;
        }
    }
    Node.Bounds = Bounds;

    if (Count <= LeafTriangleCount)
    {
        return NodeIndex;
    }

    FBox CentroidBounds(EForceInit::ForceInitToZero);
    bool bCentroidBoundsValid = false;
    for (int32 LocalIndex = Start; LocalIndex < Start + Count; ++LocalIndex)
    {
        const FVector& C = BVH.Triangles[BVH.TriangleOrder[LocalIndex]].Centroid;
        if (!bCentroidBoundsValid)
        {
            CentroidBounds = FBox(C, C);
            bCentroidBoundsValid = true;
        }
        else
        {
            CentroidBounds += C;
        }
    }

    const FVector Extent = CentroidBounds.GetSize();
    int32 Axis = 0;
    if (Extent.Y > Extent.X && Extent.Y >= Extent.Z)
    {
        Axis = 1;
    }
    else if (Extent.Z > Extent.X && Extent.Z >= Extent.Y)
    {
        Axis = 2;
    }

    const int32 Mid = Start + Count / 2;
    std::sort(BVH.TriangleOrder.GetData() + Start, BVH.TriangleOrder.GetData() + Start + Count, [&BVH, Axis](const int32 A, const int32 B)
    {
        return BVH.Triangles[A].Centroid[Axis] < BVH.Triangles[B].Centroid[Axis];
    });

    Node.LeftChild = BuildBVHNode(BVH, Start, Mid - Start);
    Node.RightChild = BuildBVHNode(BVH, Mid, Start + Count - Mid);
    return NodeIndex;
}

bool BuildPlateBVH(const FPlanetState& State, const int32 PlateIndex, FPlateBVH& OutBVH)
{
    OutBVH = FPlateBVH();
    OutBVH.PlateIndex = PlateIndex;

    for (int32 TriBase = 0; TriBase + 2 < State.TriangleIndices.Num(); TriBase += 3)
    {
        const int32 A = State.TriangleIndices[TriBase];
        const int32 B = State.TriangleIndices[TriBase + 1];
        const int32 C = State.TriangleIndices[TriBase + 2];
        if (!State.Samples.IsValidIndex(A) || !State.Samples.IsValidIndex(B) || !State.Samples.IsValidIndex(C))
        {
            continue;
        }

        if (State.Samples[A].PlateIndex != PlateIndex
            || State.Samples[B].PlateIndex != PlateIndex
            || State.Samples[C].PlateIndex != PlateIndex)
        {
            continue;
        }

        FPlateTriangle Tri;
        Tri.A = A;
        Tri.B = B;
        Tri.C = C;
        Tri.Centroid = (State.Samples[A].Position + State.Samples[B].Position + State.Samples[C].Position).GetSafeNormal();
        Tri.Bounds = FBox(State.Samples[A].Position, State.Samples[A].Position);
        Tri.Bounds += State.Samples[B].Position;
        Tri.Bounds += State.Samples[C].Position;
        OutBVH.Triangles.Add(Tri);
    }

    if (OutBVH.Triangles.IsEmpty())
    {
        return false;
    }

    OutBVH.TriangleOrder.Reserve(OutBVH.Triangles.Num());
    OutBVH.RootBounds = FBox(EForceInit::ForceInitToZero);
    bool bRootValid = false;
    for (int32 TriIndex = 0; TriIndex < OutBVH.Triangles.Num(); ++TriIndex)
    {
        OutBVH.TriangleOrder.Add(TriIndex);
        if (!bRootValid)
        {
            OutBVH.RootBounds = OutBVH.Triangles[TriIndex].Bounds;
            bRootValid = true;
        }
        else
        {
            OutBVH.RootBounds += OutBVH.Triangles[TriIndex].Bounds;
        }
    }

    OutBVH.Nodes.Reserve(OutBVH.Triangles.Num() * 2);
    OutBVH.RootNode = BuildBVHNode(OutBVH, 0, OutBVH.TriangleOrder.Num());
    return true;
}

bool BoxContainsPoint(const FBox& Box, const FVector& P)
{
    return P.X >= Box.Min.X && P.X <= Box.Max.X
        && P.Y >= Box.Min.Y && P.Y <= Box.Max.Y
        && P.Z >= Box.Min.Z && P.Z <= Box.Max.Z;
}

bool FindContainingTriangle(const FPlateBVH& BVH, const FPlanetState& State, const FVector& P, int32& OutTriangleIndex)
{
    OutTriangleIndex = INDEX_NONE;
    if (BVH.RootNode == INDEX_NONE || !BoxContainsPoint(BVH.RootBounds, P))
    {
        return false;
    }

    TArray<int32, TInlineAllocator<64>> Stack;
    Stack.Add(BVH.RootNode);

    while (!Stack.IsEmpty())
    {
        const int32 NodeIndex = Stack.Pop(EAllowShrinking::No);
        const FBVHNode& Node = BVH.Nodes[NodeIndex];
        if (!BoxContainsPoint(Node.Bounds, P))
        {
            continue;
        }

        if (Node.IsLeaf())
        {
            for (int32 Offset = 0; Offset < Node.Count; ++Offset)
            {
                const int32 TriIndex = BVH.TriangleOrder[Node.Start + Offset];
                const FPlateTriangle& Tri = BVH.Triangles[TriIndex];
                const FVector& V1 = State.Samples[Tri.A].Position;
                const FVector& V2 = State.Samples[Tri.B].Position;
                const FVector& V3 = State.Samples[Tri.C].Position;
                if (PointInSphericalTriangle(P, V1, V2, V3))
                {
                    OutTriangleIndex = TriIndex;
                    return true;
                }
            }
        }
        else
        {
            if (Node.LeftChild != INDEX_NONE)
            {
                Stack.Add(Node.LeftChild);
            }
            if (Node.RightChild != INDEX_NONE)
            {
                Stack.Add(Node.RightChild);
            }
        }
    }

    return false;
}

FCrustSample InterpolateFromTriangle(
    const FPlanetState& State,
    const FPlateBVH& BVH,
    const int32 TriangleIndex,
    const FVector& Position)
{
    FCrustSample Out;
    Out.Position = Position;
    Out.DistToFront = TNumericLimits<float>::Max();

    if (!BVH.Triangles.IsValidIndex(TriangleIndex))
    {
        return Out;
    }

    const FPlateTriangle& Tri = BVH.Triangles[TriangleIndex];
    const FCrustSample& S1 = State.Samples[Tri.A];
    const FCrustSample& S2 = State.Samples[Tri.B];
    const FCrustSample& S3 = State.Samples[Tri.C];

    const FVector W = ComputeBarycentricWeights(Position, S1.Position, S2.Position, S3.Position);
    const float W1 = W.X;
    const float W2 = W.Y;
    const float W3 = W.Z;

    const int32 MaxWeightVertex = (W1 >= W2 && W1 >= W3) ? 0 : ((W2 >= W3) ? 1 : 2);
    const FCrustSample* MajoritySample = (MaxWeightVertex == 0) ? &S1 : (MaxWeightVertex == 1 ? &S2 : &S3);

    Out.Elevation = W1 * S1.Elevation + W2 * S2.Elevation + W3 * S3.Elevation;
    Out.Thickness = W1 * S1.Thickness + W2 * S2.Thickness + W3 * S3.Thickness;
    Out.OceanicAge = W1 * S1.OceanicAge + W2 * S2.OceanicAge + W3 * S3.OceanicAge;
    Out.OrogenyAge = W1 * S1.OrogenyAge + W2 * S2.OrogenyAge + W3 * S3.OrogenyAge;

    Out.CrustType = MajoritySample->CrustType;
    Out.OrogenyType = MajoritySample->OrogenyType;
    Out.PlateIndex = BVH.PlateIndex;

    FVector Ridge = W1 * S1.RidgeDirection + W2 * S2.RidgeDirection + W3 * S3.RidgeDirection;
    Ridge = ResampleProjectToTangent(Ridge, Position);
    Out.RidgeDirection = Ridge;

    FVector Fold = W1 * S1.FoldDirection + W2 * S2.FoldDirection + W3 * S3.FoldDirection;
    Fold = ResampleProjectToTangent(Fold, Position);
    Out.FoldDirection = Fold;

    return Out;
}

FCrustSample ResolveOverlapSample(const FCrustSample& A, const FCrustSample& B)
{
    auto ChooseByElevation = [&A, &B]() -> FCrustSample
    {
        return (A.Elevation >= B.Elevation) ? A : B;
    };

    if (A.CrustType == ECrustType::Continental && B.CrustType == ECrustType::Oceanic)
    {
        return A;
    }

    if (A.CrustType == ECrustType::Oceanic && B.CrustType == ECrustType::Continental)
    {
        return B;
    }

    if (A.CrustType == ECrustType::Oceanic && B.CrustType == ECrustType::Oceanic)
    {
        return (A.OceanicAge <= B.OceanicAge) ? A : B;
    }

    return ChooseByElevation();
}

FCrustSample BuildGapSample(
    const FVector& Position,
    const FPlanetState& OldState,
    const FSpatialGrid& Grid)
{
    FCrustSample Out;
    Out.Position = Position;
    Out.CrustType = ECrustType::Oceanic;
    Out.Thickness = 7.0f;
    Out.OceanicAge = 0.0f;
    Out.OrogenyType = EOrogenyType::None;
    Out.OrogenyAge = 0.0f;
    Out.DistToFront = TNumericLimits<float>::Max();
    Out.FoldDirection = FVector::ZeroVector;

    int32 NearestA = INDEX_NONE;
    int32 NearestB = INDEX_NONE;
    FindNearestSamplesDistinctPlates(Position, OldState, Grid, NearestA, NearestB);

    if (!OldState.Samples.IsValidIndex(NearestA))
    {
        Out.Elevation = PTP::AbyssalElevation;
        Out.PlateIndex = 0;
        Out.RidgeDirection = FVector::ZeroVector;
        return Out;
    }

    const FCrustSample& SA = OldState.Samples[NearestA];
    Out.PlateIndex = FMath::Max(0, SA.PlateIndex);

    if (!OldState.Samples.IsValidIndex(NearestB))
    {
        Out.Elevation = PTP::AbyssalElevation;
        const FVector Tangent = FVector::CrossProduct(Position, FVector::UpVector).GetSafeNormal();
        Out.RidgeDirection = Tangent;
        return Out;
    }

    const FCrustSample& SB = OldState.Samples[NearestB];
    const float DA = ResampleGreatCircleDistanceKm(Position, SA.Position);
    const float DB = ResampleGreatCircleDistanceKm(Position, SB.Position);
    const float DRidge = 0.5f * FMath::Abs(DA - DB);
    const float DPlate = FMath::Min(DA, DB);
    const float Alpha = DRidge / FMath::Max(1.0f, DRidge + DPlate);
    const float BorderElevation = 0.5f * (SA.Elevation + SB.Elevation);

    const float HalfGapWidth = FMath::Max(1.0f, 0.5f * (DA + DB));
    const float T = FMath::Clamp(DRidge / HalfGapWidth, 0.0f, 1.0f);
    const float RidgeElevation = PTP::RidgeElevation + (PTP::AbyssalElevation - PTP::RidgeElevation) * FMath::Sqrt(T);
    Out.Elevation = Alpha * BorderElevation + (1.0f - Alpha) * RidgeElevation;
    Out.Elevation = FMath::Clamp(Out.Elevation, PTP::AbyssalElevation, PTP::RidgeElevation);

    Out.PlateIndex = (DA <= DB) ? SA.PlateIndex : SB.PlateIndex;
    if (!OldState.Plates.IsValidIndex(Out.PlateIndex))
    {
        Out.PlateIndex = FMath::Clamp(SA.PlateIndex, 0, OldState.Plates.Num() - 1);
    }

    FVector GapDir = (SB.Position - SA.Position).GetSafeNormal();
    GapDir = ResampleProjectToTangent(GapDir, Position).GetSafeNormal();
    Out.RidgeDirection = FVector::CrossProduct(Position, GapDir).GetSafeNormal();
    return Out;
}

float ComputeAverageSampleSpacingKm(const int32 NumSamples)
{
    const double AreaPerSample = (4.0 * PI * static_cast<double>(PTP::Radius) * static_cast<double>(PTP::Radius))
        / static_cast<double>(FMath::Max(1, NumSamples));
    return static_cast<float>(FMath::Sqrt(AreaPerSample));
}

bool BuildFallbackSampleFromNearest(
    const FVector& Position,
    const FPlanetState& OldState,
    const FSpatialGrid& Grid,
    const float GapDistanceThresholdKm,
    FCrustSample& OutSample)
{
    TArray<int32> Nearest;
    GatherNearestSamples(Position, OldState, Grid, FallbackCandidateCount, Nearest);
    if (Nearest.IsEmpty())
    {
        return false;
    }

    const int32 NearestIndex = Nearest[0];
    if (!OldState.Samples.IsValidIndex(NearestIndex))
    {
        return false;
    }

    const FCrustSample& NearestSample = OldState.Samples[NearestIndex];
    if (!OldState.Plates.IsValidIndex(NearestSample.PlateIndex))
    {
        return false;
    }

    const float NearestDistanceKm = ResampleGreatCircleDistanceKm(Position, NearestSample.Position);
    if (NearestDistanceKm > GapDistanceThresholdKm)
    {
        return false;
    }

    TArray<int32, TInlineAllocator<FallbackNearestSampleCount>> SamePlate;
    SamePlate.Reserve(FallbackNearestSampleCount);
    for (const int32 CandidateIndex : Nearest)
    {
        if (!OldState.Samples.IsValidIndex(CandidateIndex))
        {
            continue;
        }

        if (OldState.Samples[CandidateIndex].PlateIndex == NearestSample.PlateIndex)
        {
            SamePlate.Add(CandidateIndex);
            if (SamePlate.Num() >= FallbackNearestSampleCount)
            {
                break;
            }
        }
    }

    if (SamePlate.IsEmpty())
    {
        return false;
    }

    double WeightSum = 0.0;
    double ElevationSum = 0.0;
    double ThicknessSum = 0.0;
    double OceanicAgeSum = 0.0;
    double OrogenyAgeSum = 0.0;
    FVector RidgeSum = FVector::ZeroVector;
    FVector FoldSum = FVector::ZeroVector;

    for (const int32 SampleIndex : SamePlate)
    {
        const FCrustSample& Source = OldState.Samples[SampleIndex];
        const float DistKm = ResampleGreatCircleDistanceKm(Position, Source.Position);
        const double Weight = 1.0 / FMath::Max(1.0e-3, static_cast<double>(DistKm));
        WeightSum += Weight;
        ElevationSum += static_cast<double>(Source.Elevation) * Weight;
        ThicknessSum += static_cast<double>(Source.Thickness) * Weight;
        OceanicAgeSum += static_cast<double>(Source.OceanicAge) * Weight;
        OrogenyAgeSum += static_cast<double>(Source.OrogenyAge) * Weight;
        RidgeSum += Source.RidgeDirection * static_cast<float>(Weight);
        FoldSum += Source.FoldDirection * static_cast<float>(Weight);
    }

    const double SafeWeightSum = FMath::Max(1.0e-8, WeightSum);
    const float InvWeightSum = static_cast<float>(1.0 / SafeWeightSum);

    OutSample = FCrustSample();
    OutSample.Position = Position;
    OutSample.PlateIndex = NearestSample.PlateIndex;
    OutSample.CrustType = NearestSample.CrustType;
    OutSample.OrogenyType = NearestSample.OrogenyType;
    OutSample.Elevation = static_cast<float>(ElevationSum / SafeWeightSum);
    OutSample.Thickness = static_cast<float>(ThicknessSum / SafeWeightSum);
    OutSample.OceanicAge = static_cast<float>(OceanicAgeSum / SafeWeightSum);
    OutSample.OrogenyAge = static_cast<float>(OrogenyAgeSum / SafeWeightSum);
    OutSample.RidgeDirection = ResampleProjectToTangent(RidgeSum * InvWeightSum, Position);
    OutSample.FoldDirection = ResampleProjectToTangent(FoldSum * InvWeightSum, Position);
    OutSample.DistToFront = TNumericLimits<float>::Max();
    return true;
}

void RebuildPlateMetadata(FPlanetState& State)
{
    State.NumPlates = State.Plates.Num();
    TArray<int32> PlateSamples;
    TArray<int32> ContinentalCounts;
    PlateSamples.Init(0, State.Plates.Num());
    ContinentalCounts.Init(0, State.Plates.Num());

    for (FPlate& Plate : State.Plates)
    {
        Plate.Area = 0.0f;
        Plate.SeedSampleIndex = INDEX_NONE;
        Plate.BoundarySamples.Reset();
    }

    for (int32 SampleIndex = 0; SampleIndex < State.Samples.Num(); ++SampleIndex)
    {
        FCrustSample& Sample = State.Samples[SampleIndex];
        if (!State.Plates.IsValidIndex(Sample.PlateIndex))
        {
            Sample.PlateIndex = 0;
        }

        ++PlateSamples[Sample.PlateIndex];
        if (Sample.CrustType == ECrustType::Continental)
        {
            ++ContinentalCounts[Sample.PlateIndex];
        }
        if (State.Plates[Sample.PlateIndex].SeedSampleIndex == INDEX_NONE)
        {
            State.Plates[Sample.PlateIndex].SeedSampleIndex = SampleIndex;
        }
    }

    const float InvSampleCount = State.Samples.IsEmpty() ? 0.0f : 1.0f / static_cast<float>(State.Samples.Num());
    for (int32 PlateIndex = 0; PlateIndex < State.Plates.Num(); ++PlateIndex)
    {
        FPlate& Plate = State.Plates[PlateIndex];
        Plate.Area = static_cast<float>(PlateSamples[PlateIndex]) * InvSampleCount;
        const int32 Oceanic = PlateSamples[PlateIndex] - ContinentalCounts[PlateIndex];
        Plate.CrustType = (ContinentalCounts[PlateIndex] >= Oceanic) ? ECrustType::Continental : ECrustType::Oceanic;
    }
}
}

bool GlobalResample(FPlanetState& State, FGlobalResampleStats* OutStats)
{
    if (State.Samples.IsEmpty() || State.Plates.IsEmpty() || State.SampleCount != State.Samples.Num())
    {
        return false;
    }

    const double StartTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Log, TEXT("[PTP] === GLOBAL RESAMPLE at t=%.2f My ==="), State.Time);

    if (!EnsureCachedLattice(State.SampleCount, GCachedLattice))
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] GlobalResample failed: unable to build cached Fibonacci lattice."));
        return false;
    }

    const FPlanetState& OldState = State;
    const FSpatialGrid SpatialGrid = BuildSpatialGrid(OldState);

    TArray<FPlateBVH> PlateBVHs;
    PlateBVHs.SetNum(OldState.Plates.Num());

    const double BVHStart = FPlatformTime::Seconds();
    ParallelFor(OldState.Plates.Num(), [&OldState, &PlateBVHs](const int32 PlateIndex)
    {
        BuildPlateBVH(OldState, PlateIndex, PlateBVHs[PlateIndex]);
    });
    const double BVHMs = (FPlatformTime::Seconds() - BVHStart) * 1000.0;

    TArray<FPointQueryResult> QueryResults;
    QueryResults.SetNum(OldState.SampleCount);

    const double QueryStart = FPlatformTime::Seconds();
    ParallelFor(OldState.SampleCount, [&OldState, &PlateBVHs, &QueryResults](const int32 SampleIndex)
    {
        const FVector& P = GCachedLattice.Positions[SampleIndex];
        FPointQueryResult Result;

        int32 HitCount = 0;
        for (const FPlateBVH& BVH : PlateBVHs)
        {
            if (BVH.RootNode == INDEX_NONE)
            {
                continue;
            }

            int32 TriIndex = INDEX_NONE;
            if (FindContainingTriangle(BVH, OldState, P, TriIndex))
            {
                if (HitCount == 0)
                {
                    Result.PrimaryHit = FPointHit{BVH.PlateIndex, TriIndex};
                }
                else if (HitCount == 1)
                {
                    Result.SecondaryHit = FPointHit{BVH.PlateIndex, TriIndex};
                }
                ++HitCount;
            }
        }

        if (HitCount == 0)
        {
            Result.Type = EPointClassification::Gap;
        }
        else if (HitCount == 1)
        {
            Result.Type = EPointClassification::Normal;
        }
        else
        {
            Result.Type = EPointClassification::Overlap;
        }

        QueryResults[SampleIndex] = Result;
    });
    const double QueryMs = (FPlatformTime::Seconds() - QueryStart) * 1000.0;

    TArray<FCrustSample> NewSamples;
    NewSamples.SetNum(OldState.SampleCount);

    FThreadSafeCounter GapCount;
    FThreadSafeCounter OverlapCount;
    FThreadSafeCounter NormalCount;
    FThreadSafeCounter FallbackCount;
    const float GapDistanceThresholdKm = FMath::Max(
        GapThresholdMinKm,
        GapThresholdSpacingFactor * ComputeAverageSampleSpacingKm(OldState.SampleCount));

    const double TransferStart = FPlatformTime::Seconds();
    ParallelFor(OldState.SampleCount, [&OldState, &PlateBVHs, &SpatialGrid, &QueryResults, &NewSamples, &GapCount, &OverlapCount, &NormalCount, &FallbackCount, GapDistanceThresholdKm](const int32 SampleIndex)
    {
        const FVector Position = GCachedLattice.Positions[SampleIndex];
        const FPointQueryResult& Query = QueryResults[SampleIndex];

        if (Query.Type == EPointClassification::Normal)
        {
            if (PlateBVHs.IsValidIndex(Query.PrimaryHit.PlateIndex))
            {
                NewSamples[SampleIndex] = InterpolateFromTriangle(
                    OldState,
                    PlateBVHs[Query.PrimaryHit.PlateIndex],
                    Query.PrimaryHit.TriangleIndex,
                    Position);
                NormalCount.Increment();
                return;
            }
        }
        else if (Query.Type == EPointClassification::Overlap)
        {
            FCrustSample FallbackSample;
            if (BuildFallbackSampleFromNearest(Position, OldState, SpatialGrid, GapDistanceThresholdKm, FallbackSample))
            {
                NewSamples[SampleIndex] = FallbackSample;
                NormalCount.Increment();
                FallbackCount.Increment();
                return;
            }

            if (PlateBVHs.IsValidIndex(Query.PrimaryHit.PlateIndex) && PlateBVHs.IsValidIndex(Query.SecondaryHit.PlateIndex))
            {
                const FCrustSample A = InterpolateFromTriangle(
                    OldState,
                    PlateBVHs[Query.PrimaryHit.PlateIndex],
                    Query.PrimaryHit.TriangleIndex,
                    Position);
                const FCrustSample B = InterpolateFromTriangle(
                    OldState,
                    PlateBVHs[Query.SecondaryHit.PlateIndex],
                    Query.SecondaryHit.TriangleIndex,
                    Position);
                NewSamples[SampleIndex] = ResolveOverlapSample(A, B);
                OverlapCount.Increment();
                return;
            }
        }

        FCrustSample FallbackSample;
        if (BuildFallbackSampleFromNearest(Position, OldState, SpatialGrid, GapDistanceThresholdKm, FallbackSample))
        {
            NewSamples[SampleIndex] = FallbackSample;
            NormalCount.Increment();
            FallbackCount.Increment();
            return;
        }

        NewSamples[SampleIndex] = BuildGapSample(Position, OldState, SpatialGrid);
        GapCount.Increment();
    });
    const double TransferMs = (FPlatformTime::Seconds() - TransferStart) * 1000.0;

    State.Samples = MoveTemp(NewSamples);
    State.SampleCount = State.Samples.Num();
    State.TriangleIndices = GCachedLattice.TriangleIndices;
    State.AdjacencyOffsets = GCachedLattice.AdjacencyOffsets;
    State.AdjacencyNeighbors = GCachedLattice.AdjacencyNeighbors;

    RebuildPlateMetadata(State);

    const double RebuildStart = FPlatformTime::Seconds();
    DetectAndClassifyBoundaries(State);
    const double RebuildMs = (FPlatformTime::Seconds() - RebuildStart) * 1000.0;
    const double TotalMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    if (OutStats)
    {
        OutStats->NormalCount = NormalCount.GetValue();
        OutStats->FallbackCount = FallbackCount.GetValue();
        OutStats->GapCount = GapCount.GetValue();
        OutStats->OverlapCount = OverlapCount.GetValue();
        OutStats->BvhBuildMs = BVHMs;
        OutStats->QueryMs = QueryMs;
        OutStats->TransferMs = TransferMs;
        OutStats->RebuildMs = RebuildMs;
        OutStats->TotalMs = TotalMs;
    }

    UE_LOG(LogTemp, Log, TEXT("[PTP] Resample BVH: %.1f ms (%d plates)"), BVHMs, State.Plates.Num());
    UE_LOG(LogTemp, Log, TEXT("[PTP] Resample queries: %.1f ms (%d points)"), QueryMs, State.SampleCount);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Resample classification: %d normal (%d fallback), %d gap, %d overlap"),
        NormalCount.GetValue(), FallbackCount.GetValue(), GapCount.GetValue(), OverlapCount.GetValue());
    UE_LOG(LogTemp, Log, TEXT("[PTP] Resample transfer: %.1f ms"), TransferMs);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Resample rebuild+boundaries: %.1f ms"), RebuildMs);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Total resample: %.1f ms"), TotalMs);

    return true;
}
