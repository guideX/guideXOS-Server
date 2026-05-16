#pragma once

#include "native_app_runtime.h"
#include "native_elf_image_loader.h"
#include "native_elf_launch_pipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeElfExecutionResult {
    bool success = false;
    std::string appId;
    std::string architecture;
    int32_t exitCode = 0;
    std::string message;
    std::vector<std::string> diagnostics;
};

class NativeElfExecutor {
public:
    static bool CanExecute(
        const NativeElfLaunchResult& launchResult,
        const NativeElfImage& image,
        const NativeAppRuntimeContext& runtimeContext,
        std::string* reason = nullptr);

    static NativeElfExecutionResult Execute(
        const NativeElfLaunchResult& launchResult,
        const NativeElfImage& image,
        const NativeAppRuntimeContext& runtimeContext);

private:
    static void LogDecision(const std::string& appId, const std::string& architecture, bool canExecute, const std::string& reason, const std::string& result);
};

} // namespace apps
} // namespace gxos
