#pragma once

#include "detector.hpp"
#include "mount_checker.hpp"
#include "open_file_checker.hpp"
#include "progress.hpp"
#include <memory>
#include <thread>
#include <vector>

class Scanner {
    std::vector<fs::path>                           targets_;
    std::vector<fs::path>                           blacklist_;
    std::vector<std::unique_ptr<IDetector>>         detectors_;
    mutable OpenFileChecker                         open_file_checker_;
    mutable MountChecker                            mount_checker_;
    unsigned                                        thread_count_{0};
    unsigned                                        max_depth_{50};

public:
    Scanner();
    void set_thread_count(unsigned n);
    void set_max_depth(unsigned d);

    void add_target(fs::path target);
    void add_blacklist(fs::path path);
    void add_detector(std::unique_ptr<IDetector> detector);

    bool is_blacklisted(const fs::path& path) const;

    std::vector<ScanResult> run() const;
};
