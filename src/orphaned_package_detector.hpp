#pragma once

#include "detector.hpp"
#include <memory>
#include <unordered_map>

class OrphanedPackageDetector : public IDetector {
    mutable std::unordered_map<std::string, bool> cache_;
    mutable int rpm_checked_{0}; // 0=unchecked, 1=available, -1=unavailable
    bool enabled_{false};

    bool rpm_available() const;
    bool is_owned_by_package(const fs::path& path) const;

public:
    void set_enabled(bool e) { enabled_ = e; }

    std::vector<ScanResult> detect(const fs::path& path) const override;
    std::string             name()                  const override;
    std::vector<fs::path>   scan_targets()          const override;
};
