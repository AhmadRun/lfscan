#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <string>

class Progress {
    std::atomic<std::size_t> count_{0};
    std::size_t              total_{0};
    std::string              label_;
    int                      throttle_ms_{150};
    std::chrono::steady_clock::time_point last_update_;
    std::mutex               mtx_;  // increment() is called from scanner threads

public:
    explicit Progress(std::string label = "Scanned")
        : label_(std::move(label))
        , last_update_(std::chrono::steady_clock::now())
    {}

    void set_total(std::size_t t) { total_ = t; }

    void increment() {
        auto c = ++count_;
        std::unique_lock<std::mutex> lock(mtx_, std::try_to_lock);
        if (!lock.owns_lock())
            return;  // another thread is already printing
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_update_);
        if (c < total_ && elapsed.count() < throttle_ms_)
            return;
        last_update_ = now;
        print();
    }

    void print() {
        auto c = count_.load();
        if (total_ > 0) {
            int pct = static_cast<int>(c * 100 / total_);
            std::cerr << '\r' << label_ << ' ' << c << '/' << total_
                      << " (" << pct << "%)   " << std::flush;
        } else {
            std::cerr << '\r' << label_ << ' ' << c << " files...   "
                      << std::flush;
        }
    }

    void done() {
        print();
        std::cerr << std::endl;
    }
};
