#include "stale_log_detector.hpp"

StaleLogDetector::StaleLogDetector(int days)
    : retention_(std::chrono::duration_cast<fs::file_time_type::duration>(
          std::chrono::hours(days * 24)))
{}

std::vector<ScanResult> StaleLogDetector::detect(
    const fs::path& path) const {
    if (!enabled_)
        return {};

    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec)
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
