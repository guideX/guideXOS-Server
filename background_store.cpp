#include "background_store.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#if !defined(GXOS_BARE_METAL)
#include <filesystem>
#endif

#if !defined(GXOS_BARE_METAL) && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if !defined(GXOS_BARE_METAL)
#include "fs.h"
#include "kernel/core/include/kernel/image_adapter.h"
#endif

namespace gxos {
namespace gui {
namespace {

std::string trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool parseUnsigned(const std::string& value, uint64_t& result)
{
    if (value.empty()) return false;
    uint64_t parsed = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
    }
    result = parsed;
    return true;
}

bool parseQuoted(const std::string& input, std::string& value)
{
    value.clear();
    const std::string source = trim(input);
    if (source.size() < 2 || source.front() != '"') return false;
    size_t i = 1;
    while (i < source.size()) {
        const char c = source[i++];
        if (c == '"') return i == source.size();
        if (static_cast<unsigned char>(c) < 0x20u) return false;
        if (c != '\\') {
            value.push_back(c);
            continue;
        }
        if (i >= source.size()) return false;
        const char escaped = source[i++];
        switch (escaped) {
        case '\\': value.push_back('\\'); break;
        case '"': value.push_back('"'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        default: return false;
        }
    }
    return false;
}

bool isHexString(const std::string& value)
{
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool boundedString(const std::string& value, size_t maxBytes, bool allowEmpty = false)
{
    if (!allowEmpty && value.empty()) return false;
    if (value.size() > maxBytes) return false;
    for (unsigned char c : value) {
        if (c < 0x20u || c == 0x7Fu) return false;
    }
    return true;
}

std::string normalizeLogicalPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.find("//") != std::string::npos) path.replace(path.find("//"), 2, "/");
    return path;
}

bool isSafeManifestKey(const std::string& key)
{
    if (key == "version" || key == "count") return true;
    const std::string prefix = "background.";
    if (key.rfind(prefix, 0) != 0) return false;
    const size_t indexBegin = prefix.size();
    const size_t indexEnd = key.find('.', indexBegin);
    if (indexEnd == std::string::npos || indexEnd == indexBegin || indexEnd + 1 >= key.size()) return false;
    const std::string indexText = key.substr(indexBegin, indexEnd - indexBegin);
    uint64_t index = 0;
    if (!parseUnsigned(indexText, index) || index > kMaxUserBackgroundRecords) return false;
    const std::string field = key.substr(indexEnd + 1);
    return field == "id" || field == "displayName" || field == "kind" || field == "owner" ||
        field == "fullImagePath" || field == "thumbnailPath" || field == "sourceName" ||
        field == "contentHash" || field == "contentSize";
}

#if !defined(GXOS_BARE_METAL)
std::string hostedExecutableDirectory()
{
#if defined(_WIN32)
    char modulePath[32768] = {};
    const DWORD length = GetModuleFileNameA(nullptr, modulePath, static_cast<DWORD>(sizeof(modulePath)));
    if (length > 0 && length < sizeof(modulePath)) {
        std::error_code ec;
        const std::filesystem::path directory = std::filesystem::path(std::string(modulePath, length)).parent_path();
        if (!directory.empty()) return directory.string();
    }
#elif defined(__linux__)
    std::error_code ec;
    const std::filesystem::path modulePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !modulePath.parent_path().empty()) return modulePath.parent_path().string();
#endif

    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (!ec) return current.string();
    return std::string();
}
#endif

std::string quote(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << c; break;
        }
    }
    out << '"';
    return out.str();
}

#if !defined(GXOS_BARE_METAL)
std::vector<BackgroundEntry> s_users;
std::vector<BackgroundEntry> s_merged;
std::vector<BackgroundEntry> s_images;
bool s_loaded = false;

bool readOwnedBytes(const std::string& logicalPath, std::vector<uint8_t>& bytes)
{
    return FS::readAll(BackgroundStore::HostPathForOwned(logicalPath), bytes);
}
#endif

} // namespace

