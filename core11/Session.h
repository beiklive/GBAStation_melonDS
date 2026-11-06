#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Core11 {
struct Settings {
    std::string bios9, bios7, firmware, dldiImage;
    bool jit = true, branch = true, literal = true, fastmem = true, directBoot = true;
    bool threadedRenderer = true;
    int blockSize = 32, language = -1;
};
bool Load(const Settings &, const std::string &rom, const std::string &save);
const std::string &LastError();
void Stop();
bool Run();
void FlushSave();
bool SaveState(const std::string &path);
bool LoadState(const std::string &path);
void Keys(unsigned mask);
void Touch(int x, int y);
void Lid(bool closed);
void Microphone(const std::int16_t *samples, int count);
int Audio(std::int16_t *dst, int count);
void DrainAudio();
struct Cheat {
    std::string name;
    std::vector<std::uint32_t> words;
};
void Cheats(const std::vector<Cheat> &cheats);
void SetScale(int scale);
struct Screen {
    const std::uint32_t *pixels;
    const std::uint8_t *layers;
    const std::uint16_t (*effects)[3];
    bool engine3D;
    bool CanEnhanceLine(unsigned y) const {
        const auto *fx = effects[y];
        // Fade and color effects use the original DS composited pixel.
        bool masterFade = (fx[2] & 0x1f) && ((fx[2] >> 14) == 1 || (fx[2] >> 14) == 2);
        bool bgFade = (fx[0] & 1) && (fx[0] & 0x80) && fx[1];
        return engine3D && !masterFade && !bgFade;
    }
};
Screen GetScreen(unsigned screen);
const std::uint32_t *GetWorld(int &w, int &h, std::uint64_t &serial);
void LogState();
} // namespace Core11
