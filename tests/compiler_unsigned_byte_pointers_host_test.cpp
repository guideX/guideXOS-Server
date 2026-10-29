// Hosted COMP4J qualification for typed unsigned-byte pointers.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/compiler_object.h"
#include "core/compiler/compiler_parser.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static Token g_tokens[COMPILER_MAX_TOKENS] = {};
static TranslationUnitIR g_unit = {};
static uint8_t g_code[COMPILER_MAX_CODE_BYTES] = {};

static uint32_t g_assertions = 0;

static bool require(bool condition, const char* message)
{
    ++g_assertions;
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        if (std::strstr(diagnostics.at(i).message, needle)) return true;
    return false;
}

static bool parse_source(const char* source, Diagnostics& diagnostics)
{
    g_unit = {};
    const uint32_t bytes = static_cast<uint32_t>(std::strlen(source));
    uint32_t tokenCount = 0;
    return lex_source(source, bytes, g_tokens, COMPILER_MAX_TOKENS, &tokenCount, diagnostics) &&
        parse_translation_unit(source, g_tokens, tokenCount, &g_unit, diagnostics);
}

struct CallResult { int32_t value; uint32_t status; };

static CallResult call_entry(void* entry)
{
    CallResult result = {};
#if defined(__GNUC__) || defined(__clang__)
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "movq %[output], 32(%%rsp)\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "movq 32(%%rsp), %%r11\n\t"
        "movl %%r15d, 4(%%r11)\n\t"
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(result.value)
        : [entry] "r"(entry), [output] "r"(&result)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    typedef int32_t (*Entry)(void*);
    result.value = reinterpret_cast<Entry>(entry)(nullptr);
#endif
    return result;
}

static CallResult execute_generated(uint32_t codeBytes, uint32_t entryOffset)
{
#if defined(_WIN32)
    void* memory = VirtualAlloc(nullptr, codeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) return {};
    std::memcpy(memory, g_code, codeBytes);
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(memory, codeBytes, PAGE_EXECUTE_READ, &oldProtection) != 0;
    FlushInstructionCache(GetCurrentProcess(), memory, codeBytes);
    const CallResult result = executable
        ? call_entry(static_cast<uint8_t*>(memory) + entryOffset) : CallResult();
    VirtualFree(memory, 0, MEM_RELEASE);
    return result;
#else
    return call_entry(g_code + entryOffset);
#endif
}

static bool compile_and_run(const char* source, int32_t expected,
                            uint32_t expectedStatus = 0, uint32_t* codeBytesOut = nullptr)
{
    Diagnostics diagnostics;
    if (!parse_source(source, diagnostics)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "COMP4J diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    uint32_t codeBytes = 0;
    uint32_t entryOffset = 0;
    if (!amd64::emit_translation_unit(g_unit, 0, g_code, sizeof(g_code), &codeBytes, &entryOffset)) {
        std::fprintf(stderr, "COMP4J AMD64 emission failed\n");
        return false;
    }
    if (codeBytesOut) *codeBytesOut = codeBytes;
    const CallResult result = execute_generated(codeBytes, entryOffset);
    if (result.value != expected || (result.status & 0xFFU) != expectedStatus)
        std::fprintf(stderr, "COMP4J run mismatch: got %d status=%u expected %d status=%u\n",
                     result.value, result.status & 0xFFU, expected, expectedStatus);
    return result.value == expected && (result.status & 0xFFU) == expectedStatus;
}

static bool rejected(const char* source, const char* diagnostic)
{
    Diagnostics diagnostics;
    const bool accepted = parse_source(source, diagnostics);
    if (accepted || !diagnostic_contains(diagnostics, diagnostic)) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "COMP4J expected diagnostic: %s\n", diagnostics.at(i).message);
        return false;
    }
    return true;
}

static bool contains_bytes(const uint8_t* bytes, uint32_t count,
                           const uint8_t* needle, uint32_t needleCount)
{
    if (!bytes || !needle || needleCount == 0 || needleCount > count) return false;
    for (uint32_t i = 0; i <= count - needleCount; ++i)
        if (std::memcmp(bytes + i, needle, needleCount) == 0) return true;
    return false;
}

static bool compile_module(const char* path, const char* source, CompiledModule* module,
                           Diagnostics& diagnostics)
{
    return compile_module_from_source(path, source, static_cast<uint32_t>(std::strlen(source)),
                                      module, diagnostics);
}

