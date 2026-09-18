#include "investment_backup.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "../../core/logging/log.h"

namespace sunrise::state::investment::store {
namespace {

/** FNV-1a constants, the same ones this codebase already uses elsewhere for content fingerprints. */
constexpr std::uint64_t kHashOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;
/** Hex digits one emitted checksum sidecar holds: one 64-bit FNV-1a value. */
constexpr std::size_t kChecksumHexDigits = 16;
/** Largest file this module will hash or copy through memory. Generous for an account database. */
constexpr std::size_t kMaxFileBytes = 64 * 1024 * 1024;

[[nodiscard]] std::uint64_t fnv1a(const std::vector<char>& data) noexcept {
    std::uint64_t hash = kHashOffsetBasis;
    for (const char byte : data) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= kHashPrime;
    }
    return hash;
}

/** Reads one whole file into memory. Returns false for a missing file -- the common case for a
 * backup early in an account's life -- without logging, since there is nothing wrong yet. */
[[nodiscard]] bool read_whole_file(const wchar_t* path, std::vector<char>& out) noexcept {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"rb") != 0 || file == nullptr) {
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size < 0 || static_cast<unsigned long>(size) > kMaxFileBytes
        || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    out.assign(static_cast<std::size_t>(size), '\0');
    const bool ok = size == 0 || std::fread(out.data(), 1, out.size(), file) == out.size();
    std::fclose(file);
    return ok;
}

[[nodiscard]] bool write_whole_file(const wchar_t* path, const std::vector<char>& data) noexcept {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"wb") != 0 || file == nullptr) {
        return false;
    }
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
    return std::fclose(file) == 0 && ok;
}

/** @return `path` with `suffix` appended, or false if the result would not fit. */
[[nodiscard]] bool suffixed(const core::path::Buffer& path,
                            std::wstring_view suffix,
                            core::path::Buffer& output) noexcept {
    output = path;
    return core::path::append(output, suffix);
}

[[nodiscard]] bool write_checksum(const core::path::Buffer& path,
                                  const std::vector<char>& data) noexcept {
    core::path::Buffer sumPath{};
    if (!suffixed(path, L".sum", sumPath)) {
        return false;
    }
    wchar_t hex[kChecksumHexDigits + 1]{};
    if (swprintf(hex, kChecksumHexDigits + 1, L"%016llX",
                static_cast<unsigned long long>(fnv1a(data)))
        != static_cast<int>(kChecksumHexDigits)) {
        return false;
    }
    std::vector<char> narrow(kChecksumHexDigits);
    for (std::size_t index = 0; index < kChecksumHexDigits; ++index) {
        narrow[index] = static_cast<char>(hex[index]);
    }
    return write_whole_file(sumPath.chars.data(), narrow);
}

/** @return The hex digit's value, or -1 when `c` is not one. */
[[nodiscard]] int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/** Parses exactly `kChecksumHexDigits` uppercase hex digits with no throwing conversion. */
[[nodiscard]] bool parse_checksum(const std::vector<char>& narrow, std::uint64_t& value) noexcept {
    if (narrow.size() != kChecksumHexDigits) {
        return false;
    }
    value = 0;
    for (const char c : narrow) {
        const int digit = hex_digit(c);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | static_cast<std::uint64_t>(digit);
    }
    return true;
}

/** @return True only when a sidecar exists and matches `data`'s own checksum. */
[[nodiscard]] bool checksum_matches(const core::path::Buffer& path,
                                    const std::vector<char>& data) noexcept {
    core::path::Buffer sumPath{};
    std::vector<char> narrow;
    std::uint64_t expected = 0;
    if (!suffixed(path, L".sum", sumPath) || !read_whole_file(sumPath.chars.data(), narrow)
        || !parse_checksum(narrow, expected)) {
        return false;
    }
    return expected == fnv1a(data);
}

} // namespace

bool backup_database(const core::path::Buffer& databasePath) noexcept {
    std::vector<char> current;
    if (!read_whole_file(databasePath.chars.data(), current)) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::warn,
                         "ev=investment_backup stage=read result=fail");
        return false;
    }
    core::path::Buffer backupPath{};
    core::path::Buffer backup2Path{};
    if (!suffixed(databasePath, L".bak", backupPath)
        || !suffixed(databasePath, L".bak2", backup2Path)) {
        return false;
    }
    // Rotate the previous generation down one slot, backup and checksum together, before either
    // is overwritten -- so a crash mid-rotation loses at most the oldest generation, never both.
    std::vector<char> previous;
    if (read_whole_file(backupPath.chars.data(), previous)) {
        core::path::Buffer backupSum{};
        core::path::Buffer backup2Sum{};
        if (suffixed(backupPath, L".sum", backupSum) && suffixed(backup2Path, L".sum", backup2Sum)) {
            (void)CopyFileW(backupPath.chars.data(), backup2Path.chars.data(), FALSE);
            (void)CopyFileW(backupSum.chars.data(), backup2Sum.chars.data(), FALSE);
        }
    }
    const bool ok = write_whole_file(backupPath.chars.data(), current)
                    && write_checksum(backupPath, current);
    if (!ok) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::warn,
                         "ev=investment_backup stage=write result=fail");
    }
    return ok;
}

bool restore_from_backup(const core::path::Buffer& databasePath) noexcept {
    core::path::Buffer candidates[2]{};
    if (!suffixed(databasePath, L".bak", candidates[0])
        || !suffixed(databasePath, L".bak2", candidates[1])) {
        return false;
    }
    for (const core::path::Buffer& candidate : candidates) {
        std::vector<char> data;
        if (!read_whole_file(candidate.chars.data(), data) || !checksum_matches(candidate, data)) {
            continue;
        }
        if (write_whole_file(databasePath.chars.data(), data)) {
            core::log::write(core::log::Channel::state,
                             core::log::Level::warn,
                             "ev=investment_backup stage=restore result=recovered");
            return true;
        }
    }
    core::log::write(core::log::Channel::state,
                     core::log::Level::error,
                     "ev=investment_backup stage=restore result=fail");
    return false;
}

} // namespace sunrise::state::investment::store
