#pragma once

#include "detector.hpp"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// Move src to dest. Falls back to copy + remove when they are on different
// filesystems (rename(2) fails with EXDEV, e.g. /tmp on tmpfs -> $HOME).
bool move_path(const fs::path& src, const fs::path& dest, std::error_code& ec);

class Quarantine {
    fs::path quarantine_dir_;
    fs::path manifest_path_;

    bool move_to_quarantine(const ScanResult& result);

public:
    explicit Quarantine(fs::path quarantine_dir);

    bool quarantine_files(const std::vector<ScanResult>& results);
};
