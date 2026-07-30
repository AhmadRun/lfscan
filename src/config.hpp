#pragma once

#include <string>
#include <vector>

struct Config {
    int              max_age_days{30};
    int              log_retention_days{90};
    bool             flag_orphaned{false};
    bool             flag_stale_logs{false};
    std::vector<std::string> extra_targets;
    std::vector<std::string> extra_blacklist;
};

// Load options from a TOML config file at `path`.
// Silently returns defaults if the file can't be read or parsed.
Config load_config(const std::string& path);
