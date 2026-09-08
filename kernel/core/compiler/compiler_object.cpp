//
// Bounded ELF64 ET_REL object writer and reader for the in-OS compiler.
//

#include "compiler_object.h"

namespace kernel {
namespace compiler {
namespace {

static const uint8_t kElfMagic[4] = {0x7F, 'E', 'L', 'F'};
static const uint8_t kMetaMagic[4] = {'G', 'X', 'M', 'O'};
static const uint16_t kElfTypeRel = 1;
static const uint16_t kElfMachineX8664 = 62;
static const uint32_t kElfVersionCurrent = 1;
static const uint32_t kSectionNull = 0;
static const uint32_t kSectionProgBits = 1;
static const uint32_t kSectionSymTab = 2;
static const uint32_t kSectionStrTab = 3;
static const uint32_t kSectionRela = 4;
static const uint32_t kSectionNoBits = 8;
static const uint64_t kSectionFlagWrite = 1;
static const uint64_t kSectionFlagAlloc = 2;
static const uint64_t kSectionFlagExec = 4;
static const uint32_t kSymbolBindLocal = 0;
static const uint32_t kSymbolBindGlobal = 1;
static const uint32_t kSymbolTypeNoType = 0;
static const uint32_t kSymbolTypeObject = 1;
static const uint32_t kSymbolTypeFunc = 2;
static const uint32_t kSymbolTypeSection = 3;
static const uint16_t kSectionUndefined = 0;
static const uint32_t kRelX8664_64 = 1;
static const uint32_t kRelX8664Pc32 = 2;
static const uint32_t kMaxObjectSections = 16;
static const uint32_t kMaxObjectSymbols = 1 + kMaxObjectSections + COMPILER_MAX_MODULE_SYMBOLS * 2;
static const uint32_t kMaxObjectStringBytes = kMaxObjectSymbols * COMPILER_FUNCTION_NAME_CAPACITY + 1;
static const uint32_t kMaxSectionNameBytes = 192;
static const uint32_t kFlagHasEntry = 1U << 0;
static const uint32_t kFlagHasHostLog = 1U << 1;
static const uint32_t kFlagReturnConstantValid = 1U << 2;
static const uint32_t kKnownMetaFlags = kFlagHasEntry | kFlagHasHostLog | kFlagReturnConstantValid;

// The kernel build service is single-job. These bounded scratch buffers keep
// the ELF writer/parser stack-safe and avoid any host/runtime allocation.
static uint8_t s_meta[COMPILER_MAX_OBJECT_BYTES];
static char s_strtab[kMaxObjectStringBytes];
static char s_shstrtab[kMaxSectionNameBytes];

struct SectionDesc {
    const char* name;
    uint32_t nameOffset;
    uint32_t type;
    uint64_t flags;
    uint32_t link;
    uint32_t info;
    uint32_t align;
    uint32_t entsize;
    uint32_t offset;
    uint32_t size;
    const uint8_t* data;
};

struct SectionView {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t align;
    uint64_t entsize;
};

struct ParsedElf {
    ElfObjectHeaderView header;
    SectionView sections[kMaxObjectSections];
    uint16_t sectionCount;
    int32_t text;
    int32_t rodata;
    int32_t data;
    int32_t relaText;
    int32_t symtab;
    int32_t strtab;
    int32_t shstrtab;
    int32_t meta;
};

class Writer {
public:
    Writer(uint8_t* bytes, uint32_t capacity)
        : m_bytes(bytes), m_capacity(capacity), m_position(0), m_ok(bytes != nullptr) {}

    bool ok() const { return m_ok; }
    uint32_t position() const { return m_position; }
    void u8(uint8_t value) { if (reserve(1)) m_bytes[m_position++] = value; }
    void u16(uint16_t value) { u8(static_cast<uint8_t>(value)); u8(static_cast<uint8_t>(value >> 8)); }
    void u32(uint32_t value) { for (uint32_t i = 0; i < 4; ++i) u8(static_cast<uint8_t>(value >> (i * 8U))); }
    void u64(uint64_t value) { for (uint32_t i = 0; i < 8; ++i) u8(static_cast<uint8_t>(value >> (i * 8U))); }
    void bytes(const uint8_t* value, uint32_t count) {
        if (count != 0 && !value) { m_ok = false; return; }
        if (!reserve(count)) return;
        for (uint32_t i = 0; i < count; ++i) m_bytes[m_position++] = value[i];
    }
    void patch_u32(uint32_t offset, uint32_t value) {
        if (!range(offset, 4)) { m_ok = false; return; }
        for (uint32_t i = 0; i < 4; ++i) m_bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
    }
private:
    bool range(uint32_t offset, uint32_t size) const { return offset <= m_capacity && size <= m_capacity - offset; }
    bool reserve(uint32_t count) {
        if (!m_ok || m_position > m_capacity || count > m_capacity - m_position) { m_ok = false; return false; }
        return true;
    }
    uint8_t* m_bytes;
    uint32_t m_capacity;
    uint32_t m_position;
    bool m_ok;
};

class Reader {
public:
    Reader(const uint8_t* bytes, uint32_t count)
        : m_bytes(bytes), m_count(count), m_position(0), m_ok(bytes != nullptr) {}
    bool ok() const { return m_ok; }
    uint32_t position() const { return m_position; }
    uint8_t u8() { if (!reserve(1)) return 0; return m_bytes[m_position++]; }
    uint16_t u16() { return static_cast<uint16_t>(u8()) | static_cast<uint16_t>(u8()) << 8; }
    uint32_t u32() { uint32_t value = 0; for (uint32_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(u8()) << (i * 8U); return value; }
    uint64_t u64() { uint64_t value = 0; for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(u8()) << (i * 8U); return value; }
    bool bytes(uint8_t* output, uint32_t count) {
        if (!output || !reserve(count)) return false;
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
        if (!m_ok || m_position > m_count || count > m_count - m_position) { m_ok = false; return false; }
        return true;
    }
    const uint8_t* m_bytes;
    uint32_t m_count;
    uint32_t m_position;
    bool m_ok;
};

static uint16_t get_u16(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1]) << 8;
}

static uint32_t get_u32(const uint8_t* bytes, uint32_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
        static_cast<uint32_t>(bytes[offset + 1]) << 8 |
        static_cast<uint32_t>(bytes[offset + 2]) << 16 |
        static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

static uint64_t get_u64(const uint8_t* bytes, uint32_t offset)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8U);
    return value;
}

static void put_u16(uint8_t* bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value); bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

static void put_u32(uint8_t* bytes, uint32_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
}

static void put_u64(uint8_t* bytes, uint32_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8U));
}

static void clear_bytes(uint8_t* bytes, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) bytes[i] = 0;
}

static bool add_u32(uint32_t left, uint32_t right, uint32_t* result)
{
    if (!result || left > 0xFFFFFFFFU - right) return false;
    *result = left + right;
    return true;
}

