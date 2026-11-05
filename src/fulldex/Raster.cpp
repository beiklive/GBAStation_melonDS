// Adapted from Pokémon Fulldex / melonDS, GPU3D_WideGpu (GPL-3.0-or-later).
#include "Raster.h"
#include <deko3d.hpp>
#include "../frontend/switch/Gfx.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
namespace Fulldex {
namespace {
constexpr u32 CmdSize = 4 * 1024 * 1024;
constexpr u32 MaxVertices = 4096 * 8 * 3; // (the wide lists' polygons, up to 8 triangles each)
constexpr u32 MaxPolygons = 4096;

// Where things are in the data memory.
constexpr u32 OffFrame = 0;                            // uniform buffer 0
constexpr u32 FrameSize = 0x200;
constexpr u32 OffDraw = 0x200;                         // uniform buffer 1, set per draw
constexpr u32 DrawSize = 0x100;
constexpr u32 OffImageDesc = 0x300;                    // image descriptors (see ImageIds)
constexpr u32 OffSamplerDesc = 0x400;                  // one sampler descriptor
// The images in the descriptor set: the final pass's two textures (what was drawn), and the
// texture and palette memory as buffer textures (wide_fsh.glsl).
enum ImageIds : u32 { ImgColor, ImgAttr, ImgTex8, ImgTex16, ImgPal16, ImgAtlas, ImageCount };
static_assert(OffImageDesc + ImageCount * 32 <= OffSamplerDesc, "the image descriptors fit");
constexpr u32 OffPolygons = 0x1000;
constexpr u32 OffTex = OffPolygons + MaxPolygons * sizeof(WideGpuPolygon);
constexpr u32 OffPal = OffTex + 512 * 1024;
constexpr u32 OffVertices = OffPal + 128 * 1024;
constexpr u32 OffAtlas = (OffVertices + MaxVertices * sizeof(WideGpuVertex) + 0xFFF) & ~0xFFFu;
constexpr u32 DataSize = OffAtlas + WideGpuAtlas::Capacity * 4;
static_assert(sizeof(WideGpuFrameConstants) <= FrameSize, "the frame constants fit");

constexpr u32 Align(u32 v, u32 a)
{
    return (v + a - 1) & ~(a - 1);
}

DkMemBlock Block(u32 size, u32 flags) {
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, Gfx::Device, Align(size, DK_MEMBLOCK_ALIGNMENT));
    maker.flags = flags;
    return dkMemBlockCreate(&maker);
}

class DekoRaster final : public Raster {
public:
    bool Init() {
        if (!Gfx::LoadShader("romfs:/shaders/nds3d_wide_vsh.dksh", mainVS) ||
            !Gfx::LoadShader("romfs:/shaders/nds3d_wide_fsh.dksh", mainFS) ||
            !Gfx::LoadShader("romfs:/shaders/nds3d_final_vsh.dksh", finalVS) ||
            !Gfx::LoadShader("romfs:/shaders/nds3d_final_fsh.dksh", finalFS)) return false;
        if (!dkShaderIsValid(&mainVS) || !dkShaderIsValid(&mainFS) ||
            !dkShaderIsValid(&finalVS) || !dkShaderIsValid(&finalFS)) return false;
        haveEarly = Gfx::LoadShader("romfs:/shaders/nds3d_early.dksh", earlyFS) && dkShaderIsValid(&earlyFS);
        DkQueueMaker qm;
        dkQueueMakerDefaults(&qm, Gfx::Device);
        qm.flags = DkQueueFlags_Graphics | DkQueueFlags_MediumPrio;
        Queue = dkQueueCreate(&qm);
        if (!Queue) return false;
        CmdMem = Block(CmdSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        DataMem = Block(DataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!CmdMem || !DataMem) return false;
        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, Gfx::Device);
        Cmd = dkCmdBufCreate(&cm);
        if (!Cmd) return false;
        Data = (u8*)dkMemBlockGetCpuAddr(DataMem);
        DataAddr = dkMemBlockGetGpuAddr(DataMem);

        // The final pass reads the targets texel by texel.
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = DkFilter_Nearest;
        sampler.magFilter = DkFilter_Nearest;
        sampler.wrapMode[0] = sampler.wrapMode[1] = sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor sd;
        dkSamplerDescriptorInitialize(&sd, &sampler);
        memcpy(Data + OffSamplerDesc, &sd, sizeof(sd));

        // The texture memory (as bytes and as 16-bit texels) and the palette memory (16-bit) as
        // buffer textures over the data memory.
        auto buffer = [&](DkImage& image, DkImageFormat format, u32 offset, u32 texels, u32 id) {
            DkImageLayoutMaker lm;
            dkImageLayoutMakerDefaults(&lm, Gfx::Device);
            lm.type = DkImageType_Buffer;
            lm.format = format;
            lm.dimensions[0] = texels;
            DkImageLayout layout;
            dkImageLayoutInitialize(&layout, &lm);
            dkImageInitialize(&image, &layout, DataMem, offset);
            DkImageView view;
            dkImageViewDefaults(&view, &image);
            DkImageDescriptor desc;
            dkImageDescriptorInitialize(&desc, &view, false, false);
            memcpy(Data + OffImageDesc + id * sizeof(desc), &desc, sizeof(desc));
        };
        buffer(Tex8Image, DkImageFormat_R8_Uint, OffTex, 512 * 1024, ImgTex8);
        buffer(Tex16Image, DkImageFormat_R16_Uint, OffTex, 256 * 1024, ImgTex16);
        buffer(Pal16Image, DkImageFormat_R16_Uint, OffPal, 64 * 1024, ImgPal16);
        buffer(AtlasImage, DkImageFormat_R32_Uint, OffAtlas, WideGpuAtlas::Capacity, ImgAtlas);
        return true;
    }

