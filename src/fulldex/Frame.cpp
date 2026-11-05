// Adapted from Pokémon Fulldex / melonDS, GPU3D_WideGpu (GPL-3.0-or-later).
#include "Frame.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace Fulldex {
using namespace GPU3D;
namespace {
u64 textureVersion = 1;
// What changed with each of the last versions of the texture memory (TextureChanged).
struct ChangeLog
{
    static constexpr int Size = 64;
    u64 Version[Size] {};
    u64 Tex[Size][16] {};
    u64 Pal[Size][4] {};
};
ChangeLog Changes;

// One texture decoded as wide_fsh.glsl's Lookup (and SoftRenderer::TextureLookup) reads it:
// colour (15 bits) | alpha (0-31) << 16 a texel, rows of `width`.
void DecodeTexture(const u8* texMem, const u8* palMem, u32 texParam, u32 texPal, u32* out)
{
    auto tex8 = [&](u32 a) -> u32 { return texMem[a & 0x7FFFF]; };
    auto tex16 = [&](u32 a) -> u32 { a &= 0x7FFFE; return texMem[a] | (texMem[a + 1] << 8); };
    auto pal16 = [&](u32 a) -> u32 { a &= 0x1FFFE; return palMem[a] | (palMem[a + 1] << 8); };
    const u32 vramaddr = (texParam & 0xFFFF) << 3;
    const int width = 8 << ((texParam >> 20) & 7), height = 8 << ((texParam >> 23) & 7);
    const u32 alpha0 = (texParam & (1u << 29)) ? 0 : 31;
    const u32 format = (texParam >> 26) & 7;
    for (int t = 0; t < height; t++)
        for (int s = 0; s < width; s++)
        {
            const u32 pos = (u32)(t * width + s);
            u32 color = 0, alpha = 31;
            switch (format)
            {
            case 1: // A3I5
            {
                const u32 pixel = tex8(vramaddr + pos);
                color = pal16((texPal << 4) + ((pixel & 0x1F) << 1));
                alpha = ((pixel >> 3) & 0x1C) + (pixel >> 6);
                break;
            }
            case 2: // 4 colours
            {
                const u32 pixel = (tex8(vramaddr + (pos >> 2)) >> ((s & 3) << 1)) & 3;
                color = pal16((texPal << 3) + (pixel << 1));
                alpha = pixel == 0 ? alpha0 : 31;
                break;
            }
            case 3: // 16 colours
            {
                u32 pixel = tex8(vramaddr + (pos >> 1));
                pixel = (s & 1) ? pixel >> 4 : pixel & 0xF;
                color = pal16((texPal << 4) + (pixel << 1));
                alpha = pixel == 0 ? alpha0 : 31;
                break;
            }
            case 4: // 256 colours
            {
                const u32 pixel = tex8(vramaddr + pos);
                color = pal16((texPal << 4) + (pixel << 1));
                alpha = pixel == 0 ? alpha0 : 31;
                break;
            }
            case 5: // compressed
            {
                u32 addr = vramaddr + (u32)(t & 0x3FC) * (u32)(width >> 2) + (u32)(s & 0x3FC) + (u32)(t & 3);
                addr &= 0x7FFFF;
                u32 slot1addr = 0x20000 + ((addr & 0x1FFFC) >> 1);
                if (addr >= 0x40000) slot1addr += 0x10000;
                const u32 val = (addr >= 0x20000 && addr < 0x40000) ? 0 : (tex8(addr) >> (2 * (s & 3)));
                const u32 palinfo = tex16(slot1addr);
                const u32 paloffset = (palinfo & 0x3FFF) << 2;
                const u32 base = texPal << 4;
                const u32 mode = palinfo >> 14;
                const u32 c0 = pal16(base + paloffset), c1 = pal16(base + paloffset + 2);
                switch (val & 3)
                {
                case 0: color = c0; break;
                case 1: color = c1; break;
                case 2:
                    if (mode == 1)
                        color = (((c0 & 0x1F) + (c1 & 0x1F)) >> 1) | ((((c0 & 0x3E0) + (c1 & 0x3E0)) >> 1) & 0x3E0) |
                            ((((c0 & 0x7C00) + (c1 & 0x7C00)) >> 1) & 0x7C00);
                    else if (mode == 3)
                        color = (((c0 & 0x1F) * 5 + (c1 & 0x1F) * 3) >> 3) | ((((c0 & 0x3E0) * 5 + (c1 & 0x3E0) * 3) >> 3) & 0x3E0) |
                            ((((c0 & 0x7C00) * 5 + (c1 & 0x7C00) * 3) >> 3) & 0x7C00);
                    else
                        color = pal16(base + paloffset + 4);
                    break;
                default:
                    if (mode == 2)
                        color = pal16(base + paloffset + 6);
                    else if (mode == 3)
                        color = (((c0 & 0x1F) * 3 + (c1 & 0x1F) * 5) >> 3) | ((((c0 & 0x3E0) * 3 + (c1 & 0x3E0) * 5) >> 3) & 0x3E0) |
                            ((((c0 & 0x7C00) * 3 + (c1 & 0x7C00) * 5) >> 3) & 0x7C00);
                    else
                    {
                        color = 0;
                        alpha = 0;
                    }
                    break;
                }
                break;
            }
            case 6: // A5I3
            {
                const u32 pixel = tex8(vramaddr + pos);
                color = pal16((texPal << 4) + ((pixel & 7) << 1));
                alpha = pixel >> 3;
                break;
            }
            case 7: // direct colour
                color = tex16(vramaddr + (pos << 1));
                alpha = (color & 0x8000) ? 31 : 0;
                break;
            default:
                break;
            }
            out[pos] = (color & 0x7FFF) | (alpha << 16);
        }
}

// The memory a texture is decoded from: its texels, and its palette entries (all of the palette
// memory for a compressed texture, whose palette offsets come from its texels; and its texels'
// second part, in texture slot 1, too).
void SourceRanges(u32 texParam, u32 texPal, u32& tex0, u32& tex1, u32& pal0, u32& pal1)
{
    const u32 vramaddr = (texParam & 0xFFFF) << 3;
    const u32 texels = (8u << ((texParam >> 20) & 7)) * (8u << ((texParam >> 23) & 7));
    switch ((texParam >> 26) & 7)
    {
    case 1: tex0 = vramaddr; tex1 = vramaddr + texels; pal0 = texPal << 4; pal1 = pal0 + 64; break;
    case 2: tex0 = vramaddr; tex1 = vramaddr + texels / 4; pal0 = texPal << 3; pal1 = pal0 + 8; break;
    case 3: tex0 = vramaddr; tex1 = vramaddr + texels / 2; pal0 = texPal << 4; pal1 = pal0 + 32; break;
    case 4: tex0 = vramaddr; tex1 = vramaddr + texels; pal0 = texPal << 4; pal1 = pal0 + 512; break;
    case 6: tex0 = vramaddr; tex1 = vramaddr + texels; pal0 = texPal << 4; pal1 = pal0 + 16; break;
    case 7: tex0 = vramaddr; tex1 = vramaddr + texels * 2; pal0 = pal1 = 0; break;
    default: tex0 = 0; tex1 = 512 * 1024; pal0 = 0; pal1 = 128 * 1024; break; // compressed: everything
    }
}

// Whether any 512-byte block of [from, to) (masked to the memory's size) is set in `blocks`.
bool AnyChanged(const u64* blocks, u32 words, u32 from, u32 to)
{
    if (to <= from) return false;
    const u32 mask = words * 64 * 512 - 1;
    if (to - from > mask) return true;
    for (u32 a = from & ~511u; a < to; a += 512)
    {
        const u32 b = (a & mask) / 512;
        if (blocks[b >> 6] & (1ull << (b & 63))) return true;
    }
    return false;
}

// Brings the atlas up to date with the texture memory's version: textures whose memory changed
// since it was last seen are decoded again when next used (everything, if that is too long ago).
void SeeVersion(WideGpuAtlas& a, u64 version)
{
    if (a.SeenVersion == version) return;
    bool all = a.SeenVersion == 0 || version < a.SeenVersion || version - a.SeenVersion >= (u64)ChangeLog::Size;
    u64 tex[16] {}, pal[4] {};
    for (u64 v = a.SeenVersion + 1; !all && v <= version; v++)
    {
        const int i = (int)(v % ChangeLog::Size);
        if (Changes.Version[i] != v) { all = true; break; }
        for (int w = 0; w < 16; w++) tex[w] |= Changes.Tex[i][w];
        for (int w = 0; w < 4; w++) pal[w] |= Changes.Pal[i][w];
    }
    for (auto& [key, e] : a.Entries)
        if (all || AnyChanged(tex, 16, e.Tex0, e.Tex1) || AnyChanged(pal, 4, e.Pal0, e.Pal1)) e.Valid = false;
    a.SeenVersion = version;
}
}

