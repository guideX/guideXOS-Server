//
// Bootstrap ELF64 executable writer and validator.
//

#include "elf_writer.h"

namespace kernel {
namespace compiler {
namespace {

static const uint16_t ELF_TYPE_EXECUTABLE = 2;
static const uint16_t ELF_MACHINE_AMD64 = 62;
static const uint32_t ELF_VERSION_CURRENT = 1;
static const uint32_t PROGRAM_TYPE_LOAD = 1;
static const uint32_t PROGRAM_TYPE_DYNAMIC = 2;
static const uint32_t PROGRAM_TYPE_INTERP = 3;
static const uint32_t PROGRAM_FLAGS_EXECUTABLE = 1;
static const uint32_t PROGRAM_FLAGS_WRITABLE = 2;
static const uint32_t PROGRAM_FLAGS_READABLE = 4;
static const uint32_t PROGRAM_HEADER_OFFSET = 64;
static const uint32_t ELF_HEADER_BYTES = 64;
static const uint32_t PROGRAM_HEADER_BYTES = 56;
static const uint32_t SEGMENT_ALIGNMENT = 0x1000;
static const uint32_t BOOTSTRAP_DATA_BYTES_LIMIT = COMPILER_MAX_LINKED_DATA_BYTES;
static const uint8_t kSourceMapMagic[4] = {'G', 'X', 'S', 'M'};
static const uint8_t kSourceMapFooterMagic[4] = {'G', 'X', 'M', 'E'};
static const uint16_t kSourceMapVersion = 1;
static const uint32_t kSourceMapFooterBytes = 8;

struct LoadRange {
    uint64_t fileStart;
    uint64_t fileEnd;
    uint64_t virtualStart;
    uint64_t virtualEnd;
};

static void clear_bytes(uint8_t* bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) bytes[i] = 0;
}

static void put_u16(uint8_t* bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

static void put_u32(uint8_t* bytes, uint32_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

static void put_u64(uint8_t* bytes, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

static uint16_t get_u16(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

static uint32_t get_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static uint64_t get_u64(const uint8_t* bytes, uint32_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

static bool add_u32(uint32_t left, uint32_t right, uint32_t* result)
{
    if (!result || left > 0xFFFFFFFFu - right) return false;
    *result = left + right;
    return true;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || left > ~static_cast<uint64_t>(0) - right) return false;
    *result = left + right;
    return true;
}

static bool align_page(uint32_t value, uint32_t* result)
{
    if (!result || value > 0xFFFFF000U) return false;
    *result = (value + 0xFFFU) & ~0xFFFU;
    return true;
}

static bool is_power_of_two(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static bool fail(ElfValidationResult* result, const char* error)
{
    if (result) {
        result->valid = false;
        result->error = error;
    }
    return false;
}

static uint64_t source_map_hash(const uint8_t* bytes, uint32_t count,
                                uint32_t zeroOffset, uint32_t zeroBytes)
{
    if (!bytes || zeroOffset > count || zeroBytes > count - zeroOffset) return 0;
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t value = i >= zeroOffset && i < zeroOffset + zeroBytes ? 0 : bytes[i];
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool fixed_text_valid(const char* value, uint32_t capacity)
{
    if (!value || capacity == 0) return false;
    uint32_t i = 0;
    while (i < capacity) {
        if (value[i] == '\0') return true;
        ++i;
    }
    return false;
}

static bool source_map_equal(const char* left, const char* right)
{
    if (!fixed_text_valid(left, COMPILER_MAX_SOURCE_PATH_BYTES) || !right) return false;
    uint32_t i = 0;
    while (i < COMPILER_MAX_SOURCE_PATH_BYTES && left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return i < COMPILER_MAX_SOURCE_PATH_BYTES && left[i] == '\0' && right[i] == '\0';
}

static bool source_map_range(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

} // namespace

bool append_bootstrap_source_map(const LinkedProgram& program,
                                 uint8_t* output, uint32_t outputCapacity,
                                 ElfLayout* layout)
{
    if (!output || !layout || layout->outputBytes > outputCapacity ||
        program.sourceFileCount > COMPILER_MAX_TRANSLATION_UNITS ||
        program.sourceMapFunctionCount > COMPILER_MAX_SOURCE_MAP_FUNCTIONS ||
        program.sourceMappingCount > COMPILER_MAX_LINKED_SOURCE_MAPPINGS ||
        (program.sourceMappingCount != 0 &&
         (program.sourceFileCount == 0 || program.sourceMapFunctionCount == 0))) return false;
    for (uint32_t i = 0; i < program.sourceFileCount; ++i) {
        if (!fixed_text_valid(program.sourceFiles[i].path, COMPILER_MAX_SOURCE_PATH_BYTES) ||
            program.sourceFiles[i].sourceBytes > COMPILER_MAX_SOURCE_BYTES) return false;
    }
    for (uint32_t i = 0; i < program.sourceMapFunctionCount; ++i)
        if (!fixed_text_valid(program.sourceMapFunctions[i].name, COMPILER_FUNCTION_NAME_CAPACITY)) return false;
    if (program.sourceMappingCount == 0) return true;
    const uint64_t payload = static_cast<uint64_t>(BOOTSTRAP_SOURCE_MAP_HEADER_BYTES) +
        static_cast<uint64_t>(program.sourceFileCount) * BOOTSTRAP_SOURCE_MAP_FILE_BYTES +
        static_cast<uint64_t>(program.sourceMapFunctionCount) * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES +
        static_cast<uint64_t>(program.sourceMappingCount) * BOOTSTRAP_SOURCE_MAP_RECORD_BYTES +
        kSourceMapFooterBytes;
    if (payload > 0xFFFFFFFFULL || layout->outputBytes > outputCapacity - payload ||
        layout->outputBytes + payload > BOOTSTRAP_MAX_ELF_BYTES) return false;
    const uint32_t start = layout->outputBytes;
    const uint32_t total = static_cast<uint32_t>(payload);
    clear_bytes(output + start, total);
    put_u32(output, start + 0, 0x4D535847U);
    put_u16(output, start + 4, kSourceMapVersion);
    put_u16(output, start + 6, BOOTSTRAP_SOURCE_MAP_HEADER_BYTES);
    put_u16(output, start + 8, program.sourceFileCount);
    put_u16(output, start + 10, program.sourceMapFunctionCount);
    put_u32(output, start + 12, program.sourceMappingCount);
    put_u32(output, start + 16, layout->codeOffset);
    put_u32(output, start + 20, layout->codeBytes);
    put_u32(output, start + 24, total);
    put_u64(output, start + 32, 0);
    uint32_t cursor = start + BOOTSTRAP_SOURCE_MAP_HEADER_BYTES;
    for (uint32_t i = 0; i < program.sourceFileCount; ++i) {
        for (uint32_t j = 0; j < COMPILER_MAX_SOURCE_PATH_BYTES; ++j)
            output[cursor + j] = static_cast<uint8_t>(program.sourceFiles[i].path[j]);
        put_u32(output, cursor + COMPILER_MAX_SOURCE_PATH_BYTES, program.sourceFiles[i].sourceBytes);
        put_u64(output, cursor + COMPILER_MAX_SOURCE_PATH_BYTES + 4, program.sourceFiles[i].sourceHash);
        cursor += BOOTSTRAP_SOURCE_MAP_FILE_BYTES;
    }
    for (uint32_t i = 0; i < program.sourceMapFunctionCount; ++i) {
        for (uint32_t j = 0; j < COMPILER_FUNCTION_NAME_CAPACITY; ++j)
            output[cursor + j] = static_cast<uint8_t>(program.sourceMapFunctions[i].name[j]);
        cursor += BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES;
    }
    for (uint32_t i = 0; i < program.sourceMappingCount; ++i) {
        const LinkedProgram::LinkedSourceMapping& mapping = program.sourceMappings[i];
        put_u16(output, cursor + 0, mapping.sourceFileIndex);
        put_u16(output, cursor + 2, mapping.functionIndex);
        put_u32(output, cursor + 4, mapping.line);
        put_u32(output, cursor + 8, mapping.column);
        put_u32(output, cursor + 12, mapping.finalCodeOffset);
        put_u32(output, cursor + 16, mapping.instructionBytes);
        cursor += BOOTSTRAP_SOURCE_MAP_RECORD_BYTES;
    }
    put_u32(output, cursor + 0, 0x454D5847U);
    put_u32(output, cursor + 4, total);
    put_u64(output, start + 32, source_map_hash(output + start, total, 32, 8));
    layout->outputBytes += total;
    return true;
}

bool resolve_bootstrap_source_mapping(const uint8_t* image, uint32_t imageBytes,
                                      uint64_t imageBase, uint32_t codeFileOffset,
                                      uint32_t codeBytes, const char* sourcePath,
                                      uint32_t line, uint32_t column,
                                      ResolvedSourceMapping* result,
                                      const char** error)
{
    if (error) *error = "source-map trailer is invalid";
    if (result) *result = {};
    if (!image || !sourcePath || sourcePath[0] == '\0' || line == 0 ||
        imageBytes < kSourceMapFooterBytes) {
        if (error) *error = "source-map request is incomplete";
        return false;
    }
    const uint32_t footer = imageBytes - kSourceMapFooterBytes;
    if (get_u32(image, footer) != 0x454D5847U) {
        if (error) *error = "source-map trailer footer is missing";
        return false;
    }
    const uint32_t payload = get_u32(image, footer + 4);
    if (payload < BOOTSTRAP_SOURCE_MAP_HEADER_BYTES + kSourceMapFooterBytes ||
        payload > imageBytes) return false;
    const uint32_t start = imageBytes - payload;
    if (get_u32(image, start) != 0x4D535847U || get_u16(image, start + 4) != kSourceMapVersion ||
        get_u16(image, start + 6) != BOOTSTRAP_SOURCE_MAP_HEADER_BYTES ||
        get_u32(image, start + 24) != payload ||
        get_u64(image, start + 32) != source_map_hash(image + start, payload, 32, 8)) return false;
    const uint32_t fileCount = get_u16(image, start + 8);
    const uint32_t functionCount = get_u16(image, start + 10);
    const uint32_t mapCount = get_u32(image, start + 12);
    const uint32_t trailerCodeOffset = get_u32(image, start + 16);
    const uint32_t trailerCodeBytes = get_u32(image, start + 20);
    if (fileCount == 0 || fileCount > COMPILER_MAX_TRANSLATION_UNITS ||
        functionCount > COMPILER_MAX_SOURCE_MAP_FUNCTIONS ||
        mapCount == 0 || mapCount > COMPILER_MAX_LINKED_SOURCE_MAPPINGS ||
        trailerCodeOffset != codeFileOffset || trailerCodeBytes != codeBytes) return false;
    const uint64_t expected = static_cast<uint64_t>(BOOTSTRAP_SOURCE_MAP_HEADER_BYTES) +
        static_cast<uint64_t>(fileCount) * BOOTSTRAP_SOURCE_MAP_FILE_BYTES +
        static_cast<uint64_t>(functionCount) * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES +
        static_cast<uint64_t>(mapCount) * BOOTSTRAP_SOURCE_MAP_RECORD_BYTES +
        kSourceMapFooterBytes;
    if (expected != payload) return false;
    uint32_t cursor = start + BOOTSTRAP_SOURCE_MAP_HEADER_BYTES;
    uint32_t matchingFile = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < fileCount; ++i) {
        const char* path = reinterpret_cast<const char*>(image + cursor);
        if (!fixed_text_valid(path, COMPILER_MAX_SOURCE_PATH_BYTES) ||
            get_u32(image, cursor + COMPILER_MAX_SOURCE_PATH_BYTES) > COMPILER_MAX_SOURCE_BYTES) {
            if (error) *error = "source-map file identity is invalid";
            return false;
        }
        if (source_map_equal(path, sourcePath)) matchingFile = i;
        cursor += BOOTSTRAP_SOURCE_MAP_FILE_BYTES;
    }
    if (matchingFile == 0xFFFFFFFFU) {
        if (error) *error = "source path is absent from the final ELF source map";
        return false;
    }
    const uint32_t functionStart = cursor;
    for (uint32_t i = 0; i < functionCount; ++i) {
        if (!fixed_text_valid(reinterpret_cast<const char*>(image + cursor),
                              COMPILER_FUNCTION_NAME_CAPACITY)) return false;
        cursor += BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES;
    }
    for (uint32_t i = 0; i < mapCount; ++i) {
        const uint16_t fileIndex = get_u16(image, cursor + 0);
        const uint16_t functionIndex = get_u16(image, cursor + 2);
        const uint32_t mappedLine = get_u32(image, cursor + 4);
        const uint32_t mappedColumn = get_u32(image, cursor + 8);
        const uint32_t finalOffset = get_u32(image, cursor + 12);
        const uint32_t instructionBytes = get_u32(image, cursor + 16);
        if (fileIndex >= fileCount || functionIndex >= functionCount || mappedLine == 0 ||
            !source_map_range(finalOffset, instructionBytes, codeBytes)) return false;
        if (fileIndex == matchingFile && mappedLine == line &&
            (column == 0 || mappedColumn == column)) {
            if (result) {
                result->finalCodeOffset = finalOffset;
                result->instructionBytes = instructionBytes;
                result->line = mappedLine;
                result->column = mappedColumn;
                const uint32_t fileOffset = start + BOOTSTRAP_SOURCE_MAP_HEADER_BYTES +
                    fileIndex * BOOTSTRAP_SOURCE_MAP_FILE_BYTES;
                for (uint32_t j = 0; j < COMPILER_MAX_SOURCE_PATH_BYTES; ++j)
                    result->sourcePath[j] = static_cast<char>(image[fileOffset + j]);
                result->sourceBytes = get_u32(image, fileOffset + COMPILER_MAX_SOURCE_PATH_BYTES);
                result->sourceHash = get_u64(image, fileOffset + COMPILER_MAX_SOURCE_PATH_BYTES + 4);
                const uint32_t functionOffset = functionStart +
                    functionIndex * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES;
                for (uint32_t j = 0; j < COMPILER_FUNCTION_NAME_CAPACITY; ++j)
                    result->functionName[j] = static_cast<char>(image[functionOffset + j]);
                if (imageBase > ~static_cast<uint64_t>(0) - codeFileOffset ||
                    imageBase + codeFileOffset > ~static_cast<uint64_t>(0) - finalOffset) return false;
                result->targetAddress = imageBase + codeFileOffset + finalOffset;
            }
            return true;
        }
        cursor += BOOTSTRAP_SOURCE_MAP_RECORD_BYTES;
    }
    if (error) *error = "requested source line has no executable mapping";
    return false;
}

bool resolve_bootstrap_source_mapping_at_address(
    const uint8_t* image, uint32_t imageBytes, uint64_t imageBase,
    uint32_t codeFileOffset, uint32_t codeBytes, uint64_t address,
    ResolvedSourceMapping* result, const char** error)
{
    if (error) *error = "source-map trailer is invalid";
    if (result) *result = {};
    if (!image || imageBytes < kSourceMapFooterBytes || codeBytes == 0 ||
        imageBase > ~static_cast<uint64_t>(0) - codeFileOffset ||
        address < imageBase + codeFileOffset) {
        if (error) *error = "source-map address request is incomplete";
        return false;
    }
    const uint64_t relativeAddress = address - (imageBase + codeFileOffset);
    if (relativeAddress >= codeBytes) {
        if (error) *error = "source-map address is outside executable code";
        return false;
    }
    const uint32_t footer = imageBytes - kSourceMapFooterBytes;
    if (get_u32(image, footer) != 0x454D5847U) {
        if (error) *error = "source-map trailer footer is missing";
        return false;
    }
    const uint32_t payload = get_u32(image, footer + 4);
    if (payload < BOOTSTRAP_SOURCE_MAP_HEADER_BYTES + kSourceMapFooterBytes ||
        payload > imageBytes) return false;
    const uint32_t start = imageBytes - payload;
    if (get_u32(image, start) != 0x4D535847U ||
        get_u16(image, start + 4) != kSourceMapVersion ||
        get_u16(image, start + 6) != BOOTSTRAP_SOURCE_MAP_HEADER_BYTES ||
        get_u32(image, start + 24) != payload ||
        get_u64(image, start + 32) != source_map_hash(image + start, payload, 32, 8)) return false;
    const uint32_t fileCount = get_u16(image, start + 8);
    const uint32_t functionCount = get_u16(image, start + 10);
    const uint32_t mapCount = get_u32(image, start + 12);
    const uint32_t trailerCodeOffset = get_u32(image, start + 16);
    const uint32_t trailerCodeBytes = get_u32(image, start + 20);
    if (fileCount == 0 || fileCount > COMPILER_MAX_TRANSLATION_UNITS ||
        functionCount > COMPILER_MAX_SOURCE_MAP_FUNCTIONS || mapCount == 0 ||
        mapCount > COMPILER_MAX_LINKED_SOURCE_MAPPINGS ||
        trailerCodeOffset != codeFileOffset || trailerCodeBytes != codeBytes) return false;
    const uint64_t expected = static_cast<uint64_t>(BOOTSTRAP_SOURCE_MAP_HEADER_BYTES) +
        static_cast<uint64_t>(fileCount) * BOOTSTRAP_SOURCE_MAP_FILE_BYTES +
        static_cast<uint64_t>(functionCount) * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES +
        static_cast<uint64_t>(mapCount) * BOOTSTRAP_SOURCE_MAP_RECORD_BYTES +
        kSourceMapFooterBytes;
    if (expected != payload) return false;

    const uint32_t fileStart = start + BOOTSTRAP_SOURCE_MAP_HEADER_BYTES;
    const uint32_t functionStart = fileStart + fileCount * BOOTSTRAP_SOURCE_MAP_FILE_BYTES;
    const uint32_t mappingStart = functionStart + functionCount * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES;
    for (uint32_t i = 0; i < fileCount; ++i) {
        const uint32_t fileOffset = fileStart + i * BOOTSTRAP_SOURCE_MAP_FILE_BYTES;
        if (!fixed_text_valid(reinterpret_cast<const char*>(image + fileOffset),
                              COMPILER_MAX_SOURCE_PATH_BYTES) ||
            get_u32(image, fileOffset + COMPILER_MAX_SOURCE_PATH_BYTES) > COMPILER_MAX_SOURCE_BYTES)
            return false;
    }
    for (uint32_t i = 0; i < functionCount; ++i) {
        if (!fixed_text_valid(reinterpret_cast<const char*>(
                                  image + functionStart + i * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES),
                              COMPILER_FUNCTION_NAME_CAPACITY)) return false;
    }
    for (uint32_t i = 0; i < mapCount; ++i) {
        const uint32_t offset = mappingStart + i * BOOTSTRAP_SOURCE_MAP_RECORD_BYTES;
        const uint16_t fileIndex = get_u16(image, offset + 0);
        const uint16_t functionIndex = get_u16(image, offset + 2);
        const uint32_t mappedLine = get_u32(image, offset + 4);
        const uint32_t mappedColumn = get_u32(image, offset + 8);
        const uint32_t finalOffset = get_u32(image, offset + 12);
        const uint32_t instructionBytes = get_u32(image, offset + 16);
        if (fileIndex >= fileCount || functionIndex >= functionCount || mappedLine == 0 ||
            !source_map_range(finalOffset, instructionBytes, codeBytes)) return false;
        if (instructionBytes == 0 || relativeAddress < finalOffset ||
            relativeAddress - finalOffset >= instructionBytes) continue;
        if (result) {
            result->finalCodeOffset = finalOffset;
            result->instructionBytes = instructionBytes;
            result->line = mappedLine;
            result->column = mappedColumn;
            const uint32_t fileOffset = fileStart + fileIndex * BOOTSTRAP_SOURCE_MAP_FILE_BYTES;
            for (uint32_t j = 0; j < COMPILER_MAX_SOURCE_PATH_BYTES; ++j)
                result->sourcePath[j] = static_cast<char>(image[fileOffset + j]);
            result->sourceBytes = get_u32(image, fileOffset + COMPILER_MAX_SOURCE_PATH_BYTES);
            result->sourceHash = get_u64(image, fileOffset + COMPILER_MAX_SOURCE_PATH_BYTES + 4);
            const uint32_t functionOffset = functionStart +
                functionIndex * BOOTSTRAP_SOURCE_MAP_FUNCTION_BYTES;
            for (uint32_t j = 0; j < COMPILER_FUNCTION_NAME_CAPACITY; ++j)
                result->functionName[j] = static_cast<char>(image[functionOffset + j]);
            if (imageBase > ~static_cast<uint64_t>(0) - codeFileOffset ||
                imageBase + codeFileOffset > ~static_cast<uint64_t>(0) - finalOffset) return false;
            result->targetAddress = imageBase + codeFileOffset + finalOffset;
        }
        return true;
    }
    if (error) *error = "source-map address is unmapped";
    return false;
}

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         const uint8_t* readOnlyData,
                         uint32_t readOnlyDataBytes,
                         const uint8_t* mutableData,
                         uint32_t mutableDataBytes,
                         uint32_t entryCodeOffset,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout)
{
    if (!code || !output || !layout || codeBytes == 0 || entryCodeOffset >= codeBytes ||
        readOnlyDataBytes > BOOTSTRAP_DATA_BYTES_LIMIT || mutableDataBytes > BOOTSTRAP_DATA_BYTES_LIMIT ||
        (readOnlyDataBytes != 0 && !readOnlyData) || (mutableDataBytes != 0 && !mutableData)) return false;

    uint32_t codeFileEnd = 0;
    if (!add_u32(BOOTSTRAP_CODE_OFFSET, codeBytes, &codeFileEnd)) return false;
    uint32_t rodataOffset = 0;
    uint32_t rodataEnd = codeFileEnd;
    if (readOnlyDataBytes != 0) {
        if (!align_page(codeFileEnd, &rodataOffset) ||
            !add_u32(rodataOffset, readOnlyDataBytes, &rodataEnd)) return false;
    }
    uint32_t mutableDataOffset = 0;
    uint32_t outputBytes = rodataEnd;
    if (mutableDataBytes != 0) {
        if (!align_page(rodataEnd, &mutableDataOffset)) return false;
        if (mutableDataOffset < BOOTSTRAP_DATA_OFFSET) mutableDataOffset = BOOTSTRAP_DATA_OFFSET;
        if (!add_u32(mutableDataOffset, mutableDataBytes, &outputBytes)) return false;
    }
    const uint32_t headerBytes = PROGRAM_HEADER_OFFSET + 3U * PROGRAM_HEADER_BYTES;
    if (outputBytes > outputCapacity || outputBytes > BOOTSTRAP_MAX_ELF_BYTES || headerBytes > outputBytes)
        return false;

    uint64_t entryPoint = 0;
    if (!add_u64(BOOTSTRAP_IMAGE_BASE, BOOTSTRAP_CODE_OFFSET + entryCodeOffset, &entryPoint)) return false;

    clear_bytes(output, outputBytes);
    output[0] = 0x7F;
    output[1] = 'E';
    output[2] = 'L';
    output[3] = 'F';
    output[4] = 2;
    output[5] = 1;
    output[6] = 1;

    put_u16(output, 16, ELF_TYPE_EXECUTABLE);
    put_u16(output, 18, ELF_MACHINE_AMD64);
    put_u32(output, 20, ELF_VERSION_CURRENT);
    put_u64(output, 24, entryPoint);
    put_u64(output, 32, PROGRAM_HEADER_OFFSET);
    put_u64(output, 40, 0);
    put_u32(output, 48, 0);
    put_u16(output, 52, ELF_HEADER_BYTES);
    put_u16(output, 54, PROGRAM_HEADER_BYTES);
    const uint16_t programHeaderCount = static_cast<uint16_t>(1U +
        (readOnlyDataBytes != 0 ? 1U : 0U) + (mutableDataBytes != 0 ? 1U : 0U));
    put_u16(output, 56, programHeaderCount);
    put_u16(output, 58, 0);
    put_u16(output, 60, 0);
    put_u16(output, 62, 0);

    const uint32_t codePh = PROGRAM_HEADER_OFFSET;
    put_u32(output, codePh + 0, PROGRAM_TYPE_LOAD);
    put_u32(output, codePh + 4, PROGRAM_FLAGS_READABLE | PROGRAM_FLAGS_EXECUTABLE);
    put_u64(output, codePh + 8, 0);
    put_u64(output, codePh + 16, BOOTSTRAP_IMAGE_BASE);
    put_u64(output, codePh + 24, BOOTSTRAP_IMAGE_BASE);
    put_u64(output, codePh + 32, codeFileEnd);
    put_u64(output, codePh + 40, codeFileEnd);
    put_u64(output, codePh + 48, SEGMENT_ALIGNMENT);
    for (uint32_t i = 0; i < codeBytes; ++i) output[BOOTSTRAP_CODE_OFFSET + i] = code[i];

    uint32_t nextPh = codePh + PROGRAM_HEADER_BYTES;
    if (readOnlyDataBytes != 0) {
        put_u32(output, nextPh + 0, PROGRAM_TYPE_LOAD);
        put_u32(output, nextPh + 4, PROGRAM_FLAGS_READABLE);
        put_u64(output, nextPh + 8, rodataOffset);
        put_u64(output, nextPh + 16, BOOTSTRAP_IMAGE_BASE + rodataOffset);
        put_u64(output, nextPh + 24, BOOTSTRAP_IMAGE_BASE + rodataOffset);
        put_u64(output, nextPh + 32, readOnlyDataBytes);
        put_u64(output, nextPh + 40, readOnlyDataBytes);
        put_u64(output, nextPh + 48, SEGMENT_ALIGNMENT);
        for (uint32_t i = 0; i < readOnlyDataBytes; ++i) output[rodataOffset + i] = readOnlyData[i];
        nextPh += PROGRAM_HEADER_BYTES;
    }
    if (mutableDataBytes != 0) {
        put_u32(output, nextPh + 0, PROGRAM_TYPE_LOAD);
        put_u32(output, nextPh + 4, PROGRAM_FLAGS_READABLE | PROGRAM_FLAGS_WRITABLE);
        put_u64(output, nextPh + 8, mutableDataOffset);
        put_u64(output, nextPh + 16, BOOTSTRAP_IMAGE_BASE + mutableDataOffset);
        put_u64(output, nextPh + 24, BOOTSTRAP_IMAGE_BASE + mutableDataOffset);
        put_u64(output, nextPh + 32, mutableDataBytes);
        put_u64(output, nextPh + 40, mutableDataBytes);
        put_u64(output, nextPh + 48, SEGMENT_ALIGNMENT);
        for (uint32_t i = 0; i < mutableDataBytes; ++i) output[mutableDataOffset + i] = mutableData[i];
    }

    layout->imageBase = BOOTSTRAP_IMAGE_BASE;
    layout->entryPoint = entryPoint;
    layout->codeOffset = BOOTSTRAP_CODE_OFFSET;
    layout->codeBytes = codeBytes;
    layout->entryCodeOffset = entryCodeOffset;
    layout->dataOffset = rodataOffset;
    layout->dataAddress = readOnlyDataBytes == 0 ? 0 : BOOTSTRAP_IMAGE_BASE + rodataOffset;
    layout->dataBytes = readOnlyDataBytes;
    layout->mutableDataOffset = mutableDataOffset;
    layout->mutableDataAddress = mutableDataBytes == 0 ? 0 : BOOTSTRAP_IMAGE_BASE + mutableDataOffset;
    layout->mutableDataBytes = mutableDataBytes;
    layout->outputBytes = outputBytes;
    return true;
}

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         const uint8_t* outputData,
                         uint32_t outputDataBytes,
                         uint32_t entryCodeOffset,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout)
{
    return write_bootstrap_elf(code, codeBytes, outputData, outputDataBytes,
                               nullptr, 0, entryCodeOffset, output, outputCapacity, layout);
}

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout)
{
    return write_bootstrap_elf(code, codeBytes, nullptr, 0, nullptr, 0, 0,
                               output, outputCapacity, layout);
}

bool write_bootstrap_elf(const uint8_t* code,
                         uint32_t codeBytes,
                         const uint8_t* readOnlyData,
                         uint32_t readOnlyDataBytes,
                         uint8_t* output,
                         uint32_t outputCapacity,
                         ElfLayout* layout)
{
    return write_bootstrap_elf(code, codeBytes, readOnlyData, readOnlyDataBytes, 0,
                               output, outputCapacity, layout);
}

static bool validate_bootstrap_elf_impl(const uint8_t* image,
                                        uint32_t imageBytes,
                                        uint64_t expectedImageBase,
                                        uint32_t expectedCodeOffset,
                                        const uint8_t* expectedCode,
                                        uint32_t expectedCodeBytes,
                                        ElfValidationResult* result,
                                        const uint8_t* expectedData,
                                        uint32_t expectedDataBytes,
                                        const uint8_t* expectedMutableData,
                                        uint32_t expectedMutableDataBytes,
                                        uint32_t expectedEntryCodeOffset)
{
    if (!result) return false;
    *result = {};
    result->error = "unknown ELF validation failure";

    if (!image || imageBytes < ELF_HEADER_BYTES) return fail(result, "ELF image is smaller than ELF64 header");
    if (image[0] != 0x7F || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') return fail(result, "ELF magic mismatch");
    if (image[4] != 2) return fail(result, "ELF is not ELF64");
    if (image[5] != 1) return fail(result, "ELF is not little-endian");
    if (image[6] != 1) return fail(result, "ELF version is not current");
    if (get_u16(image, 16) != ELF_TYPE_EXECUTABLE) return fail(result, "ELF is not ET_EXEC");
    if (get_u16(image, 18) != ELF_MACHINE_AMD64) return fail(result, "ELF machine is not AMD64");
    if (get_u32(image, 20) != ELF_VERSION_CURRENT) return fail(result, "ELF version field is not current");
    if (get_u16(image, 52) < ELF_HEADER_BYTES) return fail(result, "ELF header size is too small");
    if (get_u16(image, 54) < PROGRAM_HEADER_BYTES) return fail(result, "ELF program-header size is too small");

    const uint64_t programHeaderOffset = get_u64(image, 32);
    const uint16_t programHeaderBytes = get_u16(image, 54);
    const uint16_t programHeaderCount = get_u16(image, 56);
    if (programHeaderOffset > imageBytes || programHeaderCount == 0) return fail(result, "ELF program-header table is absent or out of bounds");
    const uint64_t remaining = static_cast<uint64_t>(imageBytes) - programHeaderOffset;
    if (static_cast<uint64_t>(programHeaderCount) > remaining / programHeaderBytes) return fail(result, "ELF program-header table exceeds file bounds");

    const uint64_t entryPoint = get_u64(image, 24);
    result->entryPoint = entryPoint;
    bool entryInExecutableLoad = false;
    bool expectedDataFound = expectedDataBytes == 0;
    bool expectedMutableDataFound = expectedMutableDataBytes == 0;
    bool expectedBaseSeen = false;
    LoadRange loadRanges[4] = {};
    uint16_t loadRangeCount = 0;
    for (uint16_t i = 0; i < programHeaderCount; ++i) {
        const uint64_t headerOffset64 = programHeaderOffset + static_cast<uint64_t>(i) * programHeaderBytes;
        if (headerOffset64 > imageBytes || headerOffset64 > 0xFFFFFFFFULL) return fail(result, "ELF program-header offset overflows validator bounds");
        const uint32_t headerOffset = static_cast<uint32_t>(headerOffset64);
        const uint32_t type = get_u32(image, headerOffset + 0);
        const uint32_t flags = get_u32(image, headerOffset + 4);
        const uint64_t fileOffset = get_u64(image, headerOffset + 8);
        const uint64_t virtualAddress = get_u64(image, headerOffset + 16);
        const uint64_t fileSize = get_u64(image, headerOffset + 32);
        const uint64_t memorySize = get_u64(image, headerOffset + 40);
        const uint64_t alignment = get_u64(image, headerOffset + 48);
        if (type == PROGRAM_TYPE_INTERP) return fail(result, "PT_INTERP is forbidden for bootstrap ELF");
        if (type == PROGRAM_TYPE_DYNAMIC) return fail(result, "PT_DYNAMIC is forbidden for bootstrap ELF");
        if (type != PROGRAM_TYPE_LOAD) continue;
        if (loadRangeCount >= 4) return fail(result, "ELF contains too many PT_LOAD segments");
        ++result->loadCount;
        if ((flags & ~(PROGRAM_FLAGS_READABLE | PROGRAM_FLAGS_WRITABLE | PROGRAM_FLAGS_EXECUTABLE)) != 0 ||
            (flags & PROGRAM_FLAGS_READABLE) == 0) return fail(result, "PT_LOAD permissions are invalid");
        if ((flags & PROGRAM_FLAGS_WRITABLE) != 0 && (flags & PROGRAM_FLAGS_EXECUTABLE) != 0)
            return fail(result, "writable executable PT_LOAD is forbidden");
        if (fileOffset > imageBytes || fileSize > static_cast<uint64_t>(imageBytes) - fileOffset) return fail(result, "PT_LOAD file range exceeds ELF bounds");
        if (fileSize > memorySize || memorySize == 0) return fail(result, "PT_LOAD file size exceeds memory size");
        uint64_t fileEnd = 0;
        if (!add_u64(fileOffset, fileSize, &fileEnd)) return fail(result, "PT_LOAD file range overflows");
        uint64_t virtualEnd = 0;
        if (!add_u64(virtualAddress, memorySize, &virtualEnd)) return fail(result, "PT_LOAD virtual range overflows");
        for (uint16_t prior = 0; prior < loadRangeCount; ++prior) {
            const LoadRange& range = loadRanges[prior];
            if ((virtualAddress < range.virtualEnd && range.virtualStart < virtualEnd) ||
                (fileOffset < range.fileEnd && range.fileStart < fileEnd))
                return fail(result, "PT_LOAD segments overlap");
        }
        loadRanges[loadRangeCount++] = {fileOffset, fileEnd, virtualAddress, virtualEnd};
        if (alignment > 1) {
            if (!is_power_of_two(alignment) || (fileOffset % alignment) != (virtualAddress % alignment)) return fail(result, "PT_LOAD alignment is invalid");
        }
        if (virtualAddress == expectedImageBase) {
            expectedBaseSeen = true;
            result->imageBase = virtualAddress;
        }
        if ((flags & PROGRAM_FLAGS_EXECUTABLE) != 0) {
            ++result->executableLoadCount;
            if (entryPoint >= virtualAddress && entryPoint < virtualEnd) {
                entryInExecutableLoad = true;
                const uint64_t entryDelta = entryPoint - virtualAddress;
                uint64_t codeOffset64 = 0;
                if (!add_u64(fileOffset, entryDelta, &codeOffset64) || codeOffset64 > 0xFFFFFFFFULL) return fail(result, "ELF entry file offset overflows");
                result->codeFileOffset = static_cast<uint32_t>(codeOffset64);
                if (entryDelta >= fileSize) return fail(result, "ELF entry point is not file-backed");
            }
        } else if ((flags & PROGRAM_FLAGS_WRITABLE) != 0) {
            if (expectedMutableDataBytes != 0 && fileSize >= expectedMutableDataBytes && virtualAddress >= expectedImageBase) {
                if (fileOffset > 0xFFFFFFFFULL) return fail(result, "ELF mutable-data file offset overflows");
                result->mutableDataFileOffset = static_cast<uint32_t>(fileOffset);
                result->mutableDataVirtualAddress = virtualAddress;
                result->mutableDataBytes = expectedMutableDataBytes;
                expectedMutableDataFound = true;
            }
        } else if (expectedDataBytes != 0 && fileSize >= expectedDataBytes && virtualAddress >= expectedImageBase) {
            if (fileOffset > 0xFFFFFFFFULL) return fail(result, "ELF data file offset overflows");
            result->dataFileOffset = static_cast<uint32_t>(fileOffset);
            result->dataVirtualAddress = virtualAddress;
            result->dataBytes = expectedDataBytes;
            expectedDataFound = true;
        }
    }
    if (result->loadCount == 0) return fail(result, "ELF contains no PT_LOAD segment");
    if (result->executableLoadCount == 0) return fail(result, "ELF contains no executable PT_LOAD segment");
    if (!expectedBaseSeen) return fail(result, "ELF PT_LOAD image base does not match expected base");

    uint64_t expectedEntry = 0;
    if (!add_u64(expectedImageBase, expectedCodeOffset + expectedEntryCodeOffset, &expectedEntry)) return fail(result, "expected ELF entry address overflows");
    if (entryPoint != expectedEntry) return fail(result, "ELF entry point does not match expected code address");
    if (!entryInExecutableLoad) return fail(result, "ELF entry point is outside executable PT_LOAD");
    if (expectedDataBytes != 0 && (!expectedData || !expectedDataFound)) return fail(result, "ELF source data is not in a read-only PT_LOAD");
    if (expectedMutableDataBytes != 0 && (!expectedMutableData || !expectedMutableDataFound)) return fail(result, "ELF mutable data is not in a writable PT_LOAD");

    uint32_t expectedCodeEnd = 0;
    if (expectedCodeBytes != 0 && (!expectedCode || !add_u32(expectedCodeOffset, expectedCodeBytes, &expectedCodeEnd) || expectedCodeEnd > imageBytes)) return fail(result, "expected generated code range exceeds ELF file");
    if (expectedCodeBytes != 0) {
        if (result->codeFileOffset != expectedCodeOffset + expectedEntryCodeOffset) return fail(result, "ELF entry does not map to expected code offset");
        for (uint32_t i = 0; i < expectedCodeBytes; ++i)
            if (image[expectedCodeOffset + i] != expectedCode[i]) return fail(result, "ELF code bytes do not match generated AMD64 code");
    }
    if (expectedDataBytes != 0) {
        uint32_t end = 0;
        if (!add_u32(result->dataFileOffset, expectedDataBytes, &end) || end > imageBytes) return fail(result, "expected source data range exceeds ELF file");
        for (uint32_t i = 0; i < expectedDataBytes; ++i)
            if (image[result->dataFileOffset + i] != expectedData[i]) return fail(result, "ELF source data does not match compiler output");
    }
    if (expectedMutableDataBytes != 0) {
        uint32_t end = 0;
        if (!add_u32(result->mutableDataFileOffset, expectedMutableDataBytes, &end) || end > imageBytes) return fail(result, "expected mutable data range exceeds ELF file");
        for (uint32_t i = 0; i < expectedMutableDataBytes; ++i)
            if (image[result->mutableDataFileOffset + i] != expectedMutableData[i]) return fail(result, "ELF mutable data does not match compiler output");
    }
    result->valid = true;
    result->error = "";
    return true;
}

bool validate_bootstrap_elf(const uint8_t* image,
                            uint32_t imageBytes,
                            uint64_t expectedImageBase,
                            uint32_t expectedCodeOffset,
                            const uint8_t* expectedCode,
                            uint32_t expectedCodeBytes,
                            ElfValidationResult* result,
                            const uint8_t* expectedData,
                            uint32_t expectedDataBytes,
                            uint32_t expectedEntryCodeOffset)
{
    return validate_bootstrap_elf_impl(image, imageBytes, expectedImageBase, expectedCodeOffset,
                                       expectedCode, expectedCodeBytes, result, expectedData,
                                       expectedDataBytes, nullptr, 0, expectedEntryCodeOffset);
}

bool validate_bootstrap_elf(const uint8_t* image,
                            uint32_t imageBytes,
                            uint64_t expectedImageBase,
                            uint32_t expectedCodeOffset,
                            const uint8_t* expectedCode,
                            uint32_t expectedCodeBytes,
                            ElfValidationResult* result,
                            const uint8_t* expectedData,
                            uint32_t expectedDataBytes,
                            const uint8_t* expectedMutableData,
                            uint32_t expectedMutableDataBytes,
                            uint32_t expectedEntryCodeOffset)
{
    return validate_bootstrap_elf_impl(image, imageBytes, expectedImageBase, expectedCodeOffset,
                                       expectedCode, expectedCodeBytes, result, expectedData,
                                       expectedDataBytes, expectedMutableData, expectedMutableDataBytes,
                                       expectedEntryCodeOffset);
}

} // namespace compiler
} // namespace kernel
