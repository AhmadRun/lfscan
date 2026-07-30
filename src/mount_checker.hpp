#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

class MountChecker {
    std::vector<fs::path>                mount_points_;
    std::vector<bool>                    is_remote_;
    bool                                 populated_{false};

public:
    void populate();

    bool is_network_path(const fs::path& path) const;
};
