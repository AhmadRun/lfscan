#pragma once

#include "cleanup_item.hpp"
#include <string>
#include <vector>

class IRestorable {
public:
    virtual ~IRestorable() = default;

    virtual std::vector<QuarantinedItem> list()                    = 0;
    virtual RestoreResult                restore(
        const std::vector<std::string>& ids)                       = 0;
    virtual std::string                  name()              const = 0;
};
