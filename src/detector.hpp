#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ScanResult {
    fs::path        path;
    uintmax_t       size;
    fs::file_time_type last_modified;
    std::string     detector_name;
};

class IDetector {
public:
    virtual ~IDetector() = default;

    virtual std::vector<ScanResult> detect(const fs::path& path) const = 0;
    virtual std::string             name()                  const = 0;
    virtual std::vector<fs::path>   scan_targets()          const { return {}; }
};
