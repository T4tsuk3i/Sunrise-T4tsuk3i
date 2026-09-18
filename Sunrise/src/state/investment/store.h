#pragma once

#include <string>
#include <string_view>

#include "../../core/filesystem/path.h"
#include "../account/account_state.h"
#include "../entitlements/definition.h"
#include "../unlocks/definition.h"
#include "investment.h"

namespace sunrise::state::investment::store {

/** Bank identifiers are part of schema version 1. */
enum class Bank : int {
    accountFlags,
    profileFlags,
    characterFlags,
    objectiveValues,
    characterObjectFlags,
    characterObjectValues,
    accountProgressions,
    characterProgressions
};

[[nodiscard]] bool initialize(void* module) noexcept;
[[nodiscard]] bool validate() noexcept;
/**
 * @param path Narrow (UTF-8) form of `widePath`, what SQLite itself opens.
 * @param widePath Same file, kept alongside the narrow form so backup and restore -- which go
 * through Win32 file APIs -- never need to re-derive or re-encode it.
 */
[[nodiscard]] bool open(std::string_view path,
                        const core::path::Buffer& widePath,
                        std::string_view schema,
                        std::string_view defaults,
                        std::string_view settingsSchema,
                        std::string_view settingsDefaults) noexcept;
/**
 * Checkpoints, refreshes planner statistics and backs up the open database. Call before
 * `shutdown`, after `stop_checkpoint_thread` if it was started.
 */
void checkpoint_and_backup() noexcept;
/** Closes the database and discards session fields. Does not back up; call
 * `checkpoint_and_backup` first if this is a real shutdown, not a failed or disposable open. */
void shutdown() noexcept;
[[nodiscard]] bool read_account(AccountState& output) noexcept;
[[nodiscard]] AccountState account() noexcept;
[[nodiscard]] bool write_account(const AccountState& value) noexcept;
[[nodiscard]] bool read_settings(account::settings::AccountSettings& output) noexcept;
[[nodiscard]] bool write_settings(const account::settings::AccountSettings& value) noexcept;
void set_sign_in_time(std::uint64_t seconds) noexcept;
[[nodiscard]] bool read_family5(Family5State& output) noexcept;
[[nodiscard]] bool write_family5(const Family5State& value) noexcept;
[[nodiscard]] bool read_unlocks(unlocks::Table& output, int characterSlot = -1) noexcept;
[[nodiscard]] bool write_unlocks(const unlocks::Table& value, int characterSlot = -1) noexcept;
[[nodiscard]] bool read_unlock(Bank bank, std::uint16_t slot, std::int32_t& value) noexcept;
[[nodiscard]] bool write_unlock(Bank bank, std::uint16_t slot, std::int32_t value) noexcept;
[[nodiscard]] bool read_entitlements(entitlements::Table& output) noexcept;
[[nodiscard]] bool bootstrap_completed(std::string_view name) noexcept;
[[nodiscard]] bool complete_bootstrap(std::string_view name) noexcept;

/** An earned reward stays in the database until its inventory grant commits. */
struct PendingReward {
    std::uint64_t id{};
    std::uint32_t definitionHash{};
    std::int32_t quantity{};
    std::uint8_t kind{};
};
[[nodiscard]] bool
enqueue_reward(std::uint32_t definitionHash, std::int32_t quantity, std::uint8_t kind) noexcept;
[[nodiscard]] bool next_reward(PendingReward& output) noexcept;
[[nodiscard]] bool complete_reward(std::uint64_t id) noexcept;

} // namespace sunrise::state::investment::store
