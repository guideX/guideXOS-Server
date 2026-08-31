// Focused host-side checks for Phase 27L user-defined integer functions.
// The generated code is executed from an in-memory buffer on Windows so the
// tests exercise real E8 rel32 calls and per-function frames without a host
// compiler participating in the guest compilation path.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/elf_writer.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

struct Artifact {
    TranslationUnitIR unit;
    uint8_t data[COMPILER_MAX_TOTAL_STRING_DATA];
    uint8_t code[COMPILER_MAX_CODE_BYTES];
    uint8_t elf[BOOTSTRAP_MAX_ELF_BYTES];
    uint32_t dataBytes;
    uint32_t codeBytes;
    uint32_t entryCodeOffset;
    uint32_t elfBytes;
};

static Artifact g_first = {};
static Artifact g_second = {};
static Token g_tokens[COMPILER_MAX_TOKENS] = {};

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    if (!needle) return false;
    for (uint32_t i = 0; i < diagnostics.count(); ++i) {
        const char* message = diagnostics.at(i).message;
        if (message && std::strstr(message, needle)) return true;
    }
    return false;
}

static bool flatten_strings(TranslationUnitIR* unit, uint8_t* data,
                            uint32_t capacity, uint32_t* bytes)
{
    if (!unit || !data || !bytes) return false;
    uint32_t offset = 0;
    for (uint32_t f = 0; f < unit->functionCount; ++f) {
        FunctionIR& function = unit->functions[f];
        const uint32_t functionStart = offset;
        function.dataOffset = functionStart;
        for (uint32_t i = 0; i < function.stringCount; ++i) {
            if (function.stringOffsets[i] != offset - functionStart ||
                offset + function.strings[i].bytes + 1U > capacity) return false;
            for (uint32_t j = 0; j < function.strings[i].bytes; ++j)
                data[offset + j] = static_cast<uint8_t>(function.strings[i].data[j]);
            data[offset + function.strings[i].bytes] = 0;
            offset += function.strings[i].bytes + 1U;
        }
    }
    *bytes = offset;
    return true;
}

static bool compile_unit(const char* source, Artifact* artifact, Diagnostics* diagnostics)
{
    if (!source || !artifact || !diagnostics) return false;
    *artifact = {};
    const uint32_t sourceBytes = static_cast<uint32_t>(std::strlen(source));
    uint32_t tokenCount = 0;
    if (!lex_source(source, sourceBytes, g_tokens, COMPILER_MAX_TOKENS, &tokenCount, *diagnostics) ||
        !parse_translation_unit(source, g_tokens, tokenCount, &artifact->unit, *diagnostics)) return false;
    if (!flatten_strings(&artifact->unit, artifact->data, sizeof(artifact->data), &artifact->dataBytes)) return false;
    const uint64_t dataAddress = artifact->dataBytes == 0 ? 0 :
        BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_DATA_OFFSET;
    if (!amd64::emit_translation_unit(artifact->unit, dataAddress, artifact->code,
                                      sizeof(artifact->code), &artifact->codeBytes,
                                      &artifact->entryCodeOffset)) return false;
    ElfLayout layout = {};
    if (!write_bootstrap_elf(artifact->code, artifact->codeBytes, artifact->data,
                             artifact->dataBytes, artifact->entryCodeOffset,
                             artifact->elf, sizeof(artifact->elf), &layout)) return false;
    artifact->elfBytes = layout.outputBytes;
    ElfValidationResult validation = {};
    return validate_bootstrap_elf(artifact->elf, artifact->elfBytes, layout.imageBase,
                                  layout.codeOffset, artifact->code, artifact->codeBytes,
                                  &validation, artifact->data, artifact->dataBytes,
                                  artifact->entryCodeOffset);
}

static int32_t read_rel32(const uint8_t* code, uint32_t offset)
{
    return static_cast<int32_t>(static_cast<uint32_t>(code[offset + 1]) |
        (static_cast<uint32_t>(code[offset + 2]) << 8) |
        (static_cast<uint32_t>(code[offset + 3]) << 16) |
        (static_cast<uint32_t>(code[offset + 4]) << 24));
}