void TextureChanged(const u64* tex, const u64* pal)
{
    const u64 version = ++textureVersion;
    const int i = (int)(version % ChangeLog::Size);
    Changes.Version[i] = version;
    memcpy(Changes.Tex[i], tex, sizeof(Changes.Tex[i]));
    memcpy(Changes.Pal[i], pal, sizeof(Changes.Pal[i]));
}

void InvalidateTextures() {
    ++textureVersion;
}

void BuildFrame(WideGpuFrame& f, int W, int H)
{
    const auto& list = RenderPolygonRAM;
    const u32 count = RenderNumPolygons;
    f.W = W;
    f.H = H;
    f.Vertices.clear();
    f.Polygons.clear();
    f.Draws.clear();

    WideGpuFrameConstants& k = f.Constants;
    const u32 dispCnt = GPU3D::RenderDispCnt;
    k.Size[0] = (u32)W;
    k.Size[1] = (u32)H;
    k.Size[2] = dispCnt;
    k.Size[3] = GPU3D::RenderAlphaRef;
    k.Scale[0] = 2.0f / W;
    k.Scale[1] = 2.0f / H;
    k.Scale[2] = 1.0f;
    k.Scale[3] = 0.0f;
    k.Fog[0] = GPU3D::RenderFogColor;
    k.Fog[1] = GPU3D::RenderFogOffset;
    k.Fog[2] = GPU3D::RenderFogShift;
    k.Fog[3] = ((dispCnt >> 7) & 1) | ((dispCnt & (1 << 6)) ? 0 : 2);
    for (int i = 0; i < 32; i++) k.Toon[i] = GPU3D::RenderToonTable[i];
    for (int i = 0; i < 8; ++i) k.Edges[i] = GPU3D::RenderEdgeTable[i];
    for (int i = 0; i < 36; i++) k.FogDensity[i] = i < 34 ? GPU3D::RenderFogDensityTable[i] : 0;

    // The rear plane, as GPU3D_Wide.cpp clears to it.
    const u32 a1 = GPU3D::RenderClearAttr1;
    const u32 clearAlpha = (a1 >> 16) & 0x1F;
    if (clearAlpha)
    {
        const u32 r = (a1 << 3) & 0xF8, gr = (a1 >> 2) & 0xF8, b = (a1 >> 7) & 0xF8;
        f.ClearColor[0] = (float)(r | (r >> 5)) / 255.0f;
        f.ClearColor[1] = (float)(gr | (gr >> 5)) / 255.0f;
        f.ClearColor[2] = (float)(b | (b >> 5)) / 255.0f;
        f.ClearColor[3] = (clearAlpha + 1) / 32.0f;
    }
    else
        f.ClearColor[0] = f.ClearColor[1] = f.ClearColor[2] = f.ClearColor[3] = 0.0f;
    f.ClearDepth = ((GPU3D::RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
    // (W-buffering is chosen for the whole frame, when it is flushed.)
    f.WDepth = false;
    for (u32 i = 0; i < count; i++)
        if (list[i])
        {
            f.WDepth = list[i]->WBuffer;
            break;
        }
    f.ClearDepthValue = f.WDepth ? 1.0f / (float)std::max<u32>(f.ClearDepth, 1) : (float)f.ClearDepth / 16777216.0f;
    f.ClearId = (a1 >> 24) & 63;
    f.ClearFog = (a1 >> 15) & 1;
    f.ClearStencil = clearAlpha ? 0x80 : 0;
    f.TexMem = GPU::VRAMFlat_Texture;
    f.PalMem = GPU::VRAMFlat_TexPal;
    f.TexVersion = textureVersion;

    const bool texturing = dispCnt & 1;
    const bool blendEnabled = dispCnt & (1 << 3);
    const float sx = W / (256.0f * 16), sy = H / (192.0f * 16);

    // Decoded textures (see WideGpuAtlas): only with the texture memory's versions to tell what
    // changed (the DS-resolution picture on the graphics chip keeps them).
    WideGpuAtlas& at = f.Atlas;
    f.AtlasOn = true;
    at.Uploads.clear();
    if (f.AtlasOn)
    {
        if (at.Texels.empty()) at.Texels.resize(WideGpuAtlas::Capacity);
        if (at.Full)
        {
            // (Filled up last frame: started again, so that what is used fits.)
            at.Entries.clear();
            at.Used = 0;
            at.Full = false;
        }
        SeeVersion(at, f.TexVersion);
    }
    // The atlas's place for a polygon's texture, decoded now if needed; ~0u if it has no room.
    auto atlasOffset = [&](u32 texParam, u32 texPal) -> u32 {
        const u32 format = (texParam >> 26) & 7;
        const u64 key = (u64)(texParam & 0x3FF0FFFF) | ((u64)(format == 7 ? 0 : texPal) << 32);
        auto it = at.Entries.find(key);
        if (it != at.Entries.end() && it->second.Valid) return it->second.Offset;
        const u32 size = (8u << ((texParam >> 20) & 7)) * (8u << ((texParam >> 23) & 7));
        WideGpuAtlas::Entry* e;
        if (it != at.Entries.end())
            e = &it->second; // decoded again in its own place
        else
        {
            if (at.Used + size > WideGpuAtlas::Capacity)
            {
                at.Full = true;
                return ~0u;
            }
            e = &at.Entries[key];
            e->Offset = at.Used;
            e->Size = size;
            at.Used += size;
        }
        SourceRanges(texParam, texPal, e->Tex0, e->Tex1, e->Pal0, e->Pal1);
        DecodeTexture(GPU::VRAMFlat_Texture, GPU::VRAMFlat_TexPal, texParam, texPal, at.Texels.data() + e->Offset);
        e->Valid = true;
        at.Uploads.push_back({ e->Offset, e->Size });
        return e->Offset;
    };

    // A polygon's triangles (a fan), appended to the vertices.
    auto add = [&](const Polygon* poly) {
        const u32 index = (u32)f.Polygons.size();
        WideGpuPolygon p;
        p.Attr = poly->Attr;
        p.TexParam = poly->TexParam;
        p.TexPalette = poly->TexPalette;
        p.Flags = (poly->WBuffer ? 1 : 0) | (texturing && ((poly->TexParam >> 26) & 0x7) ? 2 : 0);
        if (f.AtlasOn && (p.Flags & 2))
        {
            const u32 offset = atlasOffset(poly->TexParam, poly->TexPalette);
            if (offset != ~0u)
            {
                p.TexPalette = offset;
                p.Flags |= 4;
            }
        }
        f.Polygons.push_back(p);
        WideGpuVertex v[10];
        for (u32 i = 0; i < poly->NumVertices; i++)
        {
            const Vertex* vtx = poly->Vertices[i];
            WideGpuVertex& o = v[i];
            o.X = vtx->HiresPosition[0] * sx;
            o.Y = vtx->HiresPosition[1] * sy;
            o.W = poly->FinalW[i] > 0 ? (float)poly->FinalW[i] : 1.0f;
            o.Z = (float)poly->FinalZ[i];
            o.R = (float)(vtx->FinalColor[0] >> 3);
            o.G = (float)(vtx->FinalColor[1] >> 3);
            o.B = (float)(vtx->FinalColor[2] >> 3);
            o.S = (float)vtx->TexCoords[0];
            o.T = (float)vtx->TexCoords[1];
            o.Poly = index;
            o.Pad0 = o.Pad1 = 0;
        }
        for (u32 i = 1; i + 1 < poly->NumVertices; i++)
        {
            f.Vertices.push_back(v[0]);
            f.Vertices.push_back(v[i]);
            f.Vertices.push_back(v[i + 1]);
        }
    };
    auto drawable = [](const Polygon* poly) {
        return poly && !poly->Degenerate && !poly->IsShadowMask && !poly->IsShadow && poly->Type == 0 && poly->NumVertices >= 3;
    };
    auto frontFirst = [](const Polygon* poly) { return !poly->Translucent && !(poly->Attr & (1 << 14)); };

    // Whether the shader can discard any of a polygon's pixels (drawn with all its pixels): not
    // when it is opaque (alpha 31, or 0, wireframe, drawn as 31) and so is every texel: no
    // texture, or one of 4, 16 or 256 colours whose colour 0 is not transparent. (Alpha 31 then
    // passes any alpha test but one with reference 31.)
    auto neverDiscards = [&](const Polygon* poly) {
        const u32 polyAlpha = (poly->Attr >> 16) & 31;
        if ((polyAlpha != 31 && polyAlpha != 0) || GPU3D::RenderAlphaRef >= 31) return false;
        const u32 format = texturing ? (poly->TexParam >> 26) & 7 : 0;
        return format == 0 || ((format == 2 || format == 3 || format == 4) && !(poly->TexParam & (1u << 29)));
    };

    // The opaque polygons, in the game's order, in as few draws as runs of them alike: those
    // whose pixels are never discarded tested early (see WideGpuDraw::Early).
    {
        bool runEarly = false;
        u32 runFirst = 0;
        auto closeRun = [&]() {
            const u32 n = (u32)f.Vertices.size() - runFirst;
            if (!n) return;
            WideGpuDraw d {};
            d.First = runFirst;
            d.Count = n;
            d.Pixels = 0;
            d.DepthWrite = true;
            d.StencilTest = WideGpuStencil::Always;
            d.StencilRef = 0x80;
            d.StencilMask = 0xFF;
            d.WriteDepthAttr = d.WriteFogAttr = true;
            d.Early = runEarly;
            f.Draws.push_back(d);
            runFirst = (u32)f.Vertices.size();
        };
        for (u32 i = 0; i < count; i++)
            if (drawable(list[i]) && frontFirst(list[i]))
            {
                const bool early = neverDiscards(list[i]);
                if (early != runEarly)
                {
                    closeRun();
                    runEarly = early;
                }
                add(list[i]);
            }
        closeRun();
    }
    const size_t opaqueDraws = f.Draws.size();

    // The others, in order. Consecutive polygons that are drawn alike share their draws: all
    // opaque ones (decals), or all translucent ones of one polygon ID; a polygon with pixels
    // of both kinds is drawn on its own.
    struct Kind
    {
        bool Opaque, Translucent, Lequal, WriteDepth, Fog;
        s32 Bias;
        u8 Id;
        bool operator==(const Kind& o) const
        {
            return Opaque == o.Opaque && Translucent == o.Translucent && Lequal == o.Lequal && WriteDepth == o.WriteDepth &&
                Fog == o.Fog && Bias == o.Bias && Id == o.Id;
        }
    };
    Kind group {};
    u32 groupFirst = 0;
    bool open = false;
    auto close = [&]() {
        if (!open) return;
        open = false;
        const u32 n = (u32)f.Vertices.size() - groupFirst;
        if (!n) return;
        WideGpuDraw d {};
        d.First = groupFirst;
        d.Count = n;
        d.DepthLequal = group.Lequal;
        d.DepthBias = group.Bias;
        if (group.Opaque)
        {
            d.Pixels = group.Translucent ? 1 : 0;
            d.DepthWrite = true;
            d.StencilTest = WideGpuStencil::Always;
            d.StencilRef = 0x80;
            d.StencilMask = 0xFF;
            d.WriteDepthAttr = d.WriteFogAttr = true;
            f.Draws.push_back(d);
        }
        if (group.Translucent)
        {
            d.Pixels = 2;
            // Translucent texels may be discarded; stencil must follow shading.
            d.Early = false;
            d.DepthWrite = group.WriteDepth;
            d.WriteDepthAttr = group.WriteDepth;
            // A translucent pixel keeps the fog only if the polygon has it too (GPU3D_Wide.cpp's
            // pattr & fogBit): one without it writes its 0. SoulSilver's dark caves rely on this,
            // their darkness polygons taking the black fog off the lit circle around the player.
            d.WriteFogAttr = !group.Fog;
            d.StencilRef = (u8)(0x80 | group.Id);
            if (blendEnabled)
            {
                // Where the picture has no alpha yet: drawn as they are. (Nowhere, when the rear
                // plane has alpha: every pixel's stencil has 0x80 from the start. The pass is then
                // left out, rather than having the graphics chip shade all its pixels, which it
                // does before testing them, for nothing.)
                d.StencilTest = WideGpuStencil::NotEqual;
                d.StencilMask = 0x80;
                d.Blend = false;
                if (!(f.ClearStencil & 0x80)) f.Draws.push_back(d);
                // Elsewhere, blended, except over their own ID.
                d.StencilMask = 0xFF;
                d.Blend = true;
                f.Draws.push_back(d);
            }
            else
            {
                d.StencilTest = WideGpuStencil::NotEqual;
                d.StencilMask = 0xFF;
                d.Blend = false;
                f.Draws.push_back(d);
            }
        }
    };
    for (u32 i = 0; i < count; i++)
    {
        const Polygon* poly = list[i];
        if (!drawable(poly) || frontFirst(poly)) continue;
        const u32 attr = poly->Attr;
        const u32 polyAlpha = (attr >> 16) & 0x1F;
        Kind kind;
        kind.Translucent = poly->Translucent && polyAlpha != 0;
        // (Alpha 31 is reached by translucent polygons only through their texture's alpha.)
        kind.Opaque = !poly->Translucent || polyAlpha == 31 || polyAlpha == 0;
        kind.Lequal = attr & (1 << 14);
        kind.Bias = kind.Lequal ? (poly->WBuffer ? 0xFF : 0x200) : 0;
        kind.WriteDepth = attr & (1 << 11);
        kind.Fog = attr & (1 << 15);
        kind.Id = kind.Translucent ? (u8)(((attr >> 24) & 0x3F) + 1) : 0;
        if (!kind.Translucent) kind.WriteDepth = true;
        const bool mixed = kind.Opaque && kind.Translucent;
        if (!open || mixed || !(kind == group))
        {
            close();
            group = kind;
            groupFirst = (u32)f.Vertices.size();
            open = true;
        }
        add(poly);
        if (mixed) close();
    }
    close();
}
}
