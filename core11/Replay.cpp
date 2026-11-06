#include "Session.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
extern "C" void GBAStationNDSStubLogLine(const char *msg) { std::fprintf(stderr, "%s\n", msg); }
int main(int argc, char **argv) {
    if (argc < 6 || argc > 8) {
        std::fprintf(stderr, "usage: replay BIOS_DIR ROM TEST_SAVE FRAMES jit|interp [v13-state [scale]]\n");
        return 2;
    }
    auto dir = std::filesystem::path(argv[1]);
    Core11::Settings cfg;
    if (const char *threaded = std::getenv("CORE11_RENDER_THREAD"))
        cfg.threadedRenderer = threaded[0] != '0';
    if (dir != "-") {
        cfg.bios9 = (dir / "bios9.bin").string();
        cfg.bios7 = (dir / "bios7.bin").string();
        cfg.firmware = (dir / "firmware.bin").string();
    }
    cfg.jit = std::string(argv[5]) == "jit";
    if (!Core11::Load(cfg, argv[2], argv[3]))
        return 1;
    struct StopOnExit {
        ~StopOnExit() { Core11::Stop(); }
    } guard;
    if (argc >= 7 && !Core11::LoadState(argv[6]))
        return 3;
    if (argc >= 8)
        Core11::SetScale(std::atoi(argv[7]));
    const int frames = std::atoi(argv[4]);
    if (frames <= 0)
        return 2;
    for (int i = 0; i < frames; i++) {
        if (!Core11::Run())
            return 1;
    }
    Core11::LogState();
    if (argc >= 8) {
        int w, h;
        std::uint64_t serial;
        if (!Core11::GetWorld(w, h, serial)) {
            std::fprintf(stderr, "no enhanced frame\n");
            return 4;
        }
        unsigned eligible = 0;
        for (unsigned s = 0; s < 2; s++) {
            auto screen = Core11::GetScreen(s);
            for (unsigned y = 0; y < 192; y++)
                if (screen.CanEnhanceLine(y))
                    for (unsigned x = 0; x < 256; x++)
                        eligible += screen.layers[y * 256 + x] == 0x5F;
        }
        std::printf("world=%dx%d serial=%llu enhancedNativePixels=%u\n", w, h, (unsigned long long)serial,
                    eligible);
    }
    for (unsigned s = 0; s < 2; s++) {
        auto p = Core11::GetScreen(s).pixels;
        if (!p)
            return 1;
        unsigned hash = 2166136261u;
        for (int i = 0; i < 256 * 192; i++)
            hash = (hash ^ p[i]) * 16777619u;
        std::printf("screen%u=%08x\n", s, hash);
    }
}
