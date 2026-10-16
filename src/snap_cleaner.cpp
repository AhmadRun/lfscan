#include "snap_cleaner.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// ── helpers ──────────────────────────────────────────────────────────

std::string SnapCleaner::run_cmd(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string result;
    auto closer = [](FILE* f) { pclose(f); };
    std::unique_ptr<FILE, decltype(closer)> pipe(popen(cmd.c_str(), "r"), closer);
    if (!pipe)
        return {};
    while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr)
        result += buf.data();
    return result;
}

std::uint64_t SnapCleaner::snap_file_size(const std::string& snap,
                                          const std::string& rev) {
    // Snaps stored as /var/lib/snapd/snaps/<name>_<rev>.snap
    auto path = "/var/lib/snapd/snaps/" + snap + "_" + rev + ".snap";
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec) {
        // Try alternative naming: <name>_<rev>.snap with instance key
        // e.g. snap_name_rev.snap
        return 0;
    }
    return sz;
}

// ── snap list wrapper ───────────────────────────────────────────────

std::vector<SnapCleaner::SnapRev> SnapCleaner::list_all() const {
    std::vector<SnapRev> revs;

    auto out = run_cmd("snap list --all 2>/dev/null");
    if (out.empty())
        return revs;

    std::istringstream ss(out);
    std::string line;

    // Skip header line
    if (!std::getline(ss, line))
        return revs;

    while (std::getline(ss, line)) {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string name, ver, rev_str, tracking, publisher, notes;
        if (!(ls >> name >> ver >> rev_str >> tracking >> publisher))
            continue;
        std::getline(ls, notes);
        // Trim leading space from notes
        if (!notes.empty() && notes[0] == ' ')
            notes = notes.substr(1);

        SnapRev r;
        r.snap     = name;
        r.revision = rev_str;
        r.notes    = notes;
        r.disabled = (notes.find("disabled") != std::string::npos);
        revs.push_back(std::move(r));
    }

    return revs;
}

// ── interface ────────────────────────────────────────────────────────

bool SnapCleaner::available() const {
    auto ver = run_cmd("snap --version 2>/dev/null");
    return !ver.empty();
}

std::vector<CleanupItem> SnapCleaner::scan() {
    std::vector<CleanupItem> items;

    auto all = list_all();
    if (all.empty())
        return items;

    // Count totals
    std::size_t total_revs = all.size();
    std::size_t disabled_count = 0;
    std::uint64_t disabled_size = 0;
    std::uint64_t total_size = 0;

    for (const auto& r : all) {
        auto sz = snap_file_size(r.snap, r.revision);
        total_size += sz;
        if (r.disabled) {
            disabled_count++;
            disabled_size += sz;
        }
    }

    // Item for disabled (cleanable) revisions
    if (disabled_count > 0) {
        CleanupItem ci;
        ci.id          = "snap-disabled-revisions";
        ci.description = std::to_string(disabled_count)
                         + " disabled Snap revision(s) out of "
                         + std::to_string(total_revs);
        ci.size        = disabled_size;
        ci.category    = "snap";
        ci.risk        = Risk::Safe;
        ci.reversible  = true;
        ci.metadata["type"] = "old-revisions";
        items.push_back(std::move(ci));
    }

    // Count unique snap names
    std::set<std::string> unique_snaps;
    for (const auto& r : all) unique_snaps.insert(r.snap);

    // Total snap usage info
    CleanupItem ci;
    ci.id          = "snap-total";
    ci.description = std::to_string(total_revs)
                     + " total Snap revision(s) across "
                     + std::to_string(unique_snaps.size()) + " snap(s)";
    ci.size        = total_size;
    ci.category    = "snap";
    ci.risk        = Risk::Caution;
    ci.reversible  = false;
    ci.metadata["type"] = "all";
    items.push_back(std::move(ci));

    return items;
}

CleanupResult SnapCleaner::clean(const std::vector<CleanupItem>& items) {
    CleanupResult result;

    for (const auto& item : items) {
        if (item.category != "snap")
            continue;

        auto type_it = item.metadata.find("type");
        if (type_it == item.metadata.end() || type_it->second != "old-revisions") {
            result.errors.push_back(item.id
                + ": can only clean disabled revisions");
            result.failed++;
            continue;
        }

        // Get disabled revisions and remove each
        auto all = list_all();
        std::size_t removed = 0;
        std::uint64_t freed = 0;

        for (const auto& r : all) {
            if (!r.disabled)
                continue;

            auto sz = snap_file_size(r.snap, r.revision);
            auto cmd = "snap remove --revision " + r.revision + " "
                       + r.snap + " 2>&1";
            auto out = run_cmd(cmd);

            if (out.find("error:") != std::string::npos) {
                result.errors.push_back(r.snap + " rev " + r.revision
                                        + ": " + out);
                result.failed++;
            } else {
                removed++;
                freed += sz;
                std::cout << "  removed: " << r.snap
                          << " rev " << r.revision << '\n';
            }
        }

        if (removed > 0) {
            result.cleaned += removed;
            result.bytes_freed += freed;
        }
    }

    return result;
}
