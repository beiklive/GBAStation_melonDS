#include "Session.h"
#include "Args.h"
#include "NDS.h"
#include "GPU2D_Soft.h"
#include "GPU3D_Soft.h"
#include "GPU3D_Wide.h"
#include "GPU3D_WideGpu.h"
#include "SsrThreads.h"
#include "host.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

extern "C" void GBAStationNDSStubLogLine(const char *) __attribute__((weak));
namespace fs = std::filesystem;
namespace {
std::unique_ptr<melonDS::NDS> console;
std::string savePath;
std::string lastError;
bool saveDirty;
unsigned saveFrames;
int scale = 1;
std::vector<std::int16_t> mic;
size_t micOffset;
unsigned perfFrames;
std::uint64_t runNs;

void logLine(const char *line) {
    if (GBAStationNDSStubLogLine) GBAStationNDSStubLogLine(line);
    else std::fprintf(stderr, "%s\n", line);
}
bool fail(const std::string &reason) {
    lastError = reason;
    logLine(("[core11-load] FAILED: " + reason).c_str());
    return false;
}

const char *nativeRendererName() {
    if (console && dynamic_cast<melonDS::WideGpuDSRenderer *>(&console->GPU.GetRenderer3D()))
        return "gpu";
    return "software";
}

void finishGpuWork() {
    melonDS::WideWait();
    // The native GPU renderer has its own sender/fetcher jobs. WideWait only
    // finishes the enhanced picture; those native jobs also read console state.
    melonDS::WideGpuSendJoin(-2);
    if (console)
        if (auto *gpu = dynamic_cast<melonDS::WideGpuDSRenderer *>(&console->GPU.GetRenderer3D()))
            gpu->GetLine(0);
}

struct Blob {
    std::unique_ptr<std::uint8_t[]> bytes;
    std::uint32_t size = 0;
};
bool readFile(const std::string &path, Blob &out, size_t maximum, bool allowEmpty = false) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec)
        return fail("stat " + path + ": " + ec.message());
    if (!size && allowEmpty) {
        out = {};
        logLine(("[core11-load] empty battery save; starting without SRAM: " + path).c_str());
        return true;
    }
    if (!size || size > maximum)
        return fail("invalid file size " + path + ": bytes=" + std::to_string(size) +
                    " maximum=" + std::to_string(maximum));
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return fail("open " + path + ": " + std::strerror(errno));
    out.bytes = std::make_unique<std::uint8_t[]>(size);
    out.size = size;
    auto got = std::fread(out.bytes.get(), 1, size, f);
    const int readError = errno;
    std::fclose(f);
    if (got != size)
        return fail("read " + path + ": bytes=" + std::to_string(got) + "/" +
                    std::to_string(size) + " errno=" + std::to_string(readError));
    return true;
}
bool writeFile(const std::string &path, const void *data, size_t size) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}
template <class Array> bool readBios(const std::string &path, Array &out) {
    Blob b;
    if (!readFile(path, b, out.size()))
        return false;
    if (b.size != out.size())
        return fail("invalid BIOS size " + path + ": bytes=" + std::to_string(b.size));
    std::copy_n(b.bytes.get(), b.size, out.begin());
    return true;
}
} // namespace
namespace Host {
bool StopRequested = false;
int LogLevel = 0;
void SaveDirty(const uint8_t *, uint32_t) { saveDirty = true; }
} // namespace Host
namespace melonDS {
// This frontend runs arbitrary DS cartridges and has no statically recompiled game.
bool RecompRun(ARMv5 *) { return false; }
} // namespace melonDS
namespace Core11 {
const std::string &LastError() { return lastError; }
int ReadMic(std::int16_t *dst, int count) {
    const size_t n = std::min<size_t>(count, mic.size() - micOffset);
    if (n)
        std::copy_n(mic.data() + micOffset, n, dst);
    micOffset += n;
    return n;
}
void Microphone(const std::int16_t *data, int count) {
    mic.clear();
    micOffset = 0;
    if (data && count > 0)
        mic.assign(data, data + count);
}
void FlushSave() {
    if (!saveDirty || !console || !console->GetNDSSaveLength())
        return;
    const std::string tmp = savePath + ".tmp", bak = savePath + ".bak";
    if (!writeFile(tmp, console->GetNDSSave(), console->GetNDSSaveLength()))
        return;
    std::error_code ec;
    if (fs::exists(savePath, ec)) {
        fs::remove(bak, ec);
        fs::rename(savePath, bak, ec);
        if (ec)
            return;
    }
    fs::rename(tmp, savePath, ec);
    if (ec) {
        if (fs::exists(bak))
            fs::rename(bak, savePath, ec);
        return;
    }
    saveDirty = false;
}
void Stop() {
    if (!console)
        return;
    FlushSave();
    // Finish native rendering before releasing the GPU state its thread reads.
    if (auto *soft = dynamic_cast<melonDS::SoftRenderer *>(&console->GPU.GetRenderer3D()))
        soft->SetThreaded(false, console->GPU);
    finishGpuWork();
    melonDS::Wide.Owner = nullptr;
    melonDS::WideShutdown();
    melonDS::WideGpuStop();
    console->Stop();
    console.reset();
    melonDS::Wide = {};
    Microphone(nullptr, 0);
}
bool Load(const Settings &cfg, const std::string &romPath, const std::string &save) {
    Stop();
    lastError.clear();
    melonDS::NDSArgs args;
    if ((!cfg.bios9.empty() && !readBios(cfg.bios9, *args.ARM9BIOS)) ||
        (!cfg.bios7.empty() && !readBios(cfg.bios7, *args.ARM7BIOS)))
        return false;
    Blob fw, rom, sram;
    if (!cfg.firmware.empty()) {
        if (!readFile(cfg.firmware, fw, 512 * 1024))
            return false;
        if (fw.size != 128 * 1024 && fw.size != 256 * 1024 && fw.size != 512 * 1024)
            return fail("invalid firmware size " + cfg.firmware + ": bytes=" + std::to_string(fw.size));
        args.Firmware = melonDS::Firmware(fw.bytes.get(), fw.size);
    }
    if (cfg.language >= 0 && args.Firmware.GetUserDataOffset() + 512 <= args.Firmware.Length())
        for (auto &user : args.Firmware.GetUserData()) {
            user.Settings = (user.Settings & ~7u) | cfg.language;
            user.UpdateChecksum();
        }
    args.JIT = cfg.jit ? std::optional<melonDS::JITArgs>{{static_cast<unsigned>(cfg.blockSize), cfg.literal,
                                                          cfg.branch, cfg.fastmem}}
                       : std::nullopt;
    args.OutputSampleRate = 32823.;
    if (!readFile(romPath, rom, 512ULL * 1024 * 1024))
        return false;
    melonDS::NDSCart::NDSCartArgs cartArgs;
    if (!cfg.dldiImage.empty())
        cartArgs.SDCard = melonDS::FATStorageArgs{cfg.dldiImage, 0, false, std::nullopt};
    std::error_code ec;
    bool hasSave = fs::exists(save, ec);
    if (ec) return fail("stat save " + save + ": " + ec.message());
    if (!hasSave && fs::exists(save + ".bak", ec)) {
        fs::rename(save + ".bak", save, ec);
        if (ec)
            return fail("restore backup " + save + ": " + ec.message());
        hasSave = true;
    }
    if (ec) return fail("stat backup " + save + ".bak: " + ec.message());
    if (hasSave) {
        if (!readFile(save, sram, 128 * 1024 * 1024, true))
            return false;
        cartArgs.SRAMLength = sram.size;
        cartArgs.SRAM = std::move(sram.bytes);
    }
    logLine(("[core11-load] ROM bytes=" + std::to_string(rom.size) + " save bytes=" +
             std::to_string(sram.size) + " present=" + std::to_string(hasSave)).c_str());
    auto cart = melonDS::NDSCart::ParseROM(std::move(rom.bytes), rom.size, nullptr, std::move(cartArgs));
    if (!cart)
        return fail("cartridge parse rejected " + romPath);
    savePath = save;
    saveDirty = Host::StopRequested = false;
    saveFrames = 0;
    console = std::make_unique<melonDS::NDS>(std::move(args));
    console->SetNDSCart(std::move(cart));
    console->Reset();
    if (cfg.directBoot || console->NeedsDirectBoot())
        console->SetupDirectBoot(fs::path(romPath).filename().string());
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    console->RTC.SetDateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                             local.tm_min, local.tm_sec);
    console->GPU.GPU3D.InterpolationEnabled = false;
    perfFrames = 0;
    runNs = 0;
    SsrStats().Wait3DNs = 0;
    SsrStats().WaitWideNs = 0;
    for (auto &count : SsrStats().RenderCore) count = 0;
#ifdef __SWITCH__
    std::string info;
    if (!melonDS::WideGpuStart(info)) {
        Stop();
        return fail("enhanced GPU init: " + info);
    }
    // Match the donor Switch frontend: the native DS picture (including the
    // 3D scanlines used by display capture) also comes from deko3D, even at 1x.
    auto native = melonDS::WideGpuMakeDS(info);
    if (!native) {
        Stop();
        return fail("native GPU init: " + info);
    }
    console->GPU.GPU3D.SetCurrentRenderer(std::move(native));
    logLine("[core11] native3D=deko3D enhanced3D=deko3D");
#else
    if (auto *soft = dynamic_cast<melonDS::SoftRenderer *>(&console->GPU.GetRenderer3D()))
        soft->SetThreaded(cfg.threadedRenderer, console->GPU);
    logLine(cfg.threadedRenderer ? "[core11] native3D=threaded" : "[core11] native3D=inline");
#endif
    SetScale(scale);
    console->Start();
    LogState();
    return true;
}
void SetScale(int value) {
    scale = std::clamp(value, 1, 10);
    if (!console)
        return;
    finishGpuWork();
    melonDS::Wide.Width = 256 * scale;
    melonDS::Wide.Height = 192 * scale;
    melonDS::Wide.ViewWidth = 256;
    melonDS::Wide.ExtendEdges = false;
    melonDS::Wide.Owner = scale > 1 ? &console->GPU.GPU3D : nullptr;
}
bool Run() {
    if (!console || !console->IsRunning())
        return false;
    const auto begin = std::chrono::steady_clock::now();
    console->RunFrame();
    if (++saveFrames >= 120) {
        FlushSave();
        saveFrames = 0;
    }
    runNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::steady_clock::now() - begin).count();
    if (++perfFrames == 240) {
        char line[256];
        const double divisor = perfFrames * 1e6;
        std::snprintf(line, sizeof(line),
                      "[core11-perf] frames=%u jit=%d scale=%d native=%s emu=%.2fms wait3D=%.2fms waitWide=%.2fms renderCores=%u/%u/%u/%u",
                      perfFrames, console->IsJITEnabled(), scale, nativeRendererName(), runNs / divisor,
                      SsrStats().Wait3DNs.exchange(0) / divisor, SsrStats().WaitWideNs.exchange(0) / divisor,
                      SsrStats().RenderCore[0].exchange(0), SsrStats().RenderCore[1].exchange(0),
                      SsrStats().RenderCore[2].exchange(0), SsrStats().RenderCore[3].exchange(0));
        logLine(line);
        perfFrames = 0;
        runNs = 0;
    }
    return !Host::StopRequested && console->IsRunning();
}
void Keys(unsigned mask) {
    if (console)
        console->SetKeyMask(mask);
}
void Touch(int x, int y) {
    if (console) {
        if (x < 0)
            console->ReleaseScreen();
        else
            console->TouchScreen(x, y);
    }
}
void Lid(bool closed) {
    if (console)
        console->SetLidClosed(closed);
}
int Audio(std::int16_t *dst, int count) { return console ? console->SPU.ReadOutput(dst, count) : 0; }
void DrainAudio() {
    if (console)
        console->SPU.DrainOutput();
}
void Cheats(const std::vector<Cheat> &cheats) {
    if (!console)
        return;
    auto &list = console->AREngine.Cheats;
    list.clear();
    for (const auto &c : cheats) {
        list.push_back({nullptr, c.name, {}, true, c.words});
        if (c.words.size() >= 2 && (c.words[0] >> 28) == 5) {
            const auto address = c.words[0] & 0x0FFFFFFF;
            if (address) {
                char line[192];
                std::snprintf(line, sizeof(line),
                              "[core11-cheats] guard32 addr=%08X actual=%08X expected=%08X name=%s",
                              address, console->ARM7Read32(address), c.words[1], c.name.c_str());
                logLine(line);
            }
        }
    }
}
Screen GetScreen(unsigned screen) {
    if (!console)
        return {};
    unsigned top = (console->PowerControl9 & 0x8000) ? 0 : 1;
    auto &gpu = console->GPU;
    auto &renderer = static_cast<melonDS::GPU2D::SoftRenderer &>(gpu.GetRenderer2D());
    const auto unit = top ^ screen;
    const auto disp = gpu.GPU2D_A.DispCnt;
    const bool engine3D = unit == 0 && (disp & (3u << 16)) == (1u << 16) && (disp & (1u << 3)) &&
                          !(disp & (1u << 7)) && gpu.GPU3D.GetRenderXPos() == 0;
    return {gpu.Framebuffer[gpu.FrontBuffer][screen].get(), renderer.LayerFlags[unit],
            renderer.LineBlend[unit], engine3D};
}
const std::uint32_t *GetWorld(int &w, int &h, std::uint64_t &serial) {
    w = melonDS::Wide.FrontW;
    h = melonDS::Wide.FrontH;
    serial = melonDS::Wide.FrontSerial;
    return scale > 1 && w == 256 * scale && h == 192 * scale ? melonDS::Wide.Front : nullptr;
}
bool SaveState(const std::string &path) {
    if (!console)
        return false;
    finishGpuWork();
    melonDS::Savestate state;
    if (!console->DoSavestate(&state))
        return false;
    state.Finish();
    return !state.Error && writeFile(path, state.Buffer(), state.Length());
}
bool LoadState(const std::string &path) {
    Blob b;
    if (!console || !readFile(path, b, 128 * 1024 * 1024))
        return false;
    finishGpuWork();
    melonDS::Savestate state(b.bytes.get(), b.size, false);
    if (state.Error)
        return false;
    melonDS::Savestate rollback;
    if (!console->DoSavestate(&rollback) || rollback.Error)
        return false;
    rollback.Finish();
    if (!console->DoSavestate(&state) || state.Error) {
        melonDS::Savestate previous(rollback.Buffer(), rollback.Length(), false);
        console->DoSavestate(&previous);
        return false;
    }
    // Do not mix a wide image produced before the load with the restored screens.
    melonDS::Wide.Front = nullptr;
    melonDS::Wide.FrontW = melonDS::Wide.FrontH = 0;
    ++melonDS::Wide.FrontSerial;
    return true;
}
void LogState() {
    if (!console)
        return;
    std::uint32_t hashes[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
        auto p = GetScreen(i).pixels;
        if (p)
            for (unsigned j = 0; j < 256 * 192; j++)
                hashes[i] = (hashes[i] ^ p[j]) * 16777619u;
    }
    char line[192];
    std::snprintf(line, sizeof(line),
                  "[core11] jit=%d scale=%d ARM9=%08X ARM7=%08X brightness=%04X/%04X screens=%08X/%08X",
                  console->IsJITEnabled(), scale, console->GetPC(0), console->GetPC(1),
                  console->GPU.GPU2D_A.MasterBrightness, console->GPU.GPU2D_B.MasterBrightness, hashes[0],
                  hashes[1]);
    if (GBAStationNDSStubLogLine)
        GBAStationNDSStubLogLine(line);
    else
        std::fprintf(stderr, "%s\n", line);
}
} // namespace Core11
namespace melonDS::Platform {
int Mic_ReadInput(s16 *dst, int count, void *) { return Core11::ReadMic(dst, count); }
} // namespace melonDS::Platform
