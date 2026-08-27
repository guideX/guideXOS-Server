//
// AMD64 backend for the bare-metal compiler bootstrap.
//

#include "compiler_backend.h"

#include "../../../sdk/include/guidexos/app.h"

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

} // namespace

bool emit_function(const FunctionIR& function,
                   uint64_t readOnlyDataAddress,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize)
{
    static_assert(offsetof(gx_app_context, host) == 8, "generated gx_app_context host offset changed");
    static_assert(offsetof(gx_host_calls, log) == 8, "generated gx_host_calls log offset changed");

    if (!output || !outputSize || outputCapacity < AMD64_BOOTSTRAP_CODE_BYTES ||
        !is_gx_main(function.name)) return false;

    uint32_t offset = 0;
    if (function.hasHostLog) {
        if (!function.usesAppContext || readOnlyDataAddress == 0 ||
            function.logMessageBytes > COMPILER_MAX_STRING_LITERAL_BYTES) return false;

        // Microsoft x64 ABI host call:
        //   RDX = source-derived string address
        //   RAX = ctx->host->log
        //   reserve 32-byte home space plus 8-byte alignment padding
        output[offset++] = 0x48;
        output[offset++] = 0xBA; // mov rdx, imm64
        for (uint32_t i = 0; i < 8; ++i) {
            output[offset++] = static_cast<uint8_t>((readOnlyDataAddress >> (i * 8)) & 0xFFULL);
        }
        output[offset++] = 0x48;
        output[offset++] = 0x8B;
        output[offset++] = 0x41;
        output[offset++] = static_cast<uint8_t>(offsetof(gx_app_context, host)); // mov rax,[rcx+8]
        output[offset++] = 0x48;
        output[offset++] = 0x8B;
        output[offset++] = 0x40;
        output[offset++] = static_cast<uint8_t>(offsetof(gx_host_calls, log)); // mov rax,[rax+8]
        output[offset++] = 0x48;
        output[offset++] = 0x83;
        output[offset++] = 0xEC;
        output[offset++] = 0x28; // 32-byte shadow + 8-byte call alignment padding
        output[offset++] = 0xFF;
        output[offset++] = 0xD0; // call rax
        output[offset++] = 0x48;
        output[offset++] = 0x83;
        output[offset++] = 0xC4;
        output[offset++] = 0x28;
    }

    if (offset + AMD64_BOOTSTRAP_CODE_BYTES > outputCapacity) return false;

    // mov eax, imm32; ret
    output[offset++] = 0xB8;
    const uint32_t immediate = static_cast<uint32_t>(function.returnConstant);
    output[offset++] = static_cast<uint8_t>(immediate & 0xFFu);
    output[offset++] = static_cast<uint8_t>((immediate >> 8) & 0xFFu);
    output[offset++] = static_cast<uint8_t>((immediate >> 16) & 0xFFu);
    output[offset++] = static_cast<uint8_t>((immediate >> 24) & 0xFFu);
    output[offset++] = 0xC3;
    *outputSize = offset;
    return true;
}

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize)
{
    return emit_function(function, 0, output, outputCapacity, outputSize);
}

} // namespace amd64
} // namespace compiler
} // namespace kernel
