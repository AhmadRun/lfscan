#include "quarantine.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static std::string timestamp() {
    auto now  = std::time(nullptr);
    auto* ltm = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(ltm, "%Y%m%d_%H%M%S");
    return oss.str();
}

bool move_path(const fs::path& src, const fs::path& dest, std::error_code& ec) {
    ec.clear();
    fs::rename(src, dest, ec);
    if (!ec)
        return true;
    if (ec != std::errc::cross_device_link)
        return false;

    ec.clear();
    fs::copy(src, dest,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             ec);
    if (ec)
        return false;
    // Keep the original timestamp so age-based scans still see it as old
    auto ft = fs::last_write_time(src, ec);
    if (!ec && !fs::is_symlink(fs::symlink_status(src)))
        fs::last_write_time(dest, ft, ec);
    ec.clear();
    fs::remove_all(src, ec);
    if (ec) {
        // Don't leave two copies behind
        std::error_code ec2;
        fs::remove_all(dest, ec2);
        return false;
    }
    return true;
}

Quarantine::Quarantine(fs::path quarantine_dir)
    : quarantine_dir_(std::move(quarantine_dir))
    , manifest_path_(quarantine_dir_ / "manifest.txt")
{}

bool Quarantine::move_to_quarantine(const ScanResult& result) {
    std::error_code ec;

    fs::create_directories(quarantine_dir_, ec);
    if (ec) {
        std::cerr << "error: cannot create quarantine dir ("
                  << ec.message() << ")\n";
        return false;
    }

    // Build relative path: strip leading '/', keep original hierarchy
    auto rel = result.path.relative_path();
    fs::path dest = quarantine_dir_ / timestamp() / rel;

    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
        std::cerr << "error: cannot create directory in quarantine ("
                  << ec.message() << ")\n";
        return false;
    }

    if (!move_path(result.path, dest, ec)) {
        std::cerr << "error: cannot move file to quarantine ("
                  << ec.message() << ")\n";
        return false;
    }

    // Append to manifest
    std::ofstream mf(manifest_path_, std::ios::app);
    if (mf) {
        mf << result.path.string() << '\t' << dest.string() << '\t'
           << timestamp() << '\n';
    }

    return true;
}

bool Quarantine::quarantine_files(const std::vector<ScanResult>& results) {
    bool all_ok = true;
    for (const auto& r : results) {
        if (move_to_quarantine(r)) {
            std::cout << "  quarantined: " << r.path.string() << '\n';
        } else {
            std::cerr << "  FAILED: " << r.path.string() << '\n';
            all_ok = false;
        }
    }
    return all_ok;
}
