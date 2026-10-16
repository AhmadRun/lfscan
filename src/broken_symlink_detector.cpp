#include "broken_symlink_detector.hpp"

std::vector<ScanResult> BrokenSymlinkDetector::detect(
    const fs::path& path) const {
    std::error_code ec;

    auto status = fs::symlink_status(path, ec);
    if (ec || status.type() != fs::file_type::symlink)
        return {};

    // fs::status follows the link, resolving relative targets against the
    // link's own directory (not the current working directory).
    // Anything other than "not found" (e.g. permission denied) can't be
    // confirmed broken, so it isn't flagged.
    if (fs::status(path, ec).type() != fs::file_type::not_found)
        return {};

    std::vector<ScanResult> out;
    out.push_back({path, 0, {}, name()});
    return out;
}

std::string BrokenSymlinkDetector::name() const {
    return "broken-symlink";
}
