#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <deko3d.hpp>

// The frontend consumes two buffered screens and the same ready/presented fences
// as its original renderer. The emulator itself runs entirely in Core11.
namespace GPU2D {
class DekoRenderer {
  public:
    DekoRenderer();
    ~DekoRenderer();
    void SetRenderScale(int);
    bool Ready() const { return memory && shaderOk && cmd && commands; }
    int GetRenderScale() const { return scale; }
    int GetFramebufferWidth() const { return scale * 256; }
    int GetFramebufferHeight() const { return scale * 192; }
    int GetFramebufferTextureWidth() const { return GetFramebufferWidth(); }
    int GetFramebufferTextureHeight() const { return GetFramebufferHeight(); }
    dk::Image &GetFramebuffer(int slot, int screen) { return images[slot][screen]; }
    void Present();
    int Front() const { return front; }
    void RequestFramebufferCapture() {}
    bool TakeCapturedFramebufferRGBA(std::vector<std::uint8_t> &, std::vector<std::uint8_t> &) {
        return false;
    }
    bool ReadFramebufferRGBA(std::vector<std::uint8_t> &, std::vector<std::uint8_t> &);
    dk::Fence FramebufferReady[2]{}, FramebufferPresented[2]{};

  private:
    int scale = 0, front = 0;
    bool shaderOk = false;
    dk::Shader shader;
    dk::UniqueMemBlock memory, input;
    dk::Image images[2][2]{};
    dk::UniqueCmdBuf cmd;
    dk::UniqueMemBlock commands;
    dk::Fence commandReady{};
    bool submitted = false, presented[2]{};
    std::uint32_t slotBytes = 0;
    std::array<std::uint64_t, 2> worldSerial{};
    unsigned perfFrames = 0;
    std::uint64_t waitNs = 0, uploadNs = 0, submitNs = 0;
};
} // namespace GPU2D
