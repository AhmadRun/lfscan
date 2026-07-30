#pragma once

#include "detector.hpp"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

class Quarantine {
    fs::path quarantine_dir_;
    fs::path manifest_path_;

    bool move_to_quarantine(const ScanResult& result);

public:
    explicit Quarantine(fs::path quarantine_dir);

    bool quarantine_files(const std::vector<ScanResult>& results);
};
