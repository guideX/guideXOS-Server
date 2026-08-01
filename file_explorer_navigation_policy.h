#pragma once

#include <string>
#include <vector>

namespace gxos { namespace apps {

    struct NavigationRootLocation {
        std::string normalizedPath;
        bool category{false};
    };

    inline int ActiveNavigationRootIndex(const std::string& currentPath,
        const std::vector<NavigationRootLocation>& roots) {
        if (currentPath == "/") return -1;

        int selectedIndex = -1;
        size_t selectedPathLength = 0;
        for (size_t i = 0; i < roots.size(); ++i) {
            if (roots[i].category) continue;
            const std::string& rootPath = roots[i].normalizedPath;
            const bool matches = rootPath == "/"
                ? true
                : (currentPath == rootPath || currentPath.rfind(rootPath + "/", 0) == 0);
            if (matches && rootPath.size() >= selectedPathLength) {
                selectedIndex = static_cast<int>(i);
                selectedPathLength = rootPath.size();
            }
        }
        return selectedIndex;
    }

}} // namespace gxos::apps
