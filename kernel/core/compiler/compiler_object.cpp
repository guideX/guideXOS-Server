//
// Bounded persistent guideXOS relocatable object format.
//
#include "compiler_object.h"

namespace kernel {
namespace compiler {
namespace {

static const uint8_t kMagic[4] = { 'G', 'X', 'O', '1' };
static const uint32_t kFlagHasEntry = 1U << 0;
static const uint32_t kFlagHasHostLog = 1U << 1;
static const uint32_t kFlagReturnConstantValid = 1U << 2;

static uint32_t text_length(const char* value, uint32_t capacity)
{
    if (!value) return 0;
    uint32_t length = 0;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

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

static bool valid_name(const char* value, uint32_t capacity)
{
    if (!value || capacity == 0) return false;
    for (uint32_t i = 0; i < capacity; ++i) {
        if (value[i] == '\0') return i != 0;
    }
    return false;
}

static bool range_valid(uint32_t offset, uint32_t width, uint32_t size)
{
    return offset <= size && width <= size - offset;
}

static uint64_t hash_bytes_with_zero_checksum(const uint8_t* bytes, uint32_t count)
{
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t value = (i >= 92U && i < 100U) ? 0 : bytes[i];
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

class Writer {
public:
    Writer(uint8_t* bytes, uint32_t capacity) : m_bytes(bytes), m_capacity(capacity), m_position(0), m_ok(bytes != nullptr) {}

    bool ok() const { return m_ok; }
    uint32_t position() const { return m_position; }

    void u8(uint8_t value) { if (!reserve(1)) return; m_bytes[m_position++] = value; }
    void u16(uint16_t value) { u8(static_cast<uint8_t>(value)); u8(static_cast<uint8_t>(value >> 8)); }
    void u32(uint32_t value) { for (uint32_t i = 0; i < 4; ++i) u8(static_cast<uint8_t>(value >> (i * 8U))); }
    void u64(uint64_t value) { for (uint32_t i = 0; i < 8; ++i) u8(static_cast<uint8_t>(value >> (i * 8U))); }
    void bytes(const uint8_t* value, uint32_t count) {
        if (!reserve(count)) return;
        for (uint32_t i = 0; i < count; ++i) m_bytes[m_position++] = value[i];
    }
    void zeros(uint32_t count) {
        if (!reserve(count)) return;
        for (uint32_t i = 0; i < count; ++i) m_bytes[m_position++] = 0;
    }
    void patch_u16(uint32_t offset, uint16_t value) {
        if (offset > m_capacity || m_capacity - offset < 2U) { m_ok = false; return; }
        m_bytes[offset] = static_cast<uint8_t>(value); m_bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    }
    void patch_u32(uint32_t offset, uint32_t value) {
        if (offset > m_capacity || m_capacity - offset < 4U) { m_ok = false; return; }
        for (uint32_t i = 0; i < 4; ++i) m_bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
    }
    void patch_u64(uint32_t offset, uint64_t value) {
        if (offset > m_capacity || m_capacity - offset < 8U) { m_ok = false; return; }
        for (uint32_t i = 0; i < 8; ++i) m_bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
    }

private:
    bool reserve(uint32_t count) {
        if (!m_ok || count > m_capacity - m_position) { m_ok = false; return false; }
        return true;
    }
    uint8_t* m_bytes;
    uint32_t m_capacity;
    uint32_t m_position;
    bool m_ok;
};

class Reader {
public:
    Reader(const uint8_t* bytes, uint32_t count) : m_bytes(bytes), m_count(count), m_position(0), m_ok(bytes != nullptr) {}
    bool ok() const { return m_ok; }
    uint32_t position() const { return m_position; }
    uint8_t u8() { if (!reserve(1)) return 0; return m_bytes[m_position++]; }
    uint16_t u16() { return static_cast<uint16_t>(u8()) | static_cast<uint16_t>(u8()) << 8; }
    uint32_t u32() { uint32_t value = 0; for (uint32_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(u8()) << (i * 8U); return value; }
    uint64_t u64() { uint64_t value = 0; for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(u8()) << (i * 8U); return value; }
    bool bytes(uint8_t* output, uint32_t count) {
        if (!reserve(count)) return false;
        for (uint32_t i = 0; i < count; ++i) output[i] = m_bytes[m_position++];
        return true;
    }
    const uint8_t* view(uint32_t count) {
        if (!reserve(count)) return nullptr;
        const uint8_t* result = m_bytes + m_position;
        m_position += count;
        return result;
    }

private:
    bool reserve(uint32_t count) {
        if (!m_ok || count > m_count - m_position) { m_ok = false; return false; }
        return true;
    }
    const uint8_t* m_bytes;
    uint32_t m_count;
    uint32_t m_position;
    bool m_ok;
};

static void write_location(Writer& writer, const SourceLocation& location)
{
    writer.u32(location.offset); writer.u32(location.line); writer.u32(location.column);
}

static SourceLocation read_location(Reader& reader)
{
    SourceLocation location = {};
    location.offset = reader.u32(); location.line = reader.u32(); location.column = reader.u32();
    return location;
}

static void write_fixed_name(Writer& writer, const char* value, uint32_t capacity)
{
    for (uint32_t i = 0; i < capacity; ++i) writer.u8(static_cast<uint8_t>(value[i]));
}

static bool read_fixed_name(Reader& reader, char* output, uint32_t capacity)
{
    if (!reader.bytes(reinterpret_cast<uint8_t*>(output), capacity)) return false;
    return valid_name(output, capacity);
}

static void write_export(Writer& writer, const ExportSymbol& symbol)
{
    writer.u8(static_cast<uint8_t>(symbol.kind));
    writer.u8(symbol.isEntry ? 1 : 0);
    writer.u16(0);
    write_fixed_name(writer, symbol.name, sizeof(symbol.name));
    writer.u32(symbol.moduleCodeOffset); writer.u32(symbol.moduleDataOffset);
    writer.u32(symbol.size); writer.u32(symbol.alignment);
    writer.u16(symbol.elementCount); writer.u16(symbol.elementSize);
    writer.u16(symbol.parameterCount); writer.u16(0);
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i)
        writer.u8(static_cast<uint8_t>(symbol.parameterKinds[i]));
    write_location(writer, symbol.location);
}

static bool read_export(Reader& reader, ExportSymbol& symbol)
{
    symbol = {};
    const uint8_t kind = reader.u8();
    const uint8_t entry = reader.u8();
    (void)reader.u16();
    if (kind > static_cast<uint8_t>(SymbolKind::DataArray) || entry > 1 ||
        !read_fixed_name(reader, symbol.name, sizeof(symbol.name))) return false;
    symbol.kind = static_cast<SymbolKind>(kind); symbol.isEntry = entry != 0;
    symbol.moduleCodeOffset = reader.u32(); symbol.moduleDataOffset = reader.u32();
    symbol.size = reader.u32(); symbol.alignment = reader.u32();
    symbol.elementCount = reader.u16(); symbol.elementSize = reader.u16();
    symbol.parameterCount = reader.u16(); (void)reader.u16();
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) {
        const uint8_t parameterKind = reader.u8();
        if (parameterKind > static_cast<uint8_t>(ParameterKind::AppContextPointer)) return false;
        symbol.parameterKinds[i] = static_cast<ParameterKind>(parameterKind);
    }
    symbol.location = read_location(reader);
    return reader.ok();
}

static void write_import(Writer& writer, const ImportSymbol& symbol)
{
    writer.u8(static_cast<uint8_t>(symbol.kind)); writer.u8(0); writer.u16(0);
    write_fixed_name(writer, symbol.name, sizeof(symbol.name));
    writer.u16(symbol.expectedParameterCount); writer.u16(0);
    writer.u32(symbol.size); writer.u32(symbol.alignment);
    writer.u16(symbol.elementCount); writer.u16(symbol.elementSize);
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i)
        writer.u8(static_cast<uint8_t>(symbol.parameterKinds[i]));
    write_location(writer, symbol.location);
}

static bool read_import(Reader& reader, ImportSymbol& symbol)
{
    symbol = {};
    const uint8_t kind = reader.u8(); (void)reader.u8(); (void)reader.u16();
    if (kind > static_cast<uint8_t>(SymbolKind::DataArray) ||
        !read_fixed_name(reader, symbol.name, sizeof(symbol.name))) return false;
    symbol.kind = static_cast<SymbolKind>(kind);
    symbol.expectedParameterCount = reader.u16(); (void)reader.u16();
    symbol.size = reader.u32(); symbol.alignment = reader.u32();
    symbol.elementCount = reader.u16(); symbol.elementSize = reader.u16();
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) {
        const uint8_t parameterKind = reader.u8();
        if (parameterKind > static_cast<uint8_t>(ParameterKind::AppContextPointer)) return false;
        symbol.parameterKinds[i] = static_cast<ParameterKind>(parameterKind);
    }
    symbol.location = read_location(reader);
    return reader.ok();
}

static void write_relocation(Writer& writer, const RelocationRecord& relocation)
{
    writer.u8(static_cast<uint8_t>(relocation.kind)); writer.u8(relocation.width); writer.u16(0);
    writer.u32(relocation.patchOffset); writer.u32(relocation.dataOffset);
    write_fixed_name(writer, relocation.targetSymbolName, sizeof(relocation.targetSymbolName));
    write_location(writer, relocation.location);
}

static bool read_relocation(Reader& reader, RelocationRecord& relocation)
{
    relocation = {};
    const uint8_t kind = reader.u8();
    relocation.width = reader.u8(); (void)reader.u16();
    if (kind > static_cast<uint8_t>(RelocationKind::GlobalDataAddress64)) return false;
    relocation.kind = static_cast<RelocationKind>(kind);
    relocation.patchOffset = reader.u32(); relocation.dataOffset = reader.u32();
    if (!reader.bytes(reinterpret_cast<uint8_t*>(relocation.targetSymbolName), sizeof(relocation.targetSymbolName)) ||
        (relocation.kind != RelocationKind::DataAddress64 &&
         !valid_name(relocation.targetSymbolName, sizeof(relocation.targetSymbolName)))) return false;
    relocation.location = read_location(reader);
    return reader.ok();
}

static bool header_from_bytes(const uint8_t* bytes, uint32_t byteCount,
                              GxoObjectHeaderView* header, Diagnostics& diagnostics)
{
    if (!bytes || !header || byteCount < COMPILER_GXO_HEADER_BYTES || byteCount > COMPILER_MAX_OBJECT_BYTES) {
        diagnostics.error({0, 1, 1}, "GXO header is truncated or exceeds the object bound", "object");
        return false;
    }
    if (bytes[0] != kMagic[0] || bytes[1] != kMagic[1] || bytes[2] != kMagic[2] || bytes[3] != kMagic[3]) {
        diagnostics.error({0, 1, 1}, "GXO magic is invalid", "object"); return false;
    }
    Reader reader(bytes + 4, COMPILER_GXO_HEADER_BYTES - 4);
    *header = {};
    header->formatVersion = reader.u16(); header->headerSize = reader.u16();
    header->targetArchitecture = reader.u32(); header->targetAbi = reader.u32();
    header->compilerObjectAbiVersion = reader.u32(); header->flags = reader.u32();
    header->sourceHash = reader.u64(); header->sourceSize = reader.u32();
    header->sourcePathBytes = reader.u32(); header->codeSize = reader.u32();
    header->rodataSize = reader.u32(); header->rwdataSize = reader.u32();
    header->exportCount = reader.u32(); header->importCount = reader.u32();
    header->relocationCount = reader.u32(); header->functionCount = reader.u32();
    header->globalCount = reader.u32(); header->callGraphEdgeCount = reader.u32();
    header->recursiveSccCount = reader.u16(); (void)reader.u16();
    header->entryCodeOffset = reader.u32(); header->payloadBytes = reader.u32();
    header->objectBytes = reader.u32(); header->objectChecksum = reader.u64();
    if (!reader.ok() || header->headerSize != COMPILER_GXO_HEADER_BYTES ||
        header->formatVersion != COMPILER_OBJECT_FORMAT_VERSION ||
        header->targetArchitecture != COMPILER_OBJECT_ARCH_AMD64 ||
        header->targetAbi != COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1 ||
        header->compilerObjectAbiVersion != COMPILER_OBJECT_ABI_VERSION) {
        diagnostics.error({0, 1, 1}, "GXO version, target, or ABI identity is unsupported", "object"); return false;
    }
    if (header->objectBytes != byteCount || header->payloadBytes != byteCount - COMPILER_GXO_HEADER_BYTES ||
        header->sourcePathBytes == 0 || header->sourcePathBytes >= COMPILER_MAX_SOURCE_PATH_BYTES ||
        header->codeSize > COMPILER_MAX_CODE_BYTES || header->rodataSize > COMPILER_MAX_LINKED_DATA_BYTES ||
        header->rwdataSize > COMPILER_MAX_LINKED_DATA_BYTES || header->functionCount > COMPILER_MAX_FUNCTIONS ||
        header->globalCount > COMPILER_MAX_GLOBALS || header->exportCount > COMPILER_MAX_MODULE_SYMBOLS ||
        header->importCount > COMPILER_MAX_MODULE_SYMBOLS || header->relocationCount > COMPILER_MAX_MODULE_RELOCATIONS ||
        header->callGraphEdgeCount > COMPILER_MAX_CALL_GRAPH_EDGES || header->recursiveSccCount > COMPILER_MAX_FUNCTIONS) {
        diagnostics.error({0, 1, 1}, "GXO header contains an out-of-bounds count or size", "object"); return false;
    }
    if (gxo_object_checksum(bytes, byteCount) != header->objectChecksum) {
        diagnostics.error({0, 1, 1}, "GXO checksum mismatch", "object"); return false;
    }
    return true;
}

} // namespace

uint64_t gxo_object_checksum(const uint8_t* bytes, uint32_t byteCount)
{
    return bytes && byteCount >= COMPILER_GXO_HEADER_BYTES
        ? hash_bytes_with_zero_checksum(bytes, byteCount) : 0;
}

bool inspect_gxo_header(const uint8_t* bytes, uint32_t byteCount,
                        GxoObjectHeaderView* header, Diagnostics& diagnostics)
{
    return header_from_bytes(bytes, byteCount, header, diagnostics);
}

bool serialize_gxo_object(const CompiledModule& module,
                          uint8_t* output, uint32_t capacity, uint32_t* outputBytes)
{
    if (!output || !outputBytes || capacity < COMPILER_GXO_HEADER_BYTES || capacity > COMPILER_MAX_OBJECT_BYTES ||
        module.sourcePath[0] == '\0' || !valid_name(module.sourcePath, sizeof(module.sourcePath)) ||
        module.sourceBytes > COMPILER_MAX_SOURCE_BYTES || module.codeBytes > COMPILER_MAX_CODE_BYTES ||
        module.dataBytes > COMPILER_MAX_LINKED_DATA_BYTES || module.mutableDataBytes > COMPILER_MAX_LINKED_DATA_BYTES ||
        module.functionCount > COMPILER_MAX_FUNCTIONS || module.globalCount > COMPILER_MAX_GLOBALS ||
        module.exportCount > COMPILER_MAX_MODULE_SYMBOLS || module.importCount > COMPILER_MAX_MODULE_SYMBOLS ||
        module.relocationCount > COMPILER_MAX_MODULE_RELOCATIONS || module.recursiveSccCount > COMPILER_MAX_FUNCTIONS) return false;
    for (uint32_t i = 0; i < module.functionCount; ++i)
        if (module.localStorageBytes[i] > COMPILER_MAX_LOCAL_STORAGE_BYTES) return false;
    const uint32_t sourcePathBytes = text_length(module.sourcePath, sizeof(module.sourcePath));
    uint32_t callGraphEdges = 0;
    for (uint32_t i = 0; i < module.functionCount; ++i)
        for (uint32_t j = 0; j < module.functionCount; ++j)
            if (module.callGraph[i][j]) ++callGraphEdges;
    if (callGraphEdges > COMPILER_MAX_CALL_GRAPH_EDGES) return false;

    Writer writer(output, capacity);
    writer.bytes(kMagic, 4);
    writer.u16(COMPILER_OBJECT_FORMAT_VERSION); writer.u16(COMPILER_GXO_HEADER_BYTES);
    writer.u32(COMPILER_OBJECT_ARCH_AMD64); writer.u32(COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1);
    writer.u32(COMPILER_OBJECT_ABI_VERSION);
    uint32_t flags = 0;
    if (module.hasEntry) flags |= kFlagHasEntry;
    if (module.hasHostLog) flags |= kFlagHasHostLog;
    if (module.returnConstantValid) flags |= kFlagReturnConstantValid;
    writer.u32(flags); writer.u64(module.sourceHash); writer.u32(module.sourceBytes); writer.u32(sourcePathBytes);
    writer.u32(module.codeBytes); writer.u32(module.dataBytes); writer.u32(module.mutableDataBytes);
    writer.u32(module.exportCount); writer.u32(module.importCount); writer.u32(module.relocationCount);
    writer.u32(module.functionCount); writer.u32(module.globalCount); writer.u32(callGraphEdges);
    writer.u16(module.recursiveSccCount); writer.u16(0); writer.u32(module.entryCodeOffset);
    writer.u32(0); writer.u32(0); writer.u64(0);
    writer.bytes(reinterpret_cast<const uint8_t*>(module.sourcePath), sourcePathBytes);
    writer.u32(module.tokenCount); writer.u32(static_cast<uint32_t>(module.returnConstant));
    for (uint32_t i = 0; i < module.functionCount; ++i) writer.u8(module.recursiveFunction[i] ? 1 : 0);
    for (uint32_t i = 0; i < module.functionCount; ++i) writer.u32(module.localStorageBytes[i]);
    for (uint32_t i = 0; i < module.functionCount; ++i)
        for (uint32_t j = 0; j < module.functionCount; ++j) writer.u8(module.callGraph[i][j] ? 1 : 0);
    writer.bytes(module.code, module.codeBytes); writer.bytes(module.data, module.dataBytes);
    writer.bytes(module.mutableData, module.mutableDataBytes);
    for (uint32_t i = 0; i < module.exportCount; ++i) write_export(writer, module.exports[i]);
    for (uint32_t i = 0; i < module.importCount; ++i) write_import(writer, module.imports[i]);
    for (uint32_t i = 0; i < module.relocationCount; ++i) write_relocation(writer, module.relocations[i]);
    if (!writer.ok() || writer.position() > COMPILER_MAX_OBJECT_BYTES) return false;
    const uint32_t bytes = writer.position();
    writer.patch_u32(84, bytes - COMPILER_GXO_HEADER_BYTES);
    writer.patch_u32(88, bytes);
    const uint64_t checksum = gxo_object_checksum(output, bytes);
    writer.patch_u64(92, checksum);
    *outputBytes = bytes;
    return writer.ok();
}

bool deserialize_gxo_object(const uint8_t* bytes, uint32_t byteCount,
                            CompiledModule* module, Diagnostics& diagnostics)
{
    if (!module) return false;
    *module = {};
    GxoObjectHeaderView header = {};
    if (!header_from_bytes(bytes, byteCount, &header, diagnostics)) return false;
    Reader reader(bytes + COMPILER_GXO_HEADER_BYTES, byteCount - COMPILER_GXO_HEADER_BYTES);
    const uint8_t* path = reader.view(header.sourcePathBytes);
    if (!path) return false;
    for (uint32_t i = 0; i < header.sourcePathBytes; ++i) {
        if (path[i] == 0) { diagnostics.error({0, 1, 1}, "GXO source path contains an embedded NUL", "object"); return false; }
        module->sourcePath[i] = static_cast<char>(path[i]);
    }
    module->sourcePath[header.sourcePathBytes] = '\0';
    module->sourceHash = header.sourceHash; module->sourceBytes = header.sourceSize;
    module->tokenCount = reader.u32(); module->returnConstant = static_cast<int32_t>(reader.u32());
    module->hasEntry = (header.flags & kFlagHasEntry) != 0;
    module->hasHostLog = (header.flags & kFlagHasHostLog) != 0;
    module->returnConstantValid = (header.flags & kFlagReturnConstantValid) != 0;
    module->codeBytes = header.codeSize; module->dataBytes = header.rodataSize;
    module->mutableDataBytes = header.rwdataSize; module->exportCount = header.exportCount;
    module->importCount = header.importCount; module->relocationCount = header.relocationCount;
    module->functionCount = header.functionCount; module->globalCount = header.globalCount;
    module->recursiveSccCount = header.recursiveSccCount; module->entryCodeOffset = header.entryCodeOffset;
    for (uint32_t i = 0; i < module->functionCount; ++i) {
        const uint8_t flag = reader.u8();
        if (flag > 1) { diagnostics.error({0, 1, 1}, "GXO recursive metadata is malformed", "object"); return false; }
        module->recursiveFunction[i] = flag != 0;
    }
    for (uint32_t i = 0; i < module->functionCount; ++i) {
        module->localStorageBytes[i] = reader.u32();
        if (module->localStorageBytes[i] > COMPILER_MAX_LOCAL_STORAGE_BYTES) {
            diagnostics.error({0, 1, 1}, "GXO local-array storage metadata is out of bounds", "object");
            return false;
        }
    }
    uint32_t edgeCount = 0;
    for (uint32_t i = 0; i < module->functionCount; ++i) {
        for (uint32_t j = 0; j < module->functionCount; ++j) {
            const uint8_t edge = reader.u8();
            if (edge > 1) { diagnostics.error({0, 1, 1}, "GXO call graph metadata is malformed", "object"); return false; }
            module->callGraph[i][j] = edge != 0; if (edge) ++edgeCount;
        }
    }
    if (edgeCount != header.callGraphEdgeCount || edgeCount > COMPILER_MAX_CALL_GRAPH_EDGES ||
        !reader.bytes(module->code, module->codeBytes) || !reader.bytes(module->data, module->dataBytes) ||
        !reader.bytes(module->mutableData, module->mutableDataBytes)) {
        diagnostics.error({0, 1, 1}, "GXO payload is truncated", "object"); return false;
    }
    for (uint32_t i = 0; i < module->exportCount; ++i) {
        if (!read_export(reader, module->exports[i])) { diagnostics.error({0, 1, 1}, "GXO export table is malformed", "object"); return false; }
        const ExportSymbol& symbol = module->exports[i];
        if ((symbol.kind == SymbolKind::Function && (symbol.moduleCodeOffset >= module->codeBytes || symbol.parameterCount > COMPILER_MAX_PARAMETERS)) ||
            (symbol_is_data(symbol.kind) && (symbol.elementCount == 0 || symbol.elementCount > COMPILER_MAX_ARRAY_ELEMENTS ||
                symbol.elementSize != 4 || symbol.size != static_cast<uint32_t>(symbol.elementCount) * symbol.elementSize ||
                symbol.alignment != 4 ||
                !range_valid(symbol.moduleDataOffset, symbol.size, module->mutableDataBytes)))) {
            diagnostics.error(symbol.location, "GXO export offset or signature is out of bounds", "object"); return false;
        }
    }
    for (uint32_t i = 0; i < module->importCount; ++i) {
        if (!read_import(reader, module->imports[i])) { diagnostics.error({0, 1, 1}, "GXO import table is malformed", "object"); return false; }
        if (module->imports[i].kind == SymbolKind::Function && module->imports[i].expectedParameterCount > COMPILER_MAX_PARAMETERS) {
            diagnostics.error(module->imports[i].location, "GXO import signature is out of bounds", "object"); return false;
        }
        if (symbol_is_data(module->imports[i].kind) &&
            (module->imports[i].elementCount == 0 || module->imports[i].elementCount > COMPILER_MAX_ARRAY_ELEMENTS ||
             module->imports[i].elementSize != 4 ||
             module->imports[i].size != static_cast<uint32_t>(module->imports[i].elementCount) * module->imports[i].elementSize ||
             module->imports[i].alignment != 4)) {
            diagnostics.error(module->imports[i].location, "GXO data import signature is malformed", "object"); return false;
        }
    }
    for (uint32_t i = 0; i < module->relocationCount; ++i) {
        if (!read_relocation(reader, module->relocations[i])) { diagnostics.error({0, 1, 1}, "GXO relocation table is malformed", "object"); return false; }
        const RelocationRecord& relocation = module->relocations[i];
        const uint32_t expectedWidth = relocation.kind == RelocationKind::CallRel32 ? 4U : 8U;
        if (relocation.width != expectedWidth || !range_valid(relocation.patchOffset, relocation.width, module->codeBytes) ||
            (relocation.kind == RelocationKind::DataAddress64 && relocation.dataOffset >= module->dataBytes)) {
            diagnostics.error(relocation.location, "GXO relocation range or width is invalid", "object"); return false;
        }
    }
    if (!reader.ok() || reader.position() != byteCount - COMPILER_GXO_HEADER_BYTES ||
        (module->hasEntry && module->entryCodeOffset >= module->codeBytes)) {
        diagnostics.error({0, 1, 1}, "GXO has trailing or inconsistent payload data", "object"); return false;
    }
    return true;
}

bool gxo_object_identity_matches(const CompiledModule& module,
                                 const char* normalizedSourcePath,
                                 uint32_t sourceBytes, uint64_t sourceHash)
{
    return normalizedSourcePath && names_equal(module.sourcePath, normalizedSourcePath) &&
        module.sourceBytes == sourceBytes && module.sourceHash == sourceHash;
}

} // namespace compiler
} // namespace kernel
