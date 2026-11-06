#include "Video.h"
#include "Session.h"
#include "frontend/switch/Gfx.h"
#include "nds_stub/StubLog.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace GPU2D {
namespace {
constexpr std::uint32_t pixels = 256 * 192;
std::uint32_t aligned(std::uint32_t n, std::uint32_t a) { return (n + a - 1) & ~(a - 1); }
constexpr unsigned descriptorBytes = DK_IMAGE_DESCRIPTOR_ALIGNMENT * 2;
} // namespace
DekoRenderer::DekoRenderer() {
    shaderOk = Gfx::LoadShader("romfs:/shaders/Core11Compose.dksh", shader);
    cmd = dk::CmdBufMaker{Gfx::Device}.create();
    commands = dk::MemBlockMaker{Gfx::Device, 64 * 1024}
                   .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                   .create();
    SetRenderScale(1);
}
DekoRenderer::~DekoRenderer() {
    Gfx::EmuQueue.waitIdle();
    Gfx::PresentQueue.waitIdle();
}
void DekoRenderer::SetRenderScale(int n) {
    n = std::clamp(n, 1, 10);
    if (n == scale)
        return;
    dk::ImageLayout layout;
    dk::ImageLayoutMaker{Gfx::Device}
        .setDimensions(256 * n, 192 * n)
        .setFlags(DkImageFlags_UsageLoadStore | DkImageFlags_Usage2DEngine)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .initialize(layout);
    const auto imageBytes = aligned(layout.getSize(), layout.getAlignment());
    const auto size = aligned(descriptorBytes + 8 + pixels * (2 + n * n) * 4, DK_MEMBLOCK_ALIGNMENT);
    dk::UniqueMemBlock next = dk::MemBlockMaker{Gfx::Device, aligned(imageBytes * 4, DK_MEMBLOCK_ALIGNMENT)}
                                  .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
                                  .create();
    dk::UniqueMemBlock data = dk::MemBlockMaker{Gfx::Device, size * 2}
                                  .setFlags(DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached)
                                  .create();
    if (!next || !data)
        return;
    Gfx::EmuQueue.waitIdle();
    Gfx::PresentQueue.waitIdle();
    memory = std::move(next);
    input = std::move(data);
    slotBytes = size;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < 2; i++) {
            images[s][i].initialize(layout, memory, imageBytes * (s * 2 + i));
            auto *descriptors =
                reinterpret_cast<dk::ImageDescriptor *>(static_cast<char *>(input.getCpuAddr()) + s * size);
            descriptors[i].initialize(images[s][i], true);
        }
    scale = n;
    worldSerial.fill(UINT64_MAX);
    perfFrames = 0;
    waitNs = uploadNs = submitNs = 0;
    Core11::SetScale(scale);
}
void DekoRenderer::Present() {
    if (!Ready())
        return;
    int w, h;
    std::uint64_t serial;
    const auto *world = Core11::GetWorld(w, h, serial);
    const int slot = front ^ 1;
    const auto begin = std::chrono::steady_clock::now();
    if (submitted)
        commandReady.wait();
    const auto waited = std::chrono::steady_clock::now();
    auto *source = reinterpret_cast<std::uint32_t *>(static_cast<char *>(input.getCpuAddr()) +
                                                     slot * slotBytes + descriptorBytes);
    source[0] = scale;
    source[1] = world != nullptr;
    const bool copyWorld = world && worldSerial[slot] != serial;
    if (copyWorld) {
        std::memcpy(source + 2 + pixels * 2, world, w * h * 4);
        worldSerial[slot] = serial;
    }
    for (int i = 0; i < 2; i++) {
        auto screen = Core11::GetScreen(i);
        if (!screen.pixels)
            return;
        for (unsigned y = 0; y < 192; y++) {
            bool plain = screen.CanEnhanceLine(y);
            for (unsigned x = 0; x < 256; x++) {
                unsigned j = y * 256 + x;
                source[2 + i * pixels + j] =
                    (screen.pixels[j] & 0xffffff) | ((plain && screen.layers[j] == 0x5F) ? 0xff000000u : 0);
            }
        }
    }
    // CPU cached storage avoids ~98,000 uncached pixel writes every frame, even
    // at 1x. Publish only the modified range before the GPU consumes this slot.
    const auto changedBytes = descriptorBytes + 8 + pixels * (2 + (copyWorld ? scale * scale : 0)) * 4;
    input.flushCpuCache(slot * slotBytes, aligned(changedBytes, 64));
    const auto uploaded = std::chrono::steady_clock::now();
    cmd.clear();
    cmd.addMemory(commands, 0, commands.getSize());
    if (presented[slot])
        cmd.waitFence(FramebufferPresented[slot]);
    const auto addr = input.getGpuAddr() + slot * slotBytes;
    cmd.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors | DkInvalidateFlags_L2Cache);
    cmd.bindImageDescriptorSet(addr, 2);
    cmd.bindImages(DkStage_Compute, 0, {dkMakeImageHandle(0), dkMakeImageHandle(1)});
    cmd.bindStorageBuffer(DkStage_Compute, 0, addr + descriptorBytes, slotBytes - descriptorBytes);
    cmd.bindShaders(DkStageFlag_Compute, {&shader});
    cmd.dispatchCompute(GetFramebufferWidth() / 8, GetFramebufferHeight() / 8, 1);
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    cmd.signalFence(FramebufferReady[slot]);
    cmd.signalFence(commandReady);
    Gfx::EmuQueue.submitCommands(cmd.finishList());
    Gfx::EmuQueue.flush();
    submitted = true;
    presented[slot] = true;
    front = slot;
    const auto submittedAt = std::chrono::steady_clock::now();
    auto ns = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count();
    };
    waitNs += ns(begin, waited);
    uploadNs += ns(waited, uploaded);
    submitNs += ns(uploaded, submittedAt);
    if (++perfFrames == 240) {
        const double divisor = perfFrames * 1e6;
        beiklive::nds_stub::appendStubLog(
            "[core11-video] frames=%u scale=%d wait=%.2fms upload=%.2fms submit=%.2fms",
            perfFrames, scale, waitNs / divisor, uploadNs / divisor, submitNs / divisor);
        perfFrames = 0;
        waitNs = uploadNs = submitNs = 0;
    }
}
bool DekoRenderer::ReadFramebufferRGBA(std::vector<std::uint8_t> &top, std::vector<std::uint8_t> &bottom) {
    if (!submitted)
        return false;
    const unsigned bytes = GetFramebufferWidth() * GetFramebufferHeight() * 4;
    dk::UniqueMemBlock read = dk::MemBlockMaker{Gfx::Device, aligned(bytes * 2, DK_MEMBLOCK_ALIGNMENT)}
                                  .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
                                  .create();
    if (!read)
        return false;
    commandReady.wait();
    cmd.clear();
    cmd.addMemory(commands, 0, commands.getSize());
    for (int i = 0; i < 2; i++)
        cmd.copyImageToBuffer(dk::ImageView{images[front][i]},
                              {0, 0, 0, (u32)GetFramebufferWidth(), (u32)GetFramebufferHeight(), 1},
                              {read.getGpuAddr() + i * bytes});
    Gfx::EmuQueue.submitCommands(cmd.finishList());
    Gfx::EmuQueue.waitIdle();
    const auto *p = static_cast<const std::uint8_t *>(read.getCpuAddr());
    top.assign(p, p + bytes);
    bottom.assign(p + bytes, p + bytes * 2);
    return true;
}
} // namespace GPU2D
