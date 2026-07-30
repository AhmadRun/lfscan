#pragma once

#include "detector.hpp"

class BrokenSymlinkDetector : public IDetector {
public:
    std::vector<ScanResult> detect(const fs::path& path) const override;
    std::string             name()                  const override;
};
