#include "language_cache_cleaner.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <system_error>

// ── helpers ──────────────────────────────────────────────────────────

static fs::path home_dir() {
    const char* h = std::getenv("HOME");
    return h ? fs::path(h) : fs::path("/tmp");
}

static fs::path env_or(const char* var, const fs::path& fallback) {
    const char* v = std::getenv(var);
    return v ? fs::path(v) : fallback;
}

static std::uint64_t dir_size(const fs::path& p) {
    std::uint64_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(p, ec)) {
        if (fs::is_regular_file(e, ec))
            total += fs::file_size(e, ec);
    }
    return total;
}

bool LanguageCacheCleaner::tool_installed(const std::string& name) {
    std::string cmd = "which " + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

std::uint64_t LanguageCacheCleaner::remove_contents(const fs::path& dir) {
    std::uint64_t freed = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        auto sz = fs::is_regular_file(e, ec) ? fs::file_size(e, ec) : 0;
        fs::remove_all(e, ec);
        freed += sz;
    }
    return freed;
}

// ── constructor ──────────────────────────────────────────────────────

LanguageCacheCleaner::LanguageCacheCleaner() {
    // Cargo registry cache  (~/.cargo/registry/cache/)
    defs_.push_back({
        "cargo-registry",
        "Cargo registry cache (downloaded crate sources)",
        [] { return env_or("CARGO_HOME", home_dir() / ".cargo") / "registry" / "cache"; },
        "language-cache", Risk::Safe, true, "cargo",
    });

    // Cargo git checkout cache
    defs_.push_back({
        "cargo-git",
        "Cargo git checkout cache",
        [] { return env_or("CARGO_HOME", home_dir() / ".cargo") / "git" / "db"; },
        "language-cache", Risk::Safe, true, "cargo",
    });

    // npm cache  (~/.npm/_cacache/)
    defs_.push_back({
        "npm-cache",
        "npm cache (downloaded packages)",
        [] { return env_or("npm_config_cache", home_dir() / ".npm") / "_cacache"; },
        "language-cache", Risk::Safe, true, "npm",
    });

    // pip cache  (~/.cache/pip/)
    defs_.push_back({
        "pip-cache",
        "pip cache (downloaded wheels)",
        [] {
            const char* xdg = std::getenv("XDG_CACHE_HOME");
            return (xdg ? fs::path(xdg) : home_dir() / ".cache") / "pip";
        },
        "language-cache", Risk::Safe, true, "pip3",
    });

    // Go module cache  (~/go/pkg/mod/)
    defs_.push_back({
        "go-module",
        "Go module cache (downloaded modules)",
        [] {
            auto gopath = env_or("GOPATH", home_dir() / "go");
            return gopath / "pkg" / "mod";
        },
        "language-cache", Risk::Safe, true, "go",
    });

    // Gradle cache  (~/.gradle/caches/)
    defs_.push_back({
        "gradle-cache",
        "Gradle build cache",
        [] { return home_dir() / ".gradle" / "caches"; },
        "language-cache", Risk::Safe, true, "gradle",
    });

    // Maven repository  (~/.m2/repository/)
    defs_.push_back({
        "maven-repository",
        "Maven local repository (downloaded artifacts)",
        [] {
            const char* m2 = std::getenv("M2_HOME");
            return (m2 ? fs::path(m2) : home_dir() / ".m2") / "repository";
        },
        "language-cache", Risk::Safe, true, "mvn",
    });
}

// ── available ────────────────────────────────────────────────────────

bool LanguageCacheCleaner::available() const {
    for (const auto& d : defs_) {
        if (tool_installed(d.tool_binary))
            return true;
    }
    return false;
}

// ── scan ─────────────────────────────────────────────────────────────

std::vector<CleanupItem> LanguageCacheCleaner::scan() {
    std::vector<CleanupItem> items;

    for (const auto& d : defs_) {
        if (!tool_installed(d.tool_binary))
            continue;

        auto p = d.path_fn();
        std::error_code ec;
        if (!fs::exists(p, ec))
            continue;

        auto sz = dir_size(p);
        if (sz == 0)
            continue;

        CleanupItem ci;
        ci.id          = d.id;
        ci.description = d.description;
        ci.size        = sz;
        ci.category    = d.category;
        ci.reversible  = d.reversible;
        ci.risk        = d.risk;
        ci.metadata["cache_path"] = p.string();
        ci.metadata["tool"]       = d.tool_binary;
        items.push_back(std::move(ci));
    }

    return items;
}

// ── clean ────────────────────────────────────────────────────────────

CleanupResult LanguageCacheCleaner::clean(
    const std::vector<CleanupItem>& items) {

    CleanupResult result;

    for (const auto& item : items) {
        if (item.category != "language-cache")
            continue;

        auto it = item.metadata.find("cache_path");
        if (it == item.metadata.end()) {
            result.errors.push_back(item.id + ": missing cache_path metadata");
            result.failed++;
            continue;
        }

        fs::path cache_dir(it->second);
        std::error_code ec;
        if (!fs::exists(cache_dir, ec)) {
            result.errors.push_back(item.id + ": path not found");
            result.failed++;
            continue;
        }

        auto before = dir_size(cache_dir);
        auto freed  = remove_contents(cache_dir);
        if (freed == 0) {
            // Possibly empty dir — fall back to removing all
            auto total = before;
            fs::remove_all(cache_dir, ec);
            if (!ec) {
                result.cleaned++;
                result.bytes_freed += total;
            } else {
                result.errors.push_back(item.id + ": " + ec.message());
                result.failed++;
            }
        } else {
            result.cleaned++;
            result.bytes_freed += freed;
        }
    }

    return result;
}