static bool positive_machine_code()
{
    uint32_t codeBytes = 0;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char value = 255; "
            "unsigned char* p = &value; *p = 456; return *p; }", 200),
            "address-of scalar, dereference, byte truncation, and zero extension execute")) return false;
    const char* buffers =
        "int gx_main(gx_app_context* ctx) { int left = 123456; unsigned char bytes[4]; "
        "int right = 654321; unsigned char* p = bytes; p[0] = 12; p[1] = 255; "
        "return left + right + p[0] + p[1]; }";
    if (!require(compile_and_run(buffers, 778044, 0, &codeBytes),
                 "array decay, byte indexing, truncation, and adjacent sentinels execute")) return false;
    static const uint8_t zeroExtend[] = {0x0F, 0xB6, 0x02}; // movzx eax, byte ptr [rdx]
    static const uint8_t byteStore[] = {0x88, 0x02};        // mov byte ptr [rdx], al
    static const uint8_t scaleOne[] = {0x48, 0x69, 0xD2, 0x01, 0x00, 0x00, 0x00};
    if (!require(contains_bytes(g_code, codeBytes, zeroExtend, sizeof(zeroExtend)),
                 "generated AMD64 contains zero-extending byte loads")) return false;
    if (!require(contains_bytes(g_code, codeBytes, byteStore, sizeof(byteStore)),
                 "generated AMD64 contains one-byte stores")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[4]; bytes[0] = 1; bytes[1] = 42; "
            "unsigned char* p = bytes; p = p + 1; return *p; }", 42, 0, &codeBytes),
            "typed unsigned-byte pointer addition works with one-byte scale")) return false;
    if (!require(contains_bytes(g_code, codeBytes, scaleOne, sizeof(scaleOne)),
                 "generated AMD64 scales unsigned-byte pointer steps by one")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[4]; "
            "unsigned char* p = bytes; p[2] = 511; p[3] = 256; return p[2] + p[3]; }", 255),
            "unsigned-byte indexed stores truncate to one byte")) return false;

    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { char s[1]; unsigned char u = 255; "
            "s[0] = 255; return s[0] + u; }", 254),
            "signed char arrays sign extend while unsigned bytes zero extend")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char data[4]; "
            "unsigned char* p = data; int i = 0; int total = 0; "
            "while (i < 4) { p[i] = i + 252; total = total + p[i]; i = i + 1; } "
            "return total; }", 1014),
            "variable pointer indices and repeated writes execute inside the bounded array")) return false;
    if (!require(compile_and_run(
            "int first_byte(unsigned char* data) { return data[0]; } "
            "int relay(unsigned char* data) { return first_byte(data); } "
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; bytes[0] = 255; "
            "return relay(bytes); }", 255),
            "typed pointer parameters, nested calls, and AMD64 descriptor argument passing execute")) return false;
    if (!require(compile_and_run(
            "int set_byte(unsigned char* data, int index) { data[index] = 511; return data[index]; } "
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; bytes[0] = 12; "
            "int value = set_byte(bytes, 1); return bytes[1] + value; }", 510),
            "callee-owned pointer descriptors allow checked byte writes into the caller's buffer")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[3]; bytes[0] = 200; bytes[1] = 0; "
            "unsigned char* p = &bytes[0]; p = p + 1; *p = 41; "
            "p = p - 1; p[1] = 41; return bytes[1] + *p; }", 241),
            "typed one-byte pointer arithmetic advances and retreats one byte")) return false;
    return true;
}

static bool negative_and_bounds()
{
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { int words[2]; unsigned char* p = words; return 0; }",
            "cannot initialize unsigned char*"), "int* and unsigned char* are incompatible")) return false;
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; "
            "unsigned char* p = bytes; int* q = p; return 0; }",
            "cannot initialize int*"), "unsigned char* does not convert to int*")) return false;
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { return *1; }", "cannot dereference non-pointer expression"),
            "invalid dereference operand is diagnosed")) return false;
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; return bytes[2]; }",
            "array index is out of bounds"), "constant array index outside the object is rejected")) return false;
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; "
            "unsigned char* p = bytes; return p[2]; }",
            "array index is out of bounds"), "constant pointer index is rejected when array provenance is known")) return false;
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { unsigned int* p; return 0; }",
            "only unsigned char is supported"), "malformed unsigned pointer base type is rejected")) return false;
    if (!require(rejected(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[65]; return 0; }",
            "array length exceeds the bounded element limit"),
            "unsigned-byte arrays preserve the existing local element limit")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; "
            "unsigned char* p = bytes; int i = 2; p[i] = 90; return 42; }",
            0, COMPILER_RUNTIME_STATUS_INVALID_POINTER),
            "runtime descriptor rejects a one-past byte store without touching adjacent storage")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char bytes[2]; "
            "unsigned char* p = bytes; p = p + 3; return 42; }",
            0, COMPILER_RUNTIME_STATUS_POINTER_BOUNDS),
            "byte pointer arithmetic rejects positions beyond one-past")) return false;
    if (!require(compile_and_run(
            "int gx_main(gx_app_context* ctx) { unsigned char* p; return *p; }",
            0, COMPILER_RUNTIME_STATUS_INVALID_POINTER),
            "uninitialized unsigned-byte pointer fails safely at runtime")) return false;
    return true;
}

