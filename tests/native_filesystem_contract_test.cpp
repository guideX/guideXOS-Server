#include "native_app_runtime.h"
#include "platform.h"

#include <chrono>
#include <thread>

// The production hosted executable provides these small platform helpers from
// server.cpp.  The contract runner deliberately omits server.cpp because it
// owns the server's main entry point, so provide the same platform-only
// definitions here for the test executable.
namespace gxos {
PlatformInfo queryPlatform() {
    PlatformInfo info{};
    info.cpuCount = std::thread::hardware_concurrency();
    info.totalMemBytes = 512ull * 1024 * 1024;
    info.startTicks = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    return info;
}

uint64_t ticks() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

#include <iostream>
#include <string>

int main() {
    std::string failure;
    if (!gxos::apps::RunNativeFilesystemContractTest(&failure)) {
        std::cerr << "Native filesystem contract test FAIL: "
                  << (failure.empty() ? "unknown" : failure) << "\n";
        return 1;
    }
    std::cout << "Native filesystem contract test PASS\n";
    return 0;
}
