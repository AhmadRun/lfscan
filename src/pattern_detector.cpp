#include "pattern_detector.hpp"

#include <fnmatch.h>

PatternDetector::PatternDetector(std::vector<std::string> patterns)
    : patterns_(std::move(patterns))
    , name_("pattern")
{}

std::vector<ScanResult> PatternDetector::detect(const fs::path& path) const {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec)
        return {};

    auto filename = path.filename().string();

    for (const auto& pat : patterns_) {
        if (fnmatch(pat.c_str(), filename.c_str(), 0) == 0) {
            auto ft = fs::last_write_time(path, ec);
            auto sz = fs::file_size(path, ec);
            if (ec)
                return {};
            std::vector<ScanResult> out;
            out.push_back({path, sz, ft, name_ + " (" + pat + ")"});
            return out;
        }
    }

    return {};
}

std::string PatternDetector::name() const {
    return name_;
}
