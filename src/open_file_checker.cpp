#include "open_file_checker.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

void OpenFileChecker::populate() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (populated_)
        return;
    populated_ = true;

    const fs::path proc{"/proc"};

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(proc, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        std::string dirname = entry.path().filename().string();
        if (dirname.empty() || !std::all_of(dirname.begin(), dirname.end(),
                                            ::isdigit))
            continue;

        fs::path fd_dir = entry.path() / "fd";
        fs::directory_iterator fd_it(fd_dir, ec);
        if (ec) {
            ec.clear();
            continue;
        }

        for (const auto& fd_entry : fd_it) {
            std::error_code ec2;
            fs::path target = fs::read_symlink(fd_entry.path(), ec2);
            if (ec2)
                continue;

            std::string tstr = target.string();
            if (tstr.empty() || tstr[0] != '/')
                continue;

            open_files_.insert(fs::weakly_canonical(target, ec2).string());
        }
    }
}

bool OpenFileChecker::is_open(const fs::path& path) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!populated_)
        return false;

    std::error_code ec;
    auto canon = fs::weakly_canonical(path, ec);
    if (ec)
        return false;

    return open_files_.contains(canon.string());
}
