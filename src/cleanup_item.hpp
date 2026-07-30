#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class Risk { Safe, Caution, Dangerous };

enum class Action { Found, Cleaned, Skipped, Failed, Restored };

struct CleanupItem {
    std::string               id;           // unique key, e.g. "npm-cache"
    std::string               description;  // human-readable label
    std::uint64_t             size{0};      // bytes
    std::string               category;     // e.g. "language-cache"
    bool                      reversible{false};
    Risk                      risk{Risk::Safe};
    std::map<std::string, std::string> metadata; // additional info for restore / display
};

struct CleanupSummary {
    std::uint64_t             total_size{0};
    std::size_t               item_count{0};
    std::map<std::string, std::uint64_t> per_category;
};

struct CleanupResult {
    std::size_t               cleaned{0};
    std::size_t               failed{0};
    std::uint64_t             bytes_freed{0};
    std::vector<std::string>  errors;
};

struct QuarantinedItem {
    std::string               id;
    std::string               original_path;
    std::string               quarantined_path;
    std::string               timestamp;
    std::uint64_t             size{0};
};

struct RestoreResult {
    std::size_t               restored{0};
    std::size_t               failed{0};
    std::vector<std::string>  errors;
};
