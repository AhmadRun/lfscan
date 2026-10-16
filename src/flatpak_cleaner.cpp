#include "flatpak_cleaner.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

// ── helpers ──────────────────────────────────────────────────────────

std::string FlatpakCleaner::run_cmd(const std::string& cmd) {
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

std::uint64_t FlatpakCleaner::parse_flatpak_size(const std::string& s) {
    // Flatpak sizes: "659.3 MB", "1.7 GB", "113.7 kB", "1.4 MB", "0 bytes"
    // May contain UTF-8 non-breaking space (0xC2 0xA0) between number and unit
    std::string cleaned;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto c = static_cast<unsigned char>(s[i]);
        // Skip 0xC2 (UTF-8 prefix for U+0080–U+00BF) and 0xA0 (NBSP)
        if (c == 0xC2 || c == 0xA0)
            continue;
        cleaned += s[i];
    }

    std::regex re(R"(([\d.]+)\s*(kB|MB|GB|bytes))");
    std::smatch m;
    if (!std::regex_search(cleaned, m, re))
        return 0;

    double val = std::stod(m[1].str());
    std::string unit = m[2].str();

    // GLib's g_format_size() uses decimal (SI) units: 1 kB = 1000 B
    if (unit == "kB")   val *= 1e3;
    else if (unit == "MB")  val *= 1e6;
    else if (unit == "GB")  val *= 1e9;

    return static_cast<std::uint64_t>(val);
}

// ── flatpak list wrapper ─────────────────────────────────────────────

std::vector<FlatpakCleaner::FlatpakEntry> FlatpakCleaner::list_installed() const {
    std::vector<FlatpakEntry> entries;

    // List runtimes  (tab-separated: application\tbranch\tsize)
    auto runtimes_out = run_cmd(
        "flatpak list --runtime --columns=application,branch,size 2>/dev/null");
    std::istringstream rs(runtimes_out);
    std::string line;
    while (std::getline(rs, line)) {
        std::istringstream ls(line);
        std::string app, branch, size_str;
        if (!std::getline(ls, app, '\t'))   continue;
        if (!std::getline(ls, branch, '\t')) continue;
        std::getline(ls, size_str);  // rest of line

        FlatpakEntry e;
        e.id          = app;
        e.description = app + " runtime (" + branch + ")";
        e.branch      = branch;
        e.size        = parse_flatpak_size(size_str);
        e.is_runtime  = true;
        entries.push_back(std::move(e));
    }

    // List apps  (tab-separated: application\tsize)
    auto apps_out = run_cmd(
        "flatpak list --app --columns=application,size 2>/dev/null");
    std::istringstream as(apps_out);
    while (std::getline(as, line)) {
        std::istringstream ls(line);
        std::string app, size_str;
        if (!std::getline(ls, app, '\t'))   continue;
        std::getline(ls, size_str);

        FlatpakEntry e;
        e.id          = app;
        e.description = app + " (app)";
        e.branch      = "";
        e.size        = parse_flatpak_size(size_str);
        e.is_runtime  = false;
        entries.push_back(std::move(e));
    }

    return entries;
}

// ── interface ────────────────────────────────────────────────────────

bool FlatpakCleaner::available() const {
    auto ver = run_cmd("flatpak --version 2>/dev/null");
    return !ver.empty();
}

std::vector<CleanupItem> FlatpakCleaner::scan() {
    std::vector<CleanupItem> items;

    auto entries = list_installed();
    if (entries.empty())
        return items;

    // Aggregate runtimes and apps
    std::uint64_t runtimes_total = 0, apps_total = 0;
    std::size_t runtime_count = 0, app_count = 0;

    for (const auto& e : entries) {
        if (e.is_runtime) {
            runtimes_total += e.size;
            runtime_count++;
        } else {
            apps_total += e.size;
            app_count++;
        }
    }

    if (runtime_count > 0) {
        CleanupItem ci;
        ci.id          = "flatpak-runtimes";
        ci.description = std::to_string(runtime_count)
                         + " Flatpak runtime(s)";
        ci.size        = runtimes_total;
        ci.category    = "flatpak";
        ci.risk        = Risk::Safe;
        ci.reversible  = true;
        ci.metadata["type"] = "runtimes";
        items.push_back(std::move(ci));
    }

    if (app_count > 0) {
        CleanupItem ci;
        ci.id          = "flatpak-apps";
        ci.description = std::to_string(app_count)
                         + " Flatpak application(s)";
        ci.size        = apps_total;
        ci.category    = "flatpak";
        ci.risk        = Risk::Caution;
        ci.reversible  = true;
        ci.metadata["type"] = "apps";
        items.push_back(std::move(ci));
    }

    return items;
}

CleanupResult FlatpakCleaner::clean(const std::vector<CleanupItem>& items) {
    CleanupResult result;

    for (const auto& item : items) {
        if (item.category != "flatpak")
            continue;

        auto type_it = item.metadata.find("type");
        if (type_it == item.metadata.end() || type_it->second == "apps") {
            result.errors.push_back(item.id
                + ": automatic removal of Flatpak apps not supported; "
                  "use 'flatpak uninstall <app>' manually");
            result.failed++;
            continue;
        }

        // Unused runtimes
        auto before = scan();
        std::uint64_t before_size = 0;
        for (const auto& b : before)
            before_size += b.size;

        std::cout << "  Running flatpak uninstall --unused ...\n";
        auto out = run_cmd("flatpak uninstall --unused --noninteractive -y 2>&1");

        if (out.find("error:") != std::string::npos) {
            result.errors.push_back("flatpak: " + out);
            result.failed++;
            continue;
        }

        auto after = scan();
        std::uint64_t after_size = 0;
        for (const auto& a : after)
            after_size += a.size;

        auto freed = (before_size > after_size) ? (before_size - after_size) : 0;
        result.bytes_freed += freed;
        result.cleaned++;
    }

    return result;
}
