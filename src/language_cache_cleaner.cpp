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
    return (v && *v) ? fs::path(v) : fallback;
}

// Size of a file or a directory tree. Uses increment(ec) instead of a
// range-for, which throws on unreadable subdirectories.
static std::uint64_t dir_size(const fs::path& p) {
    std::error_code ec;
    auto st = fs::symlink_status(p, ec);
    if (ec)
        return 0;
    if (fs::is_regular_file(st)) {
        auto sz = fs::file_size(p, ec);
        return ec ? 0 : sz;
    }
    if (!fs::is_directory(st))
        return 0;

    std::uint64_t total = 0;
    fs::recursive_directory_iterator it(
        p, fs::directory_options::skip_permission_denied, ec);
    for (fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_regular_file(ec2) && !it->is_symlink(ec2)) {
            auto sz = it->file_size(ec2);
            if (!ec2)
                total += sz;
        }
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
    fs::directory_iterator it(dir, ec);
    for (fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
        // Count whole subtrees, not just top-level files
        auto sz = dir_size(it->path());
        std::error_code ec2;
        fs::remove_all(it->path(), ec2);
        if (!ec2)
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
            // GOMODCACHE wins; otherwise the first GOPATH entry
            if (const char* mc = std::getenv("GOMODCACHE"); mc && *mc)
                return fs::path(mc);
            std::string gopath = env_or("GOPATH", home_dir() / "go").string();
            gopath = gopath.substr(0, gopath.find(':'));
            return fs::path(gopath) / "pkg" / "mod";
        },
        "language-cache", Risk::Safe, true, "go",
    });

    // Gradle cache  (~/.gradle/caches/)
    defs_.push_back({
        "gradle-cache",
        "Gradle build cache",
        [] { return env_or("GRADLE_USER_HOME", home_dir() / ".gradle") / "caches"; },
        "language-cache", Risk::Safe, true, "gradle",
    });

    // Maven repository  (~/.m2/repository/)
    defs_.push_back({
        "maven-repository",
        "Maven local repository (downloaded artifacts)",
        // M2_HOME is the Maven install dir, not the local repository
        [] { return home_dir() / ".m2" / "repository"; },
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
