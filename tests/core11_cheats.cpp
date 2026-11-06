// White-box integration check: inspect emulated RAM without adding a debug API
// to the shipped frontend. Cheats still enter through the public Session API.
#include "../core11/Session.cpp"
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include "nds_stub/NdsCheatDatabase.hpp"
extern "C" void GBAStationNDSStubLogLine(const char *line) { std::fprintf(stderr, "%s\n", line); }
namespace beiklive::nds_stub {
void appendStubLog(const char *format, ...) {
    va_list args; va_start(args, format);
    std::vfprintf(stderr, format, args); std::fputc('\n', stderr);
    va_end(args);
}
}

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); Core11::Stop(); std::exit(1); }
}
static void frames(int n) { while (n--) check(Core11::Run(), "run frame"); }
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 2;
    char directory[] = "/tmp/core11-cheats-XXXXXX";
    check(mkdtemp(directory), "temporary directory");
    const auto save = std::string(directory) + "/test.sav";
    const auto originalDirectory = fs::current_path();
    const auto rom = fs::absolute(argv[1]).string();
    beiklive::nds_stub::NdsCheatLoadResult database;
    if (argc == 3) {
        const auto dat = fs::absolute(argv[2]);
        const auto destination = fs::path(directory) / "sdmc:/GBAStation/cheats/usrcheat.dat";
        fs::create_directories(destination.parent_path());
        fs::create_symlink(dat, destination);
        fs::current_path(directory);
        database = beiklive::nds_stub::LoadUsrCheatDatForRom(rom);
        check(database.gameMatched && !database.items.empty(), "usrcheat ROM match and parse");
    }
    for (bool jit : {false, true}) {
        Core11::Settings cfg;
        cfg.jit = jit;
        // Reproduce the user's empty .sav without changing their source file.
        check(writeFile(save, "", 0), "create empty save");
        check(Core11::Load(cfg, rom, save), "load ROM with zero-byte battery save");
        check(fs::file_size(save) == 0, "load does not overwrite empty save");
        frames(600);
        constexpr unsigned address = 0x023FE000, marker = 0x91E7CAFE;
        console->ARM7Write32(address, 0);
        Core11::Cheats({{"integration marker", {address, marker}}});
        frames(30);
        std::fprintf(stderr, "cheat jit=%d actual=%08X expected=%08X\n", jit,
                     console->ARM7Read32(address), marker);
        check(console->ARM7Read32(address) == marker, "VBlank cheat writes RAM");
        Core11::Cheats({});
        console->ARM7Write32(address, 0);
        frames(30);
        check(console->ARM7Read32(address) == 0, "disabled cheat stops writing");
        for (const auto &item : database.items) {
            if (item.name != "亿万长者" && item.name != "[SELECT]金钱最大") continue;
            const bool conditional = item.words.size() == 6;
            const auto target = item.words[conditional ? 2 : 0];
            const auto value = item.words[conditional ? 3 : 1];
            check(target == 0x0219F768 && value == 0x05F5E100, "MM3 database money code");
            Core11::Keys(0xFFF);
            console->ARM7Write32(target, 0);
            Core11::Cheats({{item.name, item.words}});
            frames(30);
            if (conditional) {
                check(console->ARM7Read32(target) == 0, "SELECT code waits for key");
                Core11::Keys(0xFFF & ~4u);
                frames(30);
            }
            check(console->ARM7Read32(target) == value, "actual usrcheat code writes game RAM");
            std::fprintf(stderr, "database cheat jit=%d name=%s value=%08X passed\n", jit,
                         item.name.c_str(), console->ARM7Read32(target));
            Core11::Cheats({});
            Core11::Keys(0xFFF);
        }
        Core11::Stop();
    }
    Core11::Settings cfg;
    check(!Core11::Load(cfg, rom, directory), "directory is not a valid save");
    check(!Core11::LastError().empty(), "unreadable save reports reason");
    check(!Core11::Load(cfg, rom + ".missing", save), "missing ROM still fails");
    check(Core11::LastError().find(".missing") != std::string::npos, "error includes failing path");
    fs::current_path(originalDirectory);
    std::filesystem::remove_all(directory);
    std::puts("cheats: interpreter and JIT enable/disable passed");
}
