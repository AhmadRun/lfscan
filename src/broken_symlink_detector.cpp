#include "broken_symlink_detector.hpp"

std::vector<ScanResult> BrokenSymlinkDetector::detect(
    const fs::path& path) const {
    std::error_code ec;

    auto status = fs::symlink_status(path, ec);
    if (ec || status.type() != fs::file_type::symlink)
        return {};

    auto target = fs::read_symlink(path, ec);
    if (ec)
        return {};

    if (fs::exists(target, ec) || ec)
        return {};

    std::vector<ScanResult> out;
    out.push_back({path, 0, {}, name()});
    return out;
}

std::string BrokenSymlinkDetector::name() const {
    return "broken-symlink";
}