static bool align_u32(uint32_t value, uint32_t alignment, uint32_t* result)
{
    if (!result || alignment == 0 || (alignment & (alignment - 1U)) != 0) return false;
    const uint32_t mask = alignment - 1U;
    if (value > 0xFFFFFFFFU - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static bool range64(uint64_t offset, uint64_t size, uint32_t limit)
{
    return offset <= limit && size <= static_cast<uint64_t>(limit) - offset;
}

static bool range32(uint32_t offset, uint32_t width, uint32_t size)
{
    return offset <= size && width <= size - offset;
}

static bool names_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    uint32_t i = 0;
    while (left[i] || right[i]) { if (left[i] != right[i]) return false; ++i; }
    return true;
}

static uint32_t text_length(const char* value, uint32_t capacity)
{
    if (!value) return 0;
    uint32_t length = 0;
    while (length < capacity && value[length]) ++length;
    return length;
}

static bool valid_name(const char* value, uint32_t capacity)
{
    if (!value || capacity == 0) return false;
    for (uint32_t i = 0; i < capacity; ++i) if (value[i] == '\0') return i != 0;
    return false;
}

static bool valid_dependency_path(const char* value, uint32_t capacity)
{
    if (!value || capacity == 0 || value[0] == '\0' || value[0] == '/' || value[0] == '\\') return false;
    uint32_t length = text_length(value, capacity);
    if (length == 0 || length >= capacity) return false;
    uint32_t componentStart = 0;
    for (uint32_t i = 0; i <= length; ++i) {
        if (value[i] != '/' && value[i] != '\\' && value[i] != '\0') continue;
        const uint32_t componentBytes = i - componentStart;
        if (componentBytes == 0 || (componentBytes == 1 && value[componentStart] == '.') ||
            (componentBytes == 2 && value[componentStart] == '.' && value[componentStart + 1] == '.')) return false;
        componentStart = i + 1;
    }
    return true;
}

static bool copy_text(char* output, uint32_t capacity, const char* input)
{
    if (!output || !input || capacity == 0) return false;
    uint32_t i = 0;
    while (i + 1 < capacity && input[i]) { output[i] = input[i]; ++i; }
    if (input[i] != '\0') { output[0] = '\0'; return false; }
    output[i] = '\0';
    return true;
}

static bool valid_data_signature(SymbolKind kind, uint16_t elementCount, uint16_t elementSize,
                                 uint32_t size, uint32_t alignment, uint64_t structTypeIdentity)
{
    if (kind == SymbolKind::DataStruct)
        return elementCount != 0 && elementCount <= COMPILER_MAX_ARRAY_ELEMENTS && elementSize != 0 &&
            elementSize <= COMPILER_MAX_STRUCT_BYTES && size != 0 &&
            size == static_cast<uint32_t>(elementCount) * elementSize &&
            size <= COMPILER_MAX_LINKED_DATA_BYTES && alignment == 4 && structTypeIdentity != 0;
    return (kind == SymbolKind::Data || kind == SymbolKind::DataArray) && elementCount != 0 &&
        elementCount <= COMPILER_MAX_ARRAY_ELEMENTS && elementSize == 4 &&
        size == static_cast<uint32_t>(elementCount) * elementSize && alignment == 4;
}

static uint64_t hash_bytes_with_zero_range(const uint8_t* bytes, uint32_t count,
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
    for (uint32_t i = 0; i < capacity; ++i) writer.u8(value[i]);
}

static bool read_fixed_name(Reader& reader, char* output, uint32_t capacity)
{
    if (!reader.bytes(reinterpret_cast<uint8_t*>(output), capacity)) return false;
    return valid_name(output, capacity);
}

static bool read_optional_name(Reader& reader, char* output, uint32_t capacity)
{
    if (!reader.bytes(reinterpret_cast<uint8_t*>(output), capacity)) return false;
    if (output[0] == '\0') {
        for (uint32_t i = 1; i < capacity; ++i) if (output[i] != '\0') return false;
        return true;
    }
    return valid_name(output, capacity);
}

static void write_struct_type(Writer& writer, const StructTypeIR& type)
{
    write_fixed_name(writer, type.name, sizeof(type.name)); writer.u16(type.fieldCount); writer.u16(0);
    writer.u32(type.sizeBytes); writer.u32(type.alignment); writer.u64(type.identity);
    for (uint32_t i = 0; i < type.fieldCount; ++i) {
        const StructFieldIR& field = type.fields[i]; write_fixed_name(writer, field.name, sizeof(field.name));
        writer.u8(static_cast<uint8_t>(field.kind)); writer.u8(0); writer.u16(field.elementCount); writer.u16(field.elementSize);
        writer.u32(field.offset); writer.u32(field.sizeBytes); writer.u32(field.alignment);
    }
}

static bool read_struct_type(Reader& reader, StructTypeIR& type)
{
    type = {};
    if (!read_fixed_name(reader, type.name, sizeof(type.name))) return false;
    type.fieldCount = reader.u16(); (void)reader.u16();
    if (type.fieldCount == 0 || type.fieldCount > COMPILER_MAX_STRUCT_FIELDS) return false;
    type.sizeBytes = reader.u32(); type.alignment = reader.u32(); type.identity = reader.u64();
    if (type.sizeBytes == 0 || type.sizeBytes > COMPILER_MAX_STRUCT_BYTES || type.alignment == 0 || type.identity == 0) return false;
    for (uint32_t i = 0; i < type.fieldCount; ++i) {
        StructFieldIR& field = type.fields[i]; if (!read_fixed_name(reader, field.name, sizeof(field.name))) return false;
        const uint8_t kind = reader.u8(); (void)reader.u8();
        if (kind > static_cast<uint8_t>(StructFieldKind::Int32)) return false;
        field.kind = static_cast<StructFieldKind>(kind); field.elementCount = reader.u16(); field.elementSize = reader.u16();
        field.offset = reader.u32(); field.sizeBytes = reader.u32(); field.alignment = reader.u32();
        if (field.elementCount == 0 || field.elementSize != 4 || field.sizeBytes != 4 || field.alignment != 4 ||
            field.offset > type.sizeBytes || field.sizeBytes > type.sizeBytes - field.offset) return false;
    }
    return reader.ok();
}

static void write_export(Writer& writer, const ExportSymbol& symbol)
{
    writer.u8(static_cast<uint8_t>(symbol.kind)); writer.u8(symbol.isEntry ? 1 : 0); writer.u16(0);
    write_fixed_name(writer, symbol.name, sizeof(symbol.name)); writer.u32(symbol.moduleCodeOffset); writer.u32(symbol.moduleDataOffset);
    writer.u32(symbol.size); writer.u32(symbol.alignment); writer.u16(symbol.elementCount); writer.u16(symbol.elementSize);
    writer.u16(symbol.parameterCount); writer.u16(0);
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) writer.u8(static_cast<uint8_t>(symbol.parameterKinds[i]));
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) writer.u64(symbol.parameterStructTypes[i]);
    write_fixed_name(writer, symbol.structTypeName, sizeof(symbol.structTypeName)); writer.u64(symbol.structTypeIdentity); write_location(writer, symbol.location);
}

static bool read_export(Reader& reader, ExportSymbol& symbol)
{
    symbol = {}; const uint8_t kind = reader.u8(); const uint8_t entry = reader.u8(); (void)reader.u16();
    if (kind > static_cast<uint8_t>(SymbolKind::DataStruct) || entry > 1 || !read_fixed_name(reader, symbol.name, sizeof(symbol.name))) return false;
    symbol.kind = static_cast<SymbolKind>(kind); symbol.isEntry = entry != 0; symbol.moduleCodeOffset = reader.u32(); symbol.moduleDataOffset = reader.u32();
    symbol.size = reader.u32(); symbol.alignment = reader.u32(); symbol.elementCount = reader.u16(); symbol.elementSize = reader.u16(); symbol.parameterCount = reader.u16(); (void)reader.u16();
    if (symbol.parameterCount > COMPILER_MAX_PARAMETERS) return false;
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) { const uint8_t kindValue = reader.u8(); if (kindValue > static_cast<uint8_t>(ParameterKind::StructPointer)) return false; symbol.parameterKinds[i] = static_cast<ParameterKind>(kindValue); }
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) symbol.parameterStructTypes[i] = reader.u64();
    if (!read_optional_name(reader, symbol.structTypeName, sizeof(symbol.structTypeName))) return false;
    symbol.structTypeIdentity = reader.u64(); symbol.location = read_location(reader); return reader.ok();
}

static void write_import(Writer& writer, const ImportSymbol& symbol)
{
    writer.u8(static_cast<uint8_t>(symbol.kind)); writer.u8(0); writer.u16(0); write_fixed_name(writer, symbol.name, sizeof(symbol.name));
    writer.u16(symbol.expectedParameterCount); writer.u16(0); writer.u32(symbol.size); writer.u32(symbol.alignment); writer.u16(symbol.elementCount); writer.u16(symbol.elementSize);
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) writer.u8(static_cast<uint8_t>(symbol.parameterKinds[i]));
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) writer.u64(symbol.parameterStructTypes[i]);
    write_fixed_name(writer, symbol.structTypeName, sizeof(symbol.structTypeName)); writer.u64(symbol.structTypeIdentity); write_location(writer, symbol.location);
}

static bool read_import(Reader& reader, ImportSymbol& symbol)
{
    symbol = {}; const uint8_t kind = reader.u8(); (void)reader.u8(); (void)reader.u16();
    if (kind > static_cast<uint8_t>(SymbolKind::DataStruct) || !read_fixed_name(reader, symbol.name, sizeof(symbol.name))) return false;
    symbol.kind = static_cast<SymbolKind>(kind); symbol.expectedParameterCount = reader.u16(); (void)reader.u16(); symbol.size = reader.u32(); symbol.alignment = reader.u32(); symbol.elementCount = reader.u16(); symbol.elementSize = reader.u16();
    if (symbol.kind == SymbolKind::Function && symbol.expectedParameterCount > COMPILER_MAX_PARAMETERS) return false;
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) { const uint8_t kindValue = reader.u8(); if (kindValue > static_cast<uint8_t>(ParameterKind::StructPointer)) return false; symbol.parameterKinds[i] = static_cast<ParameterKind>(kindValue); }
    for (uint32_t i = 0; i < COMPILER_MAX_PARAMETERS; ++i) symbol.parameterStructTypes[i] = reader.u64();
    if (!read_optional_name(reader, symbol.structTypeName, sizeof(symbol.structTypeName))) return false;
    symbol.structTypeIdentity = reader.u64(); symbol.location = read_location(reader); return reader.ok();
}

static void write_dependency(Writer& writer, const DeclarationDependency& dependency)
{
    write_fixed_name(writer, dependency.path, sizeof(dependency.path));
    writer.u32(dependency.bytes);
    writer.u64(dependency.hash);
}

static bool read_dependency(Reader& reader, DeclarationDependency& dependency)
{
    dependency = {};
    if (!read_fixed_name(reader, dependency.path, sizeof(dependency.path))) return false;
    dependency.bytes = reader.u32();
    dependency.hash = reader.u64();
    return valid_dependency_path(dependency.path, sizeof(dependency.path)) &&
        dependency.bytes <= COMPILER_MAX_DECLARATION_FILE_BYTES && dependency.hash != 0 && reader.ok();
}

