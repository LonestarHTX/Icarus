#include "Resampling.h"

#include "AdjacencyBuilder.h"
#include "Async/ParallelFor.h"
#include "BoundaryDetection.h"
#include "BoundaryTypes.h"
#include "CrustSample.h"
#include "FibonacciSphere.h"
#include "HAL/ThreadSafeCounter.h"
#include "PlanetConstants.h"
#include "PlanetState.h"
#include "Plate.h"
#include "SphericalTriangulation.h"

#include <algorithm>

namespace
{
constexpr int32 LeafTriangleCount = 8;
constexpr float TriangleInsideEpsilon = 1.0e-8f;
constexpr int32 GridResolution = 64;
constexpr int32 TargetNearestCandidates = 128;
constexpr int32 MaxSearchRing = GridResolution;
constexpr float GapEdgeThresholdMinKm = 150.0f;
constexpr float GapEdgeThresholdSpacingFactor = 6.0f;

struct FCachedFibonacciLattice
{
    int32 SampleCount = 0;
    TArray<FVector> Positions;
    TArray<int32> TriangleIndices;
    TArray<int32> AdjacencyOffsets;
    TArray<int32> AdjacencyNeighbors;
    bool bInitialized = false;
};

struct FGlobalTriangle
{
    int32 A = INDEX_NONE;
    int32 B = INDEX_NONE;
    int32 C = INDEX_NONE;
    int32 PlateA = INDEX_NONE;
    int32 PlateB = INDEX_NONE;
    int32 PlateC = INDEX_NONE;
    bool bSamePlate = false;
    int32 DivergentBoundaryVertices = 0;
    float MaxEdgeLengthKm = 0.0f;
    float MinEdgeLengthKm = 0.0f;
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

struct FGlobalBVH
{
    TArray<FGlobalTriangle> Triangles;
    TArray<int32> TriangleOrder;
    TArray<FBVHNode> Nodes;
    int32 RootNode = INDEX_NONE;
    FBox RootBounds;
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
        Grid.Cells[CellKey(CellCoord(P.X), CellCoord(P.Y), CellCoord(P.Z))].Add(Index);
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

