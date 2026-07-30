#pragma once

#include "detector.hpp"
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

class Reporter {
    bool use_color_;
    bool json_mode_;

    static std::string escape_json(const std::string& s);
    static std::string time_to_string(fs::file_time_type ft);

    const char* col(const char* ansi) const {
        return use_color_ ? ansi : "";
    }

    void print_colored(const std::vector<ScanResult>& results,
                       std::ostream& os) const;

    void print_json(const std::vector<ScanResult>& results,
                    std::ostream& os) const;

public:
    static std::string human_size(uintmax_t bytes);

    explicit Reporter(bool use_color = true, bool json_mode = false);

    void print_results(const std::vector<ScanResult>& results,
                       std::ostream& os = std::cout) const;

    void print_summary(const std::vector<ScanResult>& results,
                       std::size_t blacklisted_count,
                       std::ostream& os = std::cout) const;
};
