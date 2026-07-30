#pragma once

#include "cleanup_item.hpp"
#include "icleaner.hpp"
#include <memory>
#include <vector>

class CleanerRegistry {
    std::vector<CleanerPtr> cleaners_;

public:
    void add(CleanerPtr cleaner);

    std::vector<ICleaner*> available_cleaners() const;

    std::vector<CleanupItem> scan_all();

    CleanupResult clean_items(const std::vector<CleanupItem>& items,
                              ICleaner* cleaner);

    std::size_t count() const { return cleaners_.size(); }
};
