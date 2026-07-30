#pragma once

#include "cleanup_item.hpp"
#include "irestorable.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class QuarantineRestore : public IRestorable {
    fs::path quarantine_dir_;

public:
    explicit QuarantineRestore(fs::path quarantine_dir);

    std::vector<QuarantinedItem> list()                        override;
    RestoreResult                restore(
        const std::vector<std::string>& ids)                   override;
    std::string                  name()                  const override;
};
