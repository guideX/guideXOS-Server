#pragma once

#include <stdint.h>

#include "aarch64_gui_render.h"

namespace kernel {
namespace studio_abi {

/* This identity is carried in the package entry's `abi` property. The plain
 * guidexos-c-abi-v1 identity continues to select the original 400-byte ABI. */
static const char kV05StudioAbiIdentity[] = "guidexos-c-abi-v1-studio-v05-464";

/* Private status values used only between the ARM64 NativeElf file-stat host
 * service and the v0.5 adapter. They are never written into either public ABI
 * table; this preserves AARCH64's existing -9 quota code and v0.5's -9
 * not-found code while forwarding the filesystem's precise lookup result. */
static constexpr gx_result kV05FileStatNotFoundBridge = static_cast<gx_result>(-1001);
static constexpr gx_result kV05FileStatMountUnavailableBridge = static_cast<gx_result>(-1002);

enum class Profile : uint8_t {
    Aarch64400 = 0,
    StudioV05_464 = 1,
    Unknown = 255
};

Profile profile_from_identity(const char* identity);
bool identity_supported(const char* identity);

struct UserMemoryRange {
    uint64_t imageBase;
    uint64_t imageBytes;
    uint64_t stackBase;
    uint64_t stackBytes;
};

struct V05Adapter;

/* The adapter owns a private v0.5 context and 464-byte call table. The AARCH64
 * objects remain separately typed and are never viewed through v0.5 types. */
V05Adapter* create_v05_adapter(gx_app_context* aarch64Context,
                               gx_host_calls* aarch64Host,
                               gui_render::HostBinding* renderBinding,
                               const UserMemoryRange& userMemory);
void* v05_context(V05Adapter* adapter);
void* v05_host_table(V05Adapter* adapter);
void deactivate_v05_adapter(V05Adapter* adapter);
void destroy_v05_adapter(V05Adapter* adapter);

} // namespace studio_abi
} // namespace kernel