std::string BackgroundStore::CanonicalFullImagePath(const std::string& id)
{
    return std::string(kUserBackgroundRoot) + id + ".png";
}

std::string BackgroundStore::CanonicalThumbnailPath(const std::string& id)
{
    return std::string(kUserBackgroundRoot) + id + "_thumb.png";
}

std::string BackgroundStore::HostStorageDirectory()
{
#if defined(GXOS_BARE_METAL)
    return std::string();
#else
    const std::string executableDirectory = hostedExecutableDirectory();
    if (executableDirectory.empty()) return std::string();
    return (std::filesystem::path(executableDirectory) / "user-data" / "backgrounds").lexically_normal().string();
#endif
}

std::string BackgroundStore::HostPathForOwned(const std::string& logicalPath)
{
    std::string normalized = normalizeLogicalPath(logicalPath);
    const std::string root(kUserBackgroundRoot);
    if (normalized.rfind(root, 0) != 0) return std::string();
    const std::string relative = normalized.substr(root.size());
    if (relative.empty() || relative == "." || relative == ".." || relative.find('/') != std::string::npos || relative.find("..") != std::string::npos) return std::string();
    for (unsigned char c : relative) {
        if (c < 0x20u || c == 0x7Fu || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return std::string();
        }
    }
#if defined(GXOS_BARE_METAL)
    return std::string("user-data\\backgrounds\\") + relative;
#else
    const std::string storageDirectory = HostStorageDirectory();
    if (storageDirectory.empty()) return std::string();
    return (std::filesystem::path(storageDirectory) / relative).lexically_normal().string();
#endif
}

bool BackgroundStore::IsValidUserId(const std::string& id)
{
    static constexpr size_t kPrefixLength = 5;
    return id.size() == kPrefixLength + 32 && id.rfind("user_", 0) == 0 && isHexString(id.substr(kPrefixLength));
}

