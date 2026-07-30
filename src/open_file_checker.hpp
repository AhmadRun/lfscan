#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

namespace fs = std::filesystem;

class OpenFileChecker {
    std::unordered_set<std::string> open_files_;
    bool                            populated_{false};
    mutable std::mutex              mtx_;

public:
    void  populate();
    bool  is_open(const fs::path& path) const;
};
