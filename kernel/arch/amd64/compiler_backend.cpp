//
// AMD64 backend for the bounded bare-metal compiler.
//

#include "compiler_backend.h"

namespace kernel {
namespace compiler {
namespace amd64 {
namespace {

static bool is_gx_main(const char* name)
{
    if (!name) return false;
    const char expected[] = "gx_main";
    for (uint32_t i = 0; expected[i] || name[i]; ++i) {
        if (expected[i] != name[i]) return false;
    }
    return true;
}

static const char* message_at(const FunctionIR& function, uint32_t index)
{
    return function.logCount == 0 ? function.logMessage : function.logMessages[index];
}

static uint32_t message_length(const char* message)
{
    uint32_t length = 0;
    if (message) while (message[length]) ++length;
    return length;
}

} // namespace

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize)
{
    if (!output || !outputSize || !is_gx_main(function.name)) return false;

    const uint32_t immediate = static_cast<uint32_t>(function.returnConstant);
    const uint32_t logCount = function.logCount != 0 ? function.logCount : (function.hasLogCall ? 1u : 0u);
    if (logCount > COMPILER_MAX_LOG_CALLS) return false;
    if (logCount == 0) {
        if (outputCapacity < AMD64_BOOTSTRAP_CODE_BYTES) return false;
        // mov eax, imm32; ret
        output[0] = 0xB8;
        output[1] = static_cast<uint8_t>(immediate & 0xFFu);
        output[2] = static_cast<uint8_t>((immediate >> 8) & 0xFFu);
        output[3] = static_cast<uint8_t>((immediate >> 16) & 0xFFu);
        output[4] = static_cast<uint8_t>((immediate >> 24) & 0xFFu);
        output[5] = 0xC3;
        *outputSize = AMD64_BOOTSTRAP_CODE_BYTES;
        return true;
    }

    // Windows x64 ABI: reserve shadow space and preserve the incoming context
    // across each volatile indirect ABI call. The byte emitter is also used
    // as a cross-target backend while the kernel itself runs on ARM64.
    const uint32_t codeBytes = 23u + logCount * 22u;
    uint32_t messageBytes = 0;
    for (uint32_t i = 0; i < logCount; ++i) {
        const uint32_t length = message_length(message_at(function, i));
        if (length + 1u > AMD64_MAX_BOOTSTRAP_BYTES - codeBytes - messageBytes) return false;
        messageBytes += length + 1u;
    }
    if (outputCapacity < codeBytes + messageBytes) return false;

    uint32_t offset = 0;
    output[offset++] = 0x48; output[offset++] = 0x83; output[offset++] = 0xEC; output[offset++] = 0x28; // sub rsp,40h
    output[offset++] = 0x48; output[offset++] = 0x89; output[offset++] = 0x4C; output[offset++] = 0x24; output[offset++] = 0x20; // mov [rsp+20h],rcx
    uint32_t messageOffset = codeBytes;
    for (uint32_t i = 0; i < logCount; ++i) {
        const char* message = message_at(function, i);
        output[offset++] = 0x48; output[offset++] = 0x8B; output[offset++] = 0x4C; output[offset++] = 0x24; output[offset++] = 0x20; // mov rcx,[rsp+20h]
        output[offset++] = 0x48; output[offset++] = 0x8B; output[offset++] = 0x41; output[offset++] = 0x08; // mov rax,[rcx+8]
        output[offset++] = 0x48; output[offset++] = 0x8B; output[offset++] = 0x40; output[offset++] = 0x08; // mov rax,[rax+8]
        output[offset++] = 0x48; output[offset++] = 0x8D; output[offset++] = 0x15;
        const int32_t displacement = static_cast<int32_t>(messageOffset - (offset + 4u));
        for (uint32_t byte = 0; byte < 4; ++byte) output[offset++] = static_cast<uint8_t>(static_cast<uint32_t>(displacement) >> (byte * 8));
        output[offset++] = 0xFF; output[offset++] = 0xD0; // call rax
        messageOffset += message_length(message) + 1u;
    }
    output[offset++] = 0xB8;
    for (uint32_t i = 0; i < 4; ++i) output[offset++] = static_cast<uint8_t>(immediate >> (i * 8));
    output[offset++] = 0x48; output[offset++] = 0x83; output[offset++] = 0xC4; output[offset++] = 0x28; // add rsp,40h
    output[offset++] = 0xC3;

    offset = codeBytes;
    for (uint32_t i = 0; i < logCount; ++i) {
        const char* message = message_at(function, i);
        while (*message) output[offset++] = static_cast<uint8_t>(*message++);
        output[offset++] = 0;
    }
    *outputSize = codeBytes + messageBytes;
    return true;
}

} // namespace amd64
} // namespace compiler
} // namespace kernel
