// COMP4H hosted qualification for byte-accurate mixed-width struct fields.

#include "core/compiler/compiler_diagnostics.h"
#include "core/compiler/compiler_lexer.h"
#include "core/compiler/compiler_parser.h"
#include "core/compiler/compiler_module.h"
#include "core/compiler/compiler_linker.h"
#include "core/compiler/compiler_object.h"
#include "arch/amd64/compiler_backend.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace kernel::compiler;

namespace {

static uint32_t g_assertions = 0;

static bool require(bool value, const char* message)
{
    ++g_assertions;
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

static bool diagnostic_contains(const Diagnostics& diagnostics, const char* needle)
{
    for (uint32_t i = 0; i < diagnostics.count(); ++i)
        if (std::strstr(diagnostics.at(i).message, needle)) return true;
    return false;
}

static bool parse_text(const char* source, TranslationUnitIR* unit, Diagnostics* diagnostics)
{
    Token tokens[COMPILER_MAX_TOKENS] = {};
    uint32_t tokenCount = 0;
    return lex_source(source, static_cast<uint32_t>(std::strlen(source)), tokens,
                      COMPILER_MAX_TOKENS, &tokenCount, *diagnostics) &&
        parse_translation_unit(source, tokens, tokenCount, unit, *diagnostics);
}

struct CallResult { int32_t value; uint32_t status; };

static CallResult invoke(void* entry)
{
    CallResult result = {};
#if defined(__GNUC__) || defined(__clang__)
    asm volatile(
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "subq $64, %%rsp\n\t"
        "movq %[out], 32(%%rsp)\n\t"
        "xorq %%rcx, %%rcx\n\t"
        "call *%[entry]\n\t"
        "movq 32(%%rsp), %%r11\n\t"
        "movl %%r15d, 4(%%r11)\n\t"
        "addq $64, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        : "=a"(result.value)
        : [entry] "r"(entry), [out] "r"(&result)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
#else
    result.value = reinterpret_cast<int32_t (*)(void*)>(entry)(nullptr);
#endif
    return result;
}

static bool compile(const char* source, TranslationUnitIR* unit, uint8_t* code,
                    uint32_t* codeBytes, uint32_t* entryOffset, Diagnostics* diagnostics)
{
    if (!unit || !code || !codeBytes || !entryOffset || !diagnostics ||
        !parse_text(source, unit, diagnostics)) return false;
    return amd64::emit_translation_unit(*unit, 0, code, COMPILER_MAX_CODE_BYTES,
                                         codeBytes, entryOffset);
}

static bool compile_and_run(const char* source, int32_t expected,
                            TranslationUnitIR* parsed = nullptr,
                            uint8_t* generatedCode = nullptr,
                            uint32_t* generatedCodeBytes = nullptr)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0;
    uint32_t entryOffset = 0;
    if (!compile(source, &unit, code, &codeBytes, &entryOffset, &diagnostics)) return false;
#if defined(_WIN32)
    DWORD oldProtection = 0;
    if (!VirtualProtect(code, sizeof(code), PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
#endif
    const CallResult result = invoke(code + entryOffset);
    if (parsed) *parsed = unit;
    if (generatedCode && generatedCodeBytes) {
        std::memcpy(generatedCode, code, codeBytes);
        *generatedCodeBytes = codeBytes;
    }
    return result.value == expected && result.status == 0;
}

static bool has_bytes(const uint8_t* bytes, uint32_t count,
                      const uint8_t* pattern, uint32_t patternBytes)
{
    if (!bytes || !pattern || patternBytes == 0 || patternBytes > count) return false;
    for (uint32_t i = 0; i <= count - patternBytes; ++i)
        if (std::memcmp(bytes + i, pattern, patternBytes) == 0) return true;
    return false;
}

static bool rejected(const char* source, const char* expectedMessage)
{
    TranslationUnitIR unit = {};
    Diagnostics diagnostics;
    return !parse_text(source, &unit, &diagnostics) &&
        diagnostic_contains(diagnostics, expectedMessage) && diagnostics.count() != 0 &&
        diagnostics.at(0).location.line != 0 && diagnostics.at(0).location.column != 0;
}

static bool execution_and_layout()
{
    const char* mixed =
        "struct Record { char status; int value; char flag; }; "
        "int gx_main(gx_app_context* ctx) { struct Record r; "
        "r.status = 'A'; r.value = 27; r.flag = 'Z'; "
        "return r.value + r.status - r.flag; }";
    TranslationUnitIR unit = {};
    uint8_t code[COMPILER_MAX_CODE_BYTES] = {};
    uint32_t codeBytes = 0;
    if (!require(compile_and_run(mixed, 2, &unit, code, &codeBytes),
                 "mixed char/int fields execute with the expected value")) return false;
    const StructTypeIR& record = unit.structTypes[0];
    if (!require(record.fieldCount == 3 && record.fields[0].kind == StructFieldKind::Char8 &&
                 record.fields[0].offset == 0 && record.fields[0].sizeBytes == 1 &&
                 record.fields[1].kind == StructFieldKind::Int32 && record.fields[1].offset == 4 &&
                 record.fields[1].sizeBytes == 4 && record.fields[2].offset == 8 &&
                 record.sizeBytes == 12 && record.alignment == 4,
                 "mixed fields use conventional offsets, padding, size, and alignment")) return false;
    const uint8_t charLoad[] = {0x0F, 0xBE, 0x00};
    const uint8_t intLoad[] = {0x8B, 0x00};
    const uint8_t charStore[] = {0x88, 0x02};
    const uint8_t intStore[] = {0x89, 0x02};
    if (!require(has_bytes(code, codeBytes, charLoad, sizeof(charLoad)) &&
                 has_bytes(code, codeBytes, intLoad, sizeof(intLoad)) &&
                 has_bytes(code, codeBytes, charStore, sizeof(charStore)) &&
                 has_bytes(code, codeBytes, intStore, sizeof(intStore)),
                 "generated AMD64 uses signed byte loads, byte stores, and dword int access")) return false;

    const char* compact =
        "struct Pair { char first; char second; }; "
        "struct Tail { int number; char marker; char last; }; "
        "int gx_main(gx_app_context* ctx) { struct Pair a; struct Pair b; struct Tail t; "
        "a.first = 'A'; a.second = 'B'; b.first = 'C'; b.second = 'D'; "
        "t.number = 1000; t.marker = 'X'; t.last = 'Y'; "
        "return a.first + a.second + b.first + b.second + t.number + t.marker + t.last; }";
    unit = {};
    if (!require(compile_and_run(compact, 1443, &unit),
                 "consecutive char fields, int-first layout, and distinct instances execute")) return false;
    if (!require(unit.structTypes[0].fields[1].offset == 1 && unit.structTypes[0].sizeBytes == 2 &&
                 unit.structTypes[0].alignment == 1 && unit.structTypes[1].fields[1].offset == 4 &&
                 unit.structTypes[1].fields[2].offset == 5 && unit.structTypes[1].sizeBytes == 8,
                 "char-only and int-first layouts use their field widths and tail padding")) return false;

    const char* preservation =
        "struct Record { char left; char narrow; int value; char right; }; "
        "int gx_main(gx_app_context* ctx) { struct Record r; "
        "r.left = 65; r.narrow = 66; r.value = 27; r.right = 90; "
        "r.narrow = 258; return r.left + r.narrow + r.value + r.right; }";
    if (!require(compile_and_run(preservation, 184),
                 "byte truncation preserves adjacent char and int fields at runtime")) return false;

    const char* signedPromotion =
        "struct Cell { char value; }; "
        "int gx_main(gx_app_context* ctx) { struct Cell c; c.value = 255; "
        "if (c.value != -1) return 90; c.value = -129; "
        "return c.value + 130; }";
    if (!require(compile_and_run(signedPromotion, 257),
                 "signed char promotion and negative-value truncation execute correctly")) return false;

    const char* structArray =
        "struct Item { char tag; int value; }; "
        "int gx_main(gx_app_context* ctx) { struct Item items[2]; struct Item* p; "
        "items[0].tag = 'A'; items[0].value = 10; "
        "items[1].tag = 'B'; items[1].value = 20; p = &items[0]; p = p + 1; "
        "return p->tag + p->value; }";
    if (!require(compile_and_run(structArray, 86),
                 "mixed-width struct arrays and scaled typed pointers execute")) return false;

    const char* byteStructArray =
        "struct Byte { char value; }; "
        "int gx_main(gx_app_context* ctx) { struct Byte entries[3]; struct Byte* p; "
        "entries[0].value = 65; entries[1].value = 66; entries[2].value = 67; "
        "p = &entries[0]; p = p + 2; p->value = 255; return p->value; }";
    if (!require(compile_and_run(byteStructArray, -1),
                 "byte-aligned struct arrays preserve signed fields through pointer arithmetic")) return false;

    const char* calls =
        "struct Item { char status; int value; }; "
        "int read_status(struct Item* p) { return p->status; } "
        "char advance(char value) { return value + 1; } "
        "int gx_main(gx_app_context* ctx) { struct Item item; item.status = 'A'; "
        "item.value = 4; return read_status(&item) + advance(item.status) + item.value; }";
    if (!require(compile_and_run(calls, 135),
                 "char fields promote through supported struct-pointer and char-call paths")) return false;

    const char* zeroSizedTail =
        "struct Pair { char first; char second; }; "
        "int gx_main(gx_app_context* ctx) { struct Pair p; return p.first + p.second; }";
    if (!require(compile_and_run(zeroSizedTail, 0),
                 "small struct locals initialize without an over-wide tail store")) return false;
    return true;
}

static bool modules_and_objects()
{
    const char* providerSource =
        "struct Packet { char state; int value; }; "
        "int packet_total(struct Packet* p) { return p->state + p->value; }";
    const char* consumerSource =
        "struct Packet { char state; int value; }; int packet_total(struct Packet* p); "
        "int gx_main(gx_app_context* ctx) { struct Packet packet; "
        "packet.state = 255; packet.value = 42; return packet_total(&packet); }";
    CompiledModule modules[2] = {};
    Diagnostics diagnostics;
    if (!require(compile_module_from_source("src/provider.cpp", providerSource,
                 static_cast<uint32_t>(std::strlen(providerSource)), &modules[0], diagnostics),
                 "provider module with a char field compiles")) return false;
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/consumer.cpp", consumerSource,
                 static_cast<uint32_t>(std::strlen(consumerSource)), &modules[1], diagnostics),
                 "consumer module with the matching char-field layout compiles")) return false;

    uint8_t object[COMPILER_MAX_OBJECT_BYTES] = {};
    uint8_t repeated[COMPILER_MAX_OBJECT_BYTES] = {};
    uint32_t objectBytes = 0;
    uint32_t repeatedBytes = 0;
    if (!require(serialize_elf_object(modules[0], object, sizeof(object), &objectBytes),
                 "char-field compiler object serializes")) return false;
    ElfObjectHeaderView header = {};
    diagnostics = Diagnostics();
    if (!require(inspect_elf_object(object, objectBytes, &header, diagnostics) &&
                 header.formatVersion == 2 && header.compilerObjectAbiVersion == 13,
                 "physical object format stays v2 and semantic compiler ABI advances to 13")) return false;
    CompiledModule restored[2] = {};
    diagnostics = Diagnostics();
    if (!require(deserialize_elf_object(object, objectBytes, &restored[0], diagnostics) &&
                 restored[0].structTypes[0].fields[0].kind == StructFieldKind::Char8 &&
                 restored[0].structTypes[0].fields[1].offset == 4 &&
                 restored[0].structTypes[0].sizeBytes == 8,
                 "object round trip preserves byte field kind and mixed layout")) return false;
    object[header.metaOffset + 16U] = 12U;
    const uint64_t oldAbiChecksum = elf_object_checksum(object, objectBytes,
                                                        header.metaOffset + 112U);
    for (uint32_t i = 0; i < 8U; ++i)
        object[header.metaOffset + 112U + i] = static_cast<uint8_t>(oldAbiChecksum >> (i * 8U));
    diagnostics = Diagnostics();
    CompiledModule oldAbiRejected = {};
    if (!require(!deserialize_elf_object(object, objectBytes, &oldAbiRejected, diagnostics),
                 "ABI-12 cached objects are rejected by the ABI-13 compiler")) return false;
    if (!require(serialize_elf_object(modules[0], object, sizeof(object), &objectBytes) &&
                 serialize_elf_object(modules[0], repeated, sizeof(repeated), &repeatedBytes) &&
                 objectBytes == repeatedBytes && std::memcmp(object, repeated, objectBytes) == 0,
                 "repeated compilation serializes deterministically")) return false;
    diagnostics = Diagnostics();
    if (!require(serialize_elf_object(modules[1], object, sizeof(object), &objectBytes) &&
                 deserialize_elf_object(object, objectBytes, &restored[1], diagnostics),
                 "consumer object reopens before cross-module linking")) return false;
    LinkedProgram linked = {};
    diagnostics = Diagnostics();
    const bool linkedOk = link_modules(restored, 2, &linked, diagnostics);
    if (!linkedOk) {
        for (uint32_t i = 0; i < diagnostics.count(); ++i)
            std::fprintf(stderr, "COMP4H linker: %s\n", diagnostics.at(i).message);
    }
    if (!require(linkedOk && linked.structTypeCount == 1 &&
                 linked.structTypes[0].identity == restored[0].structTypes[0].identity,
                 "matching byte-accurate structure declarations link across modules")) return false;

    CompiledModule bad = modules[0];
    bad.structTypes[0].fields[1].offset = 1;
    if (!require(!serialize_elf_object(bad, object, sizeof(object), &objectBytes),
                 "overlapping or non-deterministic field metadata is rejected at serialization")) return false;

    const char* badOrderSource =
        "struct Packet { int value; char state; }; int packet_total(struct Packet* p) { return p->state; }";
    CompiledModule badOrder = {};
    diagnostics = Diagnostics();
    if (!require(compile_module_from_source("src/bad-layout.cpp", badOrderSource,
                 static_cast<uint32_t>(std::strlen(badOrderSource)), &badOrder, diagnostics),
                 "different field order compiles as an independent module")) return false;
    CompiledModule mismatched[2] = {restored[1], badOrder};
    diagnostics = Diagnostics();
    return require(!link_modules(mismatched, 2, &linked, diagnostics) &&
                   diagnostic_contains(diagnostics, "incompatible struct type"),
                   "cross-module layout mismatch is rejected");
}

static bool global_layout_and_diagnostics()
{
    const char* globalsSource =
        "struct Byte { char value; }; struct Byte first; int later; "
        "int gx_main(gx_app_context* ctx) { first.value = 65; later = 7; return first.value + later; }";
    CompiledModule module = {};
    Diagnostics diagnostics;
    if (!require(compile_module_from_source("src/globals.cpp", globalsSource,
                 static_cast<uint32_t>(std::strlen(globalsSource)), &module, diagnostics) &&
                 module.structTypeCount == 1 && module.structTypes[0].sizeBytes == 1 &&
                 module.structTypes[0].alignment == 1 && module.mutableDataBytes == 8 &&
                 module.exports[1].alignment == 1 && module.exports[2].moduleDataOffset == 4,
                 "byte-only global structure keeps one-byte layout and aligned following int global")) return false;
    LinkedProgram linked = {};
    diagnostics = Diagnostics();
    if (!require(link_modules(&module, 1, &linked, diagnostics) &&
                 linked.mutableDataBytes == 8 && linked.exportCount == 3,
                 "global byte structure and following int link with their recorded storage layout")) return false;

    if (!require(rejected("struct P { char x; char x; }; int gx_main(gx_app_context* c) { return 0; }",
                          "duplicate field"), "duplicate char fields retain located diagnostics")) return false;
    if (!require(rejected("struct P { char* text; }; int gx_main(gx_app_context* c) { return 0; }",
                          "pointer fields are not supported"), "char pointer fields are classified unsupported")) return false;
    if (!require(rejected("struct P { short value; }; int gx_main(gx_app_context* c) { return 0; }",
                          "struct fields must be int or char"), "unsupported field types are diagnosed")) return false;
    if (!require(rejected("struct P { char values[2]; }; int gx_main(gx_app_context* c) { return 0; }",
                          "array fields are not supported"), "array fields remain unsupported")) return false;
    if (!require(rejected("struct P { char value; }; int gx_main(gx_app_context* c) { struct P p; int* q = &p.value; return 0; }",
                          "address of char struct fields is unsupported"),
                 "char field addresses do not escape through int pointers")) return false;
    if (!require(rejected("struct P { char value; }; int gx_main(gx_app_context* c) { struct P p; int* q; p.value = q; return 0; }",
                          "require int expressions"), "invalid field assignment types are rejected")) return false;
    if (!require(rejected("struct P { struct Q value; }; int gx_main(gx_app_context* c) { return 0; }",
                          "struct fields must be int or char"), "nested structure fields remain unsupported")) return false;
    if (!require(rejected("struct P { int value; }; int gx_main(gx_app_context* c) { struct P p; return p.missing; }",
                          "struct has no field"), "invalid member access is diagnosed")) return false;

    std::string tooMany = "struct Many { ";
    for (uint32_t i = 0; i < COMPILER_MAX_STRUCT_FIELDS + 1U; ++i)
        tooMany += "char f" + std::to_string(i) + "; ";
    tooMany += "}; int gx_main(gx_app_context* c) { return 0; }";
    if (!require(rejected(tooMany.c_str(), "struct field capacity exceeded"),
                 "structure field-count limit is enforced")) return false;

    const char* tooMuchLocalStorage =
        "struct Large { int a; int b; int c; int d; int e; int f; int g; int h; "
        "int i; int j; int k; int l; int m; int n; int o; int p; }; "
        "int gx_main(gx_app_context* c) { struct Large items[5]; return 0; }";
    return require(rejected(tooMuchLocalStorage, "local storage exceeds"),
                   "structure array local-storage limit is enforced");
}

} // namespace

int main()
{
    if (!execution_and_layout() || !modules_and_objects() || !global_layout_and_diagnostics()) return 1;
    std::printf("COMP4H assertions=%u result=PASS\n", g_assertions);
    return 0;
}
