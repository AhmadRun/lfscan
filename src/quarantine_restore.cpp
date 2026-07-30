#include "quarantine_restore.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static std::string timestamp() {
    auto now  = std::time(nullptr);
    auto* ltm = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(ltm, "%Y%m%d_%H%M%S");
    return oss.str();
}

QuarantineRestore::QuarantineRestore(fs::path quarantine_dir)
    : quarantine_dir_(std::move(quarantine_dir))
{}

std::vector<QuarantinedItem> QuarantineRestore::list() {
    std::vector<QuarantinedItem> items;
    fs::path manifest = quarantine_dir_ / "manifest.txt";

    std::error_code ec;
    if (!fs::exists(manifest, ec)) {
        std::cerr << "error: manifest not found at " << manifest << '\n';
        return items;
    }

    std::ifstream mf(manifest);
    if (!mf) {
        std::cerr << "error: cannot read manifest\n";
        return items;
    }

    std::string line;
    int id_counter = 1;
    while (std::getline(mf, line)) {
        if (line.empty())
            continue;

        // Format: original_path \t quarantined_path \t timestamp
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos)
            continue;
        auto tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos)
            continue;

        QuarantinedItem qi;
        qi.id              = std::to_string(id_counter++);
        qi.original_path   = line.substr(0, tab1);
        qi.quarantined_path = line.substr(tab1 + 1, tab2 - tab1 - 1);
        qi.timestamp       = line.substr(tab2 + 1);

        std::error_code ec2;
        qi.size = fs::exists(qi.quarantined_path, ec2)
                      ? fs::file_size(qi.quarantined_path, ec2)
                      : 0;

        items.push_back(std::move(qi));
    }

    return items;
}

RestoreResult QuarantineRestore::restore(
    const std::vector<std::string>& ids) {

    RestoreResult result;
    auto all_items = list();

    for (const auto& id : ids) {
        // Find the item by ID
        const QuarantinedItem* found = nullptr;
        for (const auto& item : all_items) {
            if (item.id == id) {
                found = &item;
                break;
            }
        }

        if (!found) {
            result.errors.push_back("item '" + id + "' not found in quarantine");
            result.failed++;
            continue;
        }

        std::error_code ec;
        if (!fs::exists(found->quarantined_path, ec)) {
            result.errors.push_back("file no longer in quarantine: "
                                    + found->quarantined_path);
            result.failed++;
            continue;
        }

        // If original path exists, don't overwrite — append a suffix
        fs::path restore_dest(found->original_path);
        if (fs::exists(restore_dest, ec)) {
            auto stem    = restore_dest.stem();
            auto ext     = restore_dest.extension();
            auto parent  = restore_dest.parent_path();
            auto new_name = stem.string() + ".restored." + timestamp() + ext.string();
            restore_dest = parent / new_name;
            std::cout << "  warning: " << found->original_path
                      << " exists — restoring as " << restore_dest.string() << '\n';
        }

        // Create parent directory if needed
        fs::create_directories(restore_dest.parent_path(), ec);

        fs::rename(found->quarantined_path, restore_dest, ec);
        if (ec) {
            result.errors.push_back("failed to restore " + found->id + ": "
                                    + ec.message());
            result.failed++;
            continue;
        }

        // Remove the manifest entry (rewrite manifest without this line)
        std::vector<std::string> keep;
        std::ifstream in(quarantine_dir_ / "manifest.txt");
        std::string line;
        while (std::getline(in, line)) {
            // Keep lines that don't match this entry's quarantined path
            if (line.find(found->quarantined_path) == std::string::npos)
                keep.push_back(line);
        }
        in.close();
        std::ofstream out(quarantine_dir_ / "manifest.txt");
        for (const auto& l : keep)
            out << l << '\n';

        std::cout << "  restored: " << found->original_path << '\n';
        result.restored++;
    }

    return result;
}

std::string QuarantineRestore::name() const {
    return "quarantine-restore";
}
