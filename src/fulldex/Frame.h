// Adapted from Pokémon Fulldex / melonDS, GPU3D_WideGpu (GPL-3.0-or-later).
#pragma once
#include "../GPU3D.h"
#include <array>
#include <unordered_map>
#include <vector>
namespace Fulldex {
struct WideGpuVertex
{
    float X, Y, W, Z;   // output pixels; W for perspective; depth as the DS stores it
    float R, G, B, S;   // colour (6 bits a channel); texture coordinates in 1/16 texel
    float T;
    u32 Poly;           // index into the polygon table
    u32 Pad0, Pad1;
};
static_assert(sizeof(WideGpuVertex) == 48, "matches wide_vsh.glsl");

struct WideGpuPolygon
{
    // Flags: bit 0 W-buffer, bit 1 textured, bit 2 its texture is decoded in the atlas
    // (WideGpuAtlas), at the texel TexPalette holds then.
    u32 Attr, TexParam, TexPalette, Flags;
};

// Textures decoded once (colour, 15 bits, and alpha, 5 bits from bit 16, a texel), for the
// graphics chip to read with one fetch a pixel instead of decoding the DS's formats every time
// (that was over half its time). Kept from frame to frame; a texture is decoded again when the
// texture or palette memory it comes from has changed (the DS-resolution renderer's record of
// what changed, TextureChanged). Each renderer has its own, as each has its own memory on
// the graphics chip.
struct WideGpuAtlas
{
    static constexpr u32 Capacity = 2 * 1024 * 1024; // texels (8 MB)
    struct Entry
    {
        u32 Offset, Size;          // texels
        u32 Tex0, Tex1, Pal0, Pal1; // the bytes it was decoded from (texture, palette memory)
        bool Valid;
    };
    std::vector<u32> Texels;
    std::unordered_map<u64, Entry> Entries; // by texture parameters and palette
    u32 Used = 0;
    u64 SeenVersion = 0;
    bool Full = false;
    std::vector<std::pair<u32, u32>> Uploads; // (first texel, count) decoded this frame
};

// The shaders' uniform buffer 0 (std140).
struct WideGpuFrameConstants
{
    u32 Size[4];          // width, height, DISP3DCNT, alpha test reference
    float Scale[4];       // 2 / width, 2 / height, direction of y, depth range (both the backend's)
    u32 Fog[4];           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours,
                          // bit 2 no textures: for measuring, see GPU3D_WideGpu_Deko.cpp)
    u32 Toon[32];
    u32 Edges[8];
    u32 FogDensity[36];
};

enum class WideGpuStencil : u8 { Always, Equal, NotEqual };

struct WideGpuDraw
{
    u32 First, Count;     // vertices (triangles of three)
    u8 Pixels;            // 0 all, 1 opaque (alpha 31) only, 2 translucent only
    bool DepthLequal;     // "less or equal" (equal-depth polygons, with DepthBias), else "less"
    bool DepthWrite;
    WideGpuStencil StencilTest;
    u8 StencilRef;        // compared under StencilMask; written whole where drawn
    u8 StencilMask;
    bool Blend;
    bool WriteDepthAttr, WriteFogAttr; // the second target's two channels
    s32 DepthBias;        // subtracted from the depth tested and stored
    // Depth and stencil tested (and written) before the pixel is shaded, not after: the graphics
    // chip then shades only what is in front. For polygons whose pixels the shader never
    // discards (opaque ones with opaque textures), where that changes nothing; the shader
    // with a discard in it would otherwise have its pixels tested after shading.
    bool Early = false;
};

struct WideGpuFrame
{
    int W = 0, H = 0;
    WideGpuFrameConstants Constants {};
    std::vector<WideGpuVertex> Vertices;
    std::vector<WideGpuPolygon> Polygons;
    std::vector<WideGpuDraw> Draws;
    float ClearColor[4] {}; // red, green, blue (0-1), alpha (0 or 1)
    u32 ClearDepth = 0;     // as the DS stores it (for the fog)
    // Depth as the graphics chip tests it (see wide_vsh.glsl): Z / 2^24, tested "less"; with
    // W-buffering (the DS stores W) 1 / W, tested "greater". The rear plane's:
    bool WDepth = false;
    float ClearDepthValue = 1.0f;
    u32 ClearId = 0;
    u32 ClearFog = 0;
    u8 ClearStencil = 0;
    const u8* TexMem = nullptr; // 512 KB, read during Submit
    const u8* PalMem = nullptr; // 128 KB
    u64 TexVersion = 0;         // the same (not 0) as last time: the texture memory has not changed
    bool AtlasOn = false;       // textures from the atlas (when the backend takes it, see Build)
    WideGpuAtlas Atlas;
};


void BuildFrame(WideGpuFrame& frame, int width, int height);
void InvalidateTextures();
void TextureChanged(const u64* texture, const u64* palette);
}
