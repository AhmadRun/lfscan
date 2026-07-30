#pragma once

#include "cleanup_item.hpp"
#include "icleaner.hpp"
#include <string>

class JournalCleaner : public ICleaner {
public:
    JournalCleaner() = default;

    std::vector<CleanupItem> scan()                                        override;
    CleanupResult            clean(const std::vector<CleanupItem>& items)  override;
    std::string              name()                                  const override { return "journal"; }
    bool                     available()                             const override;

    // Target size for vacuum (e.g. "500M"). Empty = scan-only.
    void set_vacuum_target(const std::string& target);
    bool has_vacuum_target() const { return !vacuum_target_.empty(); }

private:
    std::string vacuum_target_;

    static bool can_vacuum();
    static std::uint64_t parse_size(const std::string& s);
    static std::string   run_cmd(const std::string& cmd);
};
