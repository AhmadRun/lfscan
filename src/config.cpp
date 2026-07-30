#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static std::string trim_quoted(std::string s) {
    // Strip leading whitespace and up to one " character
    auto pos = s.find_first_not_of(" \t");
    if (pos != std::string::npos)
        s.erase(0, pos);
    if (!s.empty() && s.front() == '"')
        s.erase(0, 1);
    // Strip trailing whitespace and up to one " character
    pos = s.find_last_not_of(" \t");
    if (pos != std::string::npos)
        s.erase(pos + 1);
    if (!s.empty() && s.back() == '"')
        s.pop_back();
    return s;
}

static std::vector<std::string> parse_toml_array(const std::string& value) {
    std::vector<std::string> out;
    if (value.size() < 2 || value.front() != '[' || value.back() != ']')
        return out;
    auto inner = value.substr(1, value.size() - 2);
    std::string item;
    std::istringstream ss(inner);
    while (std::getline(ss, item, ',')) {
        auto t = trim_quoted(item);
        if (!t.empty())
            out.push_back(t);
    }
    return out;
}

Config load_config(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open())
        return cfg;

    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line.resize(hash);

        auto trim_left = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                [](unsigned char c) { return !std::isspace(c); }));
        };
        auto trim_right = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(),
                [](unsigned char c) { return !std::isspace(c); }).base(),
                s.end());
        };
        trim_left(line);
        trim_right(line);

        if (line.empty() || line[0] == '[')
            continue;

        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim_right(key);
        trim_left(value);

        // Remove surrounding quotes for scalar values
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        if (key == "max_age")
            cfg.max_age_days = std::stoi(value);
        else if (key == "log_retention")
            cfg.log_retention_days = std::stoi(value);
        else if (key == "orphaned")
            cfg.flag_orphaned = (value == "true");
        else if (key == "stale_logs")
            cfg.flag_stale_logs = (value == "true");
        else if (key == "targets")
            cfg.extra_targets = parse_toml_array(value);
        else if (key == "blacklist")
            cfg.extra_blacklist = parse_toml_array(value);
    }

    return cfg;
}
