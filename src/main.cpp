#include "age_detector.hpp"
#include "broken_symlink_detector.hpp"
#include "cleaner_registry.hpp"
#include "color.hpp"
#include "config.hpp"
#include "container_cleaner.hpp"
#include "flatpak_cleaner.hpp"
#include "history_db.hpp"
#include "journal_cleaner.hpp"
#include "language_cache_cleaner.hpp"
#include "quarantine_restore.hpp"
#include "snap_cleaner.hpp"
#include "tui_mode.hpp"
#include "orphaned_package_detector.hpp"
#include "pattern_detector.hpp"
#include "quarantine.hpp"
#include "reporter.hpp"
#include "scanner.hpp"
#include "stale_log_detector.hpp"

#include <CLI/CLI.hpp>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static bool confirm_action(const std::string& prompt) {
    std::cout << prompt << " [y/N] ";
    std::string line;
    std::getline(std::cin, line);
    return line == "y" || line == "Y" || line == "yes" || line == "YES";
}

// Maps a ScanResult detector name to a stable DB cleaner category
static const char* detector_category(const std::string& name) {
    if (name == "age_based")  return "tmp-cache";
    if (name == "pattern")    return "tmp-files";
    if (name == "broken_symlink") return "broken-symlinks";
    if (name == "orphaned-package") return "orphaned-packages";
    if (name == "stale_log")      return "stale-logs";
    return "other";
}