static bool module_and_object_compatibility()
{
    static CompiledModule modules[2] = {};
    Diagnostics mainDiagnostics;
    Diagnostics providerDiagnostics;
    const char* caller =
        "int read_first(unsigned char* data); "
        "int gx_main(gx_app_context* ctx) { unsigned char bytes[1]; bytes[0] = 255; "
        "return read_first(bytes); }";
    const char* provider = "int read_first(unsigned char* data) { return data[0]; }";
    if (!require(compile_module("src/main.cpp", caller, &modules[0], mainDiagnostics) &&
                 compile_module("src/read.cpp", provider, &modules[1], providerDiagnostics),
                 "cross-module unsigned-byte pointer caller and provider compile")) return false;
    static LinkedProgram linked = {};
    Diagnostics linkDiagnostics;
    if (!require(link_modules(modules, 2, &linked, linkDiagnostics),
                 "cross-module unsigned-byte pointer signatures link")) return false;

    const char* mismatchCaller =
        "int read_first(char* data); int gx_main(gx_app_context* ctx) { return read_first(\"x\"); }";
    const char* mismatchProvider = "int read_first(unsigned char* data) { return data[0]; }";
    Diagnostics mismatchCallerDiagnostics;
    Diagnostics mismatchProviderDiagnostics;
    static CompiledModule mismatchModules[2] = {};
    if (!require(compile_module("src/main.cpp", mismatchCaller, &mismatchModules[0], mismatchCallerDiagnostics) &&
                 compile_module("src/read.cpp", mismatchProvider, &mismatchModules[1], mismatchProviderDiagnostics),
                 "signed and unsigned character pointer mismatch fixtures compile independently")) return false;
    Diagnostics mismatchDiagnostics;
    const bool mismatchRejected = !link_modules(mismatchModules, 2, &linked, mismatchDiagnostics);
    if (!mismatchRejected || !diagnostic_contains(mismatchDiagnostics, "conflicting declaration"))
        for (uint32_t i = 0; i < mismatchDiagnostics.count(); ++i)
            std::fprintf(stderr, "COMP4J link diagnostic: %s\n", mismatchDiagnostics.at(i).message);
    if (!require(mismatchRejected && diagnostic_contains(mismatchDiagnostics, "conflicting declaration"),
                 "cross-module signed and unsigned pointer signatures remain distinct")) return false;

    uint8_t object[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t objectBytes = 0;
    if (!require(serialize_gxo_object(modules[1], object, sizeof(object), &objectBytes),
                 "unsigned-byte pointer module serializes")) return false;
    CompiledModule restored = {};
    Diagnostics objectDiagnostics;
    if (!require(deserialize_gxo_object(object, objectBytes, &restored, objectDiagnostics) &&
                 restored.exports[0].parameterKinds[0] == ParameterKind::UnsignedCharacterPointer,
                 "object round trip preserves unsigned-byte pointer signature type")) return false;
    GxoObjectHeaderView header = {};
    Diagnostics headerDiagnostics;
    if (!require(inspect_gxo_header(object, objectBytes, &header, headerDiagnostics) &&
                 header.formatVersion == 2 && header.compilerObjectAbiVersion == 17,
                 "object format 2 records the COMP4M internal ABI 17")) return false;

    uint8_t incompatible[COMPILER_MAX_OBJECT_BYTES] = {};
    std::memcpy(incompatible, object, objectBytes);
    const uint32_t abiOffset = header.metaOffset + 16U;
    incompatible[abiOffset] = 16;
    incompatible[abiOffset + 1U] = incompatible[abiOffset + 2U] = incompatible[abiOffset + 3U] = 0;
    const uint64_t checksum = elf_object_checksum(incompatible, objectBytes, header.metaOffset + 112U);
    for (uint32_t i = 0; i < 8; ++i)
        incompatible[header.metaOffset + 112U + i] = static_cast<uint8_t>(checksum >> (8U * i));
    CompiledModule rejectedObject = {};
    Diagnostics incompatibleDiagnostics;
    if (!require(!deserialize_gxo_object(incompatible, objectBytes, &rejectedObject, incompatibleDiagnostics) &&
                 diagnostic_contains(incompatibleDiagnostics, "compiler identity"),
                 "cached object with pre-COMP4M ABI 16 is rejected")) return false;
    return true;
}

} // namespace

int main()
{
    if (!positive_machine_code() || !negative_and_bounds() || !module_and_object_compatibility()) return 1;
    std::printf("COMP4J assertions=%u result=PASS\n", g_assertions);
    return 0;
}
