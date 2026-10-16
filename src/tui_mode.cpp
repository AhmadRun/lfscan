#include "tui_mode.hpp"
#include "age_detector.hpp"
#include "broken_symlink_detector.hpp"
#include "pattern_detector.hpp"
#include "reporter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <map>
#include <mutex>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace fs = std::filesystem;
using namespace ftxui;

// ── helpers ──────────────────────────────────────────────────────────

static std::vector<ScanResult> run_scan() {
    Scanner s;
    s.add_target("/tmp");
    s.add_target("/var/tmp");
    const char* home = std::getenv("HOME");
    if (home)
        s.add_target(fs::path(home) / ".cache");

    s.add_detector(std::make_unique<AgeBasedDetector>(30));
    s.add_detector(std::make_unique<PatternDetector>(
        std::vector<std::string>{"*.tmp", "*.swp", "*.swo", "*~", "core", "core.*"}));
    s.add_detector(std::make_unique<BrokenSymlinkDetector>());

    return s.run();
}

static Element result_table(const std::vector<ScanResult>& results) {
    if (results.empty())
        return text(" No results.") | dim;

    std::map<std::string, std::vector<ScanResult>> groups;
    for (const auto& r : results)
        groups[r.detector_name].push_back(r);

    Elements lines;
    lines.push_back(text(""));
    uint64_t grand_total = 0;
    for (const auto& [det, items] : groups) {
        lines.push_back(text(" " + det + ":") | bold);
        uint64_t det_total = 0;
        for (const auto& r : items) {
            det_total += r.size;
            auto sz = Reporter::human_size(r.size);
            lines.push_back(text("  " + r.path.string() + "  " + sz) | dim);
        }
        lines.push_back(text("  Total: " + Reporter::human_size(det_total)));
        lines.push_back(text(""));
        grand_total += det_total;
    }
    lines.push_back(text(" Grand total: " + Reporter::human_size(grand_total)) | bold);

    return vbox(std::move(lines)) | vscroll_indicator | yframe;
}

static Element history_table(const std::vector<HistoryEntry>& entries) {
    if (entries.empty())
        return text(" No history.") | dim;

    // Group by scan_id
    std::map<std::int64_t, std::vector<HistoryEntry>> by_scan;
    for (const auto& e : entries)
        by_scan[e.scan_id].push_back(e);

    Elements lines;
    lines.push_back(text(""));
    for (const auto& [sid, ents] : by_scan) {
        uint64_t total_sz = 0;
        std::set<std::string> cleaners;
        for (const auto& e : ents) {
            total_sz += e.size_bytes;
            cleaners.insert(e.cleaner);
        }
        std::string clist;
        for (const auto& c : cleaners) {
            if (!clist.empty()) clist += ", ";
            clist += c;
        }

        lines.push_back(text(" Scan #" + std::to_string(sid)
            + "  " + std::to_string(ents.size()) + " items, "
            + Reporter::human_size(total_sz)) | bold);

        if (ents.size() <= 5) {
            for (const auto& e : ents)
                lines.push_back(text("   " + e.item_key) | dim);
        } else {
            lines.push_back(text("   Cleaners: " + clist) | dim);
        }
        lines.push_back(text(""));
    }

    return vbox(std::move(lines)) | vscroll_indicator | yframe;
}

// ── TUI main ─────────────────────────────────────────────────────────

