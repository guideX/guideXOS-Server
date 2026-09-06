// Focused host-side checks for the Phase 27D ABI, compiler, and image layout.
// These checks supplement, but never replace, the fresh QEMU bare-metal proof.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/compiler_driver.h"
#include "core/compiler/elf_writer.h"
#include "core/native_elf/native_elf_runtime.h"
#include "core/native_elf/native_elf_validator.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <string>

using namespace kernel::compiler;
using namespace kernel::native_elf;

namespace {

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool compile_text(const char* source,
                         FunctionIR* function,
                         uint8_t* code,
                         uint32_t* codeBytes,
                         uint8_t* image,
                         uint32_t* imageBytes,
                         ElfLayout* layout)
{
    Token tokens[COMPILER_MAX_TOKENS] = {};
    Diagnostics diagnostics;
    uint32_t tokenCount = 0;
    if (!lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens,
                    COMPILER_MAX_TOKENS, &tokenCount, diagnostics)) { print_diagnostics(diagnostics); return false; }
    if (!parse_function(source, tokens, tokenCount, function, diagnostics)) { print_diagnostics(diagnostics); return false; }
    uint8_t data[COMPILER_MAX_DATA_BYTES] = {};
    const uint32_t dataBytes = function->hasHostLog ? function->logMessageBytes + 1U : 0U;
    for (uint32_t i = 0; i < dataBytes; ++i) data[i] = static_cast<uint8_t>(function->logMessage[i]);
    const uint64_t dataAddress = dataBytes == 0 ? 0 : BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET;
    if (!amd64::emit_function(*function, dataAddress, code, COMPILER_MAX_CODE_BYTES, codeBytes)) { std::fprintf(stderr, "emit failed\n"); return false; }
    if (!write_bootstrap_elf(code, *codeBytes, data, dataBytes, image,
                             BOOTSTRAP_MAX_ELF_BYTES, layout)) { std::fprintf(stderr, "write failed\n"); return false; }
    *imageBytes = layout->outputBytes;
    ElfValidationResult validation = {};
    const bool valid = validate_bootstrap_elf(image, *imageBytes, layout->imageBase,
                                  layout->codeOffset, code, *codeBytes,
                                  &validation, data, dataBytes);
    if (!valid) std::fprintf(stderr, "validate failed: %s\n", validation.error);
    return valid;
}

} // namespace

