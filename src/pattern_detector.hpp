#pragma once

#include "detector.hpp"
#include <string>
#include <vector>

class PatternDetector : public IDetector {
    std::vector<std::string> patterns_;
    std::string              name_;

public:
    explicit PatternDetector(std::vector<std::string> patterns);

    std::vector<ScanResult> detect(const fs::path& path) const override;
    std::string             name()                  const override;
};
