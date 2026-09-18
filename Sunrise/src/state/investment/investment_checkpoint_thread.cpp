#include "investment_checkpoint_thread.h"

#include <Windows.h>

#include <atomic>

#include "store_internal.h"

namespace sunrise::state::investment::store {
namespace {

/**
 * How often the idle checkpoint runs. Short enough that the WAL never grows large enough for a
 * later checkpoint to itself become a stutter; long enough that a checkpoint is never competing
 * with the write it would otherwise have piggybacked on.
 */
constexpr DWORD kCheckpointIntervalMs = 15'000;

HANDLE g_thread{};
HANDLE g_stopEvent{};
std::atomic_bool g_running{};

/** One PASSIVE checkpoint: folds committed WAL pages into the main file without blocking a
 * concurrent writer or reader, the mode the SQLite docs recommend for a background scheduler. A
 * busy database (mid-transaction) simply checkpoints what it can and returns; the next tick
 * catches the rest. */
void checkpoint_passive() noexcept {
    const std::lock_guard lock(g_mutex);
    if (g_database == nullptr) {
        return;
    }
    (void)execute("PRAGMA wal_checkpoint(PASSIVE);");
}

DWORD WINAPI checkpoint_loop(LPVOID) noexcept {
    while (g_running.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(g_stopEvent, kCheckpointIntervalMs) != WAIT_TIMEOUT) {
            break; // Signalled: stop_checkpoint_thread is waiting on us.
        }
        checkpoint_passive();
    }
    return 0;
}

} // namespace

void start_checkpoint_thread() noexcept {
    if (g_running.load(std::memory_order_acquire)) {
        return;
    }
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr) {
        return; // No scheduler this run; the shutdown-time checkpoint still covers correctness.
    }
    g_running.store(true, std::memory_order_release);
    g_thread = CreateThread(nullptr, 0, &checkpoint_loop, nullptr, 0, nullptr);
    if (g_thread == nullptr) {
        g_running.store(false, std::memory_order_release);
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

void stop_checkpoint_thread() noexcept {
    if (!g_running.load(std::memory_order_acquire)) {
        return;
    }
    g_running.store(false, std::memory_order_release);
    SetEvent(g_stopEvent);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    CloseHandle(g_stopEvent);
    g_thread = nullptr;
    g_stopEvent = nullptr;
}

} // namespace sunrise::state::investment::store
