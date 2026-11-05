// Adapted from Pokémon Fulldex / melonDS, GPU3D_WideGpu (GPL-3.0-or-later).
#pragma once
#include "Frame.h"
namespace dk { struct Image; }
#include <memory>
namespace Fulldex {
class Raster {
public:
    virtual ~Raster() = default;
    virtual bool Draw(WideGpuFrame& frame, dk::Image& destination) = 0;
};
std::unique_ptr<Raster> CreateRaster();
}