struct GeneratedCallResult {
    int result;
    uint32_t runtimeFailure;
    uint32_t runtimeDepth;
};

static GeneratedCallResult call_generated_entry(void* entry)
{
#if defined(__GNUC__) || defined(__clang__)
    GeneratedCallResult result = {};
    // R14/R15 are compiler-private nonvolatile registers for generated
    // applications.  The real NativeElf trampoline preserves them; this
    // host shim supplies the same ABI boundary for direct in-memory tests.
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "movq %[output], 32(%%rsp)\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "movq 32(%%rsp), %%r11\n\t"
        "movl %%r15d, 4(%%r11)\n\t"
        "movl %%r15d, 8(%%r11)\n\t"
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(result.result)
        : [entry] "r"(entry), [output] "r"(&result)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
    return result;
#else
    typedef int (*EntryFunction)(void*);
    GeneratedCallResult result = {};
    result.result = reinterpret_cast<EntryFunction>(entry)(nullptr);
    return result;
#endif
}

static bool inspect_calls(const Artifact& artifact, uint32_t* callCount,
                          bool* hasForward, bool* hasBackward)
{
    if (!callCount || !hasForward || !hasBackward) return false;
    *callCount = 0;
    *hasForward = false;
    *hasBackward = false;
    for (uint32_t i = 0; i + 5U <= artifact.codeBytes; ++i) {
        if (artifact.code[i] != 0xE8) continue;
        const int64_t target = static_cast<int64_t>(i + 5U) + read_rel32(artifact.code, i);
        if (target < 0 || target >= artifact.codeBytes) return false;
        ++*callCount;
        if (target > static_cast<int64_t>(i)) *hasForward = true;
        if (target < static_cast<int64_t>(i)) *hasBackward = true;
    }
    return true;
}

static bool contains_bytes(const Artifact& artifact, const uint8_t* bytes, uint32_t count)
{
    if (!bytes || count == 0 || count > artifact.codeBytes) return false;
    for (uint32_t i = 0; i + count <= artifact.codeBytes; ++i) {
        bool same = true;
        for (uint32_t j = 0; j < count; ++j)
            if (artifact.code[i + j] != bytes[j]) { same = false; break; }
        if (same) return true;
    }
    return false;
}

static bool has_call_target(const Artifact& artifact, uint32_t target)
{
    for (uint32_t i = 0; i + 5U <= artifact.codeBytes; ++i) {
        if (artifact.code[i] != 0xE8) continue;
        const int64_t actual = static_cast<int64_t>(i + 5U) + read_rel32(artifact.code, i);
        if (actual == static_cast<int64_t>(target)) return true;
    }
    return false;
}

static bool execute_entry(const Artifact& artifact, int32_t expected,
                          GeneratedCallResult* observed = nullptr)
{
#if defined(_WIN32)
    void* memory = VirtualAlloc(nullptr, artifact.codeBytes, MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
    if (!memory) return false;
    std::memcpy(memory, artifact.code, artifact.codeBytes);
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(memory, artifact.codeBytes, PAGE_EXECUTE_READ,
                                           &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), memory, artifact.codeBytes);
    const GeneratedCallResult call = executable
        ? call_generated_entry(static_cast<uint8_t*>(memory) + artifact.entryCodeOffset)
        : GeneratedCallResult();
    if (observed) *observed = call;
    VirtualFree(memory, 0, MEM_RELEASE);
    return executable && call.result == expected;
#else
    (void)artifact;
    (void)expected;
    return true;
#endif
}

static bool expect_rejected(const char* source, const char* diagnostic)
{
    Diagnostics diagnostics;
    static Artifact artifact = {};
    const bool accepted = compile_unit(source, &artifact, &diagnostics);
    return !accepted && diagnostic_contains(diagnostics, diagnostic) &&
        diagnostics.count() != 0 && diagnostics.at(0).location.column != 0;
}

