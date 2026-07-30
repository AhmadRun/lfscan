#pragma once

#include "detector.hpp"
#include <chrono>

class StaleLogDetector : public IDetector {
    fs::file_time_type::duration retention_;
    bool                          enabled_{false};

public:
    explicit StaleLogDetector(int days);

    void set_enabled(bool e) { enabled_ = e; }

    std::vector<ScanResult> detect(const fs::path& path) const override;
    std::string             name()                  const override;
    std::vector<fs::path>   scan_targets()          const override;
};
