#include "orphaned_package_detector.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>

bool OrphanedPackageDetector::rpm_available() const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (rpm_checked_ == 0)
        rpm_checked_ = (std::system("rpm --version >/dev/null 2>&1") == 0) ? 1 : -1;
    return rpm_checked_ == 1;
}

// Wrap in single quotes so the shell never interprets the file name
static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += '\'';
    return out;
}

static std::string run_rpm_query(const fs::path& path) {
    std::string cmd =
        "rpm -qf --qf '%{name}' -- " + shell_quote(path.string()) + " 2>/dev/null";
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
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;
    }

    auto pkg = run_rpm_query(path);
    bool owned = !pkg.empty();

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        owned = true;

    std::lock_guard<std::mutex> lock(mtx_);
    cache_[key] = owned;
    return owned;
}

std::vector<ScanResult> OrphanedPackageDetector::detect(
    const fs::path& path) const {
    if (!enabled_)
        return {};

    // Package ownership only makes sense for /etc; never query rpm for
    // files from other scan targets like /tmp.
    if (path.lexically_normal().string().rfind("/etc/", 0) != 0)
        return {};

    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return {};

    if (is_owned_by_package(path))
        return {};

    auto ft = fs::last_write_time(path, ec);
    std::uintmax_t sz = fs::is_regular_file(path, ec) ? fs::file_size(path, ec) : 0;
    if (ec)
        sz = 0;

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
