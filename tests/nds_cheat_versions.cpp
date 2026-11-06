// c++ -std=c++17 -I nds_stub/include -I src -fsanitize=address,undefined tests/nds_cheat_versions.cpp -o /tmp/nds_cheat_versions && /tmp/nds_cheat_versions
#include "../nds_stub/src/NdsCheatDatabase.cpp"
#include "../src/CRC32.cpp"
#include <cassert>
#include <filesystem>
#include <unistd.h>
namespace beiklive::nds_stub { void appendStubLog(const char *, ...) {} }
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
static void word(Bytes &b, unsigned v) {
    for (unsigned n = 0; n < 4; ++n) b.push_back(v >> (n * 8));
}
static void name(Bytes &b, const std::string &s) {
    b.insert(b.end(), s.begin(), s.end()); b.push_back(0);
}
static void align(Bytes &b) { while (b.size() % 4) b.push_back(0); }
static Bytes record(const char *title, unsigned value) {
    Bytes b;
    name(b, title); align(b);
    word(b, 1); b.resize(b.size() + 32);
    // One enabled code, "write" plus empty description, two code words.
    word(b, (1u << 24) | 5u);
    name(b, "write"); name(b, ""); align(b);
    word(b, 2); word(b, 0x02001000); word(b, value);
    return b;
}
static void write(const fs::path &path, const Bytes &b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), b.size()); assert(f.good());
}
static void database(const fs::path &path, unsigned crc1, unsigned crc2) {
    Bytes b(0x100);
    std::memcpy(b.data(), "R4 CheatCode", 12);
    const auto a = record("version A", 1), c = record("version B", 2);
    word(b, 0x54534554); word(b, crc1); word(b, 0x130); word(b, 0);
    word(b, 0x54534554); word(b, crc2); word(b, 0x130 + a.size()); word(b, 0);
    b.resize(0x130);
    b.insert(b.end(), a.begin(), a.end()); b.insert(b.end(), c.begin(), c.end());
    write(path, b);
}
static void checkChoices(const beiklive::nds_stub::NdsCheatLoadResult &r) {
    assert(r.gameMatched && r.versionCount == 2 && r.items.size() == 4);
    assert(r.items[0].name == "version A" && r.items[2].name == "version B");
    for (int i = 0; i < 4; ++i) assert(!r.items[i].enabled);
    assert(r.items[1].parent == 0 && r.items[3].parent == 2);
    assert(r.items[1].versionGroup == 0 && r.items[3].versionGroup == 2);
    assert(r.items[1].words.back() == 1 && r.items[3].words.back() == 2);
}
int main() {
    char temp[] = "/tmp/nds-cheat-versions-XXXXXX";
    assert(mkdtemp(temp));
    const auto previous = fs::current_path(); fs::current_path(temp);
    const fs::path dat = "sdmc:/GBAStation/cheats/usrcheat.dat";
    fs::create_directories(dat.parent_path());
    Bytes header(512); std::memcpy(header.data() + 12, "TEST", 4);
    const auto crc = ~CRC32(header.data(), header.size());
    write("test.nds", header);
    database(dat, crc, crc ^ 1);
    auto r = beiklive::nds_stub::LoadUsrCheatDatForRom("test.nds");
    assert(r.gameMatched && r.checksumMatched && r.versionCount == 1);
    assert(r.items.size() == 1 && r.items[0].enabled && r.items[0].words.back() == 1);
    database(dat, crc ^ 1, crc ^ 2);
    r = beiklive::nds_stub::LoadUsrCheatDatForRom("test.nds");
    assert(!r.checksumMatched); checkChoices(r);
    database(dat, crc, crc);
    r = beiklive::nds_stub::LoadUsrCheatDatForRom("test.nds");
    assert(r.checksumMatched); checkChoices(r);
    fs::current_path(previous); fs::remove_all(temp);
    std::puts("cheat database: exact, missing and duplicated CRC selection passed");
}
