#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FTectonicViewportClient;

class STectonicViewportPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(STectonicViewportPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedPtr<FTectonicViewportClient> ViewportClient;
};
