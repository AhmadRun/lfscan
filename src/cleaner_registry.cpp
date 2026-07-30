#include "cleaner_registry.hpp"

void CleanerRegistry::add(CleanerPtr cleaner) {
    cleaners_.push_back(std::move(cleaner));
}

std::vector<ICleaner*> CleanerRegistry::available_cleaners() const {
    std::vector<ICleaner*> out;
    for (const auto& c : cleaners_) {
        if (c->available())
            out.push_back(c.get());
    }
    return out;
}

std::vector<CleanupItem> CleanerRegistry::scan_all() {
    std::vector<CleanupItem> all;
    for (auto& c : cleaners_) {
        if (!c->available())
            continue;
        auto items = c->scan();
        all.insert(all.end(), std::make_move_iterator(items.begin()),
                   std::make_move_iterator(items.end()));
    }
    return all;
}

CleanupResult CleanerRegistry::clean_items(
    const std::vector<CleanupItem>& items, ICleaner* cleaner) {
    return cleaner->clean(items);
}
