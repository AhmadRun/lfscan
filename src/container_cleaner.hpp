#pragma once

#include "cleanup_item.hpp"
#include "icleaner.hpp"
#include <string>
#include <vector>

class ContainerCleaner : public ICleaner {
public:
    std::vector<CleanupItem> scan()                                        override;
    CleanupResult            clean(const std::vector<CleanupItem>& items)  override;
    std::string              name()                                  const override { return "containers"; }
    bool                     available()                             const override;

    const std::string&       last_error() const { return last_error_; }

private:
    enum class Runtime { None, Docker, Podman };

    Runtime detect_runtime() const;
    bool    docker_accessible() const;
    bool    podman_accessible() const;
    bool    podman_socket_exists() const;
    bool    in_container() const;

    std::vector<CleanupItem> scan_docker() const;
    std::vector<CleanupItem> scan_podman() const;
    CleanupResult            clean_docker(const std::vector<CleanupItem>& items) const;
    CleanupResult            clean_podman(const std::vector<CleanupItem>& items) const;

    static std::uint64_t parse_size(const std::string& s);
    static std::string   run_cmd(const std::string& cmd);

    mutable std::string last_error_;
};
