#pragma once

#include "cleanup_item.hpp"
#include "icleaner.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class LanguageCacheCleaner : public ICleaner {
public:
    LanguageCacheCleaner();

    std::vector<CleanupItem> scan()                                        override;
    CleanupResult            clean(const std::vector<CleanupItem>& items)  override;
    std::string              name()                                  const override { return "language-caches"; }
    bool                     available()                             const override;

private:
    struct CacheDef {
        std::string               id;
        std::string               description;
        std::function<fs::path()> path_fn;    // lazy — respects env vars
        std::string               category;
        Risk                      risk{Risk::Safe};
        bool                      reversible{true};
        std::string               tool_binary; // e.g. "cargo", "npm"
    };

    std::vector<CacheDef> defs_;

    static bool tool_installed(const std::string& name);

    // Remove contents (not the directory itself) so parent structure stays
    static std::uint64_t remove_contents(const fs::path& dir);
};
