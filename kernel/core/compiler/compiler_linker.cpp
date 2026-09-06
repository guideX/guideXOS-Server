//
// Bounded guideXOS-native linker for the bootstrap compiler.
//
#include "compiler_linker.h"

#include "../../arch/amd64/compiler_backend.h"
#include "elf_writer.h"

namespace kernel {
namespace compiler {
namespace {

static bool names_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] || right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return true;
}

static uint32_t name_length(const char* value)
{
    uint32_t length = 0;
    if (value) while (value[length]) ++length;
    return length;
}

static bool path_before(const char* left, const char* right)
{
    uint32_t i = 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return static_cast<unsigned char>(left[i]) < static_cast<unsigned char>(right[i]);
}

static bool add_u32(uint32_t left, uint32_t right, uint32_t* output)
{
    if (!output || left > 0xFFFFFFFFU - right) return false;
    *output = left + right;
    return true;
}

static bool align16(uint32_t value, uint32_t* output)
{
    if (!output || value > 0xFFFFFFF0U) return false;
    *output = (value + 15U) & ~15U;
    return true;
}

static bool align4(uint32_t value, uint32_t* output)
{
    if (!output || value > 0xFFFFFFFCU) return false;
    *output = (value + 3U) & ~3U;
    return true;
}

static bool align_page(uint32_t value, uint32_t* output)
{
    if (!output || value > 0xFFFFF000U) return false;
    *output = (value + 0xFFFU) & ~0xFFFU;
    return true;
}

static void copy_name(char* output, uint32_t capacity, const char* input)
{
    if (!output || capacity == 0) return;
    uint32_t i = 0;
    if (input) while (i + 1 < capacity && input[i]) {
        output[i] = input[i];
        ++i;
    }
    output[i] = '\0';
}

static int32_t find_symbol(const LinkedProgram& program, const char* name)
{
    for (uint32_t i = 0; i < program.exportCount; ++i)
        if (names_equal(program.exports[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

static int32_t find_module_function_export(const CompiledModule& module, uint32_t functionIndex)
{
    uint32_t seen = 0;
    for (uint32_t i = 0; i < module.exportCount; ++i) {
        if (module.exports[i].kind != SymbolKind::Function) continue;
        if (seen++ == functionIndex) return static_cast<int32_t>(i);
    }
    return -1;
}

static bool patch_u64(uint8_t* code, uint32_t codeBytes, uint32_t patchOffset, uint64_t value)
{
    if (!code || patchOffset > codeBytes || codeBytes - patchOffset < 8U) return false;
    for (uint32_t i = 0; i < 8; ++i) code[patchOffset + i] = static_cast<uint8_t>(value >> (i * 8));
    return true;
}

static bool patch_u32(uint8_t* code, uint32_t codeBytes, uint32_t patchOffset, int32_t value)
{
    if (!code || patchOffset > codeBytes || codeBytes - patchOffset < 4U) return false;
    const uint32_t bits = static_cast<uint32_t>(value);
    for (uint32_t i = 0; i < 4; ++i) code[patchOffset + i] = static_cast<uint8_t>(bits >> (i * 8));
    return true;
}

static bool calculate_rel32(uint64_t target, uint64_t after, int32_t* output)
{
    if (!output) return false;
    if (target >= after) {
        const uint64_t distance = target - after;
        if (distance > 2147483647ULL) return false;
        *output = static_cast<int32_t>(distance);
        return true;
    }
    const uint64_t distance = after - target;
    if (distance > 2147483648ULL) return false;
    *output = distance == 2147483648ULL
        ? static_cast<int32_t>(0x80000000U) : -static_cast<int32_t>(distance);
    return true;
}

static bool report_kind_conflict(const SourceLocation& location, const char* name,
                                 Diagnostics& diagnostics)
{
    diagnostics.error_identifier_suffix(location, "symbol ", name, name_length(name),
                                        " defined as both function and global", "linker");
    return false;
}

static bool valid_linker_data_signature(SymbolKind kind, uint16_t elementCount, uint16_t elementSize,
                                        uint32_t size, uint32_t alignment, uint64_t structTypeIdentity)
{
    if (kind == SymbolKind::DataStruct)
        return elementCount == 1 && elementSize == size && size != 0 && size <= COMPILER_MAX_STRUCT_BYTES &&
            alignment == 4 && structTypeIdentity != 0;
    return (kind == SymbolKind::Data || kind == SymbolKind::DataArray) && elementCount != 0 &&
        elementCount <= COMPILER_MAX_ARRAY_ELEMENTS && elementSize == 4 &&
        size == static_cast<uint32_t>(elementCount) * elementSize && alignment == 4;
}

} // namespace

bool link_modules(const CompiledModule* modules, uint32_t moduleCount,
                  LinkedProgram* output, Diagnostics& diagnostics)
{
    if (!modules || !output || moduleCount == 0 || moduleCount > COMPILER_MAX_TRANSLATION_UNITS) {
        diagnostics.error((SourceLocation){0, 1, 1},
                          "linker module count exceeds bounded project limit", "linker");
        return false;
    }
    *output = {};
    output->moduleCount = moduleCount;

    const CompiledModule* ordered[COMPILER_MAX_TRANSLATION_UNITS] = {};
    for (uint32_t i = 0; i < moduleCount; ++i) {
        const CompiledModule& module = modules[i];
        if (module.sourcePath[0] == '\0' || module.codeBytes > COMPILER_MAX_CODE_BYTES ||
            module.functionCount > COMPILER_MAX_FUNCTIONS || module.globalCount > COMPILER_MAX_GLOBALS ||
            (module.functionCount != 0 && module.codeBytes == 0) ||
            module.dataBytes > COMPILER_MAX_LINKED_DATA_BYTES ||
            module.mutableDataBytes > COMPILER_MAX_LINKED_DATA_BYTES ||
            module.exportCount > COMPILER_MAX_MODULE_SYMBOLS ||
            module.importCount > COMPILER_MAX_MODULE_SYMBOLS ||
            module.relocationCount > COMPILER_MAX_MODULE_RELOCATIONS) {
            diagnostics.error((SourceLocation){0, 1, 1},
                              "compiled module exceeds bounded representation", "linker");
            return false;
        }
        ordered[i] = &module;
    }
    for (uint32_t i = 0; i < moduleCount; ++i) {
        for (uint32_t j = i + 1; j < moduleCount; ++j) {
            if (path_before(ordered[j]->sourcePath, ordered[i]->sourcePath)) {
                const CompiledModule* swap = ordered[i];
                ordered[i] = ordered[j];
                ordered[j] = swap;
            }
        }
        if (i != 0 && names_equal(ordered[i - 1]->sourcePath, ordered[i]->sourcePath)) {
            diagnostics.error((SourceLocation){0, 1, 1}, "duplicate source entry in project", "source");
            return false;
        }
    }

    // Preserve the bounded type table in the linked representation and reject
    // same-name definitions whose layout identities disagree, even when the
    // differing type is not reached by a function call in this project.
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        if (module.structTypeCount > COMPILER_MAX_STRUCT_TYPES) {
            diagnostics.error((SourceLocation){0, 1, 1}, "module struct type capacity exceeded", "struct");
            return false;
        }
        for (uint32_t t = 0; t < module.structTypeCount; ++t) {
            const StructTypeIR& type = module.structTypes[t];
            int32_t sameName = -1;
            int32_t sameIdentity = -1;
            for (uint32_t existing = 0; existing < output->structTypeCount; ++existing) {
                if (names_equal(output->structTypes[existing].name, type.name))
                    sameName = static_cast<int32_t>(existing);
                if (output->structTypes[existing].identity == type.identity)
                    sameIdentity = static_cast<int32_t>(existing);
            }
            if (sameName >= 0 && output->structTypes[sameName].identity != type.identity) {
                diagnostics.error((SourceLocation){0, 1, 1}, "incompatible struct type definition across modules", "struct");
                return false;
            }
            if (sameIdentity >= 0) continue;
            if (output->structTypeCount >= COMPILER_MAX_STRUCT_TYPES) {
                diagnostics.error((SourceLocation){0, 1, 1}, "linked struct type capacity exceeded", "struct");
                return false;
            }
            output->structTypes[output->structTypeCount++] = type;
        }
    }

    uint32_t moduleCodeOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t moduleDataOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t moduleMutableDataOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t codeOffset = 0;
    uint32_t dataOffset = 0;
    uint32_t mutableDataOffset = 0;
    for (uint32_t i = 0; i < moduleCount; ++i) {
        if (!align16(codeOffset, &codeOffset) ||
            !add_u32(codeOffset, ordered[i]->codeBytes, &codeOffset) ||
            codeOffset > COMPILER_MAX_LINKED_CODE_BYTES) {
            diagnostics.error((SourceLocation){0, 1, 1}, "linked code capacity exceeded", "linker");
            return false;
        }
        moduleCodeOffsets[i] = codeOffset - ordered[i]->codeBytes;
        moduleDataOffsets[i] = dataOffset;
        if (!add_u32(dataOffset, ordered[i]->dataBytes, &dataOffset) ||
            dataOffset > COMPILER_MAX_LINKED_DATA_BYTES) {
            diagnostics.error((SourceLocation){0, 1, 1}, "linked read-only data capacity exceeded", "linker");
            return false;
        }
        if (!align4(mutableDataOffset, &mutableDataOffset) ||
            (ordered[i]->mutableDataBytes != 0 &&
             !add_u32(mutableDataOffset, ordered[i]->mutableDataBytes, &mutableDataOffset)) ||
            mutableDataOffset > COMPILER_MAX_LINKED_DATA_BYTES) {
            diagnostics.error((SourceLocation){0, 1, 1}, "linked mutable data capacity exceeded", "linker");
            return false;
        }
        moduleMutableDataOffsets[i] = mutableDataOffset - ordered[i]->mutableDataBytes;
    }
    output->codeBytes = codeOffset;
    output->dataBytes = dataOffset;
    output->mutableDataBytes = mutableDataOffset;
    for (uint32_t i = 0; i < moduleCount; ++i) {
        for (uint32_t j = 0; j < ordered[i]->codeBytes; ++j)
            output->code[moduleCodeOffsets[i] + j] = ordered[i]->code[j];
        for (uint32_t j = 0; j < ordered[i]->dataBytes; ++j)
            output->data[moduleDataOffsets[i] + j] = ordered[i]->data[j];
        for (uint32_t j = 0; j < ordered[i]->mutableDataBytes; ++j)
            output->mutableData[moduleMutableDataOffsets[i] + j] = ordered[i]->mutableData[j];
    }

    output->dataFileOffset = 0;
    output->mutableDataFileOffset = 0;
    uint32_t codeFileEnd = 0;
    if (!add_u32(BOOTSTRAP_CODE_OFFSET, output->codeBytes, &codeFileEnd)) {
        diagnostics.error((SourceLocation){0, 1, 1}, "linked image layout overflowed", "linker");
        return false;
    }
    uint32_t rodataFileEnd = codeFileEnd;
    if (output->dataBytes != 0) {
        if (!align_page(codeFileEnd, &output->dataFileOffset) ||
            output->dataFileOffset < BOOTSTRAP_DATA_OFFSET ||
            !add_u32(output->dataFileOffset, output->dataBytes, &rodataFileEnd)) {
            diagnostics.error((SourceLocation){0, 1, 1}, "linked read-only image layout overflowed", "linker");
            return false;
        }
    }
    if (output->mutableDataBytes != 0) {
        if (!align_page(rodataFileEnd, &output->mutableDataFileOffset)) {
            diagnostics.error((SourceLocation){0, 1, 1}, "linked mutable image layout overflowed", "linker");
            return false;
        }
        if (output->mutableDataFileOffset < BOOTSTRAP_DATA_OFFSET)
            output->mutableDataFileOffset = BOOTSTRAP_DATA_OFFSET;
    }

    // One ordinary namespace contains both function and data definitions.
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t e = 0; e < module.exportCount; ++e) {
            const ExportSymbol& exportSymbol = module.exports[e];
            const int32_t existing = find_symbol(*output, exportSymbol.name);
            if (existing >= 0) {
                if (output->exports[existing].kind != exportSymbol.kind)
                    return report_kind_conflict(exportSymbol.location, exportSymbol.name, diagnostics);
                diagnostics.error_identifier(exportSymbol.location,
                    symbol_is_data(exportSymbol.kind) ? "duplicate definition for global " :
                    "duplicate definition for function ", exportSymbol.name,
                    name_length(exportSymbol.name), "linker");
                return false;
            }
            if (output->exportCount >= COMPILER_MAX_PROJECT_EXPORTS) {
                diagnostics.error(exportSymbol.location, "project export capacity exceeded", "linker");
                return false;
            }
            GlobalFunctionSymbol& global = output->exports[output->exportCount++];
            global = {};
            global.kind = exportSymbol.kind;
            copy_name(global.name, sizeof(global.name), exportSymbol.name);
            global.moduleIndex = static_cast<uint16_t>(m);
            global.parameterCount = exportSymbol.parameterCount;
            for (uint32_t p = 0; p < exportSymbol.parameterCount; ++p) {
                global.parameterKinds[p] = exportSymbol.parameterKinds[p];
                global.parameterStructTypes[p] = exportSymbol.parameterStructTypes[p];
            }
            copy_name(global.structTypeName, sizeof(global.structTypeName), exportSymbol.structTypeName);
            global.structTypeIdentity = exportSymbol.structTypeIdentity;
            global.moduleCodeOffset = exportSymbol.moduleCodeOffset;
            global.moduleDataOffset = exportSymbol.moduleDataOffset;
            global.size = exportSymbol.size;
            global.alignment = exportSymbol.alignment;
            global.elementCount = exportSymbol.elementCount;
            global.elementSize = exportSymbol.elementSize;
            global.isEntry = exportSymbol.isEntry;
            if (exportSymbol.kind == SymbolKind::Function) {
                if (global.moduleCodeOffset >= module.codeBytes ||
                    !add_u32(moduleCodeOffsets[m], global.moduleCodeOffset, &global.finalCodeOffset) ||
                    (module.codeBytes != 0 && global.finalCodeOffset >= output->codeBytes)) {
                    diagnostics.error(exportSymbol.location, "export code offset is out of bounds", "linker");
                    return false;
                }
            } else {
                if (!symbol_is_data(exportSymbol.kind) ||
                    !valid_linker_data_signature(exportSymbol.kind, global.elementCount, global.elementSize,
                        global.size, global.alignment, global.structTypeIdentity) ||
                    global.moduleDataOffset > module.mutableDataBytes ||
                    module.mutableDataBytes - global.moduleDataOffset < global.size ||
                    !add_u32(moduleMutableDataOffsets[m], global.moduleDataOffset, &global.finalDataOffset) ||
                    global.finalDataOffset > output->mutableDataBytes ||
                    output->mutableDataBytes - global.finalDataOffset < global.size) {
                    diagnostics.error(exportSymbol.location, "global data export is out of bounds", "linker");
                    return false;
                }
            }
        }
    }

    uint32_t entryCount = 0;
    for (uint32_t i = 0; i < output->exportCount; ++i) {
        if (!output->exports[i].isEntry) continue;
        if (output->exports[i].kind != SymbolKind::Function) {
            diagnostics.error((SourceLocation){0, 1, 1}, "global cannot be the gx_main entry", "linker");
            return false;
        }
        ++entryCount;
        output->entryCodeOffset = output->exports[i].finalCodeOffset;
    }
    if (entryCount == 0) {
        diagnostics.error((SourceLocation){0, 1, 1}, "missing gx_main entry function", "linker");
        return false;
    }
    if (entryCount != 1) {
        diagnostics.error((SourceLocation){0, 1, 1}, "duplicate gx_main entry function", "linker");
        return false;
    }

    // Imports are declarations that are actually used by generated code.
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t i = 0; i < module.importCount; ++i) {
            const ImportSymbol& importSymbol = module.imports[i];
            if (output->importCount >= COMPILER_MAX_PROJECT_IMPORTS) {
                diagnostics.error(importSymbol.location, "project import capacity exceeded", "linker");
                return false;
            }
            const int32_t symbolIndex = find_symbol(*output, importSymbol.name);
            if (symbolIndex < 0) {
                diagnostics.error_identifier(importSymbol.location,
                    symbol_is_data(importSymbol.kind) ? "undefined external global " :
                    "undefined external function ", importSymbol.name,
                    name_length(importSymbol.name), "linker");
                return false;
            }
            const GlobalFunctionSymbol& definition = output->exports[symbolIndex];
            if (definition.kind != importSymbol.kind) {
                if (symbol_is_data(definition.kind) && symbol_is_data(importSymbol.kind)) {
                    diagnostics.error_identifier(importSymbol.location, "conflicting declaration for global ",
                                                  importSymbol.name, name_length(importSymbol.name), "linker");
                    return false;
                }
                return report_kind_conflict(importSymbol.location, importSymbol.name, diagnostics);
            }
            if (importSymbol.kind == SymbolKind::Function) {
                bool sameSignature = definition.parameterCount == importSymbol.expectedParameterCount;
                for (uint32_t p = 0; sameSignature && p < importSymbol.expectedParameterCount; ++p)
                    sameSignature = definition.parameterKinds[p] == importSymbol.parameterKinds[p] &&
                        definition.parameterStructTypes[p] == importSymbol.parameterStructTypes[p];
                if (!sameSignature) {
                    diagnostics.error_identifier(importSymbol.location, "conflicting declaration for function ",
                                                  importSymbol.name, name_length(importSymbol.name), "function");
                    return false;
                }
            } else if (!symbol_is_data(importSymbol.kind) ||
                       !valid_linker_data_signature(importSymbol.kind, importSymbol.elementCount,
                           importSymbol.elementSize, importSymbol.size, importSymbol.alignment,
                           importSymbol.structTypeIdentity) || definition.size != importSymbol.size ||
                       definition.alignment != importSymbol.alignment ||
                       definition.elementCount != importSymbol.elementCount ||
                       definition.elementSize != importSymbol.elementSize ||
                       definition.structTypeIdentity != importSymbol.structTypeIdentity) {
                    diagnostics.error_identifier(importSymbol.location, "conflicting declaration for global ",
                                              importSymbol.name, name_length(importSymbol.name), "linker");
                return false;
            }
            ++output->importCount;
        }
    }

    // Reconstruct the bounded project function graph. Data symbols do not
    // participate in the call graph even though they share the namespace.
    bool projectGraph[COMPILER_MAX_PROJECT_EXPORTS][COMPILER_MAX_PROJECT_EXPORTS] = {};
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t f = 0; f < module.functionCount; ++f) {
            const int32_t moduleFunction = find_module_function_export(module, f);
            if (moduleFunction < 0) continue;
            const int32_t caller = find_symbol(*output, module.exports[moduleFunction].name);
            if (caller < 0) continue;
            if (module.recursiveFunction[f]) projectGraph[caller][caller] = true;
            for (uint32_t g = 0; g < module.functionCount; ++g) {
                if (!module.callGraph[f][g]) continue;
                const int32_t moduleCallee = find_module_function_export(module, g);
                if (moduleCallee < 0) continue;
                const int32_t callee = find_symbol(*output, module.exports[moduleCallee].name);
                if (callee >= 0 && output->exports[callee].kind == SymbolKind::Function)
                    projectGraph[caller][callee] = true;
            }
        }
        for (uint32_t r = 0; r < module.relocationCount; ++r) {
            const RelocationRecord& relocation = module.relocations[r];
            if (relocation.kind != RelocationKind::CallRel32) continue;
            int32_t caller = -1;
            uint32_t callerOffset = 0;
            for (uint32_t e = 0; e < module.exportCount; ++e) {
                if (module.exports[e].kind != SymbolKind::Function ||
                    module.exports[e].moduleCodeOffset > relocation.patchOffset ||
                    (caller >= 0 && module.exports[e].moduleCodeOffset < callerOffset)) continue;
                const int32_t candidate = find_symbol(*output, module.exports[e].name);
                if (candidate >= 0) {
                    caller = candidate;
                    callerOffset = module.exports[e].moduleCodeOffset;
                }
            }
            const int32_t callee = find_symbol(*output, relocation.targetSymbolName);
            if (caller >= 0 && callee >= 0 && output->exports[callee].kind == SymbolKind::Function)
                projectGraph[caller][callee] = true;
        }
    }
    for (uint32_t k = 0; k < output->exportCount; ++k)
        for (uint32_t i = 0; i < output->exportCount; ++i)
            for (uint32_t j = 0; j < output->exportCount; ++j)
                projectGraph[i][j] = projectGraph[i][j] ||
                    (projectGraph[i][k] && projectGraph[k][j]);
    output->recursiveSccCount = 0;
    for (uint32_t i = 0; i < output->exportCount; ++i) {
        if (output->exports[i].kind != SymbolKind::Function) continue;
        output->recursiveFunction[i] = projectGraph[i][i];
        if (!output->recursiveFunction[i]) continue;
        bool representative = true;
        for (uint32_t j = 0; j < i; ++j) {
            if (output->exports[j].kind == SymbolKind::Function && output->recursiveFunction[j] &&
                projectGraph[i][j] && projectGraph[j][i]) {
                representative = false;
                break;
            }
        }
        if (representative) ++output->recursiveSccCount;
    }

    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t r = 0; r < module.relocationCount; ++r) {
            const RelocationRecord& relocation = module.relocations[r];
            if (output->relocationCount >= COMPILER_MAX_PROJECT_RELOCATIONS) {
                diagnostics.error(relocation.location, "project relocation capacity exceeded", "linker");
                return false;
            }
            if (relocation.patchOffset > module.codeBytes ||
                relocation.width > module.codeBytes - relocation.patchOffset) {
                diagnostics.error(relocation.location, "relocation patch range is out of bounds", "relocation");
                return false;
            }
            uint32_t finalPatch = 0;
            if (!add_u32(moduleCodeOffsets[m], relocation.patchOffset, &finalPatch) ||
                finalPatch > output->codeBytes || relocation.width > output->codeBytes - finalPatch) {
                diagnostics.error(relocation.location, "linked relocation patch range is out of bounds", "relocation");
                return false;
            }
            const int32_t targetIndex = find_symbol(*output, relocation.targetSymbolName);
            if (relocation.kind == RelocationKind::CallRel32) {
                if (relocation.width != 4 || targetIndex < 0) {
                    if (targetIndex < 0) diagnostics.error_identifier(relocation.location,
                        "undefined external function ", relocation.targetSymbolName,
                        name_length(relocation.targetSymbolName), "linker");
                    else diagnostics.error(relocation.location, "CallRel32 target is not a function", "relocation");
                    return false;
                }
                if (output->exports[targetIndex].kind != SymbolKind::Function)
                    return report_kind_conflict(relocation.location, relocation.targetSymbolName, diagnostics);
                const uint64_t targetAddress = BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_CODE_OFFSET +
                    output->exports[targetIndex].finalCodeOffset;
                const uint64_t afterCall = BOOTSTRAP_IMAGE_BASE + BOOTSTRAP_CODE_OFFSET + finalPatch + 4U;
                int32_t displacement = 0;
                if (!calculate_rel32(targetAddress, afterCall, &displacement) ||
                    !patch_u32(output->code, output->codeBytes, finalPatch, displacement)) {
                    diagnostics.error(relocation.location, "CallRel32 relocation overflow", "relocation");
                    return false;
                }
            } else if (relocation.kind == RelocationKind::DataAddress64) {
                if (relocation.width != 8 || output->dataBytes == 0 ||
                    relocation.dataOffset > module.dataBytes ||
                    moduleDataOffsets[m] > output->dataBytes ||
                    relocation.dataOffset > output->dataBytes - moduleDataOffsets[m] ||
                    output->dataBytes - moduleDataOffsets[m] - relocation.dataOffset < 1U ||
                    !patch_u64(output->code, output->codeBytes, finalPatch,
                               BOOTSTRAP_IMAGE_BASE + output->dataFileOffset +
                               moduleDataOffsets[m] + relocation.dataOffset)) {
                    diagnostics.error(relocation.location, "DataAddress64 relocation is invalid", "relocation");
                    return false;
                }
            } else if (relocation.kind == RelocationKind::GlobalDataAddress64) {
                if (relocation.width != 8 || targetIndex < 0) {
                    diagnostics.error_identifier(relocation.location, "undefined external global ",
                                                  relocation.targetSymbolName,
                                                  name_length(relocation.targetSymbolName), "linker");
                    return false;
                }
                if (!symbol_is_data(output->exports[targetIndex].kind) ||
                    output->mutableDataFileOffset == 0 ||
                    !patch_u64(output->code, output->codeBytes, finalPatch,
                               BOOTSTRAP_IMAGE_BASE + output->mutableDataFileOffset +
                               output->exports[targetIndex].finalDataOffset)) {
                    diagnostics.error(relocation.location, "GlobalDataAddress64 relocation is invalid", "relocation");
                    return false;
                }
            } else {
                diagnostics.error(relocation.location, "unsupported internal relocation kind", "relocation");
                return false;
            }
            ++output->relocationCount;
        }
    }

    output->linked = true;
    return true;
}

} // namespace compiler
} // namespace kernel
