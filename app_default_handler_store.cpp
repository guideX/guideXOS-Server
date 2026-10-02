#include "app_default_handler_store.h"

#include "app_registry.h"
#include "fs.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gxos {
namespace apps {
namespace {

constexpr const char* kConfigHeader = "GXOS-APP-DEFAULTS 1\n";
std::mutex s_defaultHandlerStoreMutex;

uint32_t fnv1a(const std::string& value) {
    uint32_t hash = 2166136261u;
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

bool hasControl(const std::string& value) {
    for (unsigned char byte : value) {
        if (byte < 0x20u || byte == 0x7fu) return true;
    }
    return false;
}

bool parseUnsigned(const std::string& value, size_t& parsed) {
    if (value.empty()) return false;
    size_t result = 0;
    for (unsigned char byte : value) {
        if (byte < '0' || byte > '9') return false;
        const size_t digit = static_cast<size_t>(byte - '0');
        if (result > (static_cast<size_t>(-1) - digit) / 10) return false;
        result = result * 10 + digit;
    }
    parsed = result;
    return true;
}

bool writeAndFlush(const std::string& path, const std::string& text, std::string& error) {
#ifdef _WIN32
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "unable to create temporary default-handler configuration";
        return false;
    }
    size_t offset = 0;
    bool ok = true;
    while (offset < text.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(text.size() - offset, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(file, text.data() + offset, chunk, &written, nullptr) || written != chunk) {
            ok = false;
            error = "unable to write complete temporary default-handler configuration";
            break;
        }
        offset += written;
    }
    if (ok && !FlushFileBuffers(file)) {
        ok = false;
        error = "unable to flush temporary default-handler configuration";
    }
    if (!CloseHandle(file) && ok) {
        ok = false;
        error = "unable to close temporary default-handler configuration";
    }
    return ok;
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0) {
        error = "unable to create temporary default-handler configuration";
        return false;
    }
    size_t offset = 0;
    bool ok = true;
    while (offset < text.size()) {
        const ssize_t written = ::write(file, text.data() + offset, text.size() - offset);
        if (written <= 0) {
            ok = false;
            error = "unable to write complete temporary default-handler configuration";
            break;
        }
        offset += static_cast<size_t>(written);
    }
    if (ok && ::fsync(file) != 0) {
        ok = false;
        error = "unable to flush temporary default-handler configuration";
    }
    if (::close(file) != 0 && ok) {
        ok = false;
        error = "unable to close temporary default-handler configuration";
    }
    return ok;
#endif
}

bool replaceFile(const std::string& temporaryPath, const std::string& targetPath, std::string& error) {
#ifdef _WIN32
    if (!MoveFileExA(temporaryPath.c_str(), targetPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "atomic default-handler configuration replacement failed";
        return false;
    }
    return true;
#else
    if (::rename(temporaryPath.c_str(), targetPath.c_str()) != 0) {
        error = "atomic default-handler configuration replacement failed";
        return false;
    }
    const std::filesystem::path parent = std::filesystem::path(targetPath).parent_path();
    const std::string directory = parent.empty() ? "." : parent.string();
    const int handle = ::open(directory.c_str(), O_RDONLY
#ifdef O_DIRECTORY
        | O_DIRECTORY
#endif
    );
    if (handle >= 0) {
        ::fsync(handle);
        ::close(handle);
    }
    return true;
#endif
}

void removeTemporary(const std::string& path) {
#ifdef _WIN32
    DeleteFileA(path.c_str());
#else
    ::unlink(path.c_str());
#endif
}

bool readBounded(const std::string& path, std::string& text, std::string& error) {
    std::vector<uint8_t> bytes;
    const FSResult result = FS::readAll(path, bytes, kAppModelDefaultHandlerConfigMaxBytes);
    if (!result.success) {
        error = result.message.empty() ? "unable to read default-handler configuration" : result.message;
        return false;
    }
    text.assign(bytes.begin(), bytes.end());
    return true;
}

std::vector<DefaultHandlerOverride> canonicalized(const std::vector<DefaultHandlerOverride>& values,
                                                  bool& valid,
                                                  std::string& error) {
    valid = false;
    error.clear();
    if (values.size() > kAppModelMaxDefaultHandlerOverrides) {
        error = "default-handler override capacity exceeded";
        return {};
    }
    std::vector<DefaultHandlerOverride> result;
    result.reserve(values.size());
    std::set<std::string> extensions;
    for (const DefaultHandlerOverride& value : values) {
        std::string normalized;
        if (!NormalizeDocumentExtension(value.extension, normalized) || normalized != value.extension) {
            error = "default-handler extension must already be normalized";
            return {};
        }
        if (value.appId.empty() || value.appId.size() > kAppModelMaxAppIdBytes || hasControl(value.appId)) {
            error = "default-handler canonical App ID is invalid or over capacity";
            return {};
        }
        if (!extensions.insert(value.extension).second) {
            error = "duplicate default-handler extension";
            return {};
        }
        result.push_back(value);
    }
    std::sort(result.begin(), result.end(), [](const DefaultHandlerOverride& left, const DefaultHandlerOverride& right) {
        return left.extension < right.extension;
    });
    valid = true;
    return result;
}

} // namespace

DefaultAppHandlerStore::DefaultAppHandlerStore(std::string path)
    : m_path(std::move(path)) {
    std::string ignored;
    Reload(ignored);
}

bool DefaultAppHandlerStore::Reload(std::string& error) {
    std::lock_guard<std::mutex> lock(s_defaultHandlerStoreMutex);
    std::vector<DefaultHandlerOverride> loaded;
    size_t invalidCount = 0;
    DefaultHandlerStoreLoadStatus status = DefaultHandlerStoreLoadStatus::NotLoaded;
    const bool ok = ReadCurrent(loaded, invalidCount, status, error);
    m_overrides = std::move(loaded);
    m_diagnostics.loadStatus = status;
    m_diagnostics.overrideCount = m_overrides.size();
    m_diagnostics.invalidRecordCount = invalidCount;
    return ok;
}

const DefaultHandlerOverride* DefaultAppHandlerStore::Find(const std::string& normalizedExtension) const {
    const auto found = std::lower_bound(m_overrides.begin(), m_overrides.end(), normalizedExtension,
        [](const DefaultHandlerOverride& record, const std::string& extension) {
            return record.extension < extension;
        });
    return found != m_overrides.end() && found->extension == normalizedExtension ? &*found : nullptr;
}

bool DefaultAppHandlerStore::Commit(const std::vector<DefaultHandlerOverride>& overrides, std::string& error) {
    std::lock_guard<std::mutex> lock(s_defaultHandlerStoreMutex);
    error.clear();
    bool valid = false;
    const std::vector<DefaultHandlerOverride> expected = canonicalized(overrides, valid, error);
    if (!valid) {
        m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::WriteFailed;
        return false;
    }

    std::string serialized;
    if (!Serialize(expected, serialized, error)) {
        m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::WriteFailed;
        return false;
    }
    const std::string temporaryPath = m_path + ".tmp";
    if (!writeAndFlush(temporaryPath, serialized, error)) {
        removeTemporary(temporaryPath);
        m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::WriteFailed;
        return false;
    }

    std::string temporaryText;
    std::vector<DefaultHandlerOverride> temporaryRecords;
    size_t temporaryInvalidCount = 0;
    if (!readBounded(temporaryPath, temporaryText, error) ||
        !ParseText(temporaryText, temporaryRecords, temporaryInvalidCount, error) ||
        temporaryRecords != expected || temporaryInvalidCount != 0) {
        removeTemporary(temporaryPath);
        if (error.empty()) error = "temporary default-handler configuration did not verify";
        m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::VerificationFailed;
        return false;
    }

    if (!replaceFile(temporaryPath, m_path, error)) {
        removeTemporary(temporaryPath);
        m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::ReplaceFailed;
        return false;
    }

    std::string finalText;
    std::vector<DefaultHandlerOverride> finalRecords;
    size_t finalInvalidCount = 0;
    if (!readBounded(m_path, finalText, error) ||
        !ParseText(finalText, finalRecords, finalInvalidCount, error) ||
        finalRecords != expected || finalInvalidCount != 0) {
        if (error.empty()) error = "replaced default-handler configuration did not verify";
        m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::VerificationFailed;
        return false;
    }

    m_overrides = std::move(finalRecords);
    m_diagnostics.loadStatus = m_overrides.empty()
        ? DefaultHandlerStoreLoadStatus::Loaded : DefaultHandlerStoreLoadStatus::Loaded;
    m_diagnostics.overrideCount = m_overrides.size();
    m_diagnostics.invalidRecordCount = 0;
    m_diagnostics.lastWriteStatus = DefaultHandlerStoreWriteStatus::Succeeded;
    return true;
}

bool DefaultAppHandlerStore::ParseText(const std::string& text,
                                       std::vector<DefaultHandlerOverride>& overrides,
                                       size_t& invalidRecordCount,
                                       std::string& error) {
    overrides.clear();
    invalidRecordCount = 0;
    error.clear();
    if (text.empty() || text.size() > kAppModelDefaultHandlerConfigMaxBytes) {
        error = "default-handler configuration is empty or over capacity";
        return false;
    }
    if (text.compare(0, std::char_traits<char>::length(kConfigHeader), kConfigHeader) != 0) {
        error = "default-handler configuration header is invalid";
        return false;
    }

    const size_t footerStart = text.rfind("END ");
    if (footerStart == std::string::npos || footerStart == 0 || text[footerStart - 1] != '\n') {
        error = "default-handler configuration footer is missing or truncated";
        return false;
    }
    const size_t footerEnd = text.find('\n', footerStart);
    if (footerEnd == std::string::npos || footerEnd + 1 != text.size()) {
        error = "default-handler configuration footer is malformed";
        return false;
    }
    const std::string footer = text.substr(footerStart, footerEnd - footerStart);
    std::istringstream footerFields(footer);
    std::string endMarker;
    std::string countText;
    std::string checksumText;
    std::string extra;
    size_t recordCount = 0;
    if (!(footerFields >> endMarker >> countText >> checksumText) || (footerFields >> extra) ||
        endMarker != "END" || !parseUnsigned(countText, recordCount) || checksumText.size() != 8) {
        error = "default-handler configuration footer is malformed";
        return false;
    }
    uint32_t expectedChecksum = 0;
    for (char ch : checksumText) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        unsigned digit = 0;
        if (byte >= '0' && byte <= '9') digit = byte - '0';
        else if (byte >= 'a' && byte <= 'f') digit = byte - 'a' + 10;
        else if (byte >= 'A' && byte <= 'F') digit = byte - 'A' + 10;
        else { error = "default-handler configuration checksum is malformed"; return false; }
        expectedChecksum = (expectedChecksum << 4) | digit;
    }
    const size_t bodyStart = std::char_traits<char>::length(kConfigHeader);
    const std::string body = text.substr(bodyStart, footerStart - bodyStart);
    if (fnv1a(body) != expectedChecksum) {
        error = "default-handler configuration checksum mismatch";
        return false;
    }