static bool test_valid_programs()
{
    const char* primary =
        "int sum_to(int n) { int total = 0; int i = 1; "
        "while (i <= n) { total = total + i; i = i + 1; } return total; }\n"
        "int double_value(int value) { return value * 2; }\n"
        "int gx_main(gx_app_context* ctx) { return double_value(sum_to(6)); }\n";
    Diagnostics diagnostics;
    if (!compile_unit(primary, &g_first, &diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "primary diagnostic: %s\n", diagnostics.at(i).message);
        return require(false, "primary function program compiles");
    }
    if (!require(g_first.unit.functionCount == 3 && g_first.unit.entryFunction == 2,
                 "source-order function table selects gx_main at the end")) return false;
    if (!require(g_first.unit.functions[0].parameterCount == 1 &&
                 g_first.unit.functions[1].parameterCount == 1 &&
                 g_first.unit.functions[2].usesAppContext,
                 "parameter metadata is retained per function")) return false;
    if (!require(execute_entry(g_first, 42), "primary nested call executes with result 42")) return false;
    uint32_t calls = 0;
    bool forward = false;
    bool backward = false;
    if (!require(inspect_calls(g_first, &calls, &forward, &backward) && calls == 2,
                 "primary program contains two direct call opcodes")) return false;

    const char* zeroArg =
        "int answer() { return 42; }\n"
        "int gx_main(gx_app_context* ctx) { return answer(); }\n";
    if (!require(compile_unit(zeroArg, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "zero-argument function executes")) return false;

    const char* oneArg =
        "int double_value(int x) { return x * 2; }\n"
        "int gx_main(gx_app_context* ctx) { return double_value(21); }\n";
    if (!require(compile_unit(oneArg, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "one-argument function executes")) return false;

    const char* multiArg =
        "int add(int a, int b) { return a + b; }\n"
        "int gx_main(gx_app_context* ctx) { return add(20, 22); }\n";
    if (!require(compile_unit(multiArg, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "multi-argument function executes")) return false;

    const char* fourArg =
        "int add4(int a, int b, int c, int d) { return a + b + c + d; }\n"
        "int gx_main(gx_app_context* ctx) { return add4(10, 11, 12, 9); }\n";
    if (!require(compile_unit(fourArg, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "four-argument MS x64 register call executes")) return false;

    const char* expression =
        "int twenty() { return 20; } int eleven() { return 11; }\n"
        "int gx_main(gx_app_context* ctx) { return twenty() + eleven() * 2; }\n";
    if (!require(compile_unit(expression, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "calls compose inside arithmetic expressions")) return false;

    const char* condition =
        "int is_answer(int x) { return x == 42; }\n"
        "int gx_main(gx_app_context* ctx) { if (is_answer(42)) { return 42; } return 0; }\n";
    if (!require(compile_unit(condition, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "call result works in a condition")) return false;

    const char* isolation =
        "int first() { int value = 40; return value; }\n"
        "int second() { int value = 2; return value; }\n"
        "int modify(int x) { x = x + 1; return x; }\n"
        "int gx_main(gx_app_context* ctx) { int value = 41; int result = modify(value); "
        "return first() + second() + value + result - 41; }\n";
    if (!require(compile_unit(isolation, &g_first, &diagnostics) && execute_entry(g_first, 84),
                 "function-local and parameter storage are isolated")) return false;

    const char* loopControl =
        "int calculate() { int i = 0; int total = 0; while (i < 10) { i = i + 1; "
        "if (i < 3) { continue; } if (i > 8) { break; } total = total + i; } "
        "return total + 9; }\n"
        "int gx_main(gx_app_context* ctx) { return calculate(); }\n";
    if (!require(compile_unit(loopControl, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "non-entry function preserves if, while, break, and continue")) return false;

    const char* order =
        "int helper() { return 40; }\n"
        "int gx_main(gx_app_context* ctx) { return answer() + helper(); }\n"
        "int answer() { return 2; }\n";
    if (!require(compile_unit(order, &g_first, &diagnostics) && g_first.unit.entryFunction == 1 &&
                 g_first.entryCodeOffset != 0 && execute_entry(g_first, 42),
                 "gx_main entry remains correct when placed in the middle")) return false;
    if (!require(inspect_calls(g_first, &calls, &forward, &backward) && calls == 2 && forward && backward,
                 "forward and backward direct call fixups are both emitted")) return false;
    if (!require(forward && backward, "call rel32 displacements have both directions")) return false;
    return true;
}

static bool test_diagnostics_and_limits()
{
    if (!require(expect_rejected(
            "int add(int x, int x) { return x; } int gx_main(gx_app_context* ctx) { return 0; }",
            "duplicate parameter 'x'"), "duplicate parameter diagnostic")) return false;
    if (!require(expect_rejected(
            "int add() { return 1; } int add() { return 2; } int gx_main(gx_app_context* ctx) { return 0; }",
            "duplicate function 'add'"), "duplicate function diagnostic")) return false;
    if (!require(expect_rejected(
            "int gx_main(gx_app_context* ctx) { return missing(42); }",
            "unknown function 'missing'"), "unknown function diagnostic")) return false;
    if (!require(expect_rejected(
            "int add(int a, int b) { return a + b; } int gx_main(gx_app_context* ctx) { return add(1); }",
            "function 'add' expects 2 arguments, got 1"), "argument-count diagnostic")) return false;
    if (!require(expect_rejected(
            "int broken(int x) { if (x) { return 42; } } int gx_main(gx_app_context* ctx) { return 0; }",
            "function 'broken' may reach end without returning a value"), "per-function missing-return diagnostic")) return false;
    if (!require(expect_rejected(
            "int gx_main(gx_app_context* ctx) { return gx_main(1); }",
            "gx_main is not callable from source"), "gx_main call rejection")) return false;
    if (!require(expect_rejected(
            "int bad(int a, int b, int c, int d, int e) { return a; } int gx_main(gx_app_context* ctx) { return 0; }",
            "function parameter limit exceeded"), "parameter capacity rejection")) return false;

    std::string tooManyFunctions;
    for (uint32_t i = 0; i < COMPILER_MAX_FUNCTIONS; ++i) {
        char name[32] = {};
        std::snprintf(name, sizeof(name), "int f%u() { return %u; }\n", i, i);
        tooManyFunctions += name;
    }
    tooManyFunctions += "int gx_main(gx_app_context* ctx) { return f0(); }\n";
    if (!require(expect_rejected(tooManyFunctions.c_str(), "function capacity exceeded"),
                 "function-table capacity rejection")) return false;

    const char* valid = "int answer() { return 42; } int gx_main(gx_app_context* ctx) { return answer(); }";
    Diagnostics diagnostics;
    if (!require(compile_unit(valid, &g_first, &diagnostics), "valid source for output-capacity test")) return false;
    uint8_t tiny[8] = {};
    uint32_t tinyBytes = 0;
    uint32_t entryOffset = 0;
    if (!require(!amd64::emit_translation_unit(g_first.unit, 0, tiny, sizeof(tiny),
                                               &tinyBytes, &entryOffset),
                 "code-capacity rejection")) return false;
    return true;
}

static bool test_recursion_and_safety()
{
    const uint8_t guardCompare[] = {0x41, 0x81, 0xFE};
    const uint8_t guardBranch[] = {0x0F, 0x83};
    const uint8_t guardIncrement[] = {0x41, 0xFF, 0xC6};

    const char* direct =
        "int sum_down(int n) { if (n <= 0) { return 0; } return n + sum_down(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return sum_down(6) * 2; }\n";
    Diagnostics diagnostics;
    if (!require(compile_unit(direct, &g_first, &diagnostics), "direct recursion compiles")) return false;
    GeneratedCallResult directResult = {};
    if (!require(execute_entry(g_first, 42, &directResult) && directResult.runtimeFailure == 0,
                 "direct recursion returns 42")) return false;
    if (!require(g_first.unit.recursiveFunction[0] && g_first.unit.recursiveSccCount == 1,
                 "direct recursion is classified as one recursive SCC")) return false;
    uint32_t directCalls = 0;
    bool directForward = false, directBackward = false;
    if (!require(inspect_calls(g_first, &directCalls, &directForward, &directBackward) &&
                 directCalls == 2 && directBackward && has_call_target(g_first, 0),
                 "direct recursion contains a real backward self-call")) return false;
    if (!require(contains_bytes(g_first, guardCompare, sizeof(guardCompare)) &&
                 contains_bytes(g_first, guardBranch, sizeof(guardBranch)) &&
                 contains_bytes(g_first, guardIncrement, sizeof(guardIncrement)),
                 "direct user calls contain the runtime depth guard")) return false;

    const char* localIsolation =
        "int sum_copy(int n) { int current = n; if (n <= 0) { return 0; } "
        "return current + sum_copy(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return sum_copy(6) * 2; }\n";
    if (!require(compile_unit(localIsolation, &g_first, &diagnostics) &&
                 execute_entry(g_first, 42), "recursive local storage is isolated")) return false;

    const char* parameterIsolation =
        "int descend(int n) { if (n == 0) { return 42; } n = n - 1; return descend(n); }\n"
        "int gx_main(gx_app_context* ctx) { return descend(6); }\n";
    if (!require(compile_unit(parameterIsolation, &g_first, &diagnostics) &&
                 execute_entry(g_first, 42), "recursive parameter storage is isolated")) return false;

    const char* mutual =
        "int is_even(int n) { if (n == 0) { return 1; } return is_odd(n - 1); }\n"
        "int is_odd(int n) { if (n == 0) { return 0; } return is_even(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return is_even(42) * 42; }\n";
    if (!require(compile_unit(mutual, &g_first, &diagnostics), "mutual recursion compiles")) return false;
    if (!require(execute_entry(g_first, 42), "mutual recursion returns 42")) return false;
    if (!require(g_first.unit.recursiveFunction[0] && g_first.unit.recursiveFunction[1] &&
                 g_first.unit.recursiveSccCount == 1,
                 "mutual recursion is classified as one recursive SCC")) return false;
    uint32_t mutualCalls = 0;
    bool mutualForward = false, mutualBackward = false;
    if (!require(inspect_calls(g_first, &mutualCalls, &mutualForward, &mutualBackward) &&
                 mutualCalls == 3 && mutualForward && mutualBackward,
                 "mutual recursion contains forward and backward rel32 calls")) return false;

    const char* controlFlow =
        "int recursive_answer(int n) { if (n <= 0) { return 0; } "
        "if (n == 6) { return n + recursive_answer(n - 1); } "
        "return n + recursive_answer(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return recursive_answer(6) * 2; }\n";
    if (!require(compile_unit(controlFlow, &g_first, &diagnostics) &&
                 execute_entry(g_first, 42), "recursion composes with if control flow")) return false;

    const char* loop =
        "int loop_recursive(int n) { int i = 0; while (i < 1) { i = i + 1; } "
        "if (n == 0) { return 36; } return loop_recursive(n - 1) + 1; }\n"
        "int gx_main(gx_app_context* ctx) { return loop_recursive(6); }\n";
    if (!require(compile_unit(loop, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "recursion composes with while loops")) return false;

    const char* nested =
        "int add_one(int x) { return x + 1; }\n"
        "int count_down(int n) { if (n == 0) { return 36; } "
        "return count_down(n - 1) + add_one(0); }\n"
        "int gx_main(gx_app_context* ctx) { return count_down(6); }\n";
    if (!require(compile_unit(nested, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "recursive nested helper calls return 42")) return false;

    const char* callExpression =
        "int sum_down(int n) { if (n <= 0) { return 0; } int value = sum_down(n - 1); "
        "return value + n; }\n"
        "int gx_main(gx_app_context* ctx) { return sum_down(6) * 2; }\n";
    if (!require(compile_unit(callExpression, &g_first, &diagnostics) &&
                 execute_entry(g_first, 42), "recursive call expression returns 42")) return false;

    amd64::FrameLayout maximum = {};
    if (!require(amd64::calculate_frame_layout(COMPILER_MAX_PARAMETERS, COMPILER_MAX_LOCALS,
                                               COMPILER_MAX_TEMPORARY_SLOTS, false,
                                               COMPILER_MAX_TRANSIENT_STACK_BYTES, &maximum) &&
                 maximum.frameBytes == COMPILER_MAX_GENERATED_FRAME_BYTES &&
                 maximum.transientBytes == COMPILER_MAX_TRANSIENT_STACK_BYTES &&
                 maximum.activationBytes == COMPILER_MAX_GENERATED_ACTIVATION_STACK_COST &&
                 COMPILER_MAX_RUNTIME_CALL_DEPTH == 90,
                 "recursive stack accounting matches the shared safety policy")) return false;

    const char* boundary =
        "int recurse(int n) { if (n == 0) { return 42; } return recurse(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return recurse(88); }\n";
    if (!require(compile_unit(boundary, &g_first, &diagnostics), "safe depth boundary compiles")) return false;
    GeneratedCallResult boundaryResult = {};
    if (!require(execute_entry(g_first, 42, &boundaryResult) && boundaryResult.runtimeFailure == 0,
                 "depth below the derived limit succeeds")) return false;

    const char* overBoundary =
        "int recurse(int n) { if (n == 0) { return 42; } return recurse(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return recurse(89); }\n";
    if (!require(compile_unit(overBoundary, &g_first, &diagnostics), "excess boundary source compiles")) return false;
    GeneratedCallResult overBoundaryResult = {};
    if (!require(execute_entry(g_first, 0, &overBoundaryResult) && overBoundaryResult.runtimeFailure != 0 &&
                 overBoundaryResult.runtimeDepth == COMPILER_MAX_RUNTIME_CALL_DEPTH,
                 "one activation beyond the limit fails through the software guard")) return false;

    const char* excessive =
        "int recurse(int n) { if (n == 0) { return 42; } return recurse(n - 1); }\n"
        "int gx_main(gx_app_context* ctx) { return recurse(1000000); }\n";
    if (!require(compile_unit(excessive, &g_first, &diagnostics), "extremely deep recursion compiles")) return false;
    GeneratedCallResult excessiveResult = {};
    if (!require(execute_entry(g_first, 0, &excessiveResult) && excessiveResult.runtimeFailure != 0 &&
                 excessiveResult.runtimeDepth == COMPILER_MAX_RUNTIME_CALL_DEPTH,
                 "extremely deep recursion fails deterministically before stack exhaustion")) return false;
    if (!require(compile_unit(direct, &g_first, &diagnostics) && execute_entry(g_first, 42),
                 "runtime state resets after call-depth failure")) return false;
    for (uint32_t run = 0; run < 3; ++run)
        if (!require(execute_entry(g_first, 42), "repeated recursive run succeeds")) return false;
    return true;
}

static bool test_determinism()
{
    const char* source =
        "int add(int a, int b) { return a + b; }\n"
        "int gx_main(gx_app_context* ctx) { return add(19, 23); }\n";
    Diagnostics firstDiagnostics;
    Diagnostics secondDiagnostics;
    if (!require(compile_unit(source, &g_first, &firstDiagnostics) &&
                 compile_unit(source, &g_second, &secondDiagnostics),
                 "identical multi-function sources compile twice")) return false;
    return require(g_first.codeBytes == g_second.codeBytes &&
                   g_first.elfBytes == g_second.elfBytes &&
                   std::memcmp(g_first.code, g_second.code, g_first.codeBytes) == 0 &&
                   std::memcmp(g_first.elf, g_second.elf, g_first.elfBytes) == 0,
                   "multi-function code and NativeElf bytes are deterministic");
}

} // namespace

int main()
{
    if (!test_valid_programs() || !test_diagnostics_and_limits() ||
        !test_recursion_and_safety() || !test_determinism()) return 1;
    std::puts("recursive stack accounting = PASS");
    std::puts("compiler_functions_host_test: PASS");
    return 0;
}
