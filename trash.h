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
    };

    static std::vector<TrashEntry> listEntries();
};

} // namespace apps
} // namespace gxos
