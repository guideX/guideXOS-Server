#include "file_explorer_navigation_policy.h"

#include <iostream>
#include <vector>

namespace {

bool expect(bool value, const char* label) {
    if (!value) std::cerr << "FAIL: " << label << "\n";
    return value;
}

}

int main() {
    using gxos::apps::NavigationRootLocation;
    const std::vector<NavigationRootLocation> roots{
        {"/", false},
        {"/mnt", false},
        {"/mnt/data", false},
        {"/", true}
    };
    bool ok = true;
    ok &= expect(gxos::apps::ActiveNavigationRootIndex("/", roots) == -1,
        "root shortcut is the only selected location at root");
    ok &= expect(gxos::apps::ActiveNavigationRootIndex("/mnt/photos", roots) == 1,
        "most-specific mounted root is selected for a descendant path");
    ok &= expect(gxos::apps::ActiveNavigationRootIndex("/mnt/data", roots) == 2,
        "exact location selects its matching root");
    ok &= expect(gxos::apps::ActiveNavigationRootIndex("/other", roots) == 0,
        "unmatched descendant remains under the root location");
    ok &= expect(gxos::apps::ActiveNavigationRootIndex("/mnt2", roots) == 0,
        "path-prefix lookalikes do not select the wrong root");
    ok &= expect(gxos::apps::ActiveNavigationRootIndex("/", roots) != 3,
        "duplicate category location is never selected");
    std::cout << (ok ? "File Explorer navigation regression tests PASS\n" :
        "File Explorer navigation regression tests FAIL\n");
    return ok ? 0 : 1;
}
