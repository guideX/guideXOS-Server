#pragma once

#include "app_launch_resolver.h"
#include "native_elf_image_loader.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

constexpr uint32_t kGuideXOSNativeApiVersion = 0u;
constexpr const char* kGuideXOSNativeAbiName = "guidexos-c-abi-v1";

typedef int32_t gx_result;

enum : gx_result {
    GX_OK = 0,
    GX_ERROR_NOT_IMPLEMENTED = -1,
    GX_ERROR_INVALID_ARGUMENT = -2,
    GX_ERROR_UNSUPPORTED = -3,
    GX_ERROR_FAILED = -4
};

struct NativeHostCallTable {
    uint32_t size = 0;
    uint32_t version = kGuideXOSNativeApiVersion;
    gx_result (*log)(const char* message) = nullptr;
    uint32_t (*get_api_version)() = nullptr;
    gx_result (*request_window)() = nullptr;
    gx_result (*exit)(gx_result exitCode) = nullptr;
};

enum class NativeAppLifecycleState {
    Created = 0,
    Prepared,
    Running,
    Suspended,
    Closing,
    Exited,
    Failed
};

struct NativeAppRuntimeContext {
    bool success = false;
    std::string appId;
    std::string displayName;
    std::string architecture;
    uint64_t processId = 0;
    std::string appDirectory;
    std::vector<std::string> permissions;
    NativeHostCallTable hostCalls;
    std::map<std::string, std::string> environment;
    std::vector<std::string> arguments;
    NativeAppLifecycleState lifecycleState = NativeAppLifecycleState::Created;
    std::vector<std::string> diagnostics;
};

struct NativeGxAppContext {
    uint32_t size = 0;
    uint32_t apiVersion = kGuideXOSNativeApiVersion;
    const NativeHostCallTable* host = nullptr;
    void* userData = nullptr;
};

class NativeAppRuntime {
public:
    static NativeAppRuntimeContext Prepare(
        const RegisteredApp& app,
        const LaunchDecision& launchDecision,
        const NativeElfLaunchResult& launchResult,
        const NativeElfImage& image);

    static const char* ToString(NativeAppLifecycleState state);

private:
    static void LogContext(const NativeAppRuntimeContext& context, const std::string& abi);
};

} // namespace apps
} // namespace gxos
