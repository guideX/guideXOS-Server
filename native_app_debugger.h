#pragma once

#include "native_app_runtime.h"
#include "native_elf_image_loader.h"
#include "executable_memory.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

class NativeAppDebugger {
public:
    static bool RegisterRuntime(NativeAppRuntimeContext& context,
                                const ExecutableMemoryBlock& mapping,
                                const NativeElfImage& image,
                                bool gateExecution,
                                std::string& error);
    static bool WaitForExecutionGate(uint64_t runtimeId);
    static void UnregisterRuntime(uint64_t runtimeId);
    static gx_result Command(const gx_development_debug_request& request,
                             const std::string& expectedArtifactSha256,
                             gx_development_debug_snapshot* snapshot);
    static void CancelProcess(uint64_t processId);
};

} // namespace apps
} // namespace gxos
