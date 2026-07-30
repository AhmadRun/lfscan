#include "mount_checker.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Known network/local filesystem type identifiers from /proc/self/mountinfo
static const std::unordered_set<std::string> kNetworkTypes = {
    "nfs", "nfs4", "cifs", "smb3", "lustre", "gfs2",
};

void MountChecker::populate() {
    if (populated_)
        return;
    populated_ = true;

    std::ifstream f("/proc/self/mountinfo");
    if (!f.is_open())
        return;

    std::string line;
    while (std::getline(f, line)) {
        // Format (man 5 proc):
        // id parent_id major:minor root mount_point options ... - fstype source ...
        auto dash = line.rfind(" - ");
        if (dash == std::string::npos)
            continue;

        std::string right = line.substr(dash + 3);
        std::istringstream rs(right);
        std::string fstype, source;
        rs >> fstype >> source;

        // Mount point is the 5th space-delimited field (index 4)
        std::istringstream ls(line);
        std::string field;
        int idx = 0;
        std::string mount_point;
        while (ls >> field) {
            if (idx == 4) {
                mount_point = field;
                break;
            }
            ++idx;
        }

        if (mount_point.empty())
            continue;

        bool remote = kNetworkTypes.contains(fstype);
        mount_points_.push_back(fs::path(mount_point));
        is_remote_.push_back(remote);
    }
}

bool MountChecker::is_network_path(const fs::path& path) const {
    if (!populated_)
        return false;

    auto pstr = path.lexically_normal().string();

    // Check longest matching mount point first
    std::size_t best = 0;
    int         best_idx = -1;
    for (std::size_t i = 0; i < mount_points_.size(); ++i) {
        auto mstr = mount_points_[i].string();
        if (!is_remote_[i])
            continue;
        if (pstr == mstr || pstr.rfind(mstr + '/', 0) == 0) {
            if (mstr.size() > best) {
                best = mstr.size();
                best_idx = static_cast<int>(i);
            }
        }
    }
    return best_idx >= 0;
}
