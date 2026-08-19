#include "guidexos_nativeaot_amd64_unwind_primitive.h"

#include <stddef.h>

namespace {

constexpr size_t kContextRbx = 0x90u;
constexpr size_t kContextRsp = 0x98u;
constexpr size_t kContextRbp = 0xA0u;
constexpr size_t kContextRsi = 0xA8u;
constexpr size_t kContextRdi = 0xB0u;
constexpr size_t kContextR12 = 0xD8u;
constexpr size_t kContextR13 = 0xE0u;
constexpr size_t kContextR14 = 0xE8u;
constexpr size_t kContextR15 = 0xF0u;
constexpr size_t kContextRip = 0xF8u;

uint64_t* contextRegister(void* context, uint32_t reg) {
    switch (reg) {
        case 3u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextRbx);
        case 4u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextRsp);
        case 5u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextRbp);
        case 6u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextRsi);
        case 7u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextRdi);
        case 12u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextR12);
        case 13u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextR13);
        case 14u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextR14);
        case 15u: return reinterpret_cast<uint64_t*>(
            reinterpret_cast<uint8_t*>(context) + kContextR15);
        default: return nullptr;
    }
}

void recordNonvolatileLocation(void* contextPointers, uint32_t reg,
                               uint64_t* location) {
    if (contextPointers == nullptr || location == nullptr) return;
    uint64_t** pointer = nullptr;
    switch (reg) {
        case 3u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 0u); break;
        case 5u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 16u); break;
        case 6u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 24u); break;
        case 7u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 32u); break;
        case 12u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 40u); break;
        case 13u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 48u); break;
        case 14u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 56u); break;
        case 15u: pointer = reinterpret_cast<uint64_t**>(
            reinterpret_cast<uint8_t*>(contextPointers) + 64u); break;
        default: break;
    }
    if (pointer != nullptr) *pointer = location;
}

uint16_t unwindFrameOffset(const uint8_t* code) {
    return static_cast<uint16_t>(code[0]) |
           static_cast<uint16_t>(code[1]) << 8;
}

} // namespace

