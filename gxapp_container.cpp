#include "gxapp_container.h"
#include "fs.h"

#include <cstring>
#include <limits>
#include <sstream>
#include <set>

namespace gxos {
namespace {

const uint8_t GXAPP_MAGIC[8] = { 'G', 'X', 'A', 'P', 'P', '\r', '\n', 0x1A };
const uint32_t GXAPP_FORMAT_VERSION = 1;
const uint32_t GXAPP_FLAG_REQUIRED = 1U;
const uint64_t GXAPP_MAX_PACKAGE_SIZE = 128ULL * 1024ULL * 1024ULL;
const uint64_t GXAPP_MAX_BINARY_SIZE = 64ULL * 1024ULL * 1024ULL;
const uint64_t GXAPP_MAX_METADATA_SIZE = 1024ULL * 1024ULL;
const uint64_t GXAPP_MAX_ENTRY_SIZE = 4096ULL;
const uint32_t GXAPP_MAX_ENTRY_COUNT = 64U;

enum class EntryKind : uint32_t { Metadata = 1, Binary = 2 };

struct EntryRecord {
    EntryKind kind;
    CpuArchitecture architecture;
    std::string path;
    std::vector<uint8_t> data;
};

static void appendU16(std::vector<uint8_t>& out, uint16_t value){ out.push_back(static_cast<uint8_t>(value & 0xff)); out.push_back(static_cast<uint8_t>((value >> 8) & 0xff)); }
static void appendU32(std::vector<uint8_t>& out, uint32_t value){ out.push_back(static_cast<uint8_t>(value & 0xff)); out.push_back(static_cast<uint8_t>((value >> 8) & 0xff)); out.push_back(static_cast<uint8_t>((value >> 16) & 0xff)); out.push_back(static_cast<uint8_t>((value >> 24) & 0xff)); }
static void appendU64(std::vector<uint8_t>& out, uint64_t value){ for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff)); }

