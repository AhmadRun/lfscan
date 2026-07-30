#include "orphaned_package_detector.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>

bool OrphanedPackageDetector::rpm_available() const {
    if (rpm_checked_ == 0)
        rpm_checked_ = (std::system("rpm --version >/dev/null 2>&1") == 0) ? 1 : -1;
    return rpm_checked_ == 1;
}

static std::string run_rpm_query(const fs::path& path) {
    std::string cmd =
        "rpm -qf --qf '%{name}' " + path.string() + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp)
        return {};

    std::array<char, 256> buf{};
    std::string           result;
    if (fgets(buf.data(), buf.size(), fp))
        result.assign(buf.data());
    pclose(fp);
    return result;
}

bool OrphanedPackageDetector::is_owned_by_package(
    const fs::path& path) const {
    if (!rpm_available())
        return true;  // no RPM = treat everything as "owned", avoid false positives

    auto key = path.string();
    auto it  = cache_.find(key);
    if (it != cache_.end())
        return it->second;

    auto pkg = run_rpm_query(path);
    bool owned = !pkg.empty();

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        owned = true;

    cache_[key] = owned;
    return owned;
}

std::vector<ScanResult> OrphanedPackageDetector::detect(
    const fs::path& path) const {
    if (!enabled_)
        return {};

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return {};

    if (is_owned_by_package(path))
        return {};

    auto ft = fs::last_write_time(path, ec);
    auto sz = fs::file_size(path, ec);

    std::vector<ScanResult> out;
    out.push_back({path, sz, ft, name()});
    return out;
}

std::string OrphanedPackageDetector::name() const {
    return "orphaned-package";
}

std::vector<fs::path> OrphanedPackageDetector::scan_targets() const {
    if (!enabled_)
        return {};
    return {fs::path("/etc")};
}