extern "C" void* __cdecl guideXosRtlVirtualUnwind(
    uint32_t handlerType, uint64_t imageBase, uint64_t controlPc,
    void* functionEntry, void* context,
    void** handlerData, uint64_t* establisherFrame,
    void* contextPointers) {
    (void)handlerType;
    GuidexosRuntimeFunction* nativeFunctionEntry =
        reinterpret_cast<GuidexosRuntimeFunction*>(functionEntry);
    if (nativeFunctionEntry == nullptr || context == nullptr) return nullptr;

    const uint8_t* unwindInfo = reinterpret_cast<const uint8_t*>(
        imageBase + nativeFunctionEntry->unwindData);
    const uint8_t versionAndFlags = unwindInfo[0];
    const uint8_t prologueSize = unwindInfo[1];
    const uint8_t codeCount = unwindInfo[2];
    const uint8_t flags = static_cast<uint8_t>(versionAndFlags >> 3);
    const uint64_t functionStart = imageBase + nativeFunctionEntry->beginAddress;
    const uint64_t controlOffset = controlPc >= functionStart
        ? controlPc - functionStart : 0u;
    const bool inPrologue = controlOffset < prologueSize;
    uint64_t rsp = *reinterpret_cast<uint64_t*>(
        reinterpret_cast<uint8_t*>(context) + kContextRsp);
    const uint8_t* codes = unwindInfo + 4u;

    for (uint32_t index = 0u; index < codeCount;) {
        const uint8_t* code = codes + index * 2u;
        const uint8_t codeOffset = code[0];
        const uint8_t unwindOp = static_cast<uint8_t>(code[1] & 0x0Fu);
        const uint8_t opInfo = static_cast<uint8_t>(code[1] >> 4);
        uint32_t slots = 1u;
        switch (unwindOp) {
            case 0u: // UWOP_PUSH_NONVOL
                break;
            case 1u: // UWOP_ALLOC_LARGE
                slots = opInfo == 0u ? 2u : 3u;
                break;
            case 4u: // UWOP_SAVE_NONVOL
                slots = 2u;
                break;
            case 5u: // UWOP_SAVE_NONVOL_FAR
                slots = 3u;
                break;
            case 8u: // UWOP_SAVE_XMM128
                slots = 2u;
                break;
            case 9u: // UWOP_SAVE_XMM128_FAR
                slots = 3u;
                break;
            default:
                break;
        }

        if (!inPrologue || codeOffset <= controlOffset) {
            switch (unwindOp) {
                case 0u: { // UWOP_PUSH_NONVOL
                    uint64_t* location = reinterpret_cast<uint64_t*>(rsp);
                    uint64_t* destination = contextRegister(context, opInfo);
                    if (destination != nullptr) {
                        *destination = *location;
                        recordNonvolatileLocation(contextPointers, opInfo,
                                                  location);
                    }
                    rsp += 8u;
                    break;
                }
                case 1u: { // UWOP_ALLOC_LARGE
                    uint64_t size = 0u;
                    if (opInfo == 0u) {
                        size = static_cast<uint64_t>(unwindFrameOffset(
                            codes + (index + 1u) * 2u)) * 8u;
                    } else {
                        size = static_cast<uint64_t>(unwindFrameOffset(
                            codes + (index + 1u) * 2u)) |
                            static_cast<uint64_t>(unwindFrameOffset(
                            codes + (index + 2u) * 2u)) << 16;
                    }
                    rsp += size;
                    break;
                }
                case 2u: // UWOP_ALLOC_SMALL
                    rsp += static_cast<uint64_t>(opInfo) * 8u + 8u;
                    break;
                case 3u: { // UWOP_SET_FPREG
                    uint64_t* framePointer = contextRegister(context, 5u);
                    if (framePointer != nullptr) {
                        rsp = *framePointer -
                            static_cast<uint64_t>(unwindInfo[3] >> 4) * 16u;
                    }
                    break;
                }
                case 4u: { // UWOP_SAVE_NONVOL
                    uint64_t* location = reinterpret_cast<uint64_t*>(
                        rsp + static_cast<uint64_t>(unwindFrameOffset(
                            codes + (index + 1u) * 2u)) * 8u);
                    uint64_t* destination = contextRegister(context, opInfo);
                    if (destination != nullptr) {
                        *destination = *location;
                        recordNonvolatileLocation(contextPointers, opInfo,
                                                  location);
                    }
                    break;
                }
                case 5u: { // UWOP_SAVE_NONVOL_FAR
                    const uint32_t offset = static_cast<uint32_t>(
                        unwindFrameOffset(codes + (index + 1u) * 2u)) |
                        static_cast<uint32_t>(unwindFrameOffset(
                            codes + (index + 2u) * 2u)) << 16;
                    uint64_t* location = reinterpret_cast<uint64_t*>(rsp + offset);
                    uint64_t* destination = contextRegister(context, opInfo);
                    if (destination != nullptr) {
                        *destination = *location;
                        recordNonvolatileLocation(contextPointers, opInfo,
                                                  location);
                    }
                    break;
                }
                case 10u: { // UWOP_PUSH_MACHFRAME
                    if (opInfo != 0u) rsp += 8u;
                    uint64_t* location = reinterpret_cast<uint64_t*>(rsp);
                    *reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(
                        context) + kContextRip) = location[0];
                    rsp = location[3];
                    break;
                }
                default:
                    // XMM save records consume their documented slots but do
                    // not affect the integer caller-state proof.
                    break;
            }
        }
        index += slots;
    }

    uint64_t* returnAddress = reinterpret_cast<uint64_t*>(rsp);
    *reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(context) +
        kContextRip) = *returnAddress;
    rsp += 8u;
    *reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(context) +
        kContextRsp) = rsp;
    if (establisherFrame != nullptr) *establisherFrame = rsp;
    if (handlerData != nullptr) *handlerData = nullptr;
    (void)flags;
    return nullptr;
}
