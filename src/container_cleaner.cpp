#include "container_cleaner.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────

std::string ContainerCleaner::run_cmd(const std::string& cmd) {
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

std::uint64_t ContainerCleaner::parse_size(const std::string& s) {
    std::string cleaned;
    for (char c : s) {
        if (c != '(' && c != ')' && c != '%')
            cleaned += c;
    }
    std::regex re(R"(([\d.]+)\s*(B|kB|KB|MB|GB|TB))");
    std::smatch m;
    if (!std::regex_search(cleaned, m, re))
        return 0;

    double val = std::stod(m[1].str());
    std::string unit = m[2].str();

    if (unit == "kB" || unit == "KB")   val *= 1024ULL;
    else if (unit == "MB")              val *= 1024ULL * 1024;
    else if (unit == "GB")              val *= 1024ULL * 1024 * 1024;
    else if (unit == "TB")              val *= 1024ULL * 1024 * 1024 * 1024;

    return static_cast<std::uint64_t>(val);
}

// ── environment detection ───────────────────────────────────────────

bool ContainerCleaner::in_container() const {
    return access("/.dockerenv", F_OK) == 0;
}

bool ContainerCleaner::podman_socket_exists() const {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        std::string sock = std::string(xdg) + "/podman/podman.sock";
        if (access(sock.c_str(), F_OK) == 0)
            return true;
    }

    const char* home = std::getenv("HOME");
    if (home) {
        std::string sock = std::string(home)
            + "/.local/share/containers/podman/machine/podman.sock";
        if (access(sock.c_str(), F_OK) == 0)
            return true;
    }

    if (access("/run/podman/podman.sock", F_OK) == 0)
        return true;

    return false;
}

// ── accessibility checks (test actual daemon connectivity) ─────────

bool ContainerCleaner::docker_accessible() const {
    last_error_.clear();

    auto ver = run_cmd("docker --version 2>/dev/null");
    if (ver.empty()) {
        last_error_ = "docker CLI not installed";
        return false;
    }

    const char* host = std::getenv("DOCKER_HOST");
    if (host && host[0] != '\0')
        return true;

    auto info = run_cmd("timeout 5 docker info --format '{{.OSType}}' 2>&1");
    if (info.empty()) {
        bool sock_exists = (access("/var/run/docker.sock", F_OK) == 0)
                        || (access("/run/docker.sock", F_OK) == 0);
        if (sock_exists)
            last_error_ = "Docker daemon not responding (socket exists but daemon may be down)";
        else
            last_error_ = "Docker daemon not running and no socket found";
        return false;
    }

    if (info.find("permission denied") != std::string::npos
        || info.find("access denied") != std::string::npos) {
        if (in_container())
            last_error_ = "Docker socket mounted but inaccessible inside container — check socket permissions (try --group-add docker or run as root)";
        else
            last_error_ = "User not in 'docker' group — run: sudo usermod -aG docker $USER && newgrp docker";
        return false;
    }

    if (info.find("Cannot connect") != std::string::npos
        || info.find("Is the docker daemon running") != std::string::npos) {
        last_error_ = "Docker daemon is not running";
        return false;
    }

    if (info.find("Error") != std::string::npos) {
        last_error_ = "Docker error: " + info;
        return false;
    }

    return true;
}

bool ContainerCleaner::podman_accessible() const {
    last_error_.clear();

    auto ver = run_cmd("podman --version 2>/dev/null");
    if (ver.empty()) {
        last_error_ = "podman CLI not installed";
        return false;
    }

    auto info = run_cmd("timeout 5 podman info --format '{{.Host.OS}}' 2>&1");
    if (info.empty()) {
        last_error_ = "Podman daemon not accessible";
        return false;
    }

    if (info.find("permission denied") != std::string::npos) {
        last_error_ = "Podman requires root or appropriate group membership";
        return false;
    }

    if (info.find("Error") != std::string::npos) {
        last_error_ = "Podman error: " + info;
        return false;
    }

    return true;
}

// ── runtime detection ────────────────────────────────────────────────

ContainerCleaner::Runtime ContainerCleaner::detect_runtime() const {
    std::string docker_err, podman_err;

    auto dv = run_cmd("docker --version 2>/dev/null");
    if (!dv.empty()) {
        if (docker_accessible())
            return Runtime::Docker;
        docker_err = last_error_;
    }

    auto pv = run_cmd("podman --version 2>/dev/null");
    if (!pv.empty()) {
        if (podman_accessible())
            return Runtime::Podman;
        podman_err = last_error_;
    }

    if (!docker_err.empty() && !podman_err.empty())
        last_error_ = docker_err + "; " + podman_err;
    else if (!docker_err.empty())
        last_error_ = docker_err;
    else if (!podman_err.empty())
        last_error_ = podman_err;
    else
        last_error_ = "Neither Docker nor Podman CLI found";

    return Runtime::None;
}

// ── scan ─────────────────────────────────────────────────────────────

