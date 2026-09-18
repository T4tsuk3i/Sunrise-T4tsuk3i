#include "investment_self_test.h"

#include <Windows.h>

#include <array>
#include <string>
#include <string_view>

#include "../../../resources/resource.h"
#include "../../core/filesystem/path.h"
#include "../../core/logging/log.h"
#include "store.h"
#include "store_internal.h"

namespace sunrise::state::investment::store::self_test {
namespace {

/** The disposable database this test opens and discards, beside the real one but never it. */
constexpr std::wstring_view kTestFileName = L"\\investment_self_test.sqlite3";
/** SQLite's own WAL sidecars for the test file, cleaned up alongside it. */
constexpr std::array<std::wstring_view, 2> kTestSidecars{L"-wal", L"-shm"};

/** Resource views borrow bytes from the loaded DLL, the same way the real boot loads them. */
[[nodiscard]] bool resource(void* module, int identifier, std::string_view& output) noexcept {
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

[[nodiscard]] bool narrow_path(const core::path::Buffer& path, std::string& output) noexcept {
    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, path.chars.data(), static_cast<int>(path.length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return false;
    }
    output.assign(static_cast<std::size_t>(bytes), '\0');
    return WideCharToMultiByte(CP_UTF8,
                               0,
                               path.chars.data(),
                               static_cast<int>(path.length),
                               output.data(),
                               bytes,
                               nullptr,
                               nullptr)
           == bytes;
}

void remove_test_files(const core::path::Buffer& testPath) noexcept {
    DeleteFileW(testPath.chars.data());
    for (const std::wstring_view suffix : kTestSidecars) {
        core::path::Buffer sidecar = testPath;
        if (core::path::append(sidecar, suffix)) {
            DeleteFileW(sidecar.chars.data());
        }
    }
}

void report(const char* stage, const char* field = nullptr) noexcept {
    if (field != nullptr) {
        core::log::writef(core::log::Channel::state,
                          core::log::Level::error,
                          "ev=investment_self_test stage=%s result=fail field=%s",
                          stage,
                          field);
    } else {
        core::log::writef(core::log::Channel::state,
                          core::log::Level::error,
                          "ev=investment_self_test stage=%s result=fail",
                          stage);
    }
}

/** Mutates the authored-default account distinctively, staying inside every bound
 * `account::valid_authored` checks -- this fixture only needs to prove the round trip, not
 * exercise every validity rule. */
[[nodiscard]] AccountState build_fixture() noexcept {
    AccountState fixture = account();
    // High-bit SOID: proves the signed/unsigned SQLite round trip preserves the top bit.
    fixture.primarySoid = 0xFFEEDDCCBBAA9988ULL;
    fixture.profileSetupCompleted = !fixture.profileSetupCompleted;
    if (fixture.dismantleRewardCount < fixture.dismantleRewards.size()) {
        fixture.dismantleRewards[fixture.dismantleRewardCount++] = {
            3159615086U, 25, 0b0000'0010U, 1U, DismantleMasterworkFilter::any};
    }
    return fixture;
}

/** Compares the fields `build_fixture` actually touched; returns false after the first mismatch. */
[[nodiscard]] bool compare(const AccountState& expected, const AccountState& actual) noexcept {
    if (expected.primarySoid != actual.primarySoid) {
        report("compare", "primarySoid");
        return false;
    }
    if (expected.profileSetupCompleted != actual.profileSetupCompleted) {
        report("compare", "profileSetupCompleted");
        return false;
    }
    if (expected.dismantleRewardCount != actual.dismantleRewardCount) {
        report("compare", "dismantleRewardCount");
        return false;
    }
    if (expected.dismantleRewardCount > 0) {
        const auto& expectedReward = expected.dismantleRewards[expected.dismantleRewardCount - 1];
        const auto& actualReward = actual.dismantleRewards[actual.dismantleRewardCount - 1];
        if (expectedReward.definitionHash != actualReward.definitionHash
            || expectedReward.quantity != actualReward.quantity
            || expectedReward.tierMask != actualReward.tierMask
            || expectedReward.classMask != actualReward.classMask
            || expectedReward.masterwork != actualReward.masterwork) {
            report("compare", "dismantleRewards");
            return false;
        }
    }
    return true;
}

} // namespace

void run(void* module) noexcept {
    static bool ran = false;
    if (ran) {
        return;
    }
    ran = true;

    core::path::Buffer directory{};
    core::path::Buffer testPath{};
    std::string filename;
    std::string_view schema;
    std::string_view defaults;
    std::string_view settingsSchema;
    std::string_view settingsDefaults;
    if (!core::path::artifact_directory(module, directory)
        || !core::path::append(directory, L"\\data")) {
        report("path");
        return;
    }
    CreateDirectoryW(directory.chars.data(), nullptr); // Best effort; open() surfaces a real miss.
    testPath = directory;
    if (!core::path::append(testPath, kTestFileName) || !narrow_path(testPath, filename)
        || !resource(module, IDR_INVESTMENT_SCHEMA, schema)
        || !resource(module, IDR_INVESTMENT_DEFAULTS, defaults)
        || !resource(module, IDR_ACCOUNT_SETTINGS_SCHEMA, settingsSchema)
        || !resource(module, IDR_ACCOUNT_SETTINGS_DEFAULTS, settingsDefaults)) {
        report("setup");
        return;
    }
    remove_test_files(testPath); // Start clean; a prior crashed run may have left this behind.

    if (!open(filename, testPath, schema, defaults, settingsSchema, settingsDefaults)) {
        report("open");
        remove_test_files(testPath);
        return;
    }

    const AccountState fixture = build_fixture();
    AccountState reloaded{};
    const bool roundTrip = write_account(fixture) && read_account(reloaded);
    shutdown();
    remove_test_files(testPath);

    if (!roundTrip) {
        report("round_trip");
        return;
    }
    (void)compare(fixture, reloaded);
}

} // namespace sunrise::state::investment::store::self_test
