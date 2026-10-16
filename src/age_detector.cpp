#include "age_detector.hpp"

AgeBasedDetector::AgeBasedDetector(int days)
    : max_age_(std::chrono::duration_cast<fs::file_time_type::duration>(
          std::chrono::hours(days * 24)))
    , name_("age-based (>" + std::to_string(days) + " days)")
{}

std::vector<ScanResult> AgeBasedDetector::detect(const fs::path& path) const {
    std::vector<ScanResult> results;

    std::error_code ec;
    // symlink_status: a symlink to an old file is not itself an old file
    if (!fs::is_regular_file(fs::symlink_status(path, ec)) || ec)
        return results;

    auto ft = fs::last_write_time(path, ec);
    if (ec)
        return results;

    auto age = fs::file_time_type::clock::now() - ft;
    if (age <= max_age_)
        return results;

    auto sz = fs::file_size(path, ec);
    if (ec)
        return results;

    results.push_back({path, sz, ft, name_});
    return results;
}

std::string AgeBasedDetector::name() const {
    return name_;
}