    ~DekoRaster() override {
        if (Queue) {
            dkQueueWaitIdle(Queue);
            dkQueueDestroy(Queue);
        }
        FreeTargets();
        if (Cmd) dkCmdBufDestroy(Cmd);
        if (DataMem) dkMemBlockDestroy(DataMem);
        if (CmdMem) dkMemBlockDestroy(CmdMem);
    }
    bool Draw(WideGpuFrame& f, dk::Image& destination) override {
        if (f.Vertices.size() > MaxVertices || f.Polygons.size() > MaxPolygons) return false;
        if (!Wait()) return false;

        if (f.W != W || f.H != H)
        {
            if (!MakeTargets(f.W, f.H)) return false;
        }
        // deko3D's rows run from the top, as in memory (see wide_vsh.glsl).
        f.Constants.Scale[2] = 1.0f;
        f.Constants.Scale[3] = 0.0f; // depth from 0 to 1

        memcpy(Data + OffFrame, &f.Constants, sizeof(f.Constants));
        if (!f.Polygons.empty()) memcpy(Data + OffPolygons, f.Polygons.data(), f.Polygons.size() * sizeof(WideGpuPolygon));
        // (The texture memory only when it may have changed.)
        if (!f.TexVersion || f.TexVersion != TexVersion)
        {
            memcpy(Data + OffTex, f.TexMem, 512 * 1024);
            memcpy(Data + OffPal, f.PalMem, 128 * 1024);
            TexVersion = f.TexVersion;
        }
        if (!f.Vertices.empty()) memcpy(Data + OffVertices, f.Vertices.data(), f.Vertices.size() * sizeof(WideGpuVertex));
        // The textures decoded this frame (all of them, the first time this renderer sees the
        // atlas).
        if (f.AtlasOn)
        {
            const WideGpuAtlas& a = f.Atlas;
            if (!AtlasSynced)
            {
                memcpy(Data + OffAtlas, a.Texels.data(), (size_t)a.Used * 4);
                AtlasSynced = true;
            }
            else
                for (const auto& [first, n] : a.Uploads)
                {
                    memcpy(Data + OffAtlas + (size_t)first * 4, a.Texels.data() + first, (size_t)n * 4);

                }
        }

        dkCmdBufClear(Cmd);
        dkCmdBufAddMemory(Cmd, CmdMem, 0, CmdSize);
        // What the processor wrote since the last frame is read afresh.
        dkCmdBufBarrier(Cmd, DkBarrier_None, DkInvalidateFlags_Shader | DkInvalidateFlags_Image |
            DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);

        DkImageView colorView, attrView, depthView, finalView;
        dkImageViewDefaults(&colorView, &ColorImage);
        dkImageViewDefaults(&attrView, &AttrImage);
        dkImageViewDefaults(&depthView, &DepthImage);
        dkImageViewDefaults(&finalView, &destination);
        const DkImageView* targets[2] = { &colorView, &attrView };
        dkCmdBufBindRenderTargets(Cmd, targets, 2, &depthView);
        const DkViewport vp = { 0, 0, (float)W, (float)H, 0, 1 };
        dkCmdBufSetViewports(Cmd, 0, &vp, 1);
        const DkScissor sc = { 0, 0, (u32)W, (u32)H };
        dkCmdBufSetScissors(Cmd, 0, &sc, 1);

        DkColorWriteState all;
        dkColorWriteStateDefaults(&all);
        dkCmdBufBindColorWriteState(Cmd, &all);
        dkCmdBufClearColor(Cmd, 0, DkColorMask_RGBA, f.ClearColor);
        const u32 clearAttr[4] = { f.ClearDepth | (f.ClearId << 24), f.ClearFog, 0, 0 };
        dkCmdBufClearColor(Cmd, 1, DkColorMask_RGBA, clearAttr);
        dkCmdBufClearDepthStencil(Cmd, true, f.ClearDepthValue, 0xFF, f.ClearStencil);

        const DkShader* main[2] = { &mainVS, &mainFS };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, main, 2);

