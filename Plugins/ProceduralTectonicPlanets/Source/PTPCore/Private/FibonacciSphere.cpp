#include "FibonacciSphere.h"

#include "Math/UnrealMathUtility.h"

void GenerateFibonacciSphere(TArray<FVector>& OutPositions, int32 NumPoints)
{
    OutPositions.Reset();

    if (NumPoints <= 0)
    {
        return;
    }

    OutPositions.SetNumUninitialized(NumPoints);

    const double GoldenRatio = (1.0 + FMath::Sqrt(5.0)) * 0.5;

    for (int32 i = 0; i < NumPoints; ++i)
    {
        const double Fraction = (static_cast<double>(i) + 0.5) / static_cast<double>(NumPoints);
        const double Theta = FMath::Acos(1.0 - (2.0 * Fraction));
        const double Phi = (2.0 * UE_DOUBLE_PI * static_cast<double>(i)) / GoldenRatio;

        const double SinTheta = FMath::Sin(Theta);
        const double X = SinTheta * FMath::Cos(Phi);
        const double Y = SinTheta * FMath::Sin(Phi);
        const double Z = FMath::Cos(Theta);

        OutPositions[i] = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
    }
}