    std::vector<std::string> lines;
    std::istringstream bodyInput(body);
    std::string line;
    while (std::getline(bodyInput, line)) {
        lines.push_back(line);
    }
    if (lines.size() != recordCount || recordCount > kAppModelMaxDefaultHandlerOverrides) {
        error = recordCount > kAppModelMaxDefaultHandlerOverrides
            ? "default-handler configuration exceeds its record capacity"
            : "default-handler configuration record count does not match its footer";
        return false;
    }

    std::map<std::string, DefaultHandlerOverride> parsed;
    std::set<std::string> duplicateExtensions;
    for (const std::string& recordLine : lines) {
        const size_t separator = recordLine.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= recordLine.size()) {
            ++invalidRecordCount;
            continue;
        }
        const std::string rawExtension = recordLine.substr(0, separator);
        const std::string appId = recordLine.substr(separator + 1);
        std::string normalized;
        if (!NormalizeDocumentExtension(rawExtension, normalized) || normalized != rawExtension ||
            appId.size() > kAppModelMaxAppIdBytes || hasControl(appId)) {
            ++invalidRecordCount;
            continue;
        }
        if (duplicateExtensions.find(normalized) != duplicateExtensions.end()) {
            ++invalidRecordCount;
            continue;
        }
        auto inserted = parsed.emplace(normalized, DefaultHandlerOverride{normalized, appId});
        if (!inserted.second) {
            parsed.erase(normalized);
            duplicateExtensions.insert(normalized);
            invalidRecordCount += 2;
        }
    }
    overrides.reserve(parsed.size());
    for (const auto& item : parsed) overrides.push_back(item.second);
    if (invalidRecordCount != 0) error = "one or more malformed or duplicate default-handler records were ignored";
    return true;
}

