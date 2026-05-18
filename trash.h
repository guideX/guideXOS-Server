#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

class Trash {
public:
    static uint64_t Launch();
    static int main(int argc, char** argv);

private:
    struct TrashEntry {
        std::string name;
        bool isDirectory{false};
        std::string originalPath;
        std::string currentPath;
        std::string originalFolder;
        std::string type;
        std::string iconKey;
        std::string deletedText;
        uint64_t size{0};
    };

    static std::vector<TrashEntry> listEntries();
    static bool purgeContents(std::string& error, size_t& deletedCount);
    static bool restoreEntry(const TrashEntry& entry, std::string& error, std::string& restoredPath);
    static bool deleteEntryPermanently(const TrashEntry& entry, std::string& error);
    static void render(uint64_t windowId, bool confirmEmpty, bool showProperties, int selectedIndex, const std::string& status);
};

} // namespace apps
} // namespace gxos
