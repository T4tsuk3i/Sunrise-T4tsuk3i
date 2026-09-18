#include <Windows.h>

#include <array>
#include <string_view>

#include "../../../resources/resource.h"
#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "investment_backup.h"
#include "investment_checkpoint_thread.h"
#include "investment_self_test.h"
#include "store_internal.h"

namespace sunrise::state::investment::store {
namespace {

/** Resource views borrow bytes from the loaded DLL. */
bool resource(void* module, int identifier, std::string_view& output) noexcept {
    const auto loadedModule = static_cast<HMODULE>(module);
    const HRSRC found = FindResourceW(loadedModule, MAKEINTRESOURCEW(identifier), RT_RCDATA);
    if (found == nullptr) {
        return false;
    }
    const DWORD size = SizeofResource(loadedModule, found);
    const HGLOBAL loaded = LoadResource(loadedModule, found);
    const auto* bytes =
        loaded != nullptr ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (bytes == nullptr || size == 0) {
        return false;
    }
    output = {bytes, size};
    return true;
}

/** SQLite's own WAL sidecars for the primary database, removed alongside it when it is unusable
 * and not worth carrying into a fresh reinitialization. */
constexpr std::array<std::wstring_view, 2> kPrimarySidecars{L"-wal", L"-shm"};

void remove_unusable_primary(const core::path::Buffer& primaryPath) noexcept {
    DeleteFileW(primaryPath.chars.data());
    for (const std::wstring_view suffix : kPrimarySidecars) {
        core::path::Buffer sidecar = primaryPath;
        if (core::path::append(sidecar, suffix)) {
            DeleteFileW(sidecar.chars.data());
        }
    }
}

/**
 * True only once the primary is both open and passes `validate`. A file that opens cleanly but
 * fails validation (a broken foreign key, an unreadable account row) is exactly as unusable as
 * one that would not open at all, so it goes through the same recovery path below rather than
 * being left open for `core::initialize_state` to reject on its own, harder-failing call.
 */
[[nodiscard]] bool open_and_validate(const core::path::Buffer& primaryPath,
                                     std::string_view filename,
                                     std::string_view schema,
                                     std::string_view defaults,
                                     std::string_view settingsSchema,
                                     std::string_view settingsDefaults) noexcept {
    if (!open(filename, primaryPath, schema, defaults, settingsSchema, settingsDefaults)) {
        return false;
    }
    if (validate()) {
        return true;
    }
    shutdown(); // Opened, but not trustworthy; free the handle before the next attempt tries it.
    return false;
}

/**
 * Opens and validates the primary database, recovering in two steps when it will not: first the
 * standalone backup, then -- only if even that fails -- a fresh database from the authored
 * defaults. A player never sees "the game refuses to start" for a persistence problem; the worst
 * case is losing progress back to the last good backup, or to nothing, not losing the ability to
 * play at all.
 */
[[nodiscard]] bool open_with_recovery(const core::path::Buffer& primaryPath,
                                      std::string_view filename,
                                      std::string_view schema,
                                      std::string_view defaults,
                                      std::string_view settingsSchema,
                                      std::string_view settingsDefaults) noexcept {
    if (open_and_validate(
            primaryPath, filename, schema, defaults, settingsSchema, settingsDefaults)) {
        return true;
    }
    if (restore_from_backup(primaryPath)
        && open_and_validate(
            primaryPath, filename, schema, defaults, settingsSchema, settingsDefaults)) {
        return true;
    }
    core::log::write(core::log::Channel::state,
                     core::log::Level::error,
                     "ev=investment_store stage=open result=reset reason=unrecoverable");
    remove_unusable_primary(primaryPath);
    return open_and_validate(
        primaryPath, filename, schema, defaults, settingsSchema, settingsDefaults);
}

} // namespace

/** Schema and seed data stay in DLL resources; only the persistent database is materialized. */
bool initialize(void* module) noexcept {
    // Verifies the store's own read/write code against a disposable database, never the real
    // one, once per process, before the real database is touched.
    self_test::run(module);
    core::path::Buffer directory;
    if (!core::path::artifact_directory(module, directory)
        || !core::path::append(directory, L"\\data")) {
        return false;
    }
    if (!CreateDirectoryW(directory.chars.data(), nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    if (!core::path::append(directory, L"\\investment.sqlite3")) {
        return false;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8,
                                          0,
                                          directory.chars.data(),
                                          static_cast<int>(directory.length),
                                          nullptr,
                                          0,
                                          nullptr,
                                          nullptr);
    if (bytes <= 0) {
        return false;
    }
    std::string filename(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            directory.chars.data(),
                            static_cast<int>(directory.length),
                            filename.data(),
                            bytes,
                            nullptr,
                            nullptr)
        != bytes) {
        return false;
    }
    std::string_view schema;
    std::string_view defaults;
    std::string_view settingsSchema;
    std::string_view settingsDefaults;
    if (!resource(module, IDR_INVESTMENT_SCHEMA, schema)
        || !resource(module, IDR_INVESTMENT_DEFAULTS, defaults)
        || !resource(module, IDR_ACCOUNT_SETTINGS_SCHEMA, settingsSchema)
        || !resource(module, IDR_ACCOUNT_SETTINGS_DEFAULTS, settingsDefaults)
        || !open_with_recovery(
            directory, filename, schema, defaults, settingsSchema, settingsDefaults)) {
        return false;
    }
    start_checkpoint_thread();
    return true;
}

} // namespace sunrise::state::investment::store
