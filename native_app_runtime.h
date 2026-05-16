#pragma once

#include "app_launch_resolver.h"
#include "native_elf_image_loader.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct NativeGxAppContext;

constexpr uint32_t kGuideXOSNativeApiVersion = 0u;
constexpr const char* kGuideXOSNativeAbiName = "guidexos-c-abi-v1";

typedef int32_t gx_result;

enum : gx_result {
    GX_OK = 0,
    GX_ERROR_NOT_IMPLEMENTED = -1,
    GX_ERROR_INVALID_ARGUMENT = -2,
    GX_ERROR_UNSUPPORTED = -3,
    GX_ERROR_FAILED = -4,
    GX_ERROR_PERMISSION_DENIED = -5,
    GX_ERROR_INTERNAL = -6
};

typedef uint64_t gx_handle;

struct NativeHostCallTable {
    uint32_t size = 0;
    uint32_t version = kGuideXOSNativeApiVersion;
    gx_result (*log)(NativeGxAppContext* ctx, const char* message) = nullptr;
    uint32_t (*get_api_version)(NativeGxAppContext* ctx) = nullptr;
    gx_result (*request_window)(NativeGxAppContext* ctx, const char* title, int width, int height, gx_handle* outWindow) = nullptr;
    gx_result (*exit)(NativeGxAppContext* ctx, gx_result exitCode) = nullptr;
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
    uint32_t hostLogCallCount = 0;
    std::string lastHostLogMessage;
    uint32_t lastApiVersionReturned = 0;
    uint32_t unsupportedHostCallCount = 0;
    std::vector<gx_handle> createdWindowHandles;
    uint32_t requestWindowCallCount = 0;
    gx_handle lastCreatedWindowId = 0;
    std::string lastRequestedWindowTitle;
    gx_result lastRequestWindowResult = GX_OK;
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

    static void BeginHostCallDispatch(NativeAppRuntimeContext& context);
    static void EndHostCallDispatch(NativeAppRuntimeContext& context);
    static const char* ToString(NativeAppLifecycleState state);

private:
    static void LogContext(const NativeAppRuntimeContext& context, const std::string& abi);
};

} // namespace apps
} // namespace gxos