int run_tui(HistoryDB& history, const std::string& db_path) {
    auto screen = ScreenInteractive::Fullscreen();

    // State shared with the scan/watch worker threads is guarded by
    // state_mtx; the threads are joined before this function returns, so
    // the references they capture stay valid.
    std::mutex              state_mtx;
    int                    tab_index = 0;
    std::vector<ScanResult> scan_results;
    std::string             status_text = "Ready";
    std::atomic<bool>       scanning{false};
    std::thread             scan_thread;
    std::thread             watch_thread;

    // History cache
    std::vector<HistoryEntry> history_cache;
    auto refresh_history = [&] {
        history_cache = history.query("", 200);
    };

    // Watch mode
    std::atomic<bool> watch_active{false};
    std::vector<ScanResult> watch_results;
    int                     watch_iteration{0};
    std::string             watch_status = "Stopped";

    // Only signals the worker: joining here would freeze the UI until the
    // current scan finishes. It is joined on the next start or on exit.
    auto stop_watch = [&] {
        watch_active.store(false);
        std::lock_guard<std::mutex> lock(state_mtx);
        watch_status = "Stopped";
    };

    auto start_watch = [&] {
        if (watch_active.load()) return;
        if (watch_thread.joinable())
            watch_thread.join();
        watch_active.store(true);
        {
            std::lock_guard<std::mutex> lock(state_mtx);
            watch_status = "Running";
            watch_iteration = 0;
        }
        watch_thread = std::thread([&] {
            while (watch_active.load()) {
                auto res = run_scan();
                {
                    std::lock_guard<std::mutex> lock(state_mtx);
                    watch_results = std::move(res);
                    watch_iteration++;
                }
                screen.PostEvent(Event::Custom);
                for (int i = 0; i < 10 && watch_active.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    };

    // ── Tab components ───────────────────────────────────────────
    auto scan_tab = Container::Vertical({
        Button(" Run Scan ", [&] {
            if (scanning.exchange(true)) return;
            if (scan_thread.joinable())
                scan_thread.join();
            {
                std::lock_guard<std::mutex> lock(state_mtx);
                status_text = "Scanning...";
            }
            scan_thread = std::thread([&] {
                auto results = run_scan();
                ScanRecord rec;
                rec.dry_run = true;
                rec.paths_scanned = "/tmp,/var/tmp,.cache";
                auto sid = history.log_scan(rec);
                if (sid >= 0) {
                    for (const auto& r : results) {
                        HistoryEntry e;
                        e.scan_id    = sid;
                        e.cleaner    = r.detector_name;
                        e.category   = r.detector_name;
                        e.item_key   = r.path.string();
                        e.size_bytes = static_cast<std::int64_t>(r.size);
                        e.action     = "found";
                        history.log_result(e);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(state_mtx);
                    status_text = std::to_string(results.size())
                                + " file(s) flagged.";
                    scan_results = std::move(results);
                }
                scanning = false;
                screen.PostEvent(Event::Custom);
            });
        }),
        Renderer([&] {
            if (scanning)
                return text(" Scanning...") | bold | color(Color::Yellow);
            std::lock_guard<std::mutex> lock(state_mtx);
            return result_table(scan_results);
        }),
    });

    auto history_tab = Container::Vertical({
        Button(" Refresh ", refresh_history),
        Renderer([&] { return history_table(history_cache); }),
    });

    auto watch_tab = Container::Vertical({
        Container::Horizontal({
            Button(" Start Watch ", start_watch),
            Button(" Stop Watch ",  stop_watch),
        }),
        Renderer([&] {
            std::lock_guard<std::mutex> lock(state_mtx);
            Elements lines;
            lines.push_back(text(""));
            lines.push_back(
                text(" Status: " + watch_status)
                | (watch_active.load() ? color(Color::Green) : color(Color::GrayDark)));
            lines.push_back(text(" Iterations: " + std::to_string(watch_iteration)));
            if (!watch_results.empty()) {
                uint64_t total = 0;
                std::string det;
                for (const auto& r : watch_results) {
                    total += r.size;
                    if (det.empty()) det = r.detector_name;
                    else if (det != r.detector_name) det = "mixed";
                }
                lines.push_back(
                    text(" Last: " + std::to_string(watch_results.size())
                         + " file(s), " + Reporter::human_size(total)));
                for (const auto& r : watch_results)
                    lines.push_back(text("  " + r.path.string()) | dim);
            }
            return vbox(std::move(lines)) | vscroll_indicator | yframe;
        }),
    });

    // ── Layout ──────────────────────────────────────────────────
    std::vector<std::string> tab_entries = {" Scan ", " History ", " Watch "};
    auto menu = Menu(&tab_entries, &tab_index);

    auto tabs = Container::Tab(
        {scan_tab, history_tab, watch_tab}, &tab_index
    );

    auto quit_btn = Button(" Quit ", [&] { screen.Exit(); });

    auto main_container = Container::Horizontal({
        menu,
        tabs,
        quit_btn,
    });

    auto status_line = [&] {
        std::lock_guard<std::mutex> lock(state_mtx);
        return status_text;
    };

    auto renderer = Renderer(main_container, [&] {
        return vbox(Elements{
            text(" lfscan — System Cleanup Tool") | bold | hcenter,
            separator(),
            hbox(Elements{
                menu->Render(),
                separatorEmpty(),
                quit_btn->Render(),
            }),
            separator(),
            tabs->Render() | flex,
            separator(),
            text(" " + status_line()) | dim,
        });
    });

    // Initial history load
    refresh_history();

    screen.Loop(renderer);

    // Don't leave workers running against this function's locals
    watch_active.store(false);
    if (watch_thread.joinable())
        watch_thread.join();
    if (scan_thread.joinable())
        scan_thread.join();
    return 0;
}
