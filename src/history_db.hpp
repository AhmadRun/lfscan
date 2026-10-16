#pragma once

#include "cleanup_item.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct ScanRecord {
    std::int64_t  id{0};
    std::string   timestamp;
    std::int64_t  duration_ms{0};
    bool          dry_run{true};
    std::string   paths_scanned;
};

struct HistoryEntry {
    std::int64_t  id;
    std::int64_t  scan_id;
    std::string   cleaner;
    std::string   category;
    std::string   item_key;
    std::int64_t  size_bytes;
    std::string   action;  // 'found' | 'cleaned' | 'skipped' | 'failed' | 'restored'
};

class HistoryDB {
    void* db_{nullptr};

public:
    ~HistoryDB();

    bool open(const std::string& path);
    bool is_open() const { return db_ != nullptr; }

    std::int64_t log_scan(const ScanRecord& rec);
    bool         update_scan_duration(std::int64_t scan_id, std::int64_t duration_ms);
    bool         log_result(const HistoryEntry& entry);

    std::vector<HistoryEntry> query(const std::string& cleaner = "",
                                    std::int64_t limit = 100) const;

    // Aggregate: bytes per cleaner over last N days
    std::vector<std::pair<std::string, std::int64_t>>
    bytes_per_cleaner(int days = 30) const;
};
