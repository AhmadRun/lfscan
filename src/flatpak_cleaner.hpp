#pragma once

#include "cleanup_item.hpp"
#include "icleaner.hpp"
#include <string>
#include <vector>

class FlatpakCleaner : public ICleaner {
public:
    std::vector<CleanupItem> scan()                                        override;
    CleanupResult            clean(const std::vector<CleanupItem>& items)  override;
    std::string              name()                                  const override { return "flatpak"; }
    bool                     available()                             const override;

private:
    struct FlatpakEntry {
        std::string id;
        std::string description;
        std::string branch;
        std::uint64_t size;
        bool is_runtime;
    };

    std::vector<FlatpakEntry> list_installed() const;

    static std::string   run_cmd(const std::string& cmd);
    static std::uint64_t parse_flatpak_size(const std::string& s);
};
