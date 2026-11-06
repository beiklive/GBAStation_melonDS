#pragma once

#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>

namespace Core11 {
enum class SeedResult { Existing, Missing, Copied, Error };

// libnx has no usable inode identity or fchmod for std::filesystem::copy_file.
// Only the disposable .seed file may be truncated; existing saves are preserved.
inline SeedResult SeedBatterySave(const std::filesystem::path &source,
                                 const std::filesystem::path &target,
                                 std::string &error) {
    namespace fs = std::filesystem;
    error.clear();
    const fs::path staging = target.string() + ".seed";
    const fs::path backup = target.string() + ".bak";
    auto fail = [&](const char *step, const fs::path &path, std::error_code ec) {
        error = std::string(step) + " path=" + path.string() + ": " + ec.message();
        return SeedResult::Error;
    };
    auto ioError = [] {
        return std::error_code(errno ? errno : EIO, std::generic_category());
    };
    std::error_code ec;
    for (const auto &path : {target, backup}) {
        const bool present = fs::exists(path, ec);
        if (ec) return fail("stat", path, ec);
        if (present) return SeedResult::Existing;
    }
    const bool present = fs::exists(source, ec);
    if (ec) return fail("stat", source, ec);
    if (!present) return SeedResult::Missing;
    if (!fs::is_regular_file(source, ec))
        return fail("source is not a regular file", source,
                    ec ? ec : std::make_error_code(std::errc::invalid_argument));

    fs::create_directories(target.parent_path(), ec);
    if (ec) return fail("mkdir", target.parent_path(), ec);
    errno = 0;
    FILE *in = std::fopen(source.c_str(), "rb");
    if (!in) return fail("open source", source, ioError());
    errno = 0;
    FILE *out = std::fopen(staging.c_str(), "wb");
    if (!out) {
        const auto saved = ioError();
        std::fclose(in);
        return fail("open staging", staging, saved);
    }
    std::array<unsigned char, 32768> buffer;
    SeedResult result = SeedResult::Copied;
    for (;;) {
        errno = 0;
        const auto size = std::fread(buffer.data(), 1, buffer.size(), in);
        if (std::ferror(in)) {
            result = fail("read source", source, ioError());
            break;
        }
        errno = 0;
        if (size && std::fwrite(buffer.data(), 1, size, out) != size) {
            result = fail("write staging", staging, ioError());
            break;
        }
        if (size < buffer.size()) break;
    }
    errno = 0;
    if (std::fclose(in) && result != SeedResult::Error)
        result = fail("close source", source, ioError());
    errno = 0;
    if (std::fclose(out) && result != SeedResult::Error)
        result = fail("close staging", staging, ioError());
    if (result == SeedResult::Error) {
        fs::remove(staging, ec);
        return result;
    }
    // Recheck before publishing; never replace progress created while copying.
    for (const auto &path : {target, backup}) {
        const bool exists = fs::exists(path, ec);
        if (ec) return fail("stat before rename", path, ec);
        if (exists) {
            fs::remove(staging, ec);
            return SeedResult::Existing;
        }
    }
    fs::rename(staging, target, ec);
    if (ec) return fail("rename staging", target, ec);
    return SeedResult::Copied;
}
} // namespace Core11