static uint32_t append_string(char* table, uint32_t capacity, uint32_t* used, const char* value)
{
    if (!table || !used || !value) return 0xFFFFFFFFU;
    const uint32_t length = text_length(value, COMPILER_FUNCTION_NAME_CAPACITY);
    if (length >= COMPILER_FUNCTION_NAME_CAPACITY || *used > capacity || length + 1U > capacity - *used) return 0xFFFFFFFFU;
    const uint32_t offset = *used; for (uint32_t i = 0; i < length; ++i) table[*used + i] = value[i]; table[*used + length] = 0; *used += length + 1U; return offset;
}

static uint32_t append_unique_string(char* table, uint32_t capacity, uint32_t* used, const char* value)
{
    if (!table || !used || !value) return 0xFFFFFFFFU;
    uint32_t at = 0;
    while (at < *used) {
        const uint32_t length = text_length(table + at, capacity - at);
        if (length >= capacity - at) return 0xFFFFFFFFU;
        if (names_equal(table + at, value)) return at;
        at += length + 1U;
    }
    return append_string(table, capacity, used, value);
}

static uint32_t find_string_offset(const char* table, uint32_t used, const char* value)
{
    if (!table || !value) return 0xFFFFFFFFU;
    uint32_t at = 0;
    while (at < used) { if (names_equal(table + at, value)) return at; const uint32_t length = text_length(table + at, used - at); if (length >= used - at) return 0xFFFFFFFFU; at += length + 1U; }
    return 0xFFFFFFFFU;
}

static int32_t add_section(SectionDesc* sections, uint16_t* count, const char* name, uint32_t type,
                           uint64_t flags, uint32_t align, uint32_t entsize, uint32_t size, const uint8_t* data)
{
    if (!sections || !count || !name || *count >= kMaxObjectSections) return -1;
    SectionDesc& section = sections[(*count)++]; section = {}; section.name = name; section.type = type; section.flags = flags; section.align = align; section.entsize = entsize; section.size = size; section.data = data; return static_cast<int32_t>(*count - 1U);
}

static void write_symbol(uint8_t* output, uint32_t offset, uint32_t name, uint8_t info,
                         uint16_t section, uint64_t value, uint64_t size)
{
    put_u32(output, offset, name); output[offset + 4] = info; output[offset + 5] = 0; put_u16(output, offset + 6, section); put_u64(output, offset + 8, value); put_u64(output, offset + 16, size);
}