static bool readU16(const std::vector<uint8_t>& in, size_t& offset, uint16_t& value){ if (offset + 2 > in.size()) return false; value = static_cast<uint16_t>(in[offset]) | (static_cast<uint16_t>(in[offset + 1]) << 8); offset += 2; return true; }
static bool readU32(const std::vector<uint8_t>& in, size_t& offset, uint32_t& value){ if (offset + 4 > in.size()) return false; value = static_cast<uint32_t>(in[offset]) | (static_cast<uint32_t>(in[offset + 1]) << 8) | (static_cast<uint32_t>(in[offset + 2]) << 16) | (static_cast<uint32_t>(in[offset + 3]) << 24); offset += 4; return true; }
static bool readU64(const std::vector<uint8_t>& in, size_t& offset, uint64_t& value){ if (offset + 8 > in.size()) return false; value = 0; for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(in[offset + i]) << (i * 8); offset += 8; return true; }
static bool checkedAdd(size_t a, uint64_t b, size_t& result){ if (b > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return false; size_t bb = static_cast<size_t>(b); if (a > std::numeric_limits<size_t>::max() - bb) return false; result = a + bb; return true; }

static std::string jsonEscape(const std::string& value){
    std::string out;
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string makeMetadataJson(const GXAppMetadata& metadata, const std::vector<GXAppBinary>& binaries){
    std::ostringstream json;
    json << "{\n";
    json << "  \"format\": \"gxapp\",\n";
    json << "  \"formatVersion\": " << GXAPP_FORMAT_VERSION << ",\n";
    json << "  \"applicationName\": \"" << jsonEscape(metadata.applicationName) << "\",\n";
    json << "  \"version\": \"" << jsonEscape(metadata.version) << "\",\n";
    json << "  \"requiredGuideXOSVersion\": \"" << jsonEscape(metadata.requiredGuideXOSVersion) << "\",\n";
    json << "  \"binaries\": [\n";
    for (size_t i = 0; i < binaries.size(); ++i) {
        const GXAppBinary& binary = binaries[i];
        const char* arch = GXAppContainer::ArchitectureToString(binary.architecture);
        json << "    { \"architecture\": \"" << arch << "\", \"path\": \"bin/" << arch << "/app.bin\", \"entryPoint\": \"" << jsonEscape(binary.entryPoint) << "\" }";
        if (i + 1 < binaries.size()) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";
    return json.str();
}

static bool parseJsonStringField(const std::string& json, const std::string& name, std::string& value){
    std::string key = "\"" + name + "\"";
    size_t keyPos = json.find(key);
    if (keyPos == std::string::npos) return false;
    size_t colon = json.find(':', keyPos + key.size());
    if (colon == std::string::npos) return false;
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) return false;
    value.clear();
    for (size_t i = quote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') return true;
        if (c == '\\' && i + 1 < json.size()) {
            char e = json[++i];
            switch (e) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(e); break;
            }
        } else {
            value.push_back(c);
        }
    }
    return false;
}

static bool looksLikeJsonObject(const std::string& json){
    size_t first = json.find_first_not_of(" \t\r\n");
    size_t last = json.find_last_not_of(" \t\r\n");
    return first != std::string::npos && json[first] == '{' && json[last] == '}';
}

static bool parseMetadataJson(const std::string& json, GXAppMetadata& metadata){
    if (!looksLikeJsonObject(json)) return false;
    std::string format;
    if (!parseJsonStringField(json, "format", format) || format != "gxapp") return false;
    if (!parseJsonStringField(json, "applicationName", metadata.applicationName)) return false;
    if (!parseJsonStringField(json, "version", metadata.version)) return false;
    if (!parseJsonStringField(json, "requiredGuideXOSVersion", metadata.requiredGuideXOSVersion)) return false;
    return !metadata.applicationName.empty() && !metadata.version.empty() && !metadata.requiredGuideXOSVersion.empty();
}

static GXAppMetadata parseMetadataJson(const std::string& json){
    GXAppMetadata metadata;
    parseMetadataJson(json, metadata);
    return metadata;
}

static std::string parseEntryPointForArchitecture(const std::string& json, CpuArchitecture architecture){
    std::string arch = GXAppContainer::ArchitectureToString(architecture);
    std::string archKey = "\"architecture\"";
    size_t pos = 0;
    while ((pos = json.find(archKey, pos)) != std::string::npos) {
        size_t colon = json.find(':', pos + archKey.size());
        size_t quote = colon == std::string::npos ? std::string::npos : json.find('"', colon + 1);
        size_t endQuote = quote == std::string::npos ? std::string::npos : json.find('"', quote + 1);
        if (endQuote == std::string::npos) return std::string();
        if (json.substr(quote + 1, endQuote - quote - 1) == arch) {
            size_t objectEnd = json.find('}', endQuote + 1);
            std::string objectJson = json.substr(pos, objectEnd == std::string::npos ? std::string::npos : objectEnd - pos);
            std::string entryPoint;
            parseJsonStringField(objectJson, "entryPoint", entryPoint);
            return entryPoint;
        }
        pos = endQuote + 1;
    }
    return std::string();
}

static std::string binaryPath(CpuArchitecture architecture){ return std::string("bin/") + GXAppContainer::ArchitectureToString(architecture) + "/app.bin"; }
static bool isSupportedArchitecture(CpuArchitecture architecture){ return architecture != CpuArchitecture::Unknown && GXAppContainer::ArchitectureToString(architecture) != std::string("unknown"); }

} // namespace

GXApp GXApp::Open(const std::string& path){ return GXAppContainer::Open(path); }
GXApp GXApp::Create(const GXAppMetadata& metadata){ return GXAppContainer::Create(metadata); }

