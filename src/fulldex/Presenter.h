#pragma once
#include "Raster.h"
#include "Presentation.h"
namespace Fulldex {
// Owns display-only geometry. The emulated DS and its capture unit always use
// the original renderer. There is no extra DS CPU frame or extra audio.
class Presenter {
public:
    void Reset();
    void Init();
    void DeInit();
    bool Render(dk::Image& destination, int scale, bool dirty);
private:
    std::unique_ptr<Raster> raster;
    WideGpuFrame frame;
    u64 geometry = 0, epoch = 0;
    int lastScale = 0, failedScale = 0;
    bool visible = false;
    u16 lastScroll = 0;
};
}
