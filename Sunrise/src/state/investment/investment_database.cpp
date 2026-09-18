#include "store_internal.h"

#include "investment_backup.h"
#include "investment_checkpoint_thread.h"

namespace sunrise::state::investment::store {

sqlite3* g_database{};
std::recursive_mutex g_mutex;
Session g_session;
std::uint64_t g_failureSerial{};
core::path::Buffer g_databasePath{};

/** SQLite requires this sentinel to copy text borrowed from a caller. */
sqlite3_destructor_type copy_text() noexcept {
    return SQLITE_TRANSIENT; // NOLINT(performance-no-int-to-ptr)
}

/** SQL failures remain failures; no in-memory save replaces a failed disk write. */
bool execute(const char* sql) noexcept {
    const bool ready = g_database != nullptr
                       && sqlite3_exec(g_database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ready) {
        ++g_failureSerial;
    }
    return ready;
}

/** Nested saves belong to the outer transaction until it commits. */
Transaction::Transaction() noexcept
    : lock_(g_mutex), active_(execute("SAVEPOINT investment_write")), before_(g_session),
      failureSerial_(g_failureSerial) {}

/** A refused transaction leaves no partial rows. */
Transaction::~Transaction() {
    if (active_) {
        (void)execute("ROLLBACK TO investment_write");
        (void)execute("RELEASE investment_write");
        g_session = before_;
    }
}

/** @return True only after SQLite has committed every row. */
bool Transaction::commit() noexcept {
    if (!active_ || failureSerial_ != g_failureSerial || !execute("RELEASE investment_write")) {
        return false;
    }
    active_ = false;
    return true;
}

/** The caller holds the database lock until this statement is destroyed. */
Statement::Statement(const char* sql) noexcept {
    if (g_database != nullptr
        && sqlite3_prepare_v2(g_database, sql, -1, &statement_, nullptr) != SQLITE_OK) {
        sqlite3_finalize(statement_);
        statement_ = nullptr;
        ++g_failureSerial;
    }
}

Statement::~Statement() {
    sqlite3_finalize(statement_);
}
int Statement::step() noexcept {
    const int result = statement_ != nullptr ? sqlite3_step(statement_) : SQLITE_ERROR;
    if (result != SQLITE_ROW && result != SQLITE_DONE) {
        ++g_failureSerial;
    }
    return result;
}

/** @return Borrowed text valid until the statement advances. */
bool Statement::text(int column, std::string_view& value) const noexcept {
    if (statement_ == nullptr || sqlite3_column_type(statement_, column) != SQLITE_TEXT) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const char*>(sqlite3_column_text(statement_, column));
    if (bytes == nullptr) {
        return false;
    }
    value = {bytes, static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))};
    return true;
}

/** New databases receive schema and defaults in one durable transaction. */
bool open(std::string_view path,
          const core::path::Buffer& widePath,
          std::string_view schema,
          std::string_view defaults,
          std::string_view settingsSchema,
          std::string_view settingsDefaults) noexcept {
    const std::lock_guard lock(g_mutex);
    if (g_database != nullptr) {
        return false;
    }
    const std::string filename(path);
    if (sqlite3_open_v2(filename.c_str(),
                        &g_database,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr)
        != SQLITE_OK) {
        shutdown();
        return false;
    }
    g_databasePath = widePath;
    // One short busy wait permits a local database editor to finish its transaction.
    constexpr int kBusyMilliseconds = 5000;
    sqlite3_busy_timeout(g_database, kBusyMilliseconds);
    // WAL + synchronous=NORMAL is the standard durable pairing for this threat model: every
    // commit is still crash-safe, but a commit is a WAL append instead of a full rollback-journal
    // write, and readers never block a writer. synchronous=FULL's extra fsync bought safety this
    // pairing already has under WAL, at a cost paid on every single mutation instead of once.
    // wal_autocheckpoint=0 hands checkpoint timing to the scheduler in
    // investment_checkpoint_thread.cpp instead of SQLite's own size-triggered default, which can
    // otherwise land mid-frame during play.
    bool ready = execute("PRAGMA foreign_keys=ON;"
                         "PRAGMA journal_mode=WAL;"
                         "PRAGMA synchronous=NORMAL;"
                         "PRAGMA wal_autocheckpoint=0;"
                         "PRAGMA temp_store=MEMORY;"
                         "PRAGMA cache_size=-8000;");
    int version = -1;
    {
        Statement query("PRAGMA user_version");
        ready = ready && query.step() == SQLITE_ROW && query.column(0, version);
    }
    const std::string preferenceSchema(settingsSchema);
    const std::string preferenceDefaults(settingsDefaults);
    if (ready && version == 0) {
        Transaction transaction;
        const std::string schemaText(schema);
        const std::string defaultText(defaults);
        ready = transaction.ready() && !schema.empty() && !defaults.empty()
                && execute(schemaText.c_str()) && execute(defaultText.c_str())
                && !settingsSchema.empty() && !settingsDefaults.empty()
                && execute(preferenceSchema.c_str()) && execute(preferenceDefaults.c_str())
                && transaction.commit();
    } else if (ready) {
        // Version 2 adds account preferences and per-item seen state.
        constexpr int kSchemaVersion = 2;
        constexpr int kApplicationId = 1397902921;
        int application = 0;
        Statement query("PRAGMA application_id");
        ready = (version == 1 || version == kSchemaVersion) && query.step() == SQLITE_ROW
                && query.column(0, application) && application == kApplicationId;
    }
    if (ready && version == 1) {
        Transaction transaction;
        ready =
            transaction.ready() && !settingsSchema.empty() && !settingsDefaults.empty()
            && execute(
                "ALTER TABLE items ADD COLUMN seen INTEGER NOT NULL DEFAULT 0 CHECK(seen IN(0,1));"
                "ALTER TABLE profile_items ADD COLUMN seen INTEGER NOT NULL DEFAULT 1 CHECK(seen "
                "IN(0,1));")
            && execute(preferenceSchema.c_str()) && execute(preferenceDefaults.c_str())
            && execute("PRAGMA user_version=2") && transaction.commit();
    }
    if (!ready) {
        shutdown();
    }
    return ready;
}

/**
 * Folds the WAL fully into the main file, refreshes the query planner's statistics for the next
 * session, and backs up the result -- in that order, so the backup copies the fully folded-in
 * main file rather than one still owing pages to its WAL. Call `stop_checkpoint_thread` first, so
 * the scheduled checkpoint never races this one, and call this before `shutdown`, while the
 * database is still open. A no-op backup here (an unopened or already-closed database) is not a
 * failure; there is nothing yet worth protecting.
 */
void checkpoint_and_backup() noexcept {
    const std::lock_guard lock(g_mutex);
    if (g_database == nullptr) {
        return;
    }
    (void)execute("PRAGMA wal_checkpoint(TRUNCATE); PRAGMA optimize;");
    (void)backup_database(g_databasePath);
}

/** Saves are synchronous, so shutdown only closes the handle and discards session fields. */
void shutdown() noexcept {
    const std::lock_guard lock(g_mutex);
    sqlite3_close_v2(g_database);
    g_database = nullptr;
    g_databasePath = {};
    g_session = {};
}

} // namespace sunrise::state::investment::store
