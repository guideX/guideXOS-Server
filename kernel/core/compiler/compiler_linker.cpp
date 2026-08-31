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

} // namespace

bool link_modules(const CompiledModule* modules, uint32_t moduleCount,
                  LinkedProgram* output, Diagnostics& diagnostics)
{
    if (!modules || !output || moduleCount == 0 || moduleCount > COMPILER_MAX_TRANSLATION_UNITS) {
        const SourceLocation location = {0, 1, 1};
        diagnostics.error(location, "linker module count exceeds bounded project limit", "linker");
        return false;
    }
    *output = {};
    output->moduleCount = moduleCount;

    const CompiledModule* ordered[COMPILER_MAX_TRANSLATION_UNITS] = {};
    for (uint32_t i = 0; i < moduleCount; ++i) {
        if (modules[i].sourcePath[0] == '\0' || modules[i].codeBytes == 0 ||
            modules[i].codeBytes > COMPILER_MAX_CODE_BYTES ||
            modules[i].dataBytes > COMPILER_MAX_LINKED_DATA_BYTES ||
            modules[i].exportCount > COMPILER_MAX_FUNCTIONS ||
            modules[i].importCount > COMPILER_MAX_FUNCTIONS ||
            modules[i].relocationCount > COMPILER_MAX_MODULE_RELOCATIONS) {
            diagnostics.error((SourceLocation){0, 1, 1}, "compiled module exceeds bounded representation", "linker");
            return false;
        }
        ordered[i] = &modules[i];
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

    uint32_t moduleCodeOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t moduleDataOffsets[COMPILER_MAX_TRANSLATION_UNITS] = {};
    uint32_t codeOffset = 0;
    uint32_t dataOffset = 0;
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
    }
    output->codeBytes = codeOffset;
    output->dataBytes = dataOffset;
    for (uint32_t i = 0; i < moduleCount; ++i) {
        for (uint32_t j = 0; j < ordered[i]->codeBytes; ++j)
            output->code[moduleCodeOffsets[i] + j] = ordered[i]->code[j];
        for (uint32_t j = 0; j < ordered[i]->dataBytes; ++j)
            output->data[moduleDataOffsets[i] + j] = ordered[i]->data[j];
    }
    if (output->dataBytes == 0) {
        output->dataFileOffset = 0;
    } else {
        uint32_t codeFileEnd = 0;
        if (!add_u32(BOOTSTRAP_CODE_OFFSET, output->codeBytes, &codeFileEnd) ||
            !align_page(codeFileEnd, &output->dataFileOffset) ||
            output->dataFileOffset < BOOTSTRAP_DATA_OFFSET) {
            diagnostics.error((SourceLocation){0, 1, 1}, "linked image layout overflowed", "linker");
            return false;
        }
    }

    // The global external table is populated from sorted modules, making both
    // symbol order and final bytes independent of VFS enumeration order.
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t e = 0; e < module.exportCount; ++e) {
            const ExportSymbol& exportSymbol = module.exports[e];
            if (find_symbol(*output, exportSymbol.name) >= 0) {
                diagnostics.error_identifier(exportSymbol.location, "duplicate definition for function ",
                                              exportSymbol.name, name_length(exportSymbol.name), "linker");
                return false;
            }
            if (output->exportCount >= COMPILER_MAX_PROJECT_EXPORTS) {
                diagnostics.error(exportSymbol.location, "project export capacity exceeded", "linker");
                return false;
            }
            GlobalFunctionSymbol& global = output->exports[output->exportCount++];
            global = {};
            copy_name(global.name, sizeof(global.name), exportSymbol.name);
            global.moduleIndex = static_cast<uint16_t>(m);
            global.parameterCount = exportSymbol.parameterCount;
            global.moduleCodeOffset = exportSymbol.moduleCodeOffset;
            if (global.moduleCodeOffset >= module.codeBytes ||
                !add_u32(moduleCodeOffsets[m], global.moduleCodeOffset, &global.finalCodeOffset) ||
                global.finalCodeOffset >= output->codeBytes) {
                diagnostics.error(exportSymbol.location, "export code offset is out of bounds", "linker");
                return false;
            }
            global.isEntry = exportSymbol.isEntry;
        }
    }

    uint32_t entryCount = 0;
    for (uint32_t i = 0; i < output->exportCount; ++i) {
        if (output->exports[i].isEntry) {
            ++entryCount;
            output->entryCodeOffset = output->exports[i].finalCodeOffset;
        }
    }
    if (entryCount == 0) {
        diagnostics.error((SourceLocation){0, 1, 1}, "missing gx_main entry function", "linker");
        return false;
    }
    if (entryCount != 1) {
        diagnostics.error((SourceLocation){0, 1, 1}, "duplicate gx_main entry function", "linker");
        return false;
    }

    // Imports are declarations that are actually used by a generated call.
    // Unused prototypes therefore do not need a definition.
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t i = 0; i < module.importCount; ++i) {
            const ImportSymbol& importSymbol = module.imports[i];
            if (output->importCount >= COMPILER_MAX_PROJECT_IMPORTS) {
                diagnostics.error(importSymbol.location, "project import capacity exceeded", "linker");
                return false;
            }
            const int32_t globalIndex = find_symbol(*output, importSymbol.name);
            if (globalIndex < 0) {
                diagnostics.error_identifier(importSymbol.location, "undefined external function ",
                                              importSymbol.name, name_length(importSymbol.name), "linker");
                return false;
            }
            if (output->exports[globalIndex].parameterCount != importSymbol.expectedParameterCount) {
                diagnostics.error_identifier(importSymbol.location, "conflicting declaration for function ",
                                              importSymbol.name, name_length(importSymbol.name), "linker");
                return false;
            }
            ++output->importCount;
        }
    }

    // Reconstruct the bounded project call graph from module-local recursion
    // metadata and the external call relocations.  The module representation
    // intentionally does not carry a general relocatable-object symbol graph.
    bool projectGraph[COMPILER_MAX_PROJECT_EXPORTS][COMPILER_MAX_PROJECT_EXPORTS] = {};
    for (uint32_t m = 0; m < moduleCount; ++m) {
        const CompiledModule& module = *ordered[m];
        for (uint32_t f = 0; f < module.functionCount && f < module.exportCount; ++f) {
            const int32_t global = find_symbol(*output, module.exports[f].name);
            if (global >= 0 && module.recursiveFunction[f]) projectGraph[global][global] = true;
            for (uint32_t g = 0; g < module.functionCount && g < module.exportCount; ++g) {
                if (!module.callGraph[f][g]) continue;
                const int32_t callee = find_symbol(*output, module.exports[g].name);
                if (global >= 0 && callee >= 0) projectGraph[global][callee] = true;
            }
        }
        for (uint32_t r = 0; r < module.relocationCount; ++r) {
            const RelocationRecord& relocation = module.relocations[r];
            if (relocation.kind != RelocationKind::CallRel32) continue;
            int32_t caller = -1;
            uint32_t callerOffset = 0;
            for (uint32_t e = 0; e < module.exportCount; ++e) {
                if (module.exports[e].moduleCodeOffset <= relocation.patchOffset &&
                    (caller < 0 || module.exports[e].moduleCodeOffset >= callerOffset)) {
                    const int32_t candidate = find_symbol(*output, module.exports[e].name);
                    if (candidate >= 0) {
                        caller = candidate;
                        callerOffset = module.exports[e].moduleCodeOffset;
                    }
                }
            }
            const int32_t callee = find_symbol(*output, relocation.targetSymbolName);
            if (caller >= 0 && callee >= 0) projectGraph[caller][callee] = true;
        }
    }
    for (uint32_t k = 0; k < output->exportCount; ++k)
        for (uint32_t i = 0; i < output->exportCount; ++i)
            for (uint32_t j = 0; j < output->exportCount; ++j)
                projectGraph[i][j] = projectGraph[i][j] ||
                    (projectGraph[i][k] && projectGraph[k][j]);
    output->recursiveSccCount = 0;
    for (uint32_t i = 0; i < output->exportCount; ++i) {
        output->recursiveFunction[i] = projectGraph[i][i];
        if (!output->recursiveFunction[i]) continue;
        bool representative = true;
        for (uint32_t j = 0; j < i; ++j) {
            if (output->recursiveFunction[j] && projectGraph[i][j] && projectGraph[j][i]) {
                representative = false;
                break;
            }
        }
        if (representative) ++output->recursiveSccCount;
    }

    // Apply only the two internal relocation kinds supported by Phase 27N.
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
            if (relocation.kind == RelocationKind::CallRel32) {
                if (relocation.width != 4) {
                    diagnostics.error(relocation.location, "CallRel32 relocation width is invalid", "relocation");
                    return false;
                }
                const int32_t targetIndex = find_symbol(*output, relocation.targetSymbolName);
                if (targetIndex < 0) {
                    diagnostics.error_identifier(relocation.location, "undefined external function ",
                                                  relocation.targetSymbolName,
                                                  name_length(relocation.targetSymbolName), "linker");
                    return false;
                }
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
                if (relocation.width != 8 || relocation.dataOffset > module.dataBytes ||
                    relocation.dataOffset > output->dataBytes - moduleDataOffsets[m] ||
                    !patch_u64(output->code, output->codeBytes, finalPatch,
                               BOOTSTRAP_IMAGE_BASE + output->dataFileOffset +
                               moduleDataOffsets[m] + relocation.dataOffset)) {
                    diagnostics.error(relocation.location, "DataAddress64 relocation is invalid", "relocation");
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