std::vector<CleanupItem> ContainerCleaner::scan_docker() const {
    std::vector<CleanupItem> items;

    auto raw = run_cmd("docker system df 2>/dev/null");
    if (raw.empty())
        return items;

    std::istringstream ss(raw);
    std::string line;

    if (!std::getline(ss, line))
        return items;

    while (std::getline(ss, line)) {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string type, total, active, size_str, reclaim_str;
        if (!(ls >> type >> total >> active >> size_str))
            continue;
        std::getline(ls, reclaim_str);
        if (!reclaim_str.empty() && reclaim_str[0] == ' ')
            reclaim_str = reclaim_str.substr(1);

        auto reclaimable = parse_size(reclaim_str);
        if (reclaimable == 0)
            continue;

        std::string id = "docker-" + type;
        for (auto& c : id)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        CleanupItem ci;
        ci.id          = id;
        ci.description = "Docker " + type + " (" + total + " items, "
                         + reclaim_str + " reclaimable)";
        ci.size        = reclaimable;
        ci.category    = "containers";
        ci.risk        = Risk::Caution;
        ci.reversible  = false;
        ci.metadata["runtime"] = "docker";
        ci.metadata["type"]    = type;
        items.push_back(std::move(ci));
    }

    return items;
}

std::vector<CleanupItem> ContainerCleaner::scan_podman() const {
    std::vector<CleanupItem> items;

    auto raw = run_cmd("podman system df 2>/dev/null");
    if (raw.empty())
        return items;

    std::istringstream ss(raw);
    std::string line;

    if (!std::getline(ss, line))
        return items;

    while (std::getline(ss, line)) {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string type, total, active, size_str, reclaim_str;
        if (!(ls >> type >> total >> active >> size_str))
            continue;
        std::getline(ls, reclaim_str);
        if (!reclaim_str.empty() && reclaim_str[0] == ' ')
            reclaim_str = reclaim_str.substr(1);

        auto reclaimable = parse_size(reclaim_str);
        if (reclaimable == 0)
            continue;

        std::string id = "podman-" + type;
        for (auto& c : id)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        CleanupItem ci;
        ci.id          = id;
        ci.description = "Podman " + type + " (" + total + " items, "
                         + reclaim_str + " reclaimable)";
        ci.size        = reclaimable;
        ci.category    = "containers";
        ci.risk        = Risk::Caution;
        ci.reversible  = false;
        ci.metadata["runtime"] = "podman";
        ci.metadata["type"]    = type;
        items.push_back(std::move(ci));
    }

    return items;
}

std::vector<CleanupItem> ContainerCleaner::scan() {
    auto rt = detect_runtime();
    if (rt == Runtime::Docker)
        return scan_docker();
    if (rt == Runtime::Podman)
        return scan_podman();
    return {};
}

// ── clean ────────────────────────────────────────────────────────────

CleanupResult ContainerCleaner::clean_docker(
    const std::vector<CleanupItem>& items) const {

    CleanupResult result;
    bool has_items = false;
    for (const auto& item : items) {
        if (item.category == "containers") {
            auto rt = item.metadata.find("runtime");
            if (rt != item.metadata.end() && rt->second == "docker") {
                has_items = true;
                break;
            }
        }
    }

    if (!has_items) {
        result.errors.push_back("no docker items to clean");
        result.failed++;
        return result;
    }

    std::cout << "  Running docker system prune -a --volumes -f ...\n";
    auto out = run_cmd("docker system prune -a --volumes -f 2>&1");

    if (out.find("error") != std::string::npos
        || out.find("Error") != std::string::npos
        || out.find("permission denied") != std::string::npos) {
        result.errors.push_back("docker prune failed: " + out);
        result.failed++;
        return result;
    }

    auto freed = parse_size(out);
    result.cleaned++;
    result.bytes_freed += freed;

    return result;
}

CleanupResult ContainerCleaner::clean_podman(
    const std::vector<CleanupItem>& items) const {

    CleanupResult result;
    bool has_items = false;
    for (const auto& item : items) {
        if (item.category == "containers") {
            auto rt = item.metadata.find("runtime");
            if (rt != item.metadata.end() && rt->second == "podman") {
                has_items = true;
                break;
            }
        }
    }

    if (!has_items) {
        result.errors.push_back("no podman items to clean");
        result.failed++;
        return result;
    }

    std::cout << "  Running podman system prune --all --volumes -f ...\n";
    auto out = run_cmd("podman system prune --all --volumes -f 2>&1");

    if (out.find("error") != std::string::npos
        || out.find("Error") != std::string::npos
        || out.find("permission denied") != std::string::npos) {
        result.errors.push_back("podman prune failed: " + out);
        result.failed++;
        return result;
    }

    auto freed = parse_size(out);
    result.cleaned++;
    result.bytes_freed += freed;

    return result;
}

CleanupResult ContainerCleaner::clean(const std::vector<CleanupItem>& items) {
    for (const auto& item : items) {
        if (item.category != "containers")
            continue;
        auto rt_it = item.metadata.find("runtime");
        if (rt_it == item.metadata.end())
            continue;
        if (rt_it->second == "docker")
            return clean_docker(items);
        if (rt_it->second == "podman")
            return clean_podman(items);
    }

    auto rt = detect_runtime();
    if (rt == Runtime::Docker)
        return clean_docker(items);
    if (rt == Runtime::Podman)
        return clean_podman(items);

    CleanupResult r;
    r.errors.push_back(last_error_.empty()
        ? "no container runtime available"
        : last_error_);
    r.failed++;
    return r;
}

// ── available ────────────────────────────────────────────────────────

bool ContainerCleaner::available() const {
    return detect_runtime() != Runtime::None;
}
