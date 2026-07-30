#include "journal_cleaner.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <regex>
#include <unistd.h>
#include <sstream>
#include <stdexcept>
#include <string>

// ── helpers ──────────────────────────────────────────────────────────

std::string JournalCleaner::run_cmd(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen(cmd.c_str(), "r"), pclose);
    if (!pipe)
        return {};
    while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr)
        result += buf.data();
    return result;
}

std::uint64_t JournalCleaner::parse_size(const std::string& s) {
    // journalctl --disk-usage output:
    //   "Archived and active journals take up 1.2G in the file system."
    //   "Archived and active journals take up 88.0M in the file system."
    std::regex re(R"(([\d.]+)\s*([KMGTP]?))");
    std::smatch m;
    if (!std::regex_search(s, m, re))
        return 0;

    double val = std::stod(m[1].str());
    char unit = m[2].str().empty() ? 'B' : m[2].str()[0];

    switch (unit) {
        case 'K': val *= 1024ULL;              break;
        case 'M': val *= 1024ULL * 1024;       break;
        case 'G': val *= 1024ULL * 1024 * 1024; break;
        case 'T': val *= 1024ULL * 1024 * 1024 * 1024; break;
        default: break;
    }
    return static_cast<std::uint64_t>(val);
}

bool JournalCleaner::can_vacuum() {
    // Root can always vacuum
    if (::geteuid() == 0)
        return true;

    // Check systemd-journal group membership
    std::string groups = run_cmd("id -Gn 2>/dev/null");
    return groups.find("systemd-journal") != std::string::npos;
}

// ── interface ────────────────────────────────────────────────────────

bool JournalCleaner::available() const {
    // Check journalctl exists
    auto ver = run_cmd("journalctl --version 2>/dev/null");
    if (ver.empty())
        return false;

    // For cleaning, require vacuum permissions
    if (!vacuum_target_.empty())
        return can_vacuum();

    // For scanning only, anyone with journalctl is fine
    return true;
}

void JournalCleaner::set_vacuum_target(const std::string& target) {
    vacuum_target_ = target;
}

std::vector<CleanupItem> JournalCleaner::scan() {
    std::vector<CleanupItem> items;

    auto raw = run_cmd("journalctl --disk-usage 2>/dev/null");
    if (raw.empty())
        return items;

    auto size = parse_size(raw);
    if (size == 0)
        return items;

    CleanupItem ci;
    ci.id          = "journal";
    ci.description = "Systemd journal logs (" + raw;
    // Trim trailing whitespace/newline from raw for description
    while (!ci.description.empty() &&
           (ci.description.back() == '\n' || ci.description.back() == ' '))
        ci.description.pop_back();
    ci.description += ")";
    ci.size     = size;
    ci.category = "system-journal";
    ci.risk     = Risk::Caution;
    ci.reversible = false;
    ci.metadata["current_size"] = std::to_string(size);

    items.push_back(std::move(ci));
    return items;
}

CleanupResult JournalCleaner::clean(const std::vector<CleanupItem>& items) {
    CleanupResult result;

    for (const auto& item : items) {
        if (item.category != "system-journal")
            continue;

        if (vacuum_target_.empty()) {
            result.errors.push_back("no vacuum target set for journal");
            result.failed++;
            continue;
        }

        // Validate that the target is smaller than current
        auto current = 0ULL;
        try {
            current = std::stoull(
                item.metadata.at("current_size"));
        } catch (...) {}

        auto target = parse_size(vacuum_target_);
        if (target == 0) {
            result.errors.push_back("invalid vacuum target: "
                                    + vacuum_target_);
            result.failed++;
            continue;
        }

        auto cmd = "journalctl --vacuum-size=" + vacuum_target_ + " 2>&1";
        auto out = run_cmd(cmd);

        // journalctl vacuum returns 0 even when it can't delete due to
        // permissions — check stderr for "Permission denied"
        if (out.find("Permission denied") != std::string::npos) {
            result.errors.push_back("journal: permission denied "
                                    "(need root or systemd-journal group)");
            result.failed++;
            continue;
        }

        // Re-scan to measure actual freed space
        auto after_raw = run_cmd("journalctl --disk-usage 2>/dev/null");
        auto after = parse_size(after_raw);

        if (after >= current) {
            result.cleaned++;
            // Couldn't free anything — maybe already at target
        } else {
            result.bytes_freed += (current - after);
            result.cleaned++;
        }
    }

    return result;
}
