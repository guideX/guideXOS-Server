//
// Bounded translation-unit compilation into the guideXOS internal module form.
//

#include "compiler_module.h"

#include "compiler_driver.h"
#include "compiler_lexer.h"
#include "compiler_parser.h"
#if defined(__x86_64__)
#include "../../arch/amd64/compiler_backend.h"
#endif

namespace kernel {
namespace compiler {
namespace {

static Token s_tokens[COMPILER_MAX_TOKENS];
static TranslationUnitIR s_unit = {};
static CallSite s_callStorage[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_CALL_EXPRESSIONS] = {};
static uint16_t s_callArgumentStorage[COMPILER_MAX_FUNCTIONS][COMPILER_MAX_CALL_ARGUMENT_NODES] = {};

static uint64_t hash_bytes(const uint8_t* bytes, uint32_t count)
{
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool copy_string(char* output, uint32_t capacity, const char* input)
{
    if (!output || capacity == 0 || !input) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && input[i] != '\0') {
        output[i] = input[i];
        ++i;
    }
    output[i] = '\0';
    return input[i] == '\0';
}

static bool flatten_string_table(TranslationUnitIR& unit, uint8_t* output,
                                 uint32_t capacity, uint32_t* outputBytes)
{
    if (!output || !outputBytes || unit.functionCount > COMPILER_MAX_FUNCTIONS) return false;
    uint32_t offset = 0;
    for (uint32_t f = 0; f < unit.functionCount; ++f) {
        FunctionIR& function = unit.functions[f];
        const uint32_t functionStart = offset;
        function.dataOffset = functionStart;
        for (uint32_t i = 0; i < function.stringCount; ++i) {
            if (function.stringOffsets[i] != offset - functionStart ||
                offset + function.strings[i].bytes + 1U > capacity) return false;
            for (uint32_t j = 0; j < function.strings[i].bytes; ++j)
                output[offset + j] = static_cast<uint8_t>(function.strings[i].data[j]);
            output[offset + function.strings[i].bytes] = 0;
            offset += function.strings[i].bytes + 1U;
        }
    }
    *outputBytes = offset;
    return true;
}

static bool same_name(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] || right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return true;
}

static uint32_t name_length(const char* name)
{
    uint32_t length = 0;
    if (name) while (name[length]) ++length;
    return length;
}

static int32_t find_import(const CompiledModule& module, const char* name)
{
    for (uint32_t i = 0; i < module.importCount; ++i)
        if (same_name(module.imports[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

} // namespace

bool compile_module_from_source(const char* sourcePath,
                                const char* source,
                                uint32_t sourceBytes,
                                CompiledModule* module,
                                Diagnostics& diagnostics)
{
    if (!module || !source || !sourcePath || sourcePath[0] == '\0') {
        diagnostics.error({0, 1, 1}, "module source and path are required", "module");
        return false;
    }
    uint32_t pathBytes = 0;
    while (sourcePath[pathBytes] != '\0') ++pathBytes;
    if (sourceBytes > COMPILER_MAX_SOURCE_BYTES ||
        pathBytes + 1 > sizeof(module->sourcePath)) {
        diagnostics.error({0, 1, 1}, "module source exceeds bounded path or source limit", "module");
        return false;
    }

    *module = {};
    copy_string(module->sourcePath, sizeof(module->sourcePath), sourcePath);
    module->sourceBytes = sourceBytes;
    module->sourceHash = hash_bytes(reinterpret_cast<const uint8_t*>(source), sourceBytes);

    uint32_t tokenCount = 0;
    if (!lex_source(source, sourceBytes, s_tokens, COMPILER_MAX_TOKENS, &tokenCount, diagnostics))
        return false;
    s_unit = {};
    if (!parse_translation_unit(source, s_tokens, tokenCount, &s_unit, diagnostics,
                                &s_callStorage[0][0], &s_callArgumentStorage[0][0]))
        return false;

    module->tokenCount = tokenCount;
    module->functionCount = s_unit.functionCount;
    module->recursiveSccCount = s_unit.recursiveSccCount;
    for (uint32_t i = 0; i < COMPILER_MAX_FUNCTIONS; ++i)
        module->recursiveFunction[i] = s_unit.recursiveFunction[i];
    for (uint32_t i = 0; i < COMPILER_MAX_FUNCTIONS; ++i)
        for (uint32_t j = 0; j < COMPILER_MAX_FUNCTIONS; ++j)
            module->callGraph[i][j] = s_unit.callGraph[i][j];
    module->entryCodeOffset = 0;
    module->hasEntry = s_unit.entryFunction != COMPILER_INVALID_INDEX;
    if (!flatten_string_table(s_unit, module->data, sizeof(module->data), &module->dataBytes)) {
        diagnostics.error({0, 1, 1}, "source string data exceeds compiler limit", "data");
        return false;
    }

#if defined(__x86_64__)
    if (!amd64::emit_translation_unit_module(s_unit, module->code, sizeof(module->code),
                                             &module->codeBytes, &module->entryCodeOffset,
                                             module->relocations, COMPILER_MAX_MODULE_RELOCATIONS,
                                             &module->relocationCount)) {
        diagnostics.error({0, 1, 1}, "AMD64 backend rejected target-neutral IR", "backend");
        return false;
    }
#else
    diagnostics.error({0, 1, 1}, "bootstrap compiler backend is only available on AMD64", "backend");
    return false;
#endif

    if (module->hasEntry) {
        const FunctionIR& entry = s_unit.functions[s_unit.entryFunction];
        module->hasHostLog = entry.hasHostLog;
        module->returnConstantValid = entry.returnConstantValid;
        module->returnConstant = entry.returnConstant;
    }
    for (uint32_t i = 0; i < s_unit.functionCount; ++i) {
        const FunctionIR& function = s_unit.functions[i];
        if (module->exportCount >= COMPILER_MAX_FUNCTIONS) {
            diagnostics.error(function.location, "module export capacity exceeded", "linker");
            return false;
        }
        ExportSymbol& exportSymbol = module->exports[module->exportCount++];
        exportSymbol = {};
        if (!copy_string(exportSymbol.name, sizeof(exportSymbol.name), function.name)) return false;
        exportSymbol.moduleCodeOffset = function.codeOffset;
        exportSymbol.parameterCount = function.parameterCount;
        exportSymbol.isEntry = module->hasEntry && i == s_unit.entryFunction;
        exportSymbol.location = function.location;
        for (uint32_t c = 0; c < function.callCount; ++c) {
            const CallSite& call = function.calls[c];
            if (!call.external) continue;
            const int32_t existing = find_import(*module, call.calleeName);
            if (existing >= 0) {
                if (module->imports[existing].expectedParameterCount != call.expectedParameterCount) {
                    diagnostics.error_identifier(call.location, "conflicting declaration for function ",
                                                  call.calleeName, name_length(call.calleeName), "function");
                    return false;
                }
                continue;
            }
            if (module->importCount >= COMPILER_MAX_FUNCTIONS) {
                diagnostics.error(call.location, "module import capacity exceeded", "linker");
                return false;
            }
            ImportSymbol& importSymbol = module->imports[module->importCount++];
            importSymbol = {};
            if (!copy_string(importSymbol.name, sizeof(importSymbol.name), call.calleeName)) return false;
            importSymbol.expectedParameterCount = call.expectedParameterCount;
            importSymbol.location = call.location;
        }
    }
    return !diagnostics.has_error();
}

} // namespace compiler
} // namespace kernel
