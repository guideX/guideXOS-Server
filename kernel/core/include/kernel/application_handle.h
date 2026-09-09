#pragma once

#include <stdint.h>

#include "sdk/include/guidexos/types.h"

namespace kernel {
namespace application {

/*
 * Common opaque handle format used by NativeElf GUI runtimes.
 *
 *  63..56  format magic
 *  55..48  runtime identity
 *  47..32  runtime generation
 *  31..28  object kind
 *  27..16  object generation
 *  15..00  object slot
 *
 * Runtime identity and generation are both checked.  A recycled slot in a
 * relaunch therefore cannot make a handle from the previous runtime valid.
 */
static const uint64_t kHandleMagic = UINT64_C(0x47) << 56;
static const uint64_t kHandleMagicMask = UINT64_C(0xff) << 56;
static const uint32_t kHandleRuntimeShift = 48;
static const uint32_t kHandleGenerationShift = 32;
static const uint32_t kHandleKindShift = 28;
static const uint32_t kHandleObjectGenerationShift = 16;
static const uint64_t kHandleRuntimeMask = UINT64_C(0xff);
static const uint64_t kHandleGenerationMask = UINT64_C(0xffff);
static const uint64_t kHandleKindMask = UINT64_C(0xf);
static const uint64_t kHandleObjectGenerationMask = UINT64_C(0xfff);
static const uint64_t kHandleSlotMask = UINT64_C(0xffff);

enum class HandleKind : uint32_t {
    None = 0,
    Window = 1,
    Widget = 2,
    Service = 3
};

struct DecodedHandle {
    uint8_t runtime;
    uint16_t generation;
    HandleKind kind;
    uint16_t objectGeneration;
    uint16_t slot;
};

static inline gx_handle make_handle(uint8_t runtime, uint16_t generation,
                                    HandleKind kind, uint16_t objectGeneration,
                                    uint16_t slot)
{
    if (runtime == 0 || generation == 0 || kind == HandleKind::None || slot == 0) return 0;
    return kHandleMagic |
        ((static_cast<uint64_t>(runtime) & kHandleRuntimeMask) << kHandleRuntimeShift) |
        ((static_cast<uint64_t>(generation) & kHandleGenerationMask) << kHandleGenerationShift) |
        ((static_cast<uint64_t>(kind) & kHandleKindMask) << kHandleKindShift) |
        ((static_cast<uint64_t>(objectGeneration) & kHandleObjectGenerationMask) << kHandleObjectGenerationShift) |
        (static_cast<uint64_t>(slot) & kHandleSlotMask);
}

static inline bool decode_handle(gx_handle value, DecodedHandle* decoded)
{
    if (!decoded || (value & kHandleMagicMask) != kHandleMagic) return false;
    decoded->runtime = static_cast<uint8_t>((value >> kHandleRuntimeShift) & kHandleRuntimeMask);
    decoded->generation = static_cast<uint16_t>((value >> kHandleGenerationShift) & kHandleGenerationMask);
    decoded->kind = static_cast<HandleKind>((value >> kHandleKindShift) & kHandleKindMask);
    decoded->objectGeneration = static_cast<uint16_t>((value >> kHandleObjectGenerationShift) & kHandleObjectGenerationMask);
    decoded->slot = static_cast<uint16_t>(value & kHandleSlotMask);
    return decoded->runtime != 0 && decoded->generation != 0 && decoded->kind != HandleKind::None && decoded->slot != 0;
}

static inline bool handle_matches(gx_handle value, uint8_t runtime, uint16_t generation,
                                  HandleKind kind, uint16_t objectGeneration,
                                  uint16_t slot)
{
    DecodedHandle decoded{};
    return decode_handle(value, &decoded) && decoded.runtime == runtime &&
        decoded.generation == generation && decoded.kind == kind &&
        decoded.objectGeneration == objectGeneration && decoded.slot == slot;
}

} // namespace application
} // namespace kernel
