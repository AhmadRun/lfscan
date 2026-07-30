#pragma once

#include "detector.hpp"
#include <chrono>

class AgeBasedDetector : public IDetector {
    fs::file_time_type::duration max_age_;
    std::string                  name_;

public:
    explicit AgeBasedDetector(int days);

    std::vector<ScanResult> detect(const fs::path& path) const override;
    std::string             name()                  const override;
};
