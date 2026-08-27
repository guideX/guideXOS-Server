//
// AMD64 backend for the bare-metal compiler bootstrap.
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

} // namespace

bool emit_function(const FunctionIR& function,
                   uint8_t* output,
                   uint32_t outputCapacity,
                   uint32_t* outputSize)
{
    if (!output || !outputSize || outputCapacity < AMD64_BOOTSTRAP_CODE_BYTES ||
        !is_gx_main(function.name)) return false;

    // mov eax, imm32; ret
    output[0] = 0xB8;
    const uint32_t immediate = static_cast<uint32_t>(function.returnConstant);
    output[1] = static_cast<uint8_t>(immediate & 0xFFu);
    output[2] = static_cast<uint8_t>((immediate >> 8) & 0xFFu);
    output[3] = static_cast<uint8_t>((immediate >> 16) & 0xFFu);
    output[4] = static_cast<uint8_t>((immediate >> 24) & 0xFFu);
    output[5] = 0xC3;
    *outputSize = AMD64_BOOTSTRAP_CODE_BYTES;
    return true;
}

} // namespace amd64
} // namespace compiler
} // namespace kernel
