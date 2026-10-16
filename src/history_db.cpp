#include "history_db.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sqlite3.h>
#include <string>

static std::string now_str() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

HistoryDB::~HistoryDB() {
    if (db_)
        sqlite3_close(static_cast<sqlite3*>(db_));
}

bool HistoryDB::open(const std::string& path) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "error: cannot open history DB: " << sqlite3_errmsg(db)
                  << '\n';
        sqlite3_close(db);
        return false;
    }
    db_ = db;

    // Enable WAL mode
    char* err = nullptr;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err);
    if (err) {
        std::cerr << "warning: could not enable WAL: " << err << '\n';
        sqlite3_free(err);
    }

    // Create schema
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS scans (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp     TEXT NOT NULL,
            duration_ms   INTEGER NOT NULL DEFAULT 0,
            dry_run       INTEGER NOT NULL DEFAULT 1,
            paths_scanned TEXT
        );
        CREATE TABLE IF NOT EXISTS scan_results (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            scan_id   INTEGER NOT NULL REFERENCES scans(id),
            cleaner   TEXT NOT NULL,
            category  TEXT NOT NULL,
            item_key  TEXT NOT NULL,
            size_bytes INTEGER NOT NULL DEFAULT 0,
            action    TEXT NOT NULL CHECK(action IN (
                          'found','cleaned','skipped','failed','restored'))
        );
        CREATE INDEX IF NOT EXISTS idx_scan_results_scan_id
            ON scan_results(scan_id);
        CREATE INDEX IF NOT EXISTS idx_scan_results_cleaner
            ON scan_results(cleaner);
    )";
    rc = sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (err) {
        std::cerr << "error: could not create tables: " << err << '\n';
        sqlite3_free(err);
        return false;
    }

    return true;
}

std::int64_t HistoryDB::log_scan(const ScanRecord& rec) {
    auto db = static_cast<sqlite3*>(db_);
    auto ts = rec.timestamp.empty() ? now_str() : rec.timestamp;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO scans(timestamp,duration_ms,dry_run,paths_scanned)"
                      " VALUES(?1,?2,?3,?4);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "error: prepare: " << sqlite3_errmsg(db) << '\n';
        return -1;
    }
    sqlite3_bind_text(stmt, 1, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, rec.duration_ms);
    sqlite3_bind_int(stmt, 3, rec.dry_run ? 1 : 0);
    sqlite3_bind_text(stmt, 4, rec.paths_scanned.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok)
        std::cerr << "error: insert scan: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(stmt);

    return ok ? sqlite3_last_insert_rowid(db) : -1;
}

bool HistoryDB::update_scan_duration(std::int64_t scan_id,
                                     std::int64_t duration_ms) {
    auto db = static_cast<sqlite3*>(db_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE scans SET duration_ms = ?1 WHERE id = ?2;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int64(stmt, 1, duration_ms);
    sqlite3_bind_int64(stmt, 2, scan_id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool HistoryDB::log_result(const HistoryEntry& entry) {
    auto db = static_cast<sqlite3*>(db_);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO scan_results(scan_id,cleaner,category,item_key,size_bytes,action)"
                      " VALUES(?1,?2,?3,?4,?5,?6);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int64(stmt, 1, entry.scan_id);
    sqlite3_bind_text(stmt, 2, entry.cleaner.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, entry.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.item_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, entry.size_bytes);
    sqlite3_bind_text(stmt, 6, entry.action.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<HistoryEntry> HistoryDB::query(const std::string& cleaner,
                                           std::int64_t limit) const {
    std::vector<HistoryEntry> out;
    auto db = static_cast<sqlite3*>(db_);

    std::string sql = "SELECT id,scan_id,cleaner,category,item_key,size_bytes,action"
                      " FROM scan_results";
    if (!cleaner.empty())
        sql += " WHERE cleaner = ?1";
    sql += " ORDER BY id DESC LIMIT ?2";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return out;

    int idx = 1;
    if (!cleaner.empty())
        sqlite3_bind_text(stmt, idx++, cleaner.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryEntry e;
        e.id        = sqlite3_column_int64(stmt, 0);
        e.scan_id   = sqlite3_column_int64(stmt, 1);
        e.cleaner   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.category  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        e.item_key  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        e.size_bytes = sqlite3_column_int64(stmt, 5);
        e.action    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        out.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<std::pair<std::string, std::int64_t>>
HistoryDB::bytes_per_cleaner(int days) const {
    std::vector<std::pair<std::string, std::int64_t>> out;
    auto db = static_cast<sqlite3*>(db_);

    const char* sql = R"(
        SELECT sr.cleaner, SUM(sr.size_bytes)
        FROM scan_results sr
        JOIN scans s ON s.id = sr.scan_id
        WHERE sr.action = 'cleaned'
          AND datetime(s.timestamp) >= datetime('now', ?1)
        GROUP BY sr.cleaner
        ORDER BY SUM(sr.size_bytes) DESC
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return out;

    std::string days_str = "-" + std::to_string(days) + " days";
    sqlite3_bind_text(stmt, 1, days_str.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto name  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        auto bytes = sqlite3_column_int64(stmt, 1);
        out.emplace_back(name, bytes);
    }
    sqlite3_finalize(stmt);
    return out;
}
