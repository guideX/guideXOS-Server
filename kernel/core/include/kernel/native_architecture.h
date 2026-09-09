// Canonical native payload architecture identity shared by the App Model and
// architecture backends.  The package layer consumes this contract without
// inspecting architecture registers or backend-specific preprocessor names.

#pragma once

#include <stdint.h>

namespace gxos {
namespace apps {

enum class NativeArchitecture : uint8_t {
    Unknown = 0,
    AMD64,
    ARM64
};

const char* NativeArchitectureToString(NativeArchitecture architecture);
NativeArchitecture NativeArchitectureFromString(const char* architecture);
NativeArchitecture CurrentNativeArchitecture();
uint16_t NativeArchitectureElfMachine(NativeArchitecture architecture);

} // namespace apps
} // namespace gxos
