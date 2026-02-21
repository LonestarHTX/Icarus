#include "SphericalTriangulation.h"

#include "CompGeom/ConvexHull3.h"
#include "IndexTypes.h"

using namespace UE::Geometry;

namespace
{
void EnsureOutwardWinding(const TArray<FVector>& Positions, TArray<int32>& TriangleIndices)
{
    for (int32 TriangleBase = 0; TriangleBase + 2 < TriangleIndices.Num(); TriangleBase += 3)
    {
        const int32 Idx0 = TriangleIndices[TriangleBase];
        int32& Idx1 = TriangleIndices[TriangleBase + 1];
        int32& Idx2 = TriangleIndices[TriangleBase + 2];

        const FVector A = Positions[Idx0];
        const FVector B = Positions[Idx1];
        const FVector C = Positions[Idx2];

        const FVector Normal = FVector::CrossProduct(B - A, C - A);
        const FVector Centroid = (A + B + C) / 3.0f;
        if (FVector::DotProduct(Normal, Centroid) < 0.0f)
        {
            Swap(Idx1, Idx2);
        }
    }
}
}

bool ComputeSphericalDelaunay(const TArray<FVector>& Positions, TArray<int32>& OutTriangleIndices)
{
    OutTriangleIndices.Reset();

    if (Positions.Num() < 4)
    {
        return false;
    }

    TArray<FVector3d> HullPoints;
    HullPoints.Reserve(Positions.Num());
    for (const FVector& Position : Positions)
    {
        HullPoints.Emplace(static_cast<double>(Position.X), static_cast<double>(Position.Y), static_cast<double>(Position.Z));
    }

    FConvexHull3d Hull;

    if (!Hull.Solve(TArrayView<const FVector3d>(HullPoints)) || !Hull.IsSolutionAvailable())
    {
        if (Positions.Num() > 1000)
        {
            TArray<FVector3d> ProbePoints;
            ProbePoints.Reserve(1000);
            for (int32 Index = 0; Index < 1000; ++Index)
            {
                const FVector& Position = Positions[Index];
                ProbePoints.Emplace(static_cast<double>(Position.X), static_cast<double>(Position.Y), static_cast<double>(Position.Z));
            }

            FConvexHull3d ProbeHull;
            const bool bProbeSucceeded = ProbeHull.Solve(TArrayView<const FVector3d>(ProbePoints)) && ProbeHull.IsSolutionAvailable();
            UE_LOG(LogTemp, Warning, TEXT("[PTP] Convex hull failed at %d points. 1000-point probe success=%s"),
                Positions.Num(), bProbeSucceeded ? TEXT("true") : TEXT("false"));
        }

        // If this continues to fail at scale, qhull integration is the fallback path.
        return false;
    }

    const TArray<FIndex3i>& HullTriangles = Hull.GetTriangles();
    OutTriangleIndices.Reserve(HullTriangles.Num() * 3);
    for (const FIndex3i& Tri : HullTriangles)
    {
        OutTriangleIndices.Add(Tri.A);
        OutTriangleIndices.Add(Tri.B);
        OutTriangleIndices.Add(Tri.C);
    }

    // If hull quality or scale becomes problematic at high counts, consider a qhull-backed fallback.
    EnsureOutwardWinding(Positions, OutTriangleIndices);
    return true;
}