std::string BackgroundStore::ComputeContentHash(const std::vector<uint8_t>& bytes)
{
    // Two independent FNV-1a streams provide a compact deterministic content
    // identity without bringing a crypto dependency into the shared model.
    uint64_t first = 1469598103934665603ull;
    uint64_t second = 1099511628211ull ^ 0x9E3779B97F4A7C15ull;
    for (uint8_t byte : bytes) {
        first ^= byte;
        first *= 1099511628211ull;
        second ^= static_cast<uint64_t>(byte) + 0xA5u;
        second *= 14029467366897019727ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << first << std::setw(16) << second;
    return out.str();
}

bool BackgroundStore::ParseManifestText(const std::string& text, std::vector<BackgroundEntry>& records, std::string& error)
{
    records.clear();
    error.clear();
    if (text.size() > kMaxUserBackgroundManifestBytes) {
        error = "manifest exceeds 64 KiB";
        return false;
    }
    if (text.empty()) {
        error = "manifest is empty";
        return false;
    }
    if (text.back() != '\n') {
        error = "manifest has a truncated final line";
        return false;
    }
    if (text.find('\0') != std::string::npos) {
        error = "manifest contains an embedded NUL";
        return false;
    }

    std::map<std::string, std::string> values;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > kMaxUserBackgroundManifestLineBytes) {
            error = "manifest line exceeds 2048 bytes";
            records.clear();
            return false;
        }
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            error = "manifest line is missing '='";
            records.clear();
            return false;
        }
        const std::string key = trim(line.substr(0, separator));
        if (key.empty() || key.size() > 128 || !isSafeManifestKey(key)) {
            error = "manifest contains an unknown field";
            records.clear();
            return false;
        }
        if (values.find(key) != values.end()) {
            error = "manifest contains a duplicate field";
            records.clear();
            return false;
        }
        values.emplace(key, line.substr(separator + 1));
    }

    uint64_t version = 0;
    uint64_t count = 0;
    auto versionIt = values.find("version");
    auto countIt = values.find("count");
    const std::string versionText = versionIt == values.end() ? std::string() : trim(versionIt->second);
    const std::string countText = countIt == values.end() ? std::string() : trim(countIt->second);
    if (versionIt == values.end() || countIt == values.end() || !boundedString(versionText, 32) || !boundedString(countText, 32) ||
        !parseUnsigned(versionText, version) || version != kUserBackgroundManifestVersion) {
        error = "unsupported or missing manifest version";
        return false;
    }
    if (!parseUnsigned(countText, count) || count > kMaxUserBackgroundRecords) {
        error = "manifest record count exceeds 64";
        return false;
    }

    std::set<uint64_t> recordIndexes;
    for (const auto& value : values) {
        const std::string prefix = "background.";
        if (value.first.rfind(prefix, 0) != 0) continue;
        const size_t indexEnd = value.first.find('.', prefix.size());
        uint64_t index = 0;
        if (indexEnd == std::string::npos || !parseUnsigned(value.first.substr(prefix.size(), indexEnd - prefix.size()), index) || index >= count) {
            error = "manifest contains a record outside the declared count";
            records.clear();
            return false;
        }
        recordIndexes.insert(index);
    }
    if (recordIndexes.size() != static_cast<size_t>(count)) {
        error = "manifest declared records are missing";
        records.clear();
        return false;
    }

    std::set<std::string> ids;
    const auto getQuoted = [&](size_t index, const char* field, std::string& result) {
        auto it = values.find("background." + std::to_string(index) + "." + field);
        return it != values.end() && parseQuoted(it->second, result);
    };

    for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        BackgroundEntry record{};
        std::string owner;
        std::string kind;
        std::string contentSize;
        bool valid = getQuoted(i, "id", record.id) &&
            getQuoted(i, "displayName", record.displayName) &&
            getQuoted(i, "kind", kind) &&
            getQuoted(i, "owner", owner) &&
            getQuoted(i, "fullImagePath", record.fullImagePath) &&
            getQuoted(i, "thumbnailPath", record.thumbnailPath) &&
            getQuoted(i, "sourceName", record.sourceName) &&
            getQuoted(i, "contentHash", record.contentHash) &&
            getQuoted(i, "contentSize", contentSize);
        uint64_t parsedSize = 0;
        valid = valid && boundedString(record.id, 64) && boundedString(record.displayName, 128) &&
            boundedString(record.sourceName, 255) && boundedString(record.contentHash, 64) &&
            boundedString(record.fullImagePath, 512) && boundedString(record.thumbnailPath, 512) &&
            boundedString(kind, 32) && boundedString(owner, 32) && boundedString(contentSize, 32) &&
            kind == "image" && owner == "user" && IsValidUserId(record.id) &&
            isHexString(record.contentHash) && record.contentHash.size() == 32 &&
            parseUnsigned(contentSize, parsedSize) && parsedSize > 0 && parsedSize <= 4u * 1024u * 1024u;
        valid = valid && normalizeLogicalPath(record.fullImagePath) == CanonicalFullImagePath(record.id) &&
            normalizeLogicalPath(record.thumbnailPath) == CanonicalThumbnailPath(record.id);
        if (!valid || WallpaperRegistry::FindBackgroundById(record.id) != nullptr || !ids.insert(record.id).second) {
            records.clear();
            error = "manifest contains an invalid, colliding, or duplicate record";
            return false;
        }

        record.kind = BackgroundKind::Image;
        record.owner = BackgroundOwner::UserImported;
        record.fullImagePath = CanonicalFullImagePath(record.id);
        record.thumbnailPath = CanonicalThumbnailPath(record.id);
        record.contentSize = parsedSize;
        record.topColor = 0xFF142850;
        record.bottomColor = 0xFF0F121C;
        record.accentColor = 0xFF6FA8FF;
        record.solidColor = record.topColor;
        records.push_back(std::move(record));
    }
    return true;
}