                    OutCandidates.Append(Grid.Cells[CellKey(X, Y, Z)]);
                }
            }
        }

        if (OutCandidates.Num() >= DesiredCount)
        {
            return;
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
    CollectNearestCandidates(QueryPoint, Grid, Candidates);

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

float ComputeAverageSampleSpacingKm(const int32 NumSamples)
{
    const double AreaPerSample = (4.0 * PI * static_cast<double>(PTP::Radius) * static_cast<double>(PTP::Radius))
        / static_cast<double>(FMath::Max(1, NumSamples));
    return static_cast<float>(FMath::Sqrt(AreaPerSample));
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

int32 BuildBVHNode(FGlobalBVH& BVH, const int32 Start, const int32 Count)
{
    const int32 NodeIndex = BVH.Nodes.AddDefaulted();
    FBVHNode& Node = BVH.Nodes[NodeIndex];
    Node.Start = Start;
    Node.Count = Count;

    bool bBoundsValid = false;
    FBox Bounds(EForceInit::ForceInitToZero);
    for (int32 LocalIndex = Start; LocalIndex < Start + Count; ++LocalIndex)
    {
        const FGlobalTriangle& Tri = BVH.Triangles[BVH.TriangleOrder[LocalIndex]];
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

    bool bCentroidBoundsValid = false;
    FBox CentroidBounds(EForceInit::ForceInitToZero);
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

bool BuildGlobalBVH(const FPlanetState& State, FGlobalBVH& OutBVH)
{
    OutBVH = FGlobalBVH();

    OutBVH.Triangles.Reserve(State.TriangleIndices.Num() / 3);
    for (int32 TriBase = 0; TriBase + 2 < State.TriangleIndices.Num(); TriBase += 3)
    {
        const int32 A = State.TriangleIndices[TriBase];
        const int32 B = State.TriangleIndices[TriBase + 1];
        const int32 C = State.TriangleIndices[TriBase + 2];
        if (!State.Samples.IsValidIndex(A) || !State.Samples.IsValidIndex(B) || !State.Samples.IsValidIndex(C))
        {
            continue;
        }

        FGlobalTriangle Tri;
        Tri.A = A;
        Tri.B = B;
        Tri.C = C;
        Tri.PlateA = State.Samples[A].PlateIndex;
        Tri.PlateB = State.Samples[B].PlateIndex;
        Tri.PlateC = State.Samples[C].PlateIndex;
        Tri.bSamePlate = (Tri.PlateA == Tri.PlateB) && (Tri.PlateB == Tri.PlateC);
        if (State.SampleBoundaryInfo.IsValidIndex(A)
            && State.SampleBoundaryInfo[A].BoundaryType == EPTPBoundaryType::Divergent)
        {
            ++Tri.DivergentBoundaryVertices;
        }
        if (State.SampleBoundaryInfo.IsValidIndex(B)
            && State.SampleBoundaryInfo[B].BoundaryType == EPTPBoundaryType::Divergent)
        {
            ++Tri.DivergentBoundaryVertices;
        }
        if (State.SampleBoundaryInfo.IsValidIndex(C)
            && State.SampleBoundaryInfo[C].BoundaryType == EPTPBoundaryType::Divergent)
        {
            ++Tri.DivergentBoundaryVertices;
        }
        const FVector& VA = State.Samples[A].Position;
        const FVector& VB = State.Samples[B].Position;
        const FVector& VC = State.Samples[C].Position;

        const float EdgeAB = ResampleGreatCircleDistanceKm(VA, VB);
        const float EdgeBC = ResampleGreatCircleDistanceKm(VB, VC);
        const float EdgeCA = ResampleGreatCircleDistanceKm(VC, VA);
        Tri.MaxEdgeLengthKm = FMath::Max3(EdgeAB, EdgeBC, EdgeCA);
        Tri.MinEdgeLengthKm = FMath::Min3(EdgeAB, EdgeBC, EdgeCA);

        Tri.Centroid = (VA + VB + VC).GetSafeNormal();
        Tri.Bounds = FBox(VA, VA);
        Tri.Bounds += VB;
        Tri.Bounds += VC;

        OutBVH.Triangles.Add(Tri);
    }

    if (OutBVH.Triangles.IsEmpty())
    {
        return false;
    }

    OutBVH.TriangleOrder.Reserve(OutBVH.Triangles.Num());
    bool bRootValid = false;
    OutBVH.RootBounds = FBox(EForceInit::ForceInitToZero);
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

bool FindContainingTriangle(const FGlobalBVH& BVH, const FPlanetState& State, const FVector& P, int32& OutTriangleIndex)
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
                const FGlobalTriangle& Tri = BVH.Triangles[TriIndex];

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

int32 ResolveWinningPlate(const FGlobalTriangle& Tri, const FVector& Weights)
{
    if (Tri.PlateA == Tri.PlateB)
    {
        return Tri.PlateA;
    }
    if (Tri.PlateA == Tri.PlateC)
    {
        return Tri.PlateA;
    }
    if (Tri.PlateB == Tri.PlateC)
    {
        return Tri.PlateB;
    }

    if (Weights.X >= Weights.Y && Weights.X >= Weights.Z)
    {
        return Tri.PlateA;
    }
    if (Weights.Y >= Weights.Z)
    {
        return Tri.PlateB;
    }
    return Tri.PlateC;
}

FCrustSample InterpolateFromTriangle(
    const FPlanetState& State,
    const FGlobalTriangle& Tri,
    const FVector& Position,
    const FVector& Weights,
    const int32 AssignedPlateIndex)
{
    FCrustSample Out;
    Out.Position = Position;
    Out.DistToFront = TNumericLimits<float>::Max();

    const FCrustSample& S1 = State.Samples[Tri.A];
    const FCrustSample& S2 = State.Samples[Tri.B];
    const FCrustSample& S3 = State.Samples[Tri.C];

    const float W1 = Weights.X;
    const float W2 = Weights.Y;
    const float W3 = Weights.Z;

    const int32 MaxWeightVertex = (W1 >= W2 && W1 >= W3) ? 0 : ((W2 >= W3) ? 1 : 2);
    const FCrustSample* MajoritySample = (MaxWeightVertex == 0) ? &S1 : (MaxWeightVertex == 1 ? &S2 : &S3);

    Out.Elevation = W1 * S1.Elevation + W2 * S2.Elevation + W3 * S3.Elevation;
    Out.Thickness = W1 * S1.Thickness + W2 * S2.Thickness + W3 * S3.Thickness;
    Out.OceanicAge = W1 * S1.OceanicAge + W2 * S2.OceanicAge + W3 * S3.OceanicAge;
    Out.OrogenyAge = W1 * S1.OrogenyAge + W2 * S2.OrogenyAge + W3 * S3.OrogenyAge;

    Out.CrustType = MajoritySample->CrustType;
    Out.OrogenyType = MajoritySample->OrogenyType;
    Out.PlateIndex = AssignedPlateIndex;

    FVector Ridge = W1 * S1.RidgeDirection + W2 * S2.RidgeDirection + W3 * S3.RidgeDirection;
    Out.RidgeDirection = ResampleProjectToTangent(Ridge, Position);

    FVector Fold = W1 * S1.FoldDirection + W2 * S2.FoldDirection + W3 * S3.FoldDirection;
    Out.FoldDirection = ResampleProjectToTangent(Fold, Position);

    return Out;
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
        Out.RidgeDirection = FVector::CrossProduct(Position, FVector::UpVector).GetSafeNormal();
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
    const float GapEdgeThresholdKm = FMath::Max(
        GapEdgeThresholdMinKm,
        GapEdgeThresholdSpacingFactor * ComputeAverageSampleSpacingKm(OldState.SampleCount));

    FGlobalBVH GlobalBVH;
    const double BVHStart = FPlatformTime::Seconds();
    if (!BuildGlobalBVH(OldState, GlobalBVH))
    {
        UE_LOG(LogTemp, Error, TEXT("[PTP] GlobalResample failed: global BVH build failed."));
        return false;
    }
    const double BVHMs = (FPlatformTime::Seconds() - BVHStart) * 1000.0;

    TArray<FCrustSample> NewSamples;
    NewSamples.SetNum(OldState.SampleCount);

    FThreadSafeCounter NormalCount;
    FThreadSafeCounter GapCount;

    const double QueryStart = FPlatformTime::Seconds();
    ParallelFor(OldState.SampleCount, [&OldState, &GlobalBVH, &SpatialGrid, &NewSamples, &NormalCount, &GapCount, GapEdgeThresholdKm](const int32 SampleIndex)
    {
        const FVector Position = GCachedLattice.Positions[SampleIndex];

        int32 TriangleIndex = INDEX_NONE;
        if (!FindContainingTriangle(GlobalBVH, OldState, Position, TriangleIndex) || !GlobalBVH.Triangles.IsValidIndex(TriangleIndex))
        {
            NewSamples[SampleIndex] = BuildGapSample(Position, OldState, SpatialGrid);
            GapCount.Increment();
            return;
        }

        const FGlobalTriangle& Tri = GlobalBVH.Triangles[TriangleIndex];
        const FVector Weights = ComputeBarycentricWeights(
            Position,
            OldState.Samples[Tri.A].Position,
            OldState.Samples[Tri.B].Position,
            OldState.Samples[Tri.C].Position);

        const bool bGapTriangle = (!Tri.bSamePlate)
            && (Tri.DivergentBoundaryVertices >= 2)
            && (Tri.MaxEdgeLengthKm > GapEdgeThresholdKm)
            && (Tri.MinEdgeLengthKm > GapEdgeThresholdKm);
        if (bGapTriangle)
        {
            NewSamples[SampleIndex] = BuildGapSample(Position, OldState, SpatialGrid);
            GapCount.Increment();
            return;
        }

        const int32 AssignedPlate = Tri.bSamePlate
            ? Tri.PlateA
            : ResolveWinningPlate(Tri, Weights);

        NewSamples[SampleIndex] = InterpolateFromTriangle(OldState, Tri, Position, Weights, AssignedPlate);
        NormalCount.Increment();
    });
    const double QueryMs = (FPlatformTime::Seconds() - QueryStart) * 1000.0;

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
        OutStats->FallbackCount = 0;
        OutStats->GapCount = GapCount.GetValue();
        OutStats->OverlapCount = 0;
        OutStats->BvhBuildMs = BVHMs;
        OutStats->QueryMs = QueryMs;
        OutStats->TransferMs = 0.0;
        OutStats->RebuildMs = RebuildMs;
        OutStats->TotalMs = TotalMs;
    }

    UE_LOG(LogTemp, Log, TEXT("[PTP] Global BVH: %.1f ms (%d triangles)"), BVHMs, GlobalBVH.Triangles.Num());
    UE_LOG(LogTemp, Log, TEXT("[PTP] Point queries + interpolation: %.1f ms (%d points)"), QueryMs, State.SampleCount);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Gap edge threshold: %.1f km"), GapEdgeThresholdKm);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Classification: %d normal, %d gap (%.2f%%), 0 fallback"),
        NormalCount.GetValue(),
        GapCount.GetValue(),
        State.SampleCount > 0 ? (100.0 * static_cast<double>(GapCount.GetValue()) / static_cast<double>(State.SampleCount)) : 0.0);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Rebuild + boundaries: %.1f ms"), RebuildMs);
    UE_LOG(LogTemp, Log, TEXT("[PTP] Total resample: %.1f ms"), TotalMs);

    return true;
}