int main()
{
    if (!require(NATIVE_ELF_APPLICATION_STACK_SIZE == 64U * 1024U &&
                 NATIVE_ELF_RUNTIME_SAFETY_RESERVE_BYTES == 8192U &&
                 NATIVE_ELF_TRAMPOLINE_ENTRY_OVERHEAD_BYTES == 0x28U &&
                 COMPILER_MAX_GENERATED_FRAME_BYTES == 576U &&
                 COMPILER_MAX_TRANSIENT_STACK_BYTES == 128U &&
                 COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST == 760U &&
                 COMPILER_MAX_RUNTIME_CALL_DEPTH == 75U,
                 "Phase 27M stack policy constants are stable")) return 1;
    if (!require(sizeof(NativeElfTrampolineResult) == 24U &&
                 offsetof(NativeElfTrampolineResult, returnValue) == 0U &&
                 offsetof(NativeElfTrampolineResult, applicationRsp) == 8U &&
                 offsetof(NativeElfTrampolineResult, runtimeStatus) == 16U &&
                 offsetof(NativeElfTrampolineResult, runtimeCallDepth) == 20U &&
                 native_runtime_status_name(NativeRuntimeStatus::CallDepthExceeded) != nullptr,
                 "trampoline runtime-status ABI is append-only and explicit")) return 1;

    NativeAppStackLayout stack = {};
    if (!require(calculate_application_stack_layout(APPLICATION_STACK_BASE,
                                                    APPLICATION_STACK_SIZE, &stack),
                 "fixed application stack layout is valid")) return 1;
    if (!require(stack.top == APPLICATION_STACK_BASE + APPLICATION_STACK_SIZE &&
                 (stack.top & 0xFULL) == 0,
                 "application stack top is 16-byte aligned")) return 1;
    if (!require(!calculate_application_stack_layout(APPLICATION_STACK_BASE + 1,
                                                     APPLICATION_STACK_SIZE, &stack),
                 "misaligned stack base is rejected")) return 1;
    if (!require(native_app_pointer_in_range(APPLICATION_STACK_BASE + 8,
                                             APPLICATION_STACK_BASE,
                                             APPLICATION_STACK_SIZE) &&
                 !native_app_pointer_in_range(APPLICATION_STACK_BASE + APPLICATION_STACK_SIZE,
                                              APPLICATION_STACK_BASE,
                                              APPLICATION_STACK_SIZE),
                 "stack bounds are half-open")) return 1;

    uint32_t maximumReadableBytes = 0;
    if (!require(native_app_log_pointer_range(BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET + 254,
                                              BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET,
                                              256, &maximumReadableBytes) &&
                 maximumReadableBytes == 2,
                 "log pointer validation enforces data bounds")) return 1;
    if (!require(!native_app_pointer_in_range(~static_cast<uint64_t>(0),
                                             ~static_cast<uint64_t>(0) - 1,
                                             4),
                 "overflowing pointer ranges are rejected")) return 1;

    gx_host_calls hostCalls = {};
    hostCalls.size = sizeof(gx_host_calls);
    hostCalls.version = GX_API_VERSION;
    gx_app_context appContext = {};
    appContext.size = sizeof(gx_app_context);
    appContext.apiVersion = GX_API_VERSION;
    appContext.host = &hostCalls;
    appContext.userData = &hostCalls;
    if (!require(appContext.size == sizeof(gx_app_context) &&
                 appContext.apiVersion == GX_API_VERSION &&
                 appContext.host == &hostCalls && appContext.userData == &hostCalls &&
                 hostCalls.log == nullptr && hostCalls.get_api_version == nullptr,
                 "minimal app context and zeroed unsupported host table")) return 1;

    const char* source =
        "int gx_main(gx_app_context* ctx) { log(ctx, \"Hello from guideXOS!\"); return 42; }";
    FunctionIR function = {};
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint8_t image[BOOTSTRAP_MAX_ELF_BYTES] = {};
    uint32_t codeBytes = 0;
    uint32_t imageBytes = 0;
    ElfLayout layout = {};
    if (!require(compile_text(source, &function, code, &codeBytes, image, &imageBytes, &layout),
                 "host-call source pipeline")) return 1;
    if (!require(function.usesAppContext && function.hasHostLog &&
                 function.logMessageBytes == 20 && function.returnConstant == 42,
                 "host-call IR carries context, literal, and return")) return 1;
    if (!require(codeBytes == 34 && code[0] == 0x48 && code[1] == 0xBA &&
                 code[10] == 0x48 && code[14] == 0x48 && code[18] == 0x48 &&
                 code[22] == 0xFF && code[23] == 0xD0 &&
                 code[28] == 0xB8 && code[29] == 0x2A && code[30] == 0 &&
                 code[31] == 0 && code[32] == 0 && code[33] == 0xC3,
                 "AMD64 host-call and return emission")) return 1;
    if (!require(layout.dataOffset == BOOTSTRAP_DATA_OFFSET && layout.dataBytes == 21,
                 "source data is placed at the read-only segment offset")) return 1;

    NativeElfValidationResult validation = {};
    if (!require(validate_native_elf(image, imageBytes, default_validation_policy(), &validation) &&
                 validation.loadCount == 2 && validation.executableLoadCount == 1,
                 "two-segment NativeElf image validates")) return 1;

    std::string oversized = "int gx_main(gx_app_context* ctx) { log(ctx, \"";
    oversized.append(256, 'x');
    oversized += "\"); return 42; }";
    Token oversizedTokens[COMPILER_MAX_TOKENS] = {};
    Diagnostics oversizedDiagnostics;
    uint32_t oversizedTokenCount = 0;
    if (!require(lex_source(oversized.c_str(), static_cast<uint32_t>(oversized.size()),
                            oversizedTokens, COMPILER_MAX_TOKENS, &oversizedTokenCount,
                            oversizedDiagnostics),
                 "oversized literal reaches parser")) return 1;
    FunctionIR oversizedFunction = {};
    if (!require(!parse_function(oversized.c_str(), oversizedTokens, oversizedTokenCount,
                                 &oversizedFunction, oversizedDiagnostics),
                 "oversized literal is rejected")) return 1;

    NativeAppExecutionContext context = {};
    context.state = NativeAppExecutionState::Empty;
    context.state = NativeAppExecutionState::Loaded;
    context.state = NativeAppExecutionState::Prepared;
    context.state = NativeAppExecutionState::Running;
    context.state = NativeAppExecutionState::Returned;
    context.state = NativeAppExecutionState::Cleaned;
    context.runtimeStatus = NativeRuntimeStatus::CallDepthExceeded;
    context.runtimeCallDepth = COMPILER_MAX_RUNTIME_CALL_DEPTH;
    if (!require(context.state == NativeAppExecutionState::Cleaned,
                 "runtime state enum supports deterministic lifecycle")) return 1;
    if (!require(context.runtimeStatus == NativeRuntimeStatus::CallDepthExceeded &&
                 context.runtimeCallDepth == 75U,
                 "runtime state carries bounded-call failure details")) return 1;

    std::puts("native_elf_runtime_host_test: PASS");
    return 0;
}