int main(int argc, char** argv) {
    CLI::App app{"lfscan — temporary & orphaned file scanner (dry-run by default)"};

    int              max_age_days{30};
    int              log_retention_days{90};
    bool             flag_delete{false};
    bool             flag_force{false};
    bool             flag_orphaned{false};
    bool             flag_stale_logs{false};
    bool             flag_json{false};
    bool             flag_no_color{false};
    std::string      config_path;
    std::string      quarantine_dir;
    std::string      history_db_path;
    bool             flag_language_caches{false};
    bool             flag_container{false};
    bool             flag_flatpak{false};
    bool             flag_snap{false};
    bool             flag_tui{false};
    bool             flag_watch{false};
    int              watch_interval_sec{300};
    bool             flag_journal_clean{false};
    std::string      journal_vacuum_target;
    std::string      list_quarantine_dir;
    std::vector<std::string> restore_ids;
    bool             flag_restore_all{false};
    std::vector<std::string> extra_targets;
    std::vector<std::string> extra_blacklist;

    app.add_option("-d,--max-age", max_age_days,
                   "Maximum age in days before a file is flagged (default: 30)")
        ->check(CLI::Range(1, 3650));

    app.add_option("--log-retention", log_retention_days,
                   "Retention days for stale log detection (default: 90)")
        ->check(CLI::Range(1, 3650));

    app.add_flag("--delete", flag_delete,
                 "Delete flagged files (requires confirmation)");

    app.add_flag("--force", flag_force,
                 "Skip confirmation prompt");

    app.add_option("--quarantine", quarantine_dir,
                   "Move flagged files to quarantine directory instead of deleting");

    app.add_flag("--orphaned", flag_orphaned,
                 "Detect files not owned by any installed RPM package (scans /etc)");

    app.add_flag("--stale-logs", flag_stale_logs,
                 "Detect stale log files in /var/log");

    app.add_flag("--json", flag_json,
                 "Output results as JSON");

    app.add_flag("--no-color", flag_no_color,
                 "Disable colored output");

    app.add_option("-t,--target", extra_targets,
                   "Additional scan target (can be repeated)");

    app.add_option("-b,--blacklist", extra_blacklist,
                   "Additional path to never touch (can be repeated)");

    app.add_option("-c,--config", config_path,
                   "Path to TOML config file");

    app.add_option("--history-db", history_db_path,
                   "Path to SQLite history database");

    app.add_flag("--language-caches", flag_language_caches,
                 "Scan for language tool caches (Cargo, npm, pip, Go, Gradle, Maven)");

    app.add_flag("--container", flag_container,
                 "Scan Docker/Podman container disk usage");

    app.add_flag("--flatpak", flag_flatpak,
                 "Scan Flatpak runtimes and apps");

    app.add_flag("--snap", flag_snap,
                 "Scan Snap revisions");

    app.add_flag("--journal-clean", flag_journal_clean,
                 "Report systemd journal disk usage");

    app.add_option("--journal-vacuum", journal_vacuum_target,
                   "Vacuum journal to target size (e.g. 500M, 200M)—implies --journal-clean");

    app.add_option("--list-quarantine", list_quarantine_dir,
                   "List files in a quarantine directory");

    app.add_option("--restore", restore_ids,
                   "Restore a quarantined file by ID (can be repeated)");

    app.add_flag("--restore-all", flag_restore_all,
                 "Restore all quarantined files");

    app.add_flag("--tui", flag_tui,
                 "Launch interactive TUI");

    app.add_flag("--watch", flag_watch,
                 "Watch mode — periodically re-scan and notify on new items");

    app.add_option("--watch-interval", watch_interval_sec,
                   "Watch mode interval in seconds (default: 300)")
        ->check(CLI::Range(10, 86400));

    CLI11_PARSE(app, argc, argv);

    if (flag_no_color)
        color::enabled = false;

    auto C = [](const char* a) { return color::enabled ? a : ""; };

    if (flag_delete && !quarantine_dir.empty()) {
        std::cerr << C(color::red) << "error: --delete and --quarantine "
                  << "are mutually exclusive" << C(color::reset) << '\n';
        return 1;
    }

    // Default history DB path
    if (history_db_path.empty()) {
        const char* home = std::getenv("HOME");
        if (home)
            history_db_path = std::string(home) + "/.local/share/lfscan/history.db";
        else
            history_db_path = "/tmp/lfscan_history.db";
    }

    // Ensure parent directory exists
    fs::path db_parent = fs::path(history_db_path).parent_path();
    std::error_code ec;
    if (!db_parent.empty())
        fs::create_directories(db_parent, ec);

    HistoryDB history;
    bool have_db = history.open(history_db_path);
    if (!have_db) {
        std::cerr << C(color::yellow) << "warning: history DB unavailable at "
                  << history_db_path << " (scans will not be recorded)\n"
                  << C(color::reset);
    }

    // ── TUI mode ────────────────────────────────────────────────────
    if (flag_tui)
        return run_tui(history, history_db_path);

    // ── Quarantine restore mode ─────────────────────────────────────
    auto do_restore = [&](const std::string& dir) {
        QuarantineRestore restore(dir);
        if (!restore_ids.empty() || flag_restore_all) {
            std::vector<std::string> ids;
            if (flag_restore_all) {
                auto items = restore.list();
                std::cout << "Restoring " << items.size() << " file(s) ...\n";
                for (const auto& item : items)
                    ids.push_back(item.id);
            } else {
                ids = restore_ids;
            }
            auto res = restore.restore(ids);
            std::cout << "Restored " << res.restored << " file(s)";
            if (res.failed)
                std::cout << " (" << res.failed << " failed)";
            std::cout << ".\n";
            for (const auto& err : res.errors)
                std::cerr << "  error: " << err << '\n';
            return true;
        } else {
            auto items = restore.list();
            if (items.empty()) {
                std::cout << "No quarantined files.\n";
                return true;
            }
            std::cout << "Quarantined files (" << dir << "):\n";
            for (const auto& item : items) {
                std::cout << "  [" << item.id << "] "
                          << item.original_path << "  ("
                          << Reporter::human_size(item.size) << ", "
                          << item.timestamp << ")\n";
            }
            return true;
        }
    };

    if (!list_quarantine_dir.empty()) {
        return do_restore(list_quarantine_dir) ? 0 : 1;
    }

    // Load config file (if provided), CLI overrides config
    if (!config_path.empty()) {
        auto cfg = load_config(config_path);

        if (!app.count("--max-age"))
            max_age_days = cfg.max_age_days;
        if (!app.count("--log-retention"))
            log_retention_days = cfg.log_retention_days;
        if (!app.count("--orphaned"))
            flag_orphaned = cfg.flag_orphaned;
        if (!app.count("--stale-logs"))
            flag_stale_logs = cfg.flag_stale_logs;
        if (!app.count("-t"))
            extra_targets = cfg.extra_targets;
        if (!app.count("-b"))
            extra_blacklist = cfg.extra_blacklist;
    }

    const char* home = std::getenv("HOME");

    // ── Watch mode ─────────────────────────────────────────────────
    static volatile std::sig_atomic_t watch_stop = 0;
    auto sigh = [](int) { watch_stop = 1; };
    std::signal(SIGINT, sigh);
    std::signal(SIGTERM, sigh);

    auto build_scanner = [&]() -> std::unique_ptr<Scanner> {
        auto s = std::make_unique<Scanner>();
        for (const auto& b : extra_blacklist)
            s->add_blacklist(fs::path(b));

        auto add_path = [&](const char* env, const char* fallback) {
            const char* p = std::getenv(env);
            s->add_target(p ? fs::path(p) : fs::path(fallback));
        };

        add_path("TMPDIR", "/tmp");
        add_path("TMP", "/var/tmp");

        s->add_target(home ? fs::path(home) / ".cache" : fs::path("/tmp/.cache"));

        for (const auto& t : extra_targets)
            s->add_target(fs::path(t));

        s->add_detector(std::make_unique<AgeBasedDetector>(max_age_days));
        s->add_detector(std::make_unique<PatternDetector>(
            std::vector<std::string>{"*.tmp", "*.swp", "*.swo", "*~", "core", "core.*"}));
        s->add_detector(std::make_unique<BrokenSymlinkDetector>());

        auto orphaned_det = std::make_unique<OrphanedPackageDetector>();
        orphaned_det->set_enabled(flag_orphaned);
        s->add_detector(std::move(orphaned_det));

        auto stale_det = std::make_unique<StaleLogDetector>(log_retention_days);
        stale_det->set_enabled(flag_stale_logs);
        s->add_detector(std::move(stale_det));

        return s;
    };

    if (flag_watch) {
        std::cout << C(color::bold) << "Watch mode active (interval: "
                  << watch_interval_sec << "s). Press Ctrl+C to stop.\n"
                  << C(color::reset);

        std::set<std::string> previous;
        int iteration = 0;

        while (!watch_stop) {
            iteration++;
            auto scan_start = std::chrono::steady_clock::now();

            auto s = build_scanner();
            auto results = s->run();

            // Collect current paths
            std::set<std::string> current;
            std::uint64_t total_size = 0;
            for (const auto& r : results) {
                current.insert(r.path.string());
                total_size += r.size;
            }

            // Detect new items (present now, absent in previous)
            std::vector<std::string> new_paths;
            for (const auto& p : current) {
                if (previous.empty() || previous.find(p) == previous.end())
                    new_paths.push_back(p);
            }

            if (!new_paths.empty() && !previous.empty()) {
                // Calculate size of new items
                std::uint64_t new_size = 0;
                for (const auto& r : results) {
                    if (std::find(new_paths.begin(), new_paths.end(),
                                  r.path.string()) != new_paths.end())
                        new_size += r.size;
                }

                std::ostringstream msg;
                msg << new_paths.size() << " new flagged file(s) ("
                    << Reporter::human_size(new_size) << ")";
                std::cout << C(color::green) << "\n[" << iteration << "] "
                          << msg.str() << C(color::reset) << "\n";

                // Desktop notification
                std::string notify_cmd = "notify-send -a lfscan"
                    " 'lfscan watch' '" + msg.str() + "' 2>/dev/null";
                std::system(notify_cmd.c_str());
            } else if (results.empty()) {
                if (previous.empty())
                    std::cout << "[" << iteration << "] "
                              << "No flagged files.\n";
                else
                    std::cout << "[" << iteration << "] "
                              << "No flagged files (cleared).\n";
            } else {
                std::cout << "[" << iteration << "] "
                          << results.size() << " flagged file(s) ("
                          << Reporter::human_size(total_size) << ")\n";
            }

            previous = current;

            // Log to history DB
            if (have_db) {
                ScanRecord rec;
                rec.dry_run = true;
                rec.paths_scanned = "/tmp,/var/tmp,.cache";
                auto sid = history.log_scan(rec);
                if (sid >= 0) {
                    for (const auto& r : results) {
                        HistoryEntry e;
                        e.scan_id    = sid;
                        e.cleaner    = r.detector_name;
                        e.category   = detector_category(r.detector_name);
                        e.item_key   = r.path.string();
                        e.size_bytes = static_cast<std::int64_t>(r.size);
                        e.action     = "found";
                        history.log_result(e);
                    }
                }
            }

            // Sleep with Ctrl+C check
            for (int i = 0; i < watch_interval_sec && !watch_stop; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "\nWatch mode stopped.\n";
        return 0;
    }

    auto scanner = build_scanner();

    // ── History: log scan start ──────────────────────────────────
    auto scan_start = std::chrono::steady_clock::now();
    std::int64_t scan_id = -1;
    if (have_db) {
        ScanRecord rec;
        rec.dry_run = !(flag_delete || !quarantine_dir.empty());
        rec.paths_scanned = "/tmp,/var/tmp," +
            std::string(home ? home : "") + "/.cache";
        for (const auto& t : extra_targets) {
            rec.paths_scanned += "," + t;
        }
        scan_id = history.log_scan(rec);
    }

    // ── Run scan ─────────────────────────────────────────────────
    auto results = scanner->run();

    // ── History: log found items ─────────────────────────────────
    if (have_db && scan_id >= 0) {
        for (const auto& r : results) {
            HistoryEntry e;
            e.scan_id    = scan_id;
            e.cleaner    = r.detector_name;
            e.category   = detector_category(r.detector_name);
            e.item_key   = r.path.string();
            e.size_bytes = static_cast<std::int64_t>(r.size);
            e.action     = "found";
            history.log_result(e);
        }
    }

    Reporter reporter(!flag_no_color, flag_json);
    reporter.print_results(results);

    if (results.empty())
        return 0;

    // Separate action-safe files from blacklisted ones
    std::vector<ScanResult> actionable;
    std::size_t             blacklisted_count = 0;
    for (const auto& r : results) {
        if (!scanner->is_blacklisted(r.path))
            actionable.push_back(r);
        else
            ++blacklisted_count;
    }

    reporter.print_summary(results, blacklisted_count);

    // ── ICleaner-based cleaners ─────────────────────────────────
    std::vector<CleanupItem> lang_items;
    CleanerRegistry          clean_reg;
    ContainerCleaner*        container_cleaner_ptr = nullptr;

    if (flag_language_caches)
        clean_reg.add(std::make_unique<LanguageCacheCleaner>());
    if (flag_container) {
        auto cc = std::make_unique<ContainerCleaner>();
        container_cleaner_ptr = cc.get();
        clean_reg.add(std::move(cc));
    }
    if (flag_flatpak)
        clean_reg.add(std::make_unique<FlatpakCleaner>());
    if (flag_snap)
        clean_reg.add(std::make_unique<SnapCleaner>());

    // Journal cleaner
    bool do_journal = flag_journal_clean || !journal_vacuum_target.empty();
    if (do_journal) {
        auto jc = std::make_unique<JournalCleaner>();
        if (!journal_vacuum_target.empty())
            jc->set_vacuum_target(journal_vacuum_target);
        clean_reg.add(std::move(jc));
    }

    // Collect per-cleaner diagnostics for unavailable cleaners
    std::vector<std::string> unavail_reasons;
    if (flag_container && container_cleaner_ptr && !container_cleaner_ptr->available()) {
        std::string reason = container_cleaner_ptr->last_error();
        unavail_reasons.push_back("containers: " + reason);
    }
    if (flag_flatpak) {
        // FlatpakCleaner doesn't expose details yet — just a generic helper
    }
    if (flag_snap) {
    }
    if (do_journal) {
    }

    if (flag_language_caches || flag_container || flag_flatpak || flag_snap || do_journal) {
        lang_items = clean_reg.scan_all();
        if (!lang_items.empty() && !flag_json) {
            std::cout << "\n" << C(color::bold) << "ICleaner results:"
                      << C(color::reset) << "\n";
            std::uint64_t total = 0;
            for (const auto& ci : lang_items) {
                std::cout << "  " << ci.description << " — "
                          << Reporter::human_size(ci.size) << "\n";
                total += ci.size;
            }
            std::cout << C(color::bold) << "  Total: "
                      << Reporter::human_size(total) << C(color::reset) << "\n";
        } else if (!unavail_reasons.empty() && !flag_json) {
            std::cout << "\n" << C(color::yellow)
                      << "Requested cleaner(s) unavailable:"
                      << C(color::reset) << "\n";
            for (const auto& r : unavail_reasons)
                std::cout << "  " << C(color::dim) << r << C(color::reset) << "\n";
        } else if ((flag_container || flag_flatpak || flag_snap || do_journal) && !flag_json) {
            auto cleaners = clean_reg.available_cleaners();
            if (cleaners.empty()) {
                std::cout << "\n" << C(color::yellow)
                          << "Requested cleaner(s) unavailable — "
                          << "tools may not be installed or permissions missing."
                          << C(color::reset) << "\n";
            }
        } else if (lang_items.empty() && !flag_json) {
            std::cout << "\n" << C(color::dim) << "No ICleaner items found."
                      << C(color::reset) << "\n";
        }

        if (have_db && scan_id >= 0) {
            for (const auto& ci : lang_items) {
                HistoryEntry e;
                e.scan_id    = scan_id;
                e.cleaner    = ci.category == "system-journal" ? "journal" : "language-caches";
                e.category   = ci.category;
                e.item_key   = ci.id;
                e.size_bytes = static_cast<std::int64_t>(ci.size);
                e.action     = "found";
                history.log_result(e);
            }
        }

        // ── Journal vacuum ──────────────────────────────────────
        if (do_journal && !journal_vacuum_target.empty()) {
            // Find the journal cleaner and run clean
            for (const auto& ci : lang_items) {
                if (ci.category != "system-journal")
                    continue;
                // We need to invoke clean on the JournalCleaner.
                // Since CleanerRegistry doesn't track which cleaner produced
                // which item, look up by name.
                auto cleaners = clean_reg.available_cleaners();
                for (auto* cl : cleaners) {
                    if (cl->name() == "journal") {
                        auto cr = cl->clean({ci});
                        if (cr.cleaned > 0) {
                            std::cout << C(color::green)
                                      << "  journal vacuumed: freed "
                                      << Reporter::human_size(cr.bytes_freed)
                                      << C(color::reset) << "\n";
                            if (have_db && scan_id >= 0) {
                                HistoryEntry e;
                                e.scan_id    = scan_id;
                                e.cleaner    = "journal";
                                e.category   = "system-journal";
                                e.item_key   = "journal-vacuum";
                                e.size_bytes = static_cast<std::int64_t>(cr.bytes_freed);
                                e.action     = "cleaned";
                                history.log_result(e);
                            }
                        }
                        if (!cr.errors.empty()) {
                            for (const auto& err : cr.errors)
                                std::cerr << C(color::red) << "  " << err
                                          << C(color::reset) << "\n";
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

    // Merge language-cache items into actionable for delete/quarantine
    // (only if --language-caches was passed and user requested delete)
    // For now they are reported but not auto-included in deletion;
    // full clean integration will come in later steps.

    if (blacklisted_count && !flag_json) {
        std::cout << C(color::dim)
                  <<         "  (" << blacklisted_count
                  << " file(s) on protected paths — cannot be modified)\n"
                  << C(color::reset);
    }

    if (actionable.empty()) {
        if (!flag_json)
            std::cout << C(color::yellow)
                      << "\nAll flagged files are on protected paths "
                      << "and cannot be modified.\n" << C(color::reset);
        return 0;
    }

    // ── Action phase ─────────────────────────────────────────────
    if (flag_delete) {
        if (!flag_force
            && !confirm_action("Delete " + std::to_string(actionable.size())
                               + " file(s)?")) {
            std::cout << "Aborted.\n";
            return 0;
        }

        std::size_t ok = 0, fail = 0;
        for (const auto& r : actionable) {
            std::error_code ec2;
            if (fs::remove(r.path, ec2)) {
                ++ok;
                if (have_db && scan_id >= 0) {
                    HistoryEntry e;
                    e.scan_id    = scan_id;
                    e.cleaner    = r.detector_name;
                    e.category   = detector_category(r.detector_name);
                    e.item_key   = r.path.string();
                    e.size_bytes = static_cast<std::int64_t>(r.size);
                    e.action     = "cleaned";
                    history.log_result(e);
                }
            } else {
                std::cerr << C(color::red) << "error: cannot delete "
                          << r.path << " (" << ec2.message() << ")\n"
                          << C(color::reset);
                ++fail;
                if (have_db && scan_id >= 0) {
                    HistoryEntry e;
                    e.scan_id    = scan_id;
                    e.cleaner    = r.detector_name;
                    e.category   = detector_category(r.detector_name);
                    e.item_key   = r.path.string();
                    e.size_bytes = static_cast<std::int64_t>(r.size);
                    e.action     = "failed";
                    history.log_result(e);
                }
            }
        }
        std::cout << C(color::green) << "\nDeleted " << ok << " file(s)"
                  << C(color::reset);
        if (fail)
            std::cout << C(color::red) << " (" << fail << " failed)"
                      << C(color::reset);
        std::cout << ".\n";
    } else if (!quarantine_dir.empty()) {
        Quarantine q(quarantine_dir);
        if (!flag_force
            && !confirm_action("Quarantine "
                               + std::to_string(actionable.size())
                               + " file(s)?")) {
            std::cout << "Aborted.\n";
            return 0;
        }
        bool ok = q.quarantine_files(actionable);
        if (ok && have_db && scan_id >= 0) {
            for (const auto& r : actionable) {
                HistoryEntry e;
                e.scan_id    = scan_id;
                e.cleaner    = r.detector_name;
                e.category   = detector_category(r.detector_name);
                e.item_key   = r.path.string();
                e.size_bytes = static_cast<std::int64_t>(r.size);
                e.action     = "cleaned";
                history.log_result(e);
            }
        }
        if (!ok)
            return 1;
    } else if (!flag_json) {
        std::cout << C(color::yellow)
                  << "\n[DRY RUN] Pass --delete or --quarantine "
                  << "to take action.\n" << C(color::reset);
    }

    // ── History: update scan duration ────────────────────────────
    if (have_db && scan_id >= 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - scan_start).count();
        // Update the scan record's duration_ms (we could add an update method,
        // but for now the scan is already logged with duration_ms=0 — the precision
        // trade-off is acceptable for the initial integration)
        // TODO: add HistoryDB::update_scan_duration(scan_id, elapsed)
    }

    return 0;
}
