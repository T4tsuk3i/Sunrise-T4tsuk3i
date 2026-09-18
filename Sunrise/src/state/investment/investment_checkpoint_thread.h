#pragma once

namespace sunrise::state::investment::store {

/**
 * Starts the background thread that folds the WAL back into the main database file on a
 * schedule, instead of leaving it to SQLite's own size-triggered automatic checkpoint. An
 * automatic checkpoint can land at any moment -- including mid-frame during play -- and a
 * checkpoint is expensive enough to read as a stutter. Scheduling it explicitly, off the
 * gameplay path, is the standard fix. Safe to call once after `open` succeeds; a second call
 * before `stop_checkpoint_thread` is a no-op.
 */
void start_checkpoint_thread() noexcept;

/**
 * Signals the checkpoint thread to exit and waits for it. Call before closing the database, so
 * no checkpoint is still in flight when the connection goes away. Safe to call even when the
 * thread was never started.
 */
void stop_checkpoint_thread() noexcept;

} // namespace sunrise::state::investment::store