bool GXApp::Save(const std::string& path) const{
    std::vector<uint8_t> out;
    out.insert(out.end(), GXAPP_MAGIC, GXAPP_MAGIC + sizeof(GXAPP_MAGIC));
    appendU32(out, GXAPP_FORMAT_VERSION);
    appendU32(out, GXAPP_FLAG_REQUIRED);
    appendU32(out, static_cast<uint32_t>(1 + binaries_.size()));
    std::vector<EntryRecord> entries;
    EntryRecord metadataEntry;
    metadataEntry.kind = EntryKind::Metadata;
    metadataEntry.architecture = CpuArchitecture::Unknown;
    metadataEntry.path = "metadata.json";
    std::string metadataJson = makeMetadataJson(metadata_, binaries_);
    metadataEntry.data.assign(metadataJson.begin(), metadataJson.end());
    entries.push_back(metadataEntry);
    for (const GXAppBinary& binary : binaries_) {
        EntryRecord entry;
        entry.kind = EntryKind::Binary;
        entry.architecture = binary.architecture;
        entry.path = binaryPath(binary.architecture);
        entry.data = binary.data;
        entries.push_back(entry);
    }
    for (const EntryRecord& entry : entries) {
        if (entry.path.size() > std::numeric_limits<uint16_t>::max()) return false;
        appendU32(out, static_cast<uint32_t>(entry.kind));
        appendU32(out, static_cast<uint32_t>(entry.architecture));
        appendU16(out, static_cast<uint16_t>(entry.path.size()));
        appendU64(out, static_cast<uint64_t>(entry.data.size()));
        out.insert(out.end(), entry.path.begin(), entry.path.end());
        out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    return FS::writeAll(path, out);
}

bool GXApp::IsValid() const{ return valid_; }
const std::string& GXApp::GetLastError() const{ return lastError_; }
const GXAppMetadata& GXApp::GetMetadata() const{ return metadata_; }
void GXApp::SetMetadata(const GXAppMetadata& metadata){ metadata_ = metadata; }

bool GXApp::AddBinary(CpuArchitecture architecture, const std::string& entryPoint, const std::vector<uint8_t>& binary){
    if (!isSupportedArchitecture(architecture) || binary.empty() || binary.size() > GXAPP_MAX_BINARY_SIZE || entryPoint.size() > GXAPP_MAX_ENTRY_SIZE) return false;
    for (GXAppBinary& existing : binaries_) {
        if (existing.architecture == architecture) { existing.entryPoint = entryPoint; existing.data = binary; return true; }
    }
    GXAppBinary entry;
    entry.architecture = architecture;
    entry.entryPoint = entryPoint;
    entry.data = binary;
    binaries_.push_back(entry);
    return true;
}

bool GXApp::HasBinary(CpuArchitecture architecture) const{
    for (const GXAppBinary& binary : binaries_) if (binary.architecture == architecture) return true;
    return false;
}

std::vector<uint8_t> GXApp::GetBinary(CpuArchitecture architecture) const{
    for (const GXAppBinary& binary : binaries_) if (binary.architecture == architecture) return binary.data;
    return std::vector<uint8_t>();
}

bool GXApp::ExtractBinary(CpuArchitecture architecture, const std::string& outputPath) const{
    std::vector<uint8_t> binary = GetBinary(architecture);
    if (binary.empty()) return false;
    return FS::writeAll(outputPath, binary);
}

std::string GXApp::GetEntryPoint(CpuArchitecture architecture) const{
    for (const GXAppBinary& binary : binaries_) if (binary.architecture == architecture) return binary.entryPoint;
    return std::string();
}

std::vector<CpuArchitecture> GXApp::GetArchitectures() const{
    std::vector<CpuArchitecture> architectures;
    for (const GXAppBinary& binary : binaries_) architectures.push_back(binary.architecture);
    return architectures;
}

void GXApp::setError(const std::string& error){ valid_ = false; lastError_ = error; }

GXApp GXAppContainer::Create(const GXAppMetadata& metadata){
    GXApp app;
    app.metadata_ = metadata;
    app.valid_ = true;
    return app;
}

GXApp GXAppContainer::Open(const std::string& path){
    GXApp app;
    std::vector<uint8_t> data;
    FSResult readResult = FS::readAll(path, data, GXAPP_MAX_PACKAGE_SIZE);
    if (!readResult.success) { app.setError(std::string("failed to read gxapp package: ") + readResult.message); return app; }
    if (data.size() > GXAPP_MAX_PACKAGE_SIZE) { app.setError("gxapp package exceeds maximum size"); return app; }
    size_t offset = 0;
    if (data.size() < sizeof(GXAPP_MAGIC) + 12 || std::memcmp(data.data(), GXAPP_MAGIC, sizeof(GXAPP_MAGIC)) != 0) { app.setError("invalid gxapp header"); return app; }
    offset += sizeof(GXAPP_MAGIC);
    uint32_t formatVersion; uint32_t flags; uint32_t entryCount;
    if (!readU32(data, offset, formatVersion) || !readU32(data, offset, flags) || !readU32(data, offset, entryCount)) { app.setError("truncated gxapp header"); return app; }
    if (formatVersion != GXAPP_FORMAT_VERSION) { app.setError("unsupported gxapp format version"); return app; }
    if ((flags & GXAPP_FLAG_REQUIRED) != GXAPP_FLAG_REQUIRED) { app.setError("invalid gxapp flags"); return app; }
    if (entryCount == 0 || entryCount > GXAPP_MAX_ENTRY_COUNT) { app.setError("invalid gxapp entry count"); return app; }
    bool foundMetadata = false;
    std::string metadataJson;
    std::vector<EntryRecord> binaryEntries;
    std::set<CpuArchitecture> seenArchitectures;
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t kindValue; uint32_t archValue; uint16_t pathSize; uint64_t dataSize;
        if (!readU32(data, offset, kindValue) || !readU32(data, offset, archValue) || !readU16(data, offset, pathSize) || !readU64(data, offset, dataSize)) { app.setError("truncated gxapp entry header"); return app; }
        size_t pathEnd;
        if (!checkedAdd(offset, pathSize, pathEnd) || pathEnd > data.size()) { app.setError("invalid gxapp entry path size"); return app; }
        std::string entryPath(reinterpret_cast<const char*>(&data[offset]), pathSize);
        offset = pathEnd;
        size_t dataEnd;
        if (!checkedAdd(offset, dataSize, dataEnd) || dataEnd > data.size()) { app.setError("invalid gxapp entry data size"); return app; }
        EntryKind kind = static_cast<EntryKind>(kindValue);
        CpuArchitecture architecture = static_cast<CpuArchitecture>(archValue);
        if (kind == EntryKind::Metadata) {
            if (foundMetadata || entryPath != "metadata.json" || dataSize > GXAPP_MAX_METADATA_SIZE) { app.setError("invalid gxapp metadata entry"); return app; }
            metadataJson.assign(reinterpret_cast<const char*>(&data[offset]), static_cast<size_t>(dataSize));
            if (!parseMetadataJson(metadataJson, app.metadata_)) { app.setError("malformed gxapp metadata"); return app; }
            foundMetadata = true;
        } else if (kind == EntryKind::Binary) {
            if (!isSupportedArchitecture(architecture) || entryPath != binaryPath(architecture) || dataSize == 0 || dataSize > GXAPP_MAX_BINARY_SIZE) { app.setError("invalid gxapp binary entry"); return app; }
            if (!seenArchitectures.insert(architecture).second) { app.setError("duplicate gxapp architecture entry"); return app; }
            EntryRecord record;
            record.kind = kind;
            record.architecture = architecture;
            record.path = entryPath;
            record.data.assign(data.begin() + offset, data.begin() + dataEnd);
            binaryEntries.push_back(record);
        } else {
            app.setError("unknown gxapp entry type");
            return app;
        }
        offset = dataEnd;
    }
    if (offset != data.size()) { app.setError("trailing bytes after gxapp entries"); return app; }
    if (!foundMetadata) { app.setError("missing gxapp metadata"); return app; }
    if (app.metadata_.applicationName.empty() || app.metadata_.version.empty() || app.metadata_.requiredGuideXOSVersion.empty()) { app.setError("incomplete gxapp metadata"); return app; }
    if (binaryEntries.empty()) { app.setError("gxapp package has no architecture binaries"); return app; }
    for (const EntryRecord& record : binaryEntries) {
        std::string entryPoint = parseEntryPointForArchitecture(metadataJson, record.architecture);
        if (entryPoint.empty()) entryPoint = "main";
        app.AddBinary(record.architecture, entryPoint, record.data);
    }
    app.valid_ = true;
    return app;
}

bool GXAppContainer::CreatePackage(const std::string& path, const GXAppMetadata& metadata, const std::vector<GXAppBinary>& binaries){
    GXApp app = Create(metadata);
    for (const GXAppBinary& binary : binaries) {
        if (!app.AddBinary(binary.architecture, binary.entryPoint, binary.data)) return false;
    }
    return app.Save(path);
}

bool GXAppContainer::ExtractBinary(const std::string& packagePath, CpuArchitecture architecture, const std::string& outputPath){
    GXApp app = Open(packagePath);
    if (!app.IsValid()) return false;
    return app.ExtractBinary(architecture, outputPath);
}

const char* GXAppContainer::ArchitectureToString(CpuArchitecture architecture){
    return CpuArchitectureToString(architecture);
}

CpuArchitecture GXAppContainer::ArchitectureFromString(const std::string& architecture){
    return CpuArchitectureFromString(architecture);
}

} // namespace gxos
