#include "compiler_backend.h"

namespace kernel {
namespace compiler {
namespace arm64 {
namespace {

static bool is_gx_main(const char* name)
{
    if (!name) return false;
    const char expected[] = "gx_main";
    for (uint32_t i = 0; expected[i] || name[i]; ++i) if (expected[i] != name[i]) return false;
    return true;
}

static void put_u32(uint8_t* output, uint32_t offset, uint32_t value)
{
    output[offset + 0] = static_cast<uint8_t>(value);
    output[offset + 1] = static_cast<uint8_t>(value >> 8);
    output[offset + 2] = static_cast<uint8_t>(value >> 16);
    output[offset + 3] = static_cast<uint8_t>(value >> 24);
}

static uint32_t movz_w0(uint16_t value)
{
    return 0x52800000u | (static_cast<uint32_t>(value) << 5);
}

static uint32_t movk_w0(uint16_t value)
{
    return 0x72A00000u | (static_cast<uint32_t>(value) << 5);
}

static bool adr_x1(int32_t byteDelta, uint32_t* instruction)
{
    if (!instruction || byteDelta < -1048576 || byteDelta > 1048575) return false;
    const uint32_t immediate = static_cast<uint32_t>(byteDelta);
    *instruction = 0x10000001u | ((immediate & 3u) << 29) | (((immediate >> 2) & 0x7ffffu) << 5);
    return true;
}

static uint32_t message_length(const char* value)
{
    uint32_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static const char* message_at(const FunctionIR& function, uint32_t index)
{
    return function.logCount == 0 ? function.logMessage : function.logMessages[index];
}

} // namespace

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize)
{
    if (!output || !outputSize || !is_gx_main(function.name)) return false;
    const uint32_t logCount = function.logCount != 0 ? function.logCount : (function.hasLogCall ? 1u : 0u);
    if (logCount > COMPILER_MAX_LOG_CALLS) return false;
    uint32_t messageBytes = 0;
    for (uint32_t i = 0; i < logCount; ++i) messageBytes += message_length(message_at(function, i)) + 1u;
    // stp/mov prologue, five instructions per ABI log call, movz/movk,
    // ldp/ret epilogue.  x19 is callee-saved and preserves ctx across BLR.
    const uint32_t codeBytes = logCount == 0 ? 12u : 24u + logCount * 20u;
    if (codeBytes > outputCapacity || messageBytes > outputCapacity - codeBytes ||
        codeBytes + messageBytes > ARM64_MAX_BOOTSTRAP_BYTES) return false;

    uint32_t offset = 0;
    if (logCount != 0) {
        put_u32(output, offset, 0xA9BF7BF3u); offset += 4; // stp x19,x30,[sp,#-16]!
        put_u32(output, offset, 0xAA0003F3u); offset += 4; // mov x19,x0
        uint32_t messageOffset = codeBytes;
        for (uint32_t i = 0; i < logCount; ++i) {
            put_u32(output, offset, 0xF9400670u); offset += 4; // ldr x16,[x19,#8]
            put_u32(output, offset, 0xF9400610u); offset += 4; // ldr x16,[x16,#8]
            uint32_t adrInstruction = 0;
            const int32_t messageDelta = static_cast<int32_t>(messageOffset) - static_cast<int32_t>(offset);
            if (!adr_x1(messageDelta, &adrInstruction)) return false;
            put_u32(output, offset, adrInstruction); offset += 4;
            put_u32(output, offset, 0xAA1303E0u); offset += 4; // mov x0,x19
            put_u32(output, offset, 0xD63F0200u); offset += 4; // blr x16
            messageOffset += message_length(message_at(function, i)) + 1u;
        }
    }
    put_u32(output, offset, movz_w0(static_cast<uint16_t>(static_cast<uint32_t>(function.returnConstant) & 0xffffu))); offset += 4;
    put_u32(output, offset, movk_w0(static_cast<uint16_t>((static_cast<uint32_t>(function.returnConstant) >> 16) & 0xffffu))); offset += 4;
    if (logCount != 0) {
        put_u32(output, offset, 0xA8C17BF3u); offset += 4; // ldp x19,x30,[sp],#16
    }
    put_u32(output, offset, 0xD65F03C0u); offset += 4; // ret
    offset = codeBytes;
    for (uint32_t i = 0; i < logCount; ++i) {
        const char* message = message_at(function, i);
        while (*message) output[offset++] = static_cast<uint8_t>(*message++);
        output[offset++] = 0;
    }
    *outputSize = codeBytes + messageBytes;
    return true;
}

} // namespace arm64
} // namespace compiler
} // namespace kernel
