// c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined tests/core11_save_seed.cpp -o /tmp/core11_save_seed && /tmp/core11_save_seed
#include "../core11/SaveSeed.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>

namespace fs = std::filesystem;
static void write(const fs::path &path, const std::string &bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), bytes.size());
    f.close();
    assert(f.good());
}
static std::string read(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    assert(f.good());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
int main() {
    char name[] = "/tmp/core11-save-seed-XXXXXX";
    const auto directory = mkdtemp(name);
    assert(directory);
    const fs::path root = directory;
    const auto source = root / "black.sav";
    const auto target = root / "core11/black.sav";
    const fs::path staging = target.string() + ".seed";
    const fs::path backup = target.string() + ".bak";
    std::string error;
    using Core11::SeedBatterySave;
    using Core11::SeedResult;
    assert(SeedBatterySave(source, target, error) == SeedResult::Missing);
    assert(!fs::exists(target));
    std::string original(512 * 1024 + 19, '\0');
    for (size_t i = 0; i < original.size(); ++i) original[i] = (i * 73 + i / 251) & 255;
    write(source, original);
    assert(SeedBatterySave(source, target, error) == SeedResult::Copied);
    assert(read(target) == original && read(source) == original);
    assert(!fs::exists(staging) && error.empty());

    // Retry after an old copy_file failure, including a zero-length .seed.
    for (const auto &partial : {std::string(), std::string("partial save")}) {
        fs::remove(target);
        write(staging, partial);
        assert(SeedBatterySave(source, target, error) == SeedResult::Copied);
        assert(read(target) == original && read(source) == original);
        assert(!fs::exists(staging));
    }
    write(target, "new core progress");
    assert(SeedBatterySave(source, target, error) == SeedResult::Existing);
    assert(read(target) == "new core progress");
    fs::remove(target);
    write(backup, "recoverable progress");
    assert(SeedBatterySave(source, target, error) == SeedResult::Existing);
    assert(read(backup) == "recoverable progress" && !fs::exists(target));
    fs::remove(backup);

    // A failed staging open must neither publish a save nor modify the source.
    fs::create_directory(staging);
    assert(SeedBatterySave(source, target, error) == SeedResult::Error);
    assert(error.find("open staging") != std::string::npos);
    assert(!fs::exists(target) && read(source) == original);
    fs::remove(staging);
    assert(SeedBatterySave(source, target, error) == SeedResult::Copied);
    assert(error.empty() && read(target) == original);
    fs::remove_all(root);
    std::cout << "PASS: byte copy, stale staging retry, existing save/backup protection, failure recovery\n";
}