bool BackgroundStore::SerializeManifest(const std::vector<BackgroundEntry>& records, std::string& text, std::string& error)
{
    text.clear();
    error.clear();
    if (records.size() > kMaxUserBackgroundRecords) {
        error = "manifest record count exceeds 64";
        return false;
    }

    std::vector<const BackgroundEntry*> ordered;
    ordered.reserve(records.size());
    for (const auto& record : records) ordered.push_back(&record);
    std::sort(ordered.begin(), ordered.end(), [](const BackgroundEntry* left, const BackgroundEntry* right) {
        return left->id < right->id;
    });

    std::set<std::string> ids;
    std::ostringstream out;
    out << "version=1\ncount=" << ordered.size() << "\n";
    for (size_t i = 0; i < ordered.size(); ++i) {
        const BackgroundEntry& record = *ordered[i];
        if (record.owner != BackgroundOwner::UserImported || record.kind != BackgroundKind::Image ||
            !IsValidUserId(record.id) || WallpaperRegistry::FindBackgroundById(record.id) != nullptr || !ids.insert(record.id).second ||
            !boundedString(record.displayName, 128) || !boundedString(record.sourceName, 255) ||
            !boundedString(record.contentHash, 64) || record.contentHash.size() != 32 || !isHexString(record.contentHash) ||
            record.contentSize == 0 || record.contentSize > 4u * 1024u * 1024u) {
            error = "invalid user background record";
            text.clear();
            return false;
        }
        const std::string fullPath = CanonicalFullImagePath(record.id);
        const std::string thumbPath = CanonicalThumbnailPath(record.id);
        out << "background." << i << ".id=" << quote(record.id) << '\n'
            << "background." << i << ".displayName=" << quote(record.displayName) << '\n'
            << "background." << i << ".kind=\"image\"\n"
            << "background." << i << ".owner=\"user\"\n"
            << "background." << i << ".fullImagePath=" << quote(fullPath) << '\n'
            << "background." << i << ".thumbnailPath=" << quote(thumbPath) << '\n'
            << "background." << i << ".sourceName=" << quote(record.sourceName) << '\n'
            << "background." << i << ".contentHash=" << quote(record.contentHash) << '\n'
            << "background." << i << ".contentSize=" << quote(std::to_string(record.contentSize)) << '\n';
    }
    text = out.str();
    if (text.size() > kMaxUserBackgroundManifestBytes) {
        text.clear();
        error = "manifest exceeds 64 KiB";
        return false;
    }
    return true;
}

#if !defined(GXOS_BARE_METAL)
bool BackgroundStore::ValidateOwnedRecordFiles(BackgroundEntry& record, std::string& error)
{
    std::vector<uint8_t> fullBytes;
    std::vector<uint8_t> thumbBytes;
    if (!readOwnedBytes(record.fullImagePath, fullBytes) || !readOwnedBytes(record.thumbnailPath, thumbBytes)) {
        error = "owned background file missing";
        return false;
    }
    if (fullBytes.size() != record.contentSize || ComputeContentHash(fullBytes) != record.contentHash) {
        error = "owned background content does not match manifest";
        return false;
    }
    const ImageBitmap full = ImageAdapter::LoadFromBytes(fullBytes, record.fullImagePath);
    const ImageBitmap thumb = ImageAdapter::LoadFromBytes(thumbBytes, record.thumbnailPath);
    if (full.status != ImageLoadStatus::Ok || thumb.status != ImageLoadStatus::Ok ||
        thumb.width < 1 || thumb.height < 1 || thumb.width > kUserBackgroundThumbnailMaxWidth || thumb.height > kUserBackgroundThumbnailMaxHeight) {
        error = "owned background PNG validation failed";
        return false;
    }
    return true;
}

