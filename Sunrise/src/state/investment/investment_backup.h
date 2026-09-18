#pragma once

#include "../../core/filesystem/path.h"

namespace sunrise::state::investment::store {

/**
 * Copies the closed-for-a-moment primary database over the standalone backup, rotating the
 * previous backup down one generation first, and writes a checksum sidecar beside the new one.
 * SQLite's own transactional guarantees mean the primary file is never left half-written, but
 * they say nothing about a committed file damaged afterward by an unrelated cause -- a disk
 * error, external truncation, antivirus interference. This is what `restore_from_backup` below
 * recovers from. Call only when the database connection is quiescent (after the shutdown
 * checkpoint, with the checkpoint thread already stopped).
 * @param databasePath Primary database file this backs up.
 * @return True when the backup and its checksum both wrote successfully. A failure here is
 * logged but never fails the surrounding shutdown -- the primary file itself is unaffected.
 */
bool backup_database(const core::path::Buffer& databasePath) noexcept;

/**
 * Restores the most recent backup generation whose checksum still matches over the primary
 * database path, trying one generation older if the first fails. Copies the file only; the
 * caller still owns opening it afterward.
 * @param databasePath Primary database file to restore, overwritten only on success.
 * @return True when a verified backup was copied into place.
 */
bool restore_from_backup(const core::path::Buffer& databasePath) noexcept;

} // namespace sunrise::state::investment::store
