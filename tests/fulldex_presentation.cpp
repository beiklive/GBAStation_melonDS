// Host regression for geometry presentation. Replace only the Switch GPU draw.
#include "../src/fulldex/Presenter.h"
#include <cassert>
#include <cstdio>

namespace dk { struct Image {}; }
namespace GPU {
u8 VRAMFlat_Texture[512*1024]{}, VRAMFlat_TexPal[128*1024]{};
}
namespace GPU3D {
u64 GeometrySerial{}, GeometryEpoch{};
bool RenderFrameIdentical{};
u16 RenderXPos{}, RenderEdgeTable[8]{}, RenderToonTable[32]{};
u32 RenderDispCnt{}, RenderNumPolygons{}, RenderFogColor{}, RenderFogOffset{}, RenderFogShift{};
u32 RenderClearAttr1{}, RenderClearAttr2{};
u8 RenderAlphaRef{}, RenderFogDensityTable[34]{};
std::array<Polygon*,2048> RenderPolygonRAM{};
}
namespace Fulldex {
static std::vector<WideGpuVertex> output;
static unsigned draws;
static int expectedScale = 6;
class Recorder final : public Raster {
public:
    bool Draw(WideGpuFrame& f, dk::Image&) override {
        output = f.Vertices;
        assert(f.W == 256 * expectedScale && f.H == 192 * expectedScale);
        assert(f.Constants.Size[0] == f.W && f.Constants.Size[1] == f.H);
        ++draws;
        return true;
    }
};
std::unique_ptr<Raster> CreateRaster() { return std::make_unique<Recorder>(); }
}

int main() {
    using namespace GPU3D;
    using namespace Fulldex;
    Presenter renderer;
    renderer.Init();
    dk::Image image;
    Vertex verts[3]{};
    Polygon polygon{};
    polygon.NumVertices = 3;
    polygon.Attr = 31u << 16;
    for (int i=0; i<3; ++i) {
        polygon.Vertices[i] = &verts[i];
        polygon.FinalW[i] = 4096;
        verts[i].HiresPosition[0] = i*16;
        verts[i].HiresPosition[1] = (i==2)*32;
    }
    auto move = [&](int pixels) {
        for (auto& v:verts) v.HiresPosition[0] += 16*pixels;
        ++GeometrySerial;
        RenderFrameIdentical = false;
    };
    RenderNumPolygons = 1;
    RenderPolygonRAM[0] = &polygon;
    auto show = [&] { return renderer.Render(image,6,false); };
    auto repeat = [&] { RenderFrameIdentical=true; return show(); };
    move(10);
    assert(show() && output.front().X == 60);
    assert(repeat() && draws == 1);
    move(2);
    assert(show() && output.front().X == 72 && verts[0].HiresPosition[0] == 192);
    assert(repeat() && draws == 2 && output.front().X == 72);
    move(100); // cut rather than motion
    assert(show() && output.front().X == 672);
    move(2); // an actual 60 Hz geometry update
    assert(show() && output.front().X == 684);
    move(0); // same geometry re-submitted, as opposed to an idle VBlank
    assert(show() && output.front().X == 684);
    move(2);
    assert(show() && output.front().X == 696);
    ++GeometryEpoch; // load a state, discard prior geometry
    assert(repeat() && output.front().X == 696);
    RenderXPos = 1;
    assert(!repeat());
    RenderXPos = 0;
    assert(repeat() && output.front().X == 696);
    polygon.IsShadow = true;
    move(1);
    assert(!show());
    polygon.IsShadow = false;
    move(1);
    assert(show() && output.front().X == 708);
    static_assert(MaxScale() == 10);
    for (int scale : {1,2,3,4,5,6,7,8,9,10,1,10}) {
        expectedScale = scale;
        assert(renderer.Render(image,scale,false));
        assert(output.front().X == 118 * scale);
    }
    puts("Passed: 1x to 10x geometry, live switching, exact frame positions, cut, state load and native fallback.");
    return 0;
}
