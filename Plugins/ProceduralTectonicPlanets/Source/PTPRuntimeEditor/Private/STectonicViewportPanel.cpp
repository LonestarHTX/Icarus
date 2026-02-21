#include "STectonicViewportPanel.h"

#include "STectonicCameraControls.h"
#include "TectonicViewportClient.h"

void STectonicViewportPanel::Construct(const FArguments& InArgs)
{
    ViewportClient = MakeShared<FTectonicViewportClient>();

    ChildSlot
    [
        SNew(STectonicCameraControls)
        .ViewportClient(ViewportClient)
    ];
}
