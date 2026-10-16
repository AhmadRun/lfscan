#include "stale_log_detector.hpp"

StaleLogDetector::StaleLogDetector(int days)
    : retention_(std::chrono::duration_cast<fs::file_time_type::duration>(
          std::chrono::hours(days * 24)))
{}

std::vector<ScanResult> StaleLogDetector::detect(
    const fs::path& path) const {
    if (!enabled_)
        return {};

    // Only files under /var/log are logs; other targets are handled by
    // the age-based detector.
    auto p = path.lexically_normal().string();
    if (p.rfind("/var/log/", 0) != 0)
        return {};

    std::error_code ec;
    // symlink_status: a symlink to an old file is not itself an old file
    if (!fs::is_regular_file(fs::symlink_status(path, ec)) || ec)
        return {};

    auto ft = fs::last_write_time(path, ec);
    if (ec)
        return {};

    auto age = fs::file_time_type::clock::now() - ft;
    if (age <= retention_)
        return {};

    auto sz = fs::file_size(path, ec);
    if (ec)
        return {};

    std::vector<ScanResult> out;
    out.push_back({path, sz, ft, name()});
    return out;
}

std::string StaleLogDetector::name() const {
    return "stale-log";
}

std::vector<fs::path> StaleLogDetector::scan_targets() const {
    if (!enabled_)
        return {};
    return {fs::path("/var/log")};
}
