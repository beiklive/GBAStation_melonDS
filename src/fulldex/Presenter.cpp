#include "Presenter.h"
#include <cstdio>

namespace Fulldex {
void Presenter::Init() {
    raster = CreateRaster();
    printf("[3D] display raster %s\n", raster ? "ready" : "unavailable; native fallback");
}

void Presenter::DeInit() {
    raster.reset(); // The Switch device still exists at renderer shutdown.
    Reset();
}

void Presenter::Reset() {
    // Also invalidate the decoded atlas after a state load, even when bank
    // mappings and dirty bits happen to match the pre-load frame.
    InvalidateTextures();
    frame = {};
    visible = false;
    lastScale = failedScale = 0;
}

static bool CanDraw() {
    using namespace GPU3D;
    // Fulldex doesn't reproduce bitmap clears, line primitives,
    // shadows or horizontal scroll. Use the DS output for those frames.
    if (!RenderNumPolygons || RenderXPos || (RenderDispCnt & (1u<<14))) return false;
    for (unsigned i=0; i<RenderNumPolygons; ++i) {
        const auto* p = RenderPolygonRAM[i];
        if (p && p->Degenerate) continue;
        if (!p || p->IsShadow || p->IsShadowMask || p->Type != 0 ||
            ((p->Attr >> 16) & 31) == 0) return false;
    }
    return true;
}

bool Presenter::Render(dk::Image& destination, int scale, bool dirty) {
    if (!raster) return false;
    bool changed = epoch != GPU3D::GeometryEpoch || lastScale != scale;
    if (changed) {
        Reset();
        epoch = GPU3D::GeometryEpoch;
        lastScale = scale;
    }
    if (failedScale == scale) return false;
    if (!changed && !dirty && geometry == GPU3D::GeometrySerial &&
        lastScroll == GPU3D::RenderXPos && GPU3D::RenderFrameIdentical)
        return visible;
    geometry = GPU3D::GeometrySerial;
    lastScroll = GPU3D::RenderXPos;
    if (!CanDraw()) {
        visible = false;
        return false;
    }

    BuildFrame(frame, 256 * scale, 192 * scale);
    visible = raster->Draw(frame, destination);
    if (!visible) failedScale = scale;
    return visible;
}
}
