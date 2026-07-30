#pragma once

#include "cleanup_item.hpp"
#include <memory>
#include <string>
#include <vector>

class ICleaner {
public:
    virtual ~ICleaner() = default;

    virtual std::vector<CleanupItem> scan()                = 0;
    virtual CleanupResult            clean(
        const std::vector<CleanupItem>& items)             = 0;
    virtual std::string              name()     const      = 0;
    virtual bool                     available() const      = 0;
};

using CleanerPtr = std::unique_ptr<ICleaner>;
