#include "Session.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <unistd.h>

extern "C" void GBAStationNDSStubLogLine(const char *line) { std::fprintf(stderr, "%s\n", line); }
static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); Core11::Stop(); std::exit(1); }
}
using Hash = std::array<unsigned, 2>;
static std::vector<Hash> run(unsigned frames) {
    std::vector<Hash> result;
    for (unsigned f = 0; f < frames; ++f) {
        check(Core11::Run(), "run frame");
        Hash hashes{};
        for (unsigned screen = 0; screen < 2; ++screen) {
            const auto *pixels = Core11::GetScreen(screen).pixels;
            check(pixels != nullptr, "screen pixels");
            for (unsigned p = 0; p < 256 * 192; ++p)
                hashes[screen] = (hashes[screen] ^ pixels[p]) * 16777619u;
        }
        result.push_back(hashes);
    }
    return result;
}
static std::vector<Hash> restoredFrames(unsigned frames) {
    // v13 saves emulated state, not the displayed/in-flight render buffers.
    // Let both sides rebuild those buffers before comparing their pixels.
    run(2);
    return run(frames);
}
int main(int argc, char **argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: render_thread_check ROM\n"); return 2; }
    char name[] = "/tmp/core11-render-thread-XXXXXX";
    check(mkdtemp(name) != nullptr, "temporary directory");
    const auto directory = std::filesystem::path(name);
    const auto save = (directory / "test.sav").string();
    const auto state = (directory / "test.ss1").string();
    Core11::Settings cfg;
    cfg.threadedRenderer = false;
    check(Core11::Load(cfg, argv[1], save), "load ROM");
    run(1200);
    check(Core11::SaveState(state), "save baseline state");
    check(Core11::LoadState(state), "restore baseline state");
    const auto baseline = restoredFrames(240);
    Core11::Stop();

    cfg.threadedRenderer = true;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        check(Core11::Load(cfg, argv[1], save), "reload threaded console");
        check(Core11::LoadState(state), "restore threaded state");
        check(restoredFrames(240) == baseline, "inline/threaded screen sequence");
        const auto resume = (directory / "resume.ss1").string();
        check(Core11::SaveState(resume), "save threaded state");
        check(Core11::LoadState(resume), "restore threaded state");
        const auto first = restoredFrames(60);
        check(Core11::LoadState(resume), "repeat restore");
        const auto second = restoredFrames(60);
        for (unsigned i = 0; i < first.size(); ++i)
            if (first[i] != second[i])
                std::fprintf(stderr, "roundtrip mismatch cycle=%u frame=%u %08x/%08x vs %08x/%08x\n", cycle, i,
                             first[i][0], first[i][1], second[i][0], second[i][1]);
        check(second == first, "threaded state roundtrip");
        Core11::Stop();
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: 240-frame screen sequences match, threaded state restore and 3 console reloads");
}