bool DefaultAppHandlerStore::Serialize(const std::vector<DefaultHandlerOverride>& overrides,
                                       std::string& text,
                                       std::string& error) {
    text.clear();
    bool valid = false;
    const std::vector<DefaultHandlerOverride> records = canonicalized(overrides, valid, error);
    if (!valid) return false;

    std::string body;
    for (const DefaultHandlerOverride& record : records) {
        body += record.extension;
        body += '=';
        body += record.appId;
        body += '\n';
    }
    std::ostringstream output;
    output << kConfigHeader << body << "END " << records.size() << ' ';
    output.width(8);
    output.fill('0');
    output << std::hex << std::uppercase << fnv1a(body) << "\n";
    text = output.str();
    if (text.size() > kAppModelDefaultHandlerConfigMaxBytes) {
        text.clear();
        error = "serialized default-handler configuration exceeds its byte capacity";
        return false;
    }
    error.clear();
    return true;
}

bool DefaultAppHandlerStore::ReadCurrent(std::vector<DefaultHandlerOverride>& overrides,
                                        size_t& invalidRecordCount,
                                        DefaultHandlerStoreLoadStatus& status,
                                        std::string& error) const {
    overrides.clear();
    invalidRecordCount = 0;
    error.clear();
    if (!FS::exists(m_path)) {
        status = DefaultHandlerStoreLoadStatus::Missing;
        return true;
    }
    std::string text;
    if (!readBounded(m_path, text, error)) {
        status = DefaultHandlerStoreLoadStatus::ReadFailure;
        return false;
    }
    if (!ParseText(text, overrides, invalidRecordCount, error)) {
        overrides.clear();
        status = DefaultHandlerStoreLoadStatus::Invalid;
        invalidRecordCount = 1;
        return false;
    }
    status = invalidRecordCount == 0
        ? DefaultHandlerStoreLoadStatus::Loaded : DefaultHandlerStoreLoadStatus::PartiallyLoaded;
    return true;
}

const char* DefaultAppHandlerStore::ToString(DefaultHandlerStoreLoadStatus status) {
    switch (status) {
    case DefaultHandlerStoreLoadStatus::Missing: return "missing";
    case DefaultHandlerStoreLoadStatus::Loaded: return "loaded";
    case DefaultHandlerStoreLoadStatus::PartiallyLoaded: return "partially-loaded";
    case DefaultHandlerStoreLoadStatus::Invalid: return "invalid";
    case DefaultHandlerStoreLoadStatus::ReadFailure: return "read-failure";
    case DefaultHandlerStoreLoadStatus::NotLoaded:
    default: return "not-loaded";
    }
}

const char* DefaultAppHandlerStore::ToString(DefaultHandlerStoreWriteStatus status) {
    switch (status) {
    case DefaultHandlerStoreWriteStatus::Succeeded: return "succeeded";
    case DefaultHandlerStoreWriteStatus::WriteFailed: return "write-failed";
    case DefaultHandlerStoreWriteStatus::ReplaceFailed: return "replace-failed";
    case DefaultHandlerStoreWriteStatus::VerificationFailed: return "verification-failed";
    case DefaultHandlerStoreWriteStatus::Never:
    default: return "never";
    }
}

} // namespace apps
} // namespace gxos