static int32_t find_export_symbol(const CompiledModule& module, const char* name)
{
    for (uint32_t i = 0; i < module.exportCount; ++i) if (names_equal(module.exports[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

static int32_t find_import_symbol(const CompiledModule& module, const char* name)
{
    for (uint32_t i = 0; i < module.importCount; ++i) if (names_equal(module.imports[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

static uint32_t function_symbol_size(const CompiledModule& module, uint32_t exportIndex)
{
    const uint32_t start = module.exports[exportIndex].moduleCodeOffset; uint32_t end = module.codeBytes;
    for (uint32_t i = 0; i < module.exportCount; ++i) if (i != exportIndex && module.exports[i].kind == SymbolKind::Function) { const uint32_t candidate = module.exports[i].moduleCodeOffset; if (candidate > start && candidate < end) end = candidate; }
    return end - start;
}

static bool validate_module_for_serialization(const CompiledModule& module)
{
    if (!valid_name(module.sourcePath, sizeof(module.sourcePath)) || module.sourceBytes > COMPILER_MAX_SOURCE_BYTES || module.codeBytes > COMPILER_MAX_CODE_BYTES || module.dataBytes > COMPILER_MAX_LINKED_DATA_BYTES || module.mutableDataBytes > COMPILER_MAX_LINKED_DATA_BYTES || module.functionCount > COMPILER_MAX_FUNCTIONS || module.globalCount > COMPILER_MAX_GLOBALS || module.exportCount > COMPILER_MAX_MODULE_SYMBOLS || module.importCount > COMPILER_MAX_MODULE_SYMBOLS || module.relocationCount > COMPILER_MAX_MODULE_RELOCATIONS || module.structTypeCount > COMPILER_MAX_STRUCT_TYPES || module.recursiveSccCount > COMPILER_MAX_FUNCTIONS || (module.hasEntry && (module.codeBytes == 0 || module.entryCodeOffset >= module.codeBytes))) return false;
    uint32_t edges = 0;
    for (uint32_t i = 0; i < module.functionCount; ++i) { if (module.localStorageBytes[i] > COMPILER_MAX_LOCAL_STORAGE_BYTES) return false; for (uint32_t j = 0; j < module.functionCount; ++j) if (module.callGraph[i][j]) ++edges; }
    if (edges > COMPILER_MAX_CALL_GRAPH_EDGES) return false;
    for (uint32_t i = 0; i < module.structTypeCount; ++i) { const StructTypeIR& type = module.structTypes[i]; if (!valid_name(type.name, sizeof(type.name)) || type.fieldCount == 0 || type.fieldCount > COMPILER_MAX_STRUCT_FIELDS || type.sizeBytes == 0 || type.sizeBytes > COMPILER_MAX_STRUCT_BYTES || type.alignment == 0 || type.identity == 0) return false; }
    for (uint32_t i = 0; i < module.exportCount; ++i) { const ExportSymbol& symbol = module.exports[i]; if (!valid_name(symbol.name, sizeof(symbol.name))) return false; if (symbol.kind == SymbolKind::Function) { if (symbol.moduleCodeOffset >= module.codeBytes || symbol.parameterCount > COMPILER_MAX_PARAMETERS || function_symbol_size(module, i) == 0) return false; } else if (!valid_data_signature(symbol.kind, symbol.elementCount, symbol.elementSize, symbol.size, symbol.alignment, symbol.structTypeIdentity) || !range32(symbol.moduleDataOffset, symbol.size, module.mutableDataBytes)) return false; }
    for (uint32_t i = 0; i < module.importCount; ++i) { const ImportSymbol& symbol = module.imports[i]; if (!valid_name(symbol.name, sizeof(symbol.name))) return false; if (symbol.kind == SymbolKind::Function && symbol.expectedParameterCount > COMPILER_MAX_PARAMETERS) return false; if (symbol_is_data(symbol.kind) && !valid_data_signature(symbol.kind, symbol.elementCount, symbol.elementSize, symbol.size, symbol.alignment, symbol.structTypeIdentity)) return false; }
    if (module.dependencyCount > COMPILER_MAX_DECLARATION_DEPENDENCIES ||
        module.sourceMapCount > COMPILER_MAX_SOURCE_MAPPINGS) return false;
    for (uint32_t i = 0; i < module.dependencyCount; ++i) {
        const DeclarationDependency& dependency = module.dependencies[i];
        if (!valid_dependency_path(dependency.path, sizeof(dependency.path)) ||
            dependency.bytes > COMPILER_MAX_DECLARATION_FILE_BYTES || dependency.hash == 0) return false;
        if (i != 0 && names_equal(module.dependencies[i - 1].path, dependency.path)) return false;
    }
    for (uint32_t i = 0; i < module.sourceMapCount; ++i) {
        const SourceMapping& mapping = module.sourceMappings[i];
        if (mapping.functionIndex >= module.functionCount || mapping.line == 0 ||
            mapping.column == 0 || !range32(mapping.moduleCodeOffset,
                                               mapping.instructionBytes, module.codeBytes)) return false;
        if (i != 0 && mapping.moduleCodeOffset < module.sourceMappings[i - 1].moduleCodeOffset)
            return false;
    }
    for (uint32_t i = 0; i < module.relocationCount; ++i) { const RelocationRecord& relocation = module.relocations[i]; const uint32_t width = relocation.kind == RelocationKind::CallRel32 ? 4U : 8U; if (relocation.kind > RelocationKind::GlobalDataAddress64 || relocation.width != width || !range32(relocation.patchOffset, width, module.codeBytes)) return false; if (relocation.kind == RelocationKind::DataAddress64) { if (module.dataBytes == 0 || relocation.dataOffset >= module.dataBytes) return false; } else if (!valid_name(relocation.targetSymbolName, sizeof(relocation.targetSymbolName)) || (find_export_symbol(module, relocation.targetSymbolName) < 0 && find_import_symbol(module, relocation.targetSymbolName) < 0)) return false; }
    return true;
}

static bool write_metadata(const CompiledModule& module, uint32_t sectionCount, uint32_t objectBytes,
                           uint8_t* output, uint32_t capacity, uint32_t* outputBytes)
{
    (void)sectionCount;
    if (!output || !outputBytes || capacity < COMPILER_ELF_OBJECT_META_HEADER_BYTES) return false;
    Writer writer(output, capacity); writer.bytes(kMetaMagic, 4); writer.u16(COMPILER_OBJECT_FORMAT_VERSION); writer.u16(COMPILER_ELF_OBJECT_META_HEADER_BYTES);
    writer.u32(COMPILER_OBJECT_ARCH_AMD64); writer.u32(COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1); writer.u32(COMPILER_OBJECT_ABI_VERSION);
    uint32_t flags = module.hasEntry ? kFlagHasEntry : 0; if (module.hasHostLog) flags |= kFlagHasHostLog; if (module.returnConstantValid) flags |= kFlagReturnConstantValid;
    writer.u32(flags); writer.u64(module.sourceHash); writer.u32(module.sourceBytes); const uint32_t sourcePathBytes = text_length(module.sourcePath, sizeof(module.sourcePath)); writer.u32(sourcePathBytes); writer.u32(module.tokenCount); writer.u32(static_cast<uint32_t>(module.returnConstant)); writer.u32(module.entryCodeOffset); writer.u32(module.functionCount); writer.u32(module.globalCount); writer.u16(module.recursiveSccCount); writer.u16(module.structTypeCount);
    uint32_t edgeCount = 0; for (uint32_t i = 0; i < module.functionCount; ++i) for (uint32_t j = 0; j < module.functionCount; ++j) if (module.callGraph[i][j]) ++edgeCount;
    writer.u32(edgeCount); writer.u32(module.exportCount); writer.u32(module.importCount); writer.u32(module.relocationCount); writer.u32(module.dependencyCount); writer.u32(0); writer.u32(module.sourceMapCount); writer.u32(0); writer.u32(0); writer.u32(objectBytes); writer.u64(0);
    writer.bytes(reinterpret_cast<const uint8_t*>(module.sourcePath), sourcePathBytes);
    for (uint32_t i = 0; i < module.functionCount; ++i) writer.u8(module.recursiveFunction[i] ? 1 : 0);
    for (uint32_t i = 0; i < module.functionCount; ++i) writer.u32(module.localStorageBytes[i]);
    for (uint32_t i = 0; i < module.functionCount; ++i) for (uint32_t j = 0; j < module.functionCount; ++j) writer.u8(module.callGraph[i][j] ? 1 : 0);
    for (uint32_t i = 0; i < module.structTypeCount; ++i) write_struct_type(writer, module.structTypes[i]);
    for (uint32_t i = 0; i < module.exportCount; ++i) write_export(writer, module.exports[i]);
    for (uint32_t i = 0; i < module.importCount; ++i) write_import(writer, module.imports[i]);
    for (uint32_t i = 0; i < module.relocationCount; ++i) write_location(writer, module.relocations[i].location);
    const uint32_t dependencyStart = writer.position();
    for (uint32_t i = 0; i < module.dependencyCount; ++i) write_dependency(writer, module.dependencies[i]);
    writer.patch_u32(84, writer.position() - dependencyStart);
    const uint32_t sourceMapStart = writer.position();
    for (uint32_t i = 0; i < module.sourceMapCount; ++i) {
        const SourceMapping& mapping = module.sourceMappings[i];
        writer.u16(mapping.functionIndex); writer.u16(0); writer.u32(mapping.line);
        writer.u32(mapping.column); writer.u32(mapping.moduleCodeOffset);
        writer.u32(mapping.instructionBytes);
    }
    writer.patch_u32(92, writer.position() - sourceMapStart);
    if (!writer.ok() || writer.position() > COMPILER_MAX_OBJECT_BYTES) return false;
    writer.patch_u32(88, module.sourceMapCount);
    writer.patch_u32(96, writer.position()); *outputBytes = writer.position(); return writer.ok();
}

static bool serialize_sections(const CompiledModule& module, uint32_t strtabBytes, uint32_t shstrtabBytes,
                               uint8_t* output, uint32_t capacity, uint32_t* outputBytes)
{
    SectionDesc sections[kMaxObjectSections] = {};
    uint16_t sectionCount = 1;
    const int32_t text = module.codeBytes == 0 ? -1 : add_section(sections, &sectionCount, ".text", kSectionProgBits,
        kSectionFlagAlloc | kSectionFlagExec, 16, 0, module.codeBytes, module.code);
    const int32_t rodata = module.dataBytes == 0 ? -1 : add_section(sections, &sectionCount, ".rodata", kSectionProgBits,
        kSectionFlagAlloc, 1, 0, module.dataBytes, module.data);
    const int32_t data = module.mutableDataBytes == 0 ? -1 : add_section(sections, &sectionCount, ".data", kSectionProgBits,
        kSectionFlagAlloc | kSectionFlagWrite, 4, 0, module.mutableDataBytes, module.mutableData);
    const int32_t rela = module.relocationCount == 0 ? -1 : add_section(sections, &sectionCount, ".rela.text", kSectionRela,
        0, 8, 24, module.relocationCount * 24U, nullptr);
    // All sections, including the metadata and table sections, receive a
    // local STT_SECTION symbol. The final section count is known before the
    // symbol table is added.
    const uint16_t plannedSectionCount = static_cast<uint16_t>(sectionCount + 4U);
    const uint32_t localSymbols = plannedSectionCount - 1U;
    const uint32_t symbolCount = 1U + localSymbols + module.exportCount + module.importCount;
    if (symbolCount > kMaxObjectSymbols || symbolCount > 0xFFFFFFFFU / 24U) return false;
    const int32_t symtab = add_section(sections, &sectionCount, ".symtab", kSectionSymTab, 0, 8, 24, symbolCount * 24U, nullptr);
    const int32_t strtab = add_section(sections, &sectionCount, ".strtab", kSectionStrTab, 0, 1, 0, strtabBytes, reinterpret_cast<const uint8_t*>(s_strtab));
    const int32_t shstrtab = add_section(sections, &sectionCount, ".shstrtab", kSectionStrTab, 0, 1, 0, shstrtabBytes, reinterpret_cast<const uint8_t*>(s_shstrtab));
    const int32_t meta = add_section(sections, &sectionCount, ".gx.meta", kSectionProgBits, 0, 1, 0, 0, s_meta);
    if (sectionCount != plannedSectionCount || symtab < 0 || strtab < 0 || shstrtab < 0 || meta < 0 || (module.codeBytes != 0 && text < 0) || (module.relocationCount != 0 && rela < 0)) return false;
    if (rela >= 0) { sections[rela].link = static_cast<uint32_t>(symtab); sections[rela].info = static_cast<uint32_t>(text); }
    sections[symtab].link = static_cast<uint32_t>(strtab); sections[symtab].info = 1U + localSymbols;

    uint32_t metaBytes = 0;
    if (!write_metadata(module, sectionCount, 0, s_meta, sizeof(s_meta), &metaBytes)) return false;
    sections[meta].size = metaBytes;
    for (uint16_t i = 0; i < sectionCount; ++i) {
        sections[i].nameOffset = find_string_offset(s_shstrtab, shstrtabBytes, sections[i].name ? sections[i].name : "");
        if (sections[i].nameOffset == 0xFFFFFFFFU) return false;
    }
    uint32_t position = COMPILER_ELF_OBJECT_HEADER_BYTES;
    for (uint16_t i = 1; i < sectionCount; ++i) {
        if (!align_u32(position, sections[i].align == 0 ? 1 : sections[i].align, &position)) return false;
        sections[i].offset = position;
        if (!add_u32(position, sections[i].size, &position)) return false;
    }
    uint32_t sectionHeaderOffset = 0;
    if (!align_u32(position, 8, &sectionHeaderOffset) || sectionCount > (0xFFFFFFFFU - sectionHeaderOffset) / 64U) return false;
    const uint32_t totalBytes = sectionHeaderOffset + static_cast<uint32_t>(sectionCount) * 64U;
    if (totalBytes > capacity || totalBytes > COMPILER_MAX_OBJECT_BYTES) return false;
    if (!write_metadata(module, sectionCount, totalBytes, s_meta, sizeof(s_meta), &metaBytes) || metaBytes != sections[meta].size) return false;

    clear_bytes(output, totalBytes);
    output[0] = kElfMagic[0]; output[1] = kElfMagic[1]; output[2] = kElfMagic[2]; output[3] = kElfMagic[3]; output[4] = 2; output[5] = 1; output[6] = 1;
    put_u16(output, 16, kElfTypeRel); put_u16(output, 18, kElfMachineX8664); put_u32(output, 20, kElfVersionCurrent);
    put_u64(output, 32, 0); put_u64(output, 40, sectionHeaderOffset); put_u16(output, 52, COMPILER_ELF_OBJECT_HEADER_BYTES); put_u16(output, 54, 0); put_u16(output, 56, 0); put_u16(output, 58, 64); put_u16(output, 60, sectionCount); put_u16(output, 62, static_cast<uint16_t>(shstrtab));
    for (uint16_t i = 1; i < sectionCount; ++i) if (sections[i].data && sections[i].size != 0) for (uint32_t j = 0; j < sections[i].size; ++j) output[sections[i].offset + j] = sections[i].data[j];

    write_symbol(output, sections[symtab].offset, 0, 0, 0, 0, 0);
    for (uint16_t i = 1; i < sectionCount; ++i) write_symbol(output, sections[symtab].offset + static_cast<uint32_t>(i) * 24U, 0, static_cast<uint8_t>((kSymbolBindLocal << 4) | kSymbolTypeSection), i, 0, 0);
    uint32_t nextSymbol = localSymbols + 1U;
    for (uint32_t i = 0; i < module.exportCount; ++i) {
        const ExportSymbol& symbol = module.exports[i];
        const uint32_t nameOffset = find_string_offset(s_strtab, strtabBytes, symbol.name);
        if (nameOffset == 0xFFFFFFFFU) return false;
        const uint32_t at = sections[symtab].offset + nextSymbol * 24U;
        if (symbol.kind == SymbolKind::Function) write_symbol(output, at, nameOffset, static_cast<uint8_t>((kSymbolBindGlobal << 4) | kSymbolTypeFunc), static_cast<uint16_t>(text), symbol.moduleCodeOffset, function_symbol_size(module, i));
        else write_symbol(output, at, nameOffset, static_cast<uint8_t>((kSymbolBindGlobal << 4) | kSymbolTypeObject), static_cast<uint16_t>(data), symbol.moduleDataOffset, symbol.size);
        ++nextSymbol;
    }
    for (uint32_t i = 0; i < module.importCount; ++i) {
        const ImportSymbol& symbol = module.imports[i];
        const uint32_t nameOffset = find_string_offset(s_strtab, strtabBytes, symbol.name);
        if (nameOffset == 0xFFFFFFFFU) return false;
        const uint32_t at = sections[symtab].offset + nextSymbol * 24U;
        const uint8_t type = symbol.kind == SymbolKind::Function ? kSymbolTypeFunc : kSymbolTypeObject;
        write_symbol(output, at, nameOffset, static_cast<uint8_t>((kSymbolBindGlobal << 4) | type), kSectionUndefined, 0, symbol.size);
        ++nextSymbol;
    }
    for (uint32_t i = 0; i < sections[strtab].size; ++i) output[sections[strtab].offset + i] = static_cast<uint8_t>(s_strtab[i]);
    for (uint32_t i = 0; i < sections[shstrtab].size; ++i) output[sections[shstrtab].offset + i] = static_cast<uint8_t>(s_shstrtab[i]);

    for (uint32_t i = 0; i < module.relocationCount; ++i) {
        const RelocationRecord& relocation = module.relocations[i];
        uint32_t symbolIndex = 0; uint32_t type = 0; int64_t addend = 0;
        if (relocation.kind == RelocationKind::DataAddress64) { symbolIndex = static_cast<uint32_t>(rodata); type = kRelX8664_64; addend = relocation.dataOffset; }
        else {
            const int32_t exportIndex = find_export_symbol(module, relocation.targetSymbolName);
            const int32_t importIndex = find_import_symbol(module, relocation.targetSymbolName);
            const uint32_t combined = exportIndex >= 0 ? static_cast<uint32_t>(exportIndex) : module.exportCount + static_cast<uint32_t>(importIndex);
            symbolIndex = localSymbols + 1U + combined; type = relocation.kind == RelocationKind::CallRel32 ? kRelX8664Pc32 : kRelX8664_64; addend = relocation.kind == RelocationKind::CallRel32 ? -4 : 0;
        }
        const uint32_t at = sections[rela].offset + i * 24U;
        put_u64(output, at, relocation.patchOffset); put_u64(output, at + 8, (static_cast<uint64_t>(symbolIndex) << 32) | type); put_u64(output, at + 16, static_cast<uint64_t>(addend));
    }
    for (uint16_t i = 0; i < sectionCount; ++i) {
        const uint32_t at = sectionHeaderOffset + static_cast<uint32_t>(i) * 64U;
        put_u32(output, at, sections[i].nameOffset); put_u32(output, at + 4, sections[i].type); put_u64(output, at + 8, sections[i].flags); put_u64(output, at + 16, 0); put_u64(output, at + 24, sections[i].offset); put_u64(output, at + 32, sections[i].size); put_u32(output, at + 40, sections[i].link); put_u32(output, at + 44, sections[i].info); put_u64(output, at + 48, sections[i].align); put_u64(output, at + 56, sections[i].entsize);
    }
    const uint32_t checksumOffset = sections[meta].offset + 104U;
    const uint64_t checksum = elf_object_checksum(output, totalBytes, checksumOffset);
    put_u64(output, checksumOffset, checksum); *outputBytes = totalBytes; return true;
}

static bool reject(Diagnostics& diagnostics, const char* message)
{
    diagnostics.error((SourceLocation){0, 1, 1}, message, "object"); return false;
}

static bool section_name(const uint8_t* bytes, uint32_t byteCount, const SectionView& section,
                        const SectionView& stringSection, const char* expected)
{
    if (!expected || stringSection.size > byteCount || section.name >= stringSection.size ||
        !range64(stringSection.offset, stringSection.size, byteCount)) return false;
    const uint32_t at = static_cast<uint32_t>(stringSection.offset + section.name);
    const uint32_t available = static_cast<uint32_t>(stringSection.size - section.name);
    uint32_t length = 0;
    while (length < available && bytes[at + length] != 0) ++length;
    return length < available && names_equal(reinterpret_cast<const char*>(bytes + at), expected);
}

static bool parse_elf(const uint8_t* bytes, uint32_t byteCount, ParsedElf* parsed,
                      Diagnostics& diagnostics)
{
    if (!parsed) return reject(diagnostics, "ELF object destination is missing");
    *parsed = {};
    parsed->text = parsed->rodata = parsed->data = parsed->relaText = parsed->symtab = parsed->strtab = parsed->shstrtab = parsed->meta = -1;
    if (!bytes || byteCount < COMPILER_ELF_OBJECT_HEADER_BYTES || byteCount > COMPILER_MAX_OBJECT_BYTES)
        return reject(diagnostics, "ELF object is truncated or exceeds the object bound");
    if (bytes[0] != kElfMagic[0] || bytes[1] != kElfMagic[1] || bytes[2] != kElfMagic[2] ||
        bytes[3] != kElfMagic[3] || bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1)
        return reject(diagnostics, "ELF object magic, class, endian, or version is invalid");
    if (get_u16(bytes, 16) != kElfTypeRel || get_u16(bytes, 18) != kElfMachineX8664 ||
        get_u32(bytes, 20) != kElfVersionCurrent || get_u64(bytes, 24) != 0 || get_u64(bytes, 32) != 0 ||
        get_u16(bytes, 52) < COMPILER_ELF_OBJECT_HEADER_BYTES || get_u16(bytes, 54) != 0 ||
        get_u16(bytes, 56) != 0 || get_u16(bytes, 58) < 64)
        return reject(diagnostics, "ELF object header is not a bounded ET_REL image");
    const uint64_t sectionHeaderOffset = get_u64(bytes, 40);
    const uint16_t sectionEntryBytes = get_u16(bytes, 58);
    const uint16_t sectionCount = get_u16(bytes, 60);
    const uint16_t shstrIndex = get_u16(bytes, 62);
    if (sectionEntryBytes < 64 || sectionCount == 0 || sectionCount > kMaxObjectSections || shstrIndex >= sectionCount ||
        sectionHeaderOffset > byteCount || static_cast<uint64_t>(sectionCount) >
        (static_cast<uint64_t>(byteCount) - sectionHeaderOffset) / sectionEntryBytes)
        return reject(diagnostics, "ELF section-header table is out of bounds");
    parsed->sectionCount = sectionCount;
    for (uint16_t i = 0; i < sectionCount; ++i) {
        const uint64_t at64 = sectionHeaderOffset + static_cast<uint64_t>(i) * sectionEntryBytes;
        if (!range64(at64, 64, byteCount)) return reject(diagnostics, "ELF section header is truncated");
        SectionView& section = parsed->sections[i];
        const uint32_t at = static_cast<uint32_t>(at64);
        section.name = get_u32(bytes, at); section.type = get_u32(bytes, at + 4); section.flags = get_u64(bytes, at + 8);
        section.offset = get_u64(bytes, at + 24); section.size = get_u64(bytes, at + 32); section.link = get_u32(bytes, at + 40);
        section.info = get_u32(bytes, at + 44); section.align = get_u64(bytes, at + 48); section.entsize = get_u64(bytes, at + 56);
        if (section.align != 0 && (section.align & (section.align - 1)) != 0) return reject(diagnostics, "ELF section alignment is not a power of two");
        if (section.type != kSectionNoBits && !range64(section.offset, section.size, byteCount)) return reject(diagnostics, "ELF section range exceeds file bounds");
        if (section.type == kSectionNoBits && section.offset > byteCount) return reject(diagnostics, "ELF NOBITS offset exceeds file bounds");
    }
    if (parsed->sections[0].type != kSectionNull || parsed->sections[shstrIndex].type != kSectionStrTab ||
        parsed->sections[shstrIndex].size == 0 || parsed->sections[shstrIndex].size > 0xFFFFFFFFULL ||
        !range64(parsed->sections[shstrIndex].offset, parsed->sections[shstrIndex].size, byteCount))
        return reject(diagnostics, "ELF section-name string table is invalid");
    const SectionView& shstr = parsed->sections[shstrIndex];
    if (bytes[static_cast<uint32_t>(shstr.offset)] != 0) return reject(diagnostics, "ELF section-name string table is missing its empty entry");
    for (uint16_t i = 1; i < sectionCount; ++i) {
        const SectionView& section = parsed->sections[i];
        if (section_name(bytes, byteCount, section, shstr, ".text")) { if (parsed->text >= 0) return reject(diagnostics, "ELF object contains duplicate .text sections"); parsed->text = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".rodata")) { if (parsed->rodata >= 0) return reject(diagnostics, "ELF object contains duplicate .rodata sections"); parsed->rodata = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".data")) { if (parsed->data >= 0) return reject(diagnostics, "ELF object contains duplicate .data sections"); parsed->data = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".rela.text")) { if (parsed->relaText >= 0) return reject(diagnostics, "ELF object contains duplicate .rela.text sections"); parsed->relaText = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".symtab")) { if (parsed->symtab >= 0) return reject(diagnostics, "ELF object contains duplicate .symtab sections"); parsed->symtab = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".strtab")) { if (parsed->strtab >= 0) return reject(diagnostics, "ELF object contains duplicate .strtab sections"); parsed->strtab = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".shstrtab")) { if (parsed->shstrtab >= 0) return reject(diagnostics, "ELF object contains duplicate .shstrtab sections"); parsed->shstrtab = i; }
        else if (section_name(bytes, byteCount, section, shstr, ".gx.meta")) { if (parsed->meta >= 0) return reject(diagnostics, "ELF object contains duplicate .gx.meta sections"); parsed->meta = i; }
    }
    if (parsed->symtab < 0 || parsed->strtab < 0 || parsed->shstrtab < 0 || parsed->meta < 0)
        return reject(diagnostics, "ELF object is missing a required section");
    const SectionView* text = parsed->text < 0 ? nullptr : &parsed->sections[parsed->text];
    if (text && (text->type != kSectionProgBits || text->flags != (kSectionFlagAlloc | kSectionFlagExec) || text->size == 0 ||
        text->size > COMPILER_MAX_CODE_BYTES || text->align != 16)) return reject(diagnostics, "ELF .text section metadata is invalid");
    if (parsed->rodata >= 0 && (parsed->sections[parsed->rodata].type != kSectionProgBits || parsed->sections[parsed->rodata].flags != kSectionFlagAlloc || parsed->sections[parsed->rodata].size > COMPILER_MAX_LINKED_DATA_BYTES)) return reject(diagnostics, "ELF .rodata section metadata is invalid");
    if (parsed->data >= 0 && (parsed->sections[parsed->data].type != kSectionProgBits || parsed->sections[parsed->data].flags != (kSectionFlagAlloc | kSectionFlagWrite) || parsed->sections[parsed->data].size > COMPILER_MAX_LINKED_DATA_BYTES)) return reject(diagnostics, "ELF .data section metadata is invalid");
    const SectionView& symtab = parsed->sections[parsed->symtab]; const SectionView& strtab = parsed->sections[parsed->strtab]; const SectionView& meta = parsed->sections[parsed->meta];
    if (symtab.type != kSectionSymTab || symtab.entsize != 24 || symtab.size == 0 || symtab.size % 24 != 0 || symtab.size / 24 > kMaxObjectSymbols || symtab.link != static_cast<uint32_t>(parsed->strtab) || strtab.type != kSectionStrTab || strtab.size == 0 || strtab.size > 0xFFFFFFFFULL || bytes[static_cast<uint32_t>(strtab.offset)] != 0)
        return reject(diagnostics, "ELF symbol/string table metadata is invalid");
    if (meta.type != kSectionProgBits || meta.size < COMPILER_ELF_OBJECT_META_HEADER_BYTES || meta.size > 0xFFFFFFFFULL)
        return reject(diagnostics, "ELF compiler metadata section is invalid");
    const uint32_t metaOffset = static_cast<uint32_t>(meta.offset); const uint32_t metaBytes = static_cast<uint32_t>(meta.size);
    if (bytes[metaOffset] != kMetaMagic[0] || bytes[metaOffset + 1] != kMetaMagic[1] || bytes[metaOffset + 2] != kMetaMagic[2] || bytes[metaOffset + 3] != kMetaMagic[3]) return reject(diagnostics, "ELF compiler metadata magic is invalid");
    parsed->header = {};
    parsed->header.elfType = get_u16(bytes, 16); parsed->header.machine = get_u16(bytes, 18); parsed->header.formatVersion = get_u16(bytes, metaOffset + 4); parsed->header.metaHeaderSize = get_u16(bytes, metaOffset + 6); parsed->header.targetArchitecture = get_u32(bytes, metaOffset + 8); parsed->header.targetAbi = get_u32(bytes, metaOffset + 12); parsed->header.compilerObjectAbiVersion = get_u32(bytes, metaOffset + 16); parsed->header.flags = get_u32(bytes, metaOffset + 20); parsed->header.sourceHash = get_u64(bytes, metaOffset + 24); parsed->header.sourceSize = get_u32(bytes, metaOffset + 32); parsed->header.sourcePathBytes = get_u32(bytes, metaOffset + 36); parsed->header.tokenCount = get_u32(bytes, metaOffset + 40); parsed->header.returnConstant = static_cast<int32_t>(get_u32(bytes, metaOffset + 44)); parsed->header.entryCodeOffset = get_u32(bytes, metaOffset + 48); parsed->header.functionCount = get_u32(bytes, metaOffset + 52); parsed->header.globalCount = get_u32(bytes, metaOffset + 56); parsed->header.recursiveSccCount = get_u16(bytes, metaOffset + 60); parsed->header.structTypeCount = get_u16(bytes, metaOffset + 62); parsed->header.callGraphEdgeCount = get_u32(bytes, metaOffset + 64); parsed->header.exportCount = get_u32(bytes, metaOffset + 68); parsed->header.importCount = get_u32(bytes, metaOffset + 72); parsed->header.relocationCount = get_u32(bytes, metaOffset + 76); parsed->header.dependencyCount = get_u32(bytes, metaOffset + 80); parsed->header.dependencyMetadataBytes = get_u32(bytes, metaOffset + 84); parsed->header.sourceMapCount = get_u32(bytes, metaOffset + 88); parsed->header.sourceMapMetadataBytes = get_u32(bytes, metaOffset + 92); parsed->header.metaBytes = get_u32(bytes, metaOffset + 96); parsed->header.objectBytes = get_u32(bytes, metaOffset + 100); parsed->header.objectChecksum = get_u64(bytes, metaOffset + 104); parsed->header.codeSize = text ? static_cast<uint32_t>(text->size) : 0; parsed->header.rodataSize = parsed->rodata < 0 ? 0 : static_cast<uint32_t>(parsed->sections[parsed->rodata].size); parsed->header.rwdataSize = parsed->data < 0 ? 0 : static_cast<uint32_t>(parsed->sections[parsed->data].size); parsed->header.metaOffset = metaOffset; parsed->header.sectionCount = sectionCount; parsed->header.symbolCount = static_cast<uint32_t>(symtab.size / 24U);
    if (parsed->header.formatVersion != COMPILER_OBJECT_FORMAT_VERSION || parsed->header.metaHeaderSize != COMPILER_ELF_OBJECT_META_HEADER_BYTES || parsed->header.targetArchitecture != COMPILER_OBJECT_ARCH_AMD64 || parsed->header.targetAbi != COMPILER_OBJECT_TARGET_ABI_GUIDEXOS_C_V1 || parsed->header.compilerObjectAbiVersion != COMPILER_OBJECT_ABI_VERSION || (parsed->header.flags & ~kKnownMetaFlags) != 0 || parsed->header.metaBytes != metaBytes || parsed->header.objectBytes != byteCount || parsed->header.sourceSize > COMPILER_MAX_SOURCE_BYTES || parsed->header.sourcePathBytes == 0 || parsed->header.sourcePathBytes >= COMPILER_MAX_SOURCE_PATH_BYTES || parsed->header.functionCount > COMPILER_MAX_FUNCTIONS || parsed->header.globalCount > COMPILER_MAX_GLOBALS || parsed->header.exportCount > COMPILER_MAX_MODULE_SYMBOLS || parsed->header.importCount > COMPILER_MAX_MODULE_SYMBOLS || parsed->header.relocationCount > COMPILER_MAX_MODULE_RELOCATIONS || parsed->header.structTypeCount > COMPILER_MAX_STRUCT_TYPES || parsed->header.recursiveSccCount > COMPILER_MAX_FUNCTIONS || parsed->header.dependencyCount > COMPILER_MAX_DECLARATION_DEPENDENCIES || parsed->header.dependencyMetadataBytes > COMPILER_MAX_DEPENDENCY_METADATA_BYTES || parsed->header.sourceMapCount > COMPILER_MAX_SOURCE_MAPPINGS || parsed->header.sourceMapMetadataBytes != parsed->header.sourceMapCount * 20U || ((parsed->header.flags & kFlagHasEntry) != 0 && (parsed->header.codeSize == 0 || parsed->header.entryCodeOffset >= parsed->header.codeSize)) || parsed->header.metaBytes < COMPILER_ELF_OBJECT_META_HEADER_BYTES || parsed->header.objectChecksum != elf_object_checksum(bytes, byteCount, metaOffset + 104U)) return reject(diagnostics, "ELF object compiler identity, metadata, or checksum is invalid");
    const uint32_t firstGlobalSymbol = sectionCount;
    if (parsed->header.symbolCount != firstGlobalSymbol + parsed->header.exportCount + parsed->header.importCount || symtab.info != firstGlobalSymbol || ((parsed->header.relocationCount != 0) != (parsed->relaText >= 0))) return reject(diagnostics, "ELF object symbol or relocation counts are inconsistent");
    if (parsed->relaText >= 0) { const SectionView& rela = parsed->sections[parsed->relaText]; if (rela.type != kSectionRela || rela.entsize != 24 || rela.size != static_cast<uint64_t>(parsed->header.relocationCount) * 24ULL || rela.link != static_cast<uint32_t>(parsed->symtab) || rela.info != static_cast<uint32_t>(parsed->text)) return reject(diagnostics, "ELF relocation table metadata is invalid"); }
    if ((parsed->header.rodataSize != 0) != (parsed->rodata >= 0) || (parsed->header.rwdataSize != 0) != (parsed->data >= 0)) return reject(diagnostics, "ELF data section presence is inconsistent");
    const uint32_t strBytes = static_cast<uint32_t>(strtab.size);
    for (uint32_t i = 0; i < parsed->header.symbolCount; ++i) {
        const uint32_t at = static_cast<uint32_t>(symtab.offset) + i * 24U; const uint32_t name = get_u32(bytes, at); const uint8_t info = bytes[at + 4]; const uint16_t section = get_u16(bytes, at + 6); const uint64_t value = get_u64(bytes, at + 8); const uint64_t size = get_u64(bytes, at + 16);
        if (i == 0) { if (name != 0 || info != 0 || section != 0 || value != 0 || size != 0) return reject(diagnostics, "ELF null symbol is malformed"); continue; }
        const uint32_t bind = info >> 4; const uint32_t type = info & 0x0F;
        if ((bind != kSymbolBindLocal && bind != kSymbolBindGlobal) || type > kSymbolTypeSection || name >= strBytes || section >= sectionCount) return reject(diagnostics, "ELF symbol binding, type, or name is invalid");
        uint32_t nameLength = 0; while (name + nameLength < strBytes && bytes[strtab.offset + name + nameLength] != 0) ++nameLength;
        if (name + nameLength >= strBytes || (i >= firstGlobalSymbol && nameLength == 0)) return reject(diagnostics, "ELF symbol name is invalid");
        if (i < firstGlobalSymbol && (bind != kSymbolBindLocal || type != kSymbolTypeSection || section == 0 || name != 0 || value != 0 || size != 0)) return reject(diagnostics, "ELF local section symbol is malformed");
        if (i >= firstGlobalSymbol && bind != kSymbolBindGlobal) return reject(diagnostics, "ELF global symbol binding is invalid");
        if (section == parsed->text && type == kSymbolTypeFunc && (!text || value >= text->size || size == 0 || size > text->size - value)) return reject(diagnostics, "ELF function symbol range is invalid");
        if (section == parsed->data && type == kSymbolTypeObject && (value > (parsed->data >= 0 ? parsed->sections[parsed->data].size : 0) || size > (parsed->data >= 0 ? parsed->sections[parsed->data].size : 0) - value)) return reject(diagnostics, "ELF data symbol range is invalid");
        if (section == 0 && bind == kSymbolBindGlobal && value != 0) return reject(diagnostics, "ELF undefined symbol value is invalid");
    }
    if (parsed->relaText >= 0) {
        const SectionView& rela = parsed->sections[parsed->relaText];
        for (uint32_t i = 0; i < parsed->header.relocationCount; ++i) { const uint32_t at = static_cast<uint32_t>(rela.offset) + i * 24U; const uint64_t offset = get_u64(bytes, at); const uint64_t info = get_u64(bytes, at + 8); const uint32_t symbol = static_cast<uint32_t>(info >> 32); const uint32_t type = static_cast<uint32_t>(info); const uint32_t width = type == kRelX8664Pc32 ? 4U : type == kRelX8664_64 ? 8U : 0; if (width == 0 || !text || symbol >= parsed->header.symbolCount || offset > text->size || width > text->size - offset) return reject(diagnostics, "ELF relocation type, symbol index, or patch range is invalid"); }
    }
    return true;
}

static const char* symbol_name(const uint8_t* bytes, const ParsedElf& parsed, uint32_t index)
{
    if (index >= parsed.header.symbolCount) return nullptr;
    const SectionView& symtab = parsed.sections[parsed.symtab]; const SectionView& strtab = parsed.sections[parsed.strtab]; const uint32_t name = get_u32(bytes, static_cast<uint32_t>(symtab.offset) + index * 24U);
    if (name >= strtab.size) return nullptr;
    const uint32_t at = static_cast<uint32_t>(strtab.offset + name); const uint32_t available = static_cast<uint32_t>(strtab.size - name); uint32_t length = 0; while (length < available && bytes[at + length] != 0) ++length; return length < available ? reinterpret_cast<const char*>(bytes + at) : nullptr;
}

static int32_t find_parsed_symbol(const uint8_t* bytes, const ParsedElf& parsed, const char* name)
{
    for (uint32_t i = parsed.sectionCount; i < parsed.header.symbolCount; ++i) { const char* current = symbol_name(bytes, parsed, i); if (current && names_equal(current, name)) return static_cast<int32_t>(i); }
    return -1;
}

static bool validate_metadata_payload(const uint8_t* bytes, const ParsedElf& parsed,
                                      CompiledModule* module, Diagnostics& diagnostics)
{
    const uint32_t metaOffset = parsed.header.metaOffset;
    Reader reader(bytes + metaOffset + COMPILER_ELF_OBJECT_META_HEADER_BYTES,
                  parsed.header.metaBytes - COMPILER_ELF_OBJECT_META_HEADER_BYTES);
    *module = {};
    const uint8_t* path = reader.view(parsed.header.sourcePathBytes);
    if (!path) return reject(diagnostics, "ELF compiler metadata source path is truncated");
    for (uint32_t i = 0; i < parsed.header.sourcePathBytes; ++i) {
        if (path[i] == 0) return reject(diagnostics, "ELF compiler metadata source path contains NUL");
        module->sourcePath[i] = static_cast<char>(path[i]);
    }
    module->sourcePath[parsed.header.sourcePathBytes] = 0;
    module->sourceHash = parsed.header.sourceHash; module->sourceBytes = parsed.header.sourceSize;
    module->tokenCount = parsed.header.tokenCount;
    module->entryCodeOffset = parsed.header.entryCodeOffset; module->functionCount = parsed.header.functionCount; module->globalCount = parsed.header.globalCount;
    module->recursiveSccCount = parsed.header.recursiveSccCount; module->structTypeCount = parsed.header.structTypeCount;
    module->hasEntry = (parsed.header.flags & kFlagHasEntry) != 0; module->hasHostLog = (parsed.header.flags & kFlagHasHostLog) != 0;
    module->returnConstantValid = (parsed.header.flags & kFlagReturnConstantValid) != 0; module->returnConstant = parsed.header.returnConstant;
    for (uint32_t i = 0; i < module->functionCount; ++i) { const uint8_t flag = reader.u8(); if (flag > 1) return reject(diagnostics, "ELF recursive-function metadata is malformed"); module->recursiveFunction[i] = flag != 0; }
    for (uint32_t i = 0; i < module->functionCount; ++i) { module->localStorageBytes[i] = reader.u32(); if (module->localStorageBytes[i] > COMPILER_MAX_LOCAL_STORAGE_BYTES) return reject(diagnostics, "ELF local-storage metadata is out of bounds"); }
    uint32_t edgeCount = 0;
    for (uint32_t i = 0; i < module->functionCount; ++i) for (uint32_t j = 0; j < module->functionCount; ++j) { const uint8_t edge = reader.u8(); if (edge > 1) return reject(diagnostics, "ELF call-graph metadata is malformed"); module->callGraph[i][j] = edge != 0; if (edge) ++edgeCount; }
    if (edgeCount != parsed.header.callGraphEdgeCount) return reject(diagnostics, "ELF call-graph edge count is inconsistent");
    for (uint32_t i = 0; i < module->structTypeCount; ++i) if (!read_struct_type(reader, module->structTypes[i])) return reject(diagnostics, "ELF struct metadata is malformed");
    module->exportCount = parsed.header.exportCount; module->importCount = parsed.header.importCount; module->relocationCount = parsed.header.relocationCount;
    for (uint32_t i = 0; i < module->exportCount; ++i) if (!read_export(reader, module->exports[i])) return reject(diagnostics, "ELF export metadata is malformed");
    for (uint32_t i = 0; i < module->importCount; ++i) if (!read_import(reader, module->imports[i])) return reject(diagnostics, "ELF import metadata is malformed");
    for (uint32_t i = 0; i < module->relocationCount; ++i) module->relocations[i].location = read_location(reader);
    const uint32_t dependencyStart = reader.position();
    module->dependencyCount = static_cast<uint16_t>(parsed.header.dependencyCount);
    for (uint32_t i = 0; i < module->dependencyCount; ++i)
        if (!read_dependency(reader, module->dependencies[i])) return reject(diagnostics, "ELF declaration dependency metadata is malformed");
    if (reader.position() - dependencyStart != parsed.header.dependencyMetadataBytes)
        return reject(diagnostics, "ELF declaration dependency metadata size is inconsistent");
    module->sourceMapCount = static_cast<uint32_t>(parsed.header.sourceMapCount);
    const uint32_t sourceMapStart = reader.position();
    for (uint32_t i = 0; i < module->sourceMapCount; ++i) {
        SourceMapping& mapping = module->sourceMappings[i];
        mapping.functionIndex = reader.u16(); (void)reader.u16(); mapping.line = reader.u32();
        mapping.column = reader.u32(); mapping.moduleCodeOffset = reader.u32();
        mapping.instructionBytes = reader.u32();
        if (mapping.functionIndex >= module->functionCount || mapping.line == 0 ||
            mapping.column == 0 || !range32(mapping.moduleCodeOffset,
                                               mapping.instructionBytes,
                                               parsed.header.codeSize) ||
            (i != 0 && mapping.moduleCodeOffset < module->sourceMappings[i - 1].moduleCodeOffset))
            return reject(diagnostics, "ELF source-map metadata is malformed");
    }
    if (reader.position() - sourceMapStart != parsed.header.sourceMapMetadataBytes ||
        !reader.ok() || reader.position() != parsed.header.metaBytes - COMPILER_ELF_OBJECT_META_HEADER_BYTES) return reject(diagnostics, "ELF compiler metadata has trailing or truncated bytes");

    const SectionView* text = parsed.text < 0 ? nullptr : &parsed.sections[parsed.text];
    module->codeBytes = text ? static_cast<uint32_t>(text->size) : 0; module->dataBytes = parsed.rodata < 0 ? 0 : static_cast<uint32_t>(parsed.sections[parsed.rodata].size); module->mutableDataBytes = parsed.data < 0 ? 0 : static_cast<uint32_t>(parsed.sections[parsed.data].size);
    for (uint32_t i = 0; i < module->codeBytes; ++i) module->code[i] = bytes[text->offset + i];
    for (uint32_t i = 0; i < module->dataBytes; ++i) module->data[i] = bytes[parsed.sections[parsed.rodata].offset + i];
    for (uint32_t i = 0; i < module->mutableDataBytes; ++i) module->mutableData[i] = bytes[parsed.sections[parsed.data].offset + i];
    if (module->hasEntry) { bool found = false; for (uint32_t i = 0; i < module->exportCount; ++i) if (module->exports[i].isEntry) { if (found || module->exports[i].kind != SymbolKind::Function) return reject(diagnostics, "ELF entry symbol metadata is inconsistent"); found = true; } if (!found) return reject(diagnostics, "ELF entry flag has no entry symbol"); }

    for (uint32_t i = 0; i < module->exportCount; ++i) {
        const int32_t symbol = find_parsed_symbol(bytes, parsed, module->exports[i].name);
        if (symbol < 0) return reject(diagnostics, "ELF export metadata has no matching symbol");
        const uint32_t at = static_cast<uint32_t>(parsed.sections[parsed.symtab].offset) + static_cast<uint32_t>(symbol) * 24U;
        const uint16_t section = get_u16(bytes, at + 6); const uint32_t type = bytes[at + 4] & 0x0F;
        if ((module->exports[i].kind == SymbolKind::Function && (type != kSymbolTypeFunc || section != static_cast<uint16_t>(parsed.text))) ||
            (symbol_is_data(module->exports[i].kind) && (type != kSymbolTypeObject || section != static_cast<uint16_t>(parsed.data)))) return reject(diagnostics, "ELF export symbol type does not match metadata");
    }
    for (uint32_t i = 0; i < module->importCount; ++i) {
        const int32_t symbol = find_parsed_symbol(bytes, parsed, module->imports[i].name);
        if (symbol < 0 || get_u16(bytes, static_cast<uint32_t>(parsed.sections[parsed.symtab].offset) + static_cast<uint32_t>(symbol) * 24U + 6) != kSectionUndefined) return reject(diagnostics, "ELF import is not an undefined symbol");
    }
    if (parsed.relaText >= 0) {
        const SectionView& rela = parsed.sections[parsed.relaText];
        for (uint32_t i = 0; i < module->relocationCount; ++i) {
            const uint32_t at = static_cast<uint32_t>(rela.offset) + i * 24U; const uint64_t info = get_u64(bytes, at + 8); const uint32_t symbolIndex = static_cast<uint32_t>(info >> 32); const uint32_t type = static_cast<uint32_t>(info); const int64_t addend = static_cast<int64_t>(get_u64(bytes, at + 16));
            RelocationRecord& relocation = module->relocations[i]; relocation.patchOffset = static_cast<uint32_t>(get_u64(bytes, at));
            if (type == kRelX8664Pc32) {
                relocation.kind = RelocationKind::CallRel32; relocation.width = 4; const char* name = symbol_name(bytes, parsed, symbolIndex);
                if (!name || !copy_text(relocation.targetSymbolName, sizeof(relocation.targetSymbolName), name) || addend != -4) return reject(diagnostics, "ELF PC-relative relocation is invalid");
            } else if (type == kRelX8664_64) {
                relocation.width = 8;
                const uint32_t targetSection = symbolIndex < parsed.header.symbolCount ? get_u16(bytes, static_cast<uint32_t>(parsed.sections[parsed.symtab].offset) + symbolIndex * 24U + 6) : 0xFFFFFFFFU;
                if (parsed.rodata >= 0 && targetSection == static_cast<uint32_t>(parsed.rodata)) {
                    relocation.kind = RelocationKind::DataAddress64;
                    if (addend < 0 || static_cast<uint64_t>(addend) >= module->dataBytes) return reject(diagnostics, "ELF read-only-data relocation addend is invalid");
                    relocation.dataOffset = static_cast<uint32_t>(addend);
                } else {
                    relocation.kind = RelocationKind::GlobalDataAddress64; const char* name = symbol_name(bytes, parsed, symbolIndex);
                    if (!name || !copy_text(relocation.targetSymbolName, sizeof(relocation.targetSymbolName), name) || addend != 0) return reject(diagnostics, "ELF global-data relocation is invalid");
                }
            } else return reject(diagnostics, "ELF relocation type is unsupported");
        }
    }
    return true;
}

} // namespace

uint64_t elf_object_checksum(const uint8_t* bytes, uint32_t byteCount, uint32_t checksumOffset)
{
    return bytes && byteCount <= COMPILER_MAX_OBJECT_BYTES && range32(checksumOffset, 8, byteCount) ? hash_bytes_with_zero_range(bytes, byteCount, checksumOffset, 8) : 0;
}

bool inspect_elf_object(const uint8_t* bytes, uint32_t byteCount, ElfObjectHeaderView* header, Diagnostics& diagnostics)
{
    ParsedElf parsed = {};
    if (!parse_elf(bytes, byteCount, &parsed, diagnostics)) return false;
    if (header) *header = parsed.header;
    return true;
}

bool serialize_elf_object(const CompiledModule& module, uint8_t* output, uint32_t capacity, uint32_t* outputBytes)
{
    if (!output || !outputBytes || capacity < COMPILER_ELF_OBJECT_HEADER_BYTES || capacity > COMPILER_MAX_OBJECT_BYTES || !validate_module_for_serialization(module)) return false;
    uint32_t strUsed = 1; s_strtab[0] = 0;
    for (uint32_t i = 0; i < module.exportCount; ++i) if (append_unique_string(s_strtab, sizeof(s_strtab), &strUsed, module.exports[i].name) == 0xFFFFFFFFU) return false;
    for (uint32_t i = 0; i < module.importCount; ++i) if (append_unique_string(s_strtab, sizeof(s_strtab), &strUsed, module.imports[i].name) == 0xFFFFFFFFU) return false;
    uint32_t shstrUsed = 1; s_shstrtab[0] = 0;
    const char* names[] = {".text", ".rodata", ".data", ".rela.text", ".symtab", ".strtab", ".shstrtab", ".gx.meta"};
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (append_unique_string(s_shstrtab, sizeof(s_shstrtab), &shstrUsed, names[i]) == 0xFFFFFFFFU) return false;
    return serialize_sections(module, strUsed, shstrUsed, output, capacity, outputBytes);
}

bool deserialize_elf_object(const uint8_t* bytes, uint32_t byteCount, CompiledModule* module, Diagnostics& diagnostics)
{
    if (!module) return reject(diagnostics, "ELF object destination is missing");
    ParsedElf parsed = {};
    if (!parse_elf(bytes, byteCount, &parsed, diagnostics)) { *module = {}; return false; }
    return validate_metadata_payload(bytes, parsed, module, diagnostics);
}

bool elf_object_identity_matches(const CompiledModule& module, const char* normalizedSourcePath, uint32_t sourceBytes, uint64_t sourceHash)
{
    return normalizedSourcePath && names_equal(module.sourcePath, normalizedSourcePath) && module.sourceBytes == sourceBytes && module.sourceHash == sourceHash;
}

} // namespace compiler
} // namespace kernel
