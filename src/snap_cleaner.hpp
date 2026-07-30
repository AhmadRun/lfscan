#pragma once

#include "cleanup_item.hpp"
#include "icleaner.hpp"
#include <string>
#include <vector>

class SnapCleaner : public ICleaner {
public:
    std::vector<CleanupItem> scan()                                        override;
    CleanupResult            clean(const std::vector<CleanupItem>& items)  override;
    std::string              name()                                  const override { return "snap"; }
    bool                     available()                             const override;

private:
    struct SnapRev {
        std::string snap;
        std::string revision;
        bool        disabled;
        std::string notes;
    };

    std::vector<SnapRev> list_all() const;

    static std::string   run_cmd(const std::string& cmd);
    static std::uint64_t snap_file_size(const std::string& snap,
                                        const std::string& rev);
};