        const DkBufExtents frame = { DataAddr + OffFrame, FrameSize };
        const DkBufExtents draw = { DataAddr + OffDraw, DrawSize };
        const DkBufExtents ubos[2] = { frame, draw };
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Vertex, 0, ubos, 2);
        dkCmdBufBindUniformBuffers(Cmd, DkStage_Fragment, 0, ubos, 2);
        const DkBufExtents vertices = { DataAddr + OffVertices, Align((u32)std::max<size_t>(16, f.Vertices.size() * sizeof(WideGpuVertex)), 16) };
        const DkBufExtents polygons = { DataAddr + OffPolygons, MaxPolygons * (u32)sizeof(WideGpuPolygon) };
        const DkBufExtents vert[2] = { vertices, polygons };
        dkCmdBufBindStorageBuffers(Cmd, DkStage_Vertex, 0, vert, 2);
        dkCmdBufBindImageDescriptorSet(Cmd, DataAddr + OffImageDesc, ImageCount);
        dkCmdBufBindSamplerDescriptorSet(Cmd, DataAddr + OffSamplerDesc, 1);
        {
            const DkResHandle mem[4] = { dkMakeTextureHandle(ImgTex8, 0), dkMakeTextureHandle(ImgTex16, 0),
                dkMakeTextureHandle(ImgPal16, 0), dkMakeTextureHandle(ImgAtlas, 0) };
            dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, mem, 4);
        }

        DkRasterizerState rs;
        dkRasterizerStateDefaults(&rs);
        rs.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(Cmd, &rs);
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        dkBlendStateSetOps(&blend, DkBlendOp_Add, DkBlendOp_Max);
        dkBlendStateSetFactors(&blend, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_One);
        dkCmdBufBindBlendStates(Cmd, 0, &blend, 1);

        bool earlyBound = false;
        const DkShader* early[2] = { &mainVS, &earlyFS };
        for (const WideGpuDraw& d : f.Draws)
        {
            if ((haveEarly && d.Early) != earlyBound) {
                earlyBound = haveEarly && d.Early;
                dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, earlyBound ? early : main, 2);
            }

            const u32 params[4] = { d.First, d.Pixels, (u32)d.DepthBias, f.WDepth ? 1u : 0u };
            dkCmdBufPushConstants(Cmd, draw.addr, draw.size, 0, sizeof(params), params);
            DkDepthStencilState ds;
            dkDepthStencilStateDefaults(&ds);
            ds.depthTestEnable = true;
            ds.depthWriteEnable = d.DepthWrite;
            ds.depthCompareOp = f.WDepth ? (d.DepthLequal ? DkCompareOp_Gequal : DkCompareOp_Greater)
                                         : (d.DepthLequal ? DkCompareOp_Lequal : DkCompareOp_Less);
            ds.stencilTestEnable = true;
            const DkCompareOp op = d.StencilTest == WideGpuStencil::Always ? DkCompareOp_Always :
                d.StencilTest == WideGpuStencil::Equal ? DkCompareOp_Equal : DkCompareOp_NotEqual;
            ds.stencilFrontFailOp = ds.stencilBackFailOp = DkStencilOp_Keep;
            ds.stencilFrontDepthFailOp = ds.stencilBackDepthFailOp = DkStencilOp_Keep;
            ds.stencilFrontPassOp = ds.stencilBackPassOp = DkStencilOp_Replace;
            ds.stencilFrontCompareOp = ds.stencilBackCompareOp = op;
            dkCmdBufBindDepthStencilState(Cmd, &ds);
            dkCmdBufSetStencil(Cmd, DkFace_FrontAndBack, 0xFF, d.StencilRef, d.StencilMask);
            DkColorState cs;
            dkColorStateDefaults(&cs);
            dkColorStateSetBlendEnable(&cs, 0, d.Blend);
            dkCmdBufBindColorState(Cmd, &cs);
            DkColorWriteState cw;
            dkColorWriteStateDefaults(&cw);
            dkColorWriteStateSetMask(&cw, 0, DkColorMask_RGBA);
            dkColorWriteStateSetMask(&cw, 1, (d.WriteDepthAttr ? DkColorMask_R : 0) | (d.WriteFogAttr ? DkColorMask_G : 0));
            dkCmdBufBindColorWriteState(Cmd, &cw);
            dkCmdBufDraw(Cmd, DkPrimitive_Triangles, d.Count, 1, 0, 0);
        }

        // The final pass reads what was drawn.
        dkCmdBufBarrier(Cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
        const DkImageView* finalTarget[1] = { &finalView };
        dkCmdBufBindRenderTargets(Cmd, finalTarget, 1, nullptr);
        DkDepthStencilState off;
        dkDepthStencilStateDefaults(&off);
        off.depthTestEnable = false;
        off.depthWriteEnable = false;
        off.stencilTestEnable = false;
        dkCmdBufBindDepthStencilState(Cmd, &off);
        DkColorState plain;
        dkColorStateDefaults(&plain);
        dkCmdBufBindColorState(Cmd, &plain);
        dkCmdBufBindColorWriteState(Cmd, &all);
        const DkShader* fin[2] = { &finalVS, &finalFS };
        dkCmdBufBindShaders(Cmd, DkStageFlag_GraphicsMask, fin, 2);
        const DkResHandle tex[2] = { dkMakeTextureHandle(ImgColor, 0), dkMakeTextureHandle(ImgAttr, 0) };
        dkCmdBufBindTextures(Cmd, DkStage_Fragment, 0, tex, 2);
        dkCmdBufDraw(Cmd, DkPrimitive_Triangles, 3, 1, 0, 0);

        dkCmdBufBarrier(Cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
        dkCmdBufSignalFence(Cmd, &ready, true);
        // The preceding DS compositor may still sample this image. Queue the
        // producer after it, then order the next consumer after the producer.
        dkQueueSignalFence(Gfx::EmuQueue, &released, true);
        dkQueueFlush(Gfx::EmuQueue);
        dkQueueWaitFence(Queue, &released);
        dkQueueSubmitCommands(Queue, dkCmdBufFinishList(Cmd));
        dkQueueFlush(Queue);
        dkQueueWaitFence(Gfx::EmuQueue, &ready);
        pending = true;
        return true;
    }
private:
    bool Wait() {
        if (pending && dkFenceWait(&ready, 1000000000LL) != DkResult_Success)
            return false;
        pending = false;
        return true;
    }
    bool Image(DkImage& image, DkMemBlock& mem, DkImageFormat format, int w, int h, u32 flags)
    {
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, Gfx::Device);
        lm.flags = flags;
        lm.format = format;
        lm.dimensions[0] = (u32)w;
        lm.dimensions[1] = (u32)h;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        mem = Block((u32)dkImageLayoutGetSize(&layout) + dkImageLayoutGetAlignment(&layout), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        if (!mem) return false;
        dkImageInitialize(&image, &layout, mem, 0);
        return true;
    }

    void FreeTargets()
    {
        for (DkMemBlock* m : { &ColorMem, &AttrMem, &DepthMem })
        {
            if (*m) dkMemBlockDestroy(*m);
            *m = nullptr;
        }
        W = H = 0;
    }

    bool MakeTargets(int w, int h)
    {
        dkQueueWaitIdle(Queue);
        FreeTargets();
        const u32 render = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
        if (!Image(ColorImage, ColorMem, DkImageFormat_RGBA8_Unorm, w, h, render) ||
            !Image(AttrImage, AttrMem, DkImageFormat_RG32_Uint, w, h, render) ||
            !Image(DepthImage, DepthMem, DkImageFormat_ZF32_X24S8, w, h, render))
        {
            fprintf(stderr, "[widegpu] deko3D images of %dx%d could not be had\n", w, h);
            FreeTargets();
            return false;
        }
        // The final pass's two textures.
        DkImageView colorView, attrView;
        dkImageViewDefaults(&colorView, &ColorImage);
        dkImageViewDefaults(&attrView, &AttrImage);
        DkImageDescriptor desc[2];
        dkImageDescriptorInitialize(&desc[0], &colorView, false, false);
        dkImageDescriptorInitialize(&desc[1], &attrView, false, false);
        memcpy(Data + OffImageDesc + ImgColor * sizeof(desc[0]), desc, sizeof(desc));
        W = w;
        H = h;
        fprintf(stderr, "[widegpu] deko3D targets %dx%d\n", w, h);
        return true;
    }

    dk::Shader mainVS, mainFS, earlyFS, finalVS, finalFS;
    bool haveEarly = false;
    DkMemBlock CmdMem{}, DataMem{}, ColorMem{}, AttrMem{}, DepthMem{};
    DkImage ColorImage{}, AttrImage{}, DepthImage{}, Tex8Image{}, Tex16Image{}, Pal16Image{}, AtlasImage{};
    DkQueue Queue{};
    DkCmdBuf Cmd{};
    DkFence ready{}, released{};
    u8* Data{};
    DkGpuAddr DataAddr{};
    int W{}, H{};
    bool pending{}, AtlasSynced{};
    u64 TexVersion{};
};
}
std::unique_ptr<Raster> CreateRaster() {
    auto raster = std::make_unique<DekoRaster>();
    if (!raster->Init()) return nullptr;
    return raster;
}
}
