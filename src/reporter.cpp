#include "reporter.hpp"
#include "color.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>

std::string Reporter::human_size(uintmax_t bytes) {
    constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double                v       = static_cast<double>(bytes);
    int                   u       = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    std::ostringstream oss;
    if (u == 0)
        oss << bytes << " B";
    else
        oss << std::fixed << std::setprecision(1) << v << ' ' << units[u];
    return oss.str();
}

std::string Reporter::escape_json(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
        case '"':  oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\n': oss << "\\n";  break;
        case '\t': oss << "\\t";  break;
        case '\r': oss << "\\r";  break;
        default:
            // Other control characters are not allowed raw in JSON strings
            if (static_cast<unsigned char>(c) < 0x20)
                oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c) << std::dec << std::setfill(' ');
            else
                oss << c;
            break;
        }
    }
    return oss.str();
}

std::string Reporter::time_to_string(fs::file_time_type ft) {
    auto sft = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sft);
    std::tm     tm = *std::gmtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

Reporter::Reporter(bool use_color, bool json_mode)
    : use_color_(use_color && color::enabled)
    , json_mode_(json_mode)
{}

void Reporter::print_results(const std::vector<ScanResult>& results,
                             std::ostream& os) const {
    if (json_mode_) {
        print_json(results, os);
    } else {
        print_colored(results, os);
    }
}

void Reporter::print_colored(const std::vector<ScanResult>& results,
                             std::ostream& os) const {
    if (results.empty()) {
        os << col(color::yellow) << "No files found." << col(color::reset)
           << '\n';
        return;
    }

    os << col(color::bold) << results.size() << " file(s) flagged:"
       << col(color::reset) << "\n\n";

    uintmax_t total = 0;
    for (const auto& r : results) {
        total += r.size;
        os << "  " << col(color::cyan) << r.path.string() << col(color::reset)
           << "  (" << col(color::yellow) << human_size(r.size)
           << col(color::reset) << ", " << col(color::dim)
           << r.detector_name << col(color::reset) << ")\n";
    }

    os << col(color::bold)
       << "\nTotal reclaimable space: "
       << col(color::green) << human_size(total) << col(color::reset) << '\n';
}

void Reporter::print_json(const std::vector<ScanResult>& results,
                          std::ostream& os) const {
    uintmax_t total = 0;
    os << "{\n  \"results\": [\n";
    bool first = true;
    for (const auto& r : results) {
        total += r.size;
        if (!first)
            os << ",\n";
        first = false;
        os << "    {\n"
           << "      \"path\": \"" << escape_json(r.path.string()) << "\",\n"
           << "      \"size\": " << r.size << ",\n"
           << "      \"last_modified\": \""
           << time_to_string(r.last_modified) << "\",\n"
           << "      \"detector\": \""
           << escape_json(r.detector_name) << "\"\n"
           << "    }";
    }
    os << "\n  ],\n"
       << "  \"total_files\": " << results.size() << ",\n"
       << "  \"total_size\": " << total << "\n"
       << "}\n";
}

void Reporter::print_summary(const std::vector<ScanResult>& results,
                             std::size_t blacklisted_count,
                             std::ostream& os) const {
    if (json_mode_)
        return;

    // Group by detector
    std::map<std::string, std::pair<std::size_t, uintmax_t>> groups;
    uintmax_t total = 0;
    for (const auto& r : results) {
        auto& g        = groups[r.detector_name];
        g.first  += 1;
        g.second += r.size;
        total += r.size;
    }

    if (groups.empty() && blacklisted_count == 0)
        return;

    os << col(color::bold) << "\nSummary by detector:"
       << col(color::reset) << '\n';

    for (const auto& [name, data] : groups) {
        os << "  " << col(color::cyan) << name << col(color::reset)
           << "  " << data.first << " file(s), "
           << human_size(data.second) << '\n';
    }

    os << "  " << col(color::bold) << "Total" << col(color::reset)
       << "  " << results.size() << " file(s), "
       << human_size(total) << '\n';

    if (blacklisted_count) {
        os << "  " << col(color::dim)
           << blacklisted_count << " file(s) on protected paths not shown"
           << col(color::reset) << '\n';
    }
}