bool BackgroundStore::LoadManifestFromDisk(std::vector<BackgroundEntry>& records, std::string& error)
{
    records.clear();
    const std::string hostPath = HostPathForOwned(kUserBackgroundManifestPath);
    if (hostPath.empty()) {
        error = "invalid manifest storage path";
        return false;
    }
    if (!FS::exists(hostPath)) return true;
    std::vector<uint8_t> bytes;
    const FSResult read = FS::readAll(hostPath, bytes, kMaxUserBackgroundManifestBytes);
    if (!read.success) {
        error = read.message.empty() ? "manifest read failed" : read.message;
        return false;
    }
    std::vector<BackgroundEntry> parsed;
    if (!ParseManifestText(std::string(bytes.begin(), bytes.end()), parsed, error)) return false;
    for (auto& record : parsed) {
        std::string recordError;
        if (ValidateOwnedRecordFiles(record, recordError)) {
            records.push_back(std::move(record));
        }
    }
    return true;
}
#else
bool BackgroundStore::ValidateOwnedRecordFiles(BackgroundEntry&, std::string&)
{
    return false;
}

bool BackgroundStore::LoadManifestFromDisk(std::vector<BackgroundEntry>& records, std::string& error)
{
    records.clear();
    error = "user background manifest is hosted-only in Phase 1";
    return true;
}
#endif

void BackgroundStore::RebuildMerged(const std::vector<BackgroundEntry>& records)
{
#if !defined(GXOS_BARE_METAL)
    s_users = records;
    s_merged = WallpaperRegistry::BuiltInBackgrounds();
    s_merged.insert(s_merged.end(), s_users.begin(), s_users.end());
    s_images.clear();
    for (const auto& entry : s_merged) {
        if (entry.kind == BackgroundKind::Image) s_images.push_back(entry);
    }
#else
    (void)records;
#endif
}

bool BackgroundStore::Reload(std::string& error)
{
#if defined(GXOS_BARE_METAL)
    error.clear();
    return true;
#else
    std::vector<BackgroundEntry> records;
    const bool ok = LoadManifestFromDisk(records, error);
    RebuildMerged(ok ? records : std::vector<BackgroundEntry>{});
    s_loaded = true;
    return ok;
#endif
}

void BackgroundStore::EnsureLoaded()
{
#if !defined(GXOS_BARE_METAL)
    if (!s_loaded) {
        std::string ignored;
        Reload(ignored);
    }
#endif
}

const std::vector<BackgroundEntry>& BackgroundStore::MergedBackgrounds()
{
#if defined(GXOS_BARE_METAL)
    return WallpaperRegistry::BuiltInBackgrounds();
#else
    EnsureLoaded();
    return s_merged;
#endif
}

const std::vector<BackgroundEntry>& BackgroundStore::MergedImageBackgrounds()
{
#if defined(GXOS_BARE_METAL)
    static const std::vector<BackgroundEntry> images = [] {
        std::vector<BackgroundEntry> result;
        for (const auto& entry : WallpaperRegistry::BuiltInBackgrounds()) {
            if (entry.kind == BackgroundKind::Image) result.push_back(entry);
        }
        return result;
    }();
    return images;
#else
    EnsureLoaded();
    return s_images;
#endif
}

const std::vector<BackgroundEntry>& BackgroundStore::UserBackgrounds()
{
#if defined(GXOS_BARE_METAL)
    static const std::vector<BackgroundEntry> none;
    return none;
#else
    EnsureLoaded();
    return s_users;
#endif
}

const BackgroundEntry* BackgroundStore::FindById(const std::string& id)
{
    EnsureLoaded();
#if defined(GXOS_BARE_METAL)
    return WallpaperRegistry::FindBackgroundById(id);
#else
    // Built-ins are authoritative if a future store bug attempts a collision.
    if (const BackgroundEntry* builtIn = WallpaperRegistry::FindBackgroundById(id)) return builtIn;
    auto it = std::find_if(s_users.begin(), s_users.end(), [&id](const BackgroundEntry& entry) { return entry.id == id; });
    return it == s_users.end() ? nullptr : &(*it);
#endif
}

std::string BackgroundStore::ResolveIdOrDefault(const std::string& id)
{
    return FindById(id) ? id : WallpaperRegistry::DefaultBackground().id;
}

} // namespace gui
} // namespace gxos
