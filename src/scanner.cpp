#include "scanner.hpp"

#include <iostream>
#include <map>
#include <mutex>

static const fs::path kCriticalPaths[] = {
    "/boot", "/etc", "/proc", "/sys",
};

Scanner::Scanner()
    : thread_count_(std::thread::hardware_concurrency()) {
    if (thread_count_ < 1)
        thread_count_ = 1;
    for (const auto& p : kCriticalPaths)
        blacklist_.push_back(p);
}

void Scanner::set_thread_count(unsigned n) {
    thread_count_ = (n < 1) ? 1 : n;
}

void Scanner::set_max_depth(unsigned d) {
    max_depth_ = d;
}

void Scanner::add_target(fs::path target) {
    targets_.push_back(std::move(target));
}

void Scanner::add_blacklist(fs::path path) {
    blacklist_.push_back(std::move(path));
}

void Scanner::add_detector(std::unique_ptr<IDetector> detector) {
    auto extra = detector->scan_targets();
    for (auto& t : extra)
        targets_.push_back(std::move(t));
    detectors_.push_back(std::move(detector));
}

bool Scanner::is_blacklisted(const fs::path& path) const {
    auto pstr = path.string();
    for (const auto& b : blacklist_) {
        auto bp = b.string();
        if (pstr == bp || pstr.rfind(bp + '/', 0) == 0)
            return true;
    }
    return false;
}

std::vector<ScanResult> Scanner::run() const {
    mount_checker_.populate();

    // Phase 1: collect all file paths
    std::vector<fs::path> all_paths;
    Progress              collect_prog("Collecting");

    for (const auto& target : targets_) {
        std::error_code ec;

        if (!fs::exists(target, ec) || ec) {
            std::cerr << "warning: target does not exist: " << target << '\n';
            continue;
        }

        if (mount_checker_.is_network_path(target)) {
            std::cerr << "warning: skipping network mount: " << target << '\n';
            continue;
        }

        fs::recursive_directory_iterator it(
            target, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            std::cerr << "warning: cannot open target: " << target
                      << " (" << ec.message() << ")\n";
            continue;
        }

        for (auto end = fs::recursive_directory_iterator{}; it != end;
             it.increment(ec)) {
            if (ec) {
                it.disable_recursion_pending();
                ec.clear();
                continue;
            }

            // Depth guard
            if (it.depth() >= max_depth_) {
                it.disable_recursion_pending();
                continue;
            }

            const auto& entry = *it;
            auto type = entry.symlink_status(ec).type();
            if (ec || type == fs::file_type::directory
                   || type == fs::file_type::none) {
                ec.clear();
                continue;
            }

            // Skip entries on network mounts
            if (mount_checker_.is_network_path(entry.path()))
                continue;

            all_paths.push_back(entry.path());
            collect_prog.increment();
        }
    }
    collect_prog.done();

    Progress detect_prog("Checking");
    detect_prog.set_total(all_paths.size());

    // Phase 2: process files in parallel
    std::map<fs::path, ScanResult> global_seen;
    std::mutex                     global_mutex;

    auto chunk = [&](std::size_t begin, std::size_t end) {
        std::map<fs::path, ScanResult> local;
        for (std::size_t i = begin; i < end; ++i) {
            const auto& path = all_paths[i];
            for (const auto& det : detectors_) {
                auto hits = det->detect(path);
                for (auto& hit : hits)
                    local.try_emplace(hit.path, std::move(hit));
            }
            detect_prog.increment();
        }
        std::lock_guard<std::mutex> lock(global_mutex);
        for (auto& [k, v] : local)
            global_seen.try_emplace(k, std::move(v));
    };

    std::size_t n      = all_paths.size();
    unsigned    nthread = (n < 100) ? 1 : thread_count_;
    std::size_t chunk_size = (n + nthread - 1) / nthread;

    std::vector<std::thread> threads;
    threads.reserve(nthread);
    for (unsigned t = 0; t < nthread; ++t) {
        std::size_t begin = t * chunk_size;
        std::size_t end   = std::min(begin + chunk_size, n);
        if (begin >= end)
            break;
        threads.emplace_back(chunk, begin, end);
    }
    for (auto& t : threads)
        t.join();

    detect_prog.done();

    // Phase 3: post-filter open files
    open_file_checker_.populate();

    std::vector<ScanResult> filtered;
    for (auto& [path, result] : global_seen) {
        if (open_file_checker_.is_open(path))
            continue;
        filtered.push_back(std::move(result));
    }

    return filtered;
}
