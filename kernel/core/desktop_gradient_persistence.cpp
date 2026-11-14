#include "kernel/desktop_gradient_persistence.h"
#include "kernel/vfs.h"

namespace kernel {
namespace desktop {
namespace {

static const char kDesktopAppDataFormat[] = "GUIDEXOS-DESKTOP-STATE";
static const char kDesktopAppDataPreamble[] =
    "format=GUIDEXOS-DESKTOP-STATE\nversion=1\n";

static bool spanEquals(const char* text, size_t begin, size_t end,
                       const char* expected);

static size_t boundedLength(const char* value, size_t capacity)
{
    if (!value) return capacity;
    size_t length = 0u;
    while (length < capacity && value[length]) ++length;
    return length;
}

static bool validPresetId(const char* value)
{
    const size_t length = boundedLength(value, kGradientPresetIdCapacity);
    if (length == 0u || length >= kGradientPresetIdCapacity) return false;
    for (size_t index = 0u; index < length; ++index) {
        const char ch = value[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-')) return false;
    }
    return true;
}

static bool equalText(const char* left, const char* right)
{
    if (!left || !right) return false;
    size_t index = 0u;
    while (left[index] && right[index] && left[index] == right[index]) ++index;
    return left[index] == '\0' && right[index] == '\0';
}

static void copyText(char* destination, const char* source, size_t capacity)
{
    if (!destination || capacity == 0u) return;
    size_t index = 0u;
    if (source) {
        while (index + 1u < capacity && source[index]) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static void trimSpan(const char* text, size_t* begin, size_t* end);

static DesktopAppDataReadStatus validateAppDataHeader(
    const char* text, size_t length)
{
    if (!text || length == 0u) return DesktopAppDataReadStatus::Invalid;
    size_t formatCount = 0u;
    size_t versionCount = 0u;
    bool supportedVersion = false;
    size_t cursor = 0u;
    while (cursor < length) {
        const size_t lineBegin = cursor;
        while (cursor < length && text[cursor] != '\n') ++cursor;
        const size_t lineEnd = cursor;
        if (cursor < length) ++cursor;
        size_t begin = lineBegin;
        size_t end = lineEnd;
        if (end > begin && text[end - 1u] == '\r') --end;
        trimSpan(text, &begin, &end);
        if (begin == end || text[begin] == '#' || text[begin] == ';') continue;
        size_t separator = begin;
        while (separator < end && text[separator] != '=') ++separator;
        if (separator == end) return DesktopAppDataReadStatus::Invalid;
        size_t keyBegin = begin;
        size_t keyEnd = separator;
        size_t valueBegin = separator + 1u;
        size_t valueEnd = end;
        trimSpan(text, &keyBegin, &keyEnd);
        trimSpan(text, &valueBegin, &valueEnd);
        if (spanEquals(text, keyBegin, keyEnd, "format")) {
            ++formatCount;
            supportedVersion = valueEnd - valueBegin == sizeof(kDesktopAppDataFormat) - 1u;
            if (supportedVersion) {
                for (size_t index = 0u; index < sizeof(kDesktopAppDataFormat) - 1u; ++index) {
                    if (text[valueBegin + index] != kDesktopAppDataFormat[index])
                        supportedVersion = false;
                }
            }
        } else if (spanEquals(text, keyBegin, keyEnd, "version")) {
            ++versionCount;
            if (valueEnd - valueBegin != 1u || text[valueBegin] != '1')
                return DesktopAppDataReadStatus::UnsupportedVersion;
        }
    }
    if (formatCount != 1u || versionCount != 1u)
        return DesktopAppDataReadStatus::Invalid;
    return supportedVersion ? DesktopAppDataReadStatus::Loaded
                            : DesktopAppDataReadStatus::Invalid;
}

static DesktopAppDataReadStatus readAppDataConfig(
    char* output, size_t capacity, size_t* outLength)
{
    if (!output || capacity == 0u || !outLength)
        return DesktopAppDataReadStatus::Invalid;
    *outLength = 0u;
    const vfs::MountPoint* mount = vfs::get_mount(kDesktopAppDataConfigPath);
    if (!mount || !mount->active) return DesktopAppDataReadStatus::Unavailable;

    vfs::FileInfo info{};
    const vfs::Status statStatus = vfs::stat(kDesktopAppDataConfigPath, &info);
    if (statStatus == vfs::VFS_ERR_NOT_FOUND) return DesktopAppDataReadStatus::Missing;
    if (statStatus != vfs::VFS_OK) return DesktopAppDataReadStatus::Unavailable;
    if (info.type != vfs::FILE_TYPE_REGULAR || info.size == 0u ||
        info.size >= capacity) return DesktopAppDataReadStatus::Invalid;

    const int32_t read = vfs::read_file(
        kDesktopAppDataConfigPath, output, static_cast<uint32_t>(capacity - 1u));
    if (read <= 0 || static_cast<uint64_t>(read) != info.size)
        return DesktopAppDataReadStatus::Unavailable;
    const size_t length = static_cast<size_t>(read);
    for (size_t index = 0u; index < length; ++index) {
        if (output[index] == '\0') return DesktopAppDataReadStatus::Invalid;
    }
    output[length] = '\0';
    *outLength = length;
    return validateAppDataHeader(output, length);
}

static void trimSpan(const char* text, size_t* begin, size_t* end)
{
    while (*begin < *end &&
           (text[*begin] == ' ' || text[*begin] == '\t' ||
            text[*begin] == '\r' || text[*begin] == '\n')) ++*begin;
    while (*end > *begin &&
           (text[*end - 1u] == ' ' || text[*end - 1u] == '\t' ||
            text[*end - 1u] == '\r' || text[*end - 1u] == '\n')) --*end;
}

static bool extractWallpaperId(
    const char* text, size_t length, char* output, size_t outputCapacity,
    size_t* outValueBegin, size_t* outValueEnd, size_t* outValueCount)
{
    if (!text || !output || outputCapacity == 0u) return false;
    output[0] = '\0';
    size_t cursor = 0u;
    size_t foundCount = 0u;
    size_t valueBegin = 0u;
    size_t valueEnd = 0u;
    while (cursor < length) {
        const size_t lineBegin = cursor;
        while (cursor < length && text[cursor] != '\n') ++cursor;
        const size_t lineEnd = cursor;
        if (cursor < length) ++cursor;
        size_t logicalEnd = lineEnd;
        if (logicalEnd > lineBegin && text[logicalEnd - 1u] == '\r') --logicalEnd;
        size_t separator = lineBegin;
        while (separator < logicalEnd && text[separator] != '=') ++separator;
        if (separator == logicalEnd) continue;

        size_t keyBegin = lineBegin;
        size_t keyEnd = separator;
        trimSpan(text, &keyBegin, &keyEnd);
        if (keyEnd - keyBegin != sizeof("wallpaperId") - 1u) continue;
        static const char key[] = "wallpaperId";
        bool keyMatch = true;
        for (size_t index = 0u; index < sizeof(key) - 1u; ++index) {
            if (text[keyBegin + index] != key[index]) keyMatch = false;
        }
        if (!keyMatch) continue;

        ++foundCount;
        size_t begin = separator + 1u;
        size_t end = logicalEnd;
        trimSpan(text, &begin, &end);
        valueBegin = begin;
        valueEnd = end;
        const size_t valueLength = end - begin;
        if (valueLength >= outputCapacity) return false;
        for (size_t index = 0u; index < valueLength; ++index) {
            output[index] = text[begin + index];
        }
        output[valueLength] = '\0';
    }
    if (outValueBegin) *outValueBegin = valueBegin;
    if (outValueEnd) *outValueEnd = valueEnd;
    if (outValueCount) *outValueCount = foundCount;
    return foundCount <= 1u;
}

static bool makeUpdatedConfig(
    const char* original, size_t originalLength,
    const char* requestedId, char* updated, size_t capacity,
    size_t* outLength)
{
    char currentId[kGradientPresetIdCapacity] = {};
    size_t valueBegin = 0u;
    size_t valueEnd = 0u;
    size_t valueCount = 0u;
    if (!extractWallpaperId(original, originalLength, currentId, sizeof(currentId),
                            &valueBegin, &valueEnd, &valueCount)) return false;

    const size_t idLength = boundedLength(requestedId, kGradientPresetIdCapacity);
    size_t newLength = 0u;
    if (valueCount == 0u) {
        const bool needNewline = originalLength != 0u &&
            original[originalLength - 1u] != '\n' &&
            original[originalLength - 1u] != '\r';
        static const char prefix[] = "wallpaperId=";
        newLength = originalLength + (needNewline ? 1u : 0u) +
            sizeof(prefix) - 1u + idLength + 1u;
        if (newLength >= capacity) return false;
        size_t cursor = 0u;
        for (size_t index = 0u; index < originalLength; ++index) updated[cursor++] = original[index];
        if (needNewline) updated[cursor++] = '\n';
        for (size_t index = 0u; index < sizeof(prefix) - 1u; ++index) updated[cursor++] = prefix[index];
        for (size_t index = 0u; index < idLength; ++index) updated[cursor++] = requestedId[index];
        updated[cursor++] = '\n';
        updated[cursor] = '\0';
        *outLength = cursor;
        return true;
    }

    if (valueCount != 1u || valueEnd < valueBegin ||
        originalLength - (valueEnd - valueBegin) + idLength >= capacity) return false;
    newLength = originalLength - (valueEnd - valueBegin) + idLength;
    size_t cursor = 0u;
    for (size_t index = 0u; index < valueBegin; ++index) updated[cursor++] = original[index];
    for (size_t index = 0u; index < idLength; ++index) updated[cursor++] = requestedId[index];
    for (size_t index = valueEnd; index < originalLength; ++index) updated[cursor++] = original[index];
    updated[cursor] = '\0';
    *outLength = cursor;
    return cursor == newLength;
}

struct DesktopIconConfigField {
    const char* key;
    uint32_t bit;
    size_t valueBegin;
    size_t valueEnd;
    bool present;
    bool value;
};

static bool parseConfigBoolean(const char* text, size_t begin, size_t end,
                               bool* output)
{
    if (!text || !output || begin >= end) return false;
    const size_t length = end - begin;
    if (length == 1u && text[begin] == '1') { *output = true; return true; }
    if (length == 1u && text[begin] == '0') { *output = false; return true; }
    static const char* const trueValues[] = { "true", "yes", "on" };
    static const char* const falseValues[] = { "false", "no", "off" };
    for (size_t candidate = 0u; candidate < sizeof(trueValues) / sizeof(trueValues[0]); ++candidate) {
        const char* value = trueValues[candidate];
        size_t valueLength = 0u;
        while (value[valueLength]) ++valueLength;
        if (length != valueLength) continue;
        bool match = true;
        for (size_t index = 0u; index < length; ++index) {
            char actual = text[begin + index];
            if (actual >= 'A' && actual <= 'Z') actual = static_cast<char>(actual - 'A' + 'a');
            if (actual != value[index]) match = false;
        }
        if (match) { *output = true; return true; }
    }
    for (size_t candidate = 0u; candidate < sizeof(falseValues) / sizeof(falseValues[0]); ++candidate) {
        const char* value = falseValues[candidate];
        size_t valueLength = 0u;
        while (value[valueLength]) ++valueLength;
        if (length != valueLength) continue;
        bool match = true;
        for (size_t index = 0u; index < length; ++index) {
            char actual = text[begin + index];
            if (actual >= 'A' && actual <= 'Z') actual = static_cast<char>(actual - 'A' + 'a');
            if (actual != value[index]) match = false;
        }
        if (match) { *output = false; return true; }
    }
    return false;
}

static bool spanEquals(const char* text, size_t begin, size_t end,
                       const char* expected)
{
    if (!text || !expected) return false;
    size_t length = 0u;
    while (expected[length]) ++length;
    if (end < begin || end - begin != length) return false;
    for (size_t index = 0u; index < length; ++index) {
        if (text[begin + index] != expected[index]) return false;
    }
    return true;
}

static bool extractDesktopIconVisibility(
    const char* text, size_t length, uint32_t* outVisibility,
    DesktopIconConfigField* outFields, size_t fieldCapacity,
    uint32_t defaultVisibility)
{
    if (!text || !outVisibility || !outFields || fieldCapacity < 4u ||
        (defaultVisibility & ~kDesktopIconVisibilityKnownMask) != 0u) return false;
    DesktopIconConfigField fields[4] = {
        { "showDesktopTrash", kDesktopIconVisibilityTrash, 0u, 0u, false,
            (defaultVisibility & kDesktopIconVisibilityTrash) != 0u },
        { "showDesktopThisSystem", kDesktopIconVisibilityThisSystem, 0u, 0u, false,
            (defaultVisibility & kDesktopIconVisibilityThisSystem) != 0u },
        { "showDesktopFileManager", kDesktopIconVisibilityFileManager, 0u, 0u, false,
            (defaultVisibility & kDesktopIconVisibilityFileManager) != 0u },
        { "showDesktopSystemSettings", kDesktopIconVisibilitySystemSettings, 0u, 0u, false,
            (defaultVisibility & kDesktopIconVisibilitySystemSettings) != 0u }
    };
    uint32_t visibility = defaultVisibility;
    size_t cursor = 0u;
    while (cursor < length) {
        const size_t lineBegin = cursor;
        while (cursor < length && text[cursor] != '\n') ++cursor;
        const size_t lineEnd = cursor;
        if (cursor < length) ++cursor;
        size_t logicalEnd = lineEnd;
        if (logicalEnd > lineBegin && text[logicalEnd - 1u] == '\r') --logicalEnd;
        size_t begin = lineBegin;
        size_t end = logicalEnd;
        trimSpan(text, &begin, &end);
        if (begin == end || text[begin] == '#' || text[begin] == ';') continue;

        size_t separator = begin;
        while (separator < end && text[separator] != '=') ++separator;
        if (separator == end) return false;
        size_t keyBegin = begin;
        size_t keyEnd = separator;
        trimSpan(text, &keyBegin, &keyEnd);
        if (keyBegin == keyEnd) return false;
        size_t valueBegin = separator + 1u;
        size_t valueEnd = end;
        trimSpan(text, &valueBegin, &valueEnd);

        for (size_t index = 0u; index < 4u; ++index) {
            if (!spanEquals(text, keyBegin, keyEnd, fields[index].key)) continue;
            if (fields[index].present) return false;
            bool value = false;
            if (!parseConfigBoolean(text, valueBegin, valueEnd, &value)) return false;
            fields[index].present = true;
            fields[index].value = value;
            fields[index].valueBegin = valueBegin;
            fields[index].valueEnd = valueEnd;
            if (value) visibility |= fields[index].bit;
            else visibility &= ~fields[index].bit;
            break;
        }
    }
    for (size_t index = 0u; index < 4u; ++index) outFields[index] = fields[index];
    *outVisibility = visibility;
    return true;
}

static bool makeUpdatedDesktopIconConfig(
    const char* original, size_t originalLength, uint32_t requestedVisibility,
    const DesktopIconConfigField* fields, char* updated, size_t capacity,
    size_t* outLength)
{
    if (!original || !fields || !updated || !outLength || capacity == 0u) return false;
    size_t sourceCursor = 0u;
    size_t outputCursor = 0u;
    const DesktopIconConfigField* ordered[4] = {
        &fields[0], &fields[1], &fields[2], &fields[3]
    };
    for (size_t index = 0u; index < 4u; ++index) {
        for (size_t next = index + 1u; next < 4u; ++next) {
            const size_t left = ordered[index]->present
                ? ordered[index]->valueBegin : static_cast<size_t>(-1);
            const size_t right = ordered[next]->present
                ? ordered[next]->valueBegin : static_cast<size_t>(-1);
            if (right < left) {
                const DesktopIconConfigField* temporary = ordered[index];
                ordered[index] = ordered[next];
                ordered[next] = temporary;
            }
        }
    }
    for (size_t fieldIndex = 0u; fieldIndex < 4u; ++fieldIndex) {
        const DesktopIconConfigField& field = *ordered[fieldIndex];
        const bool requested = (requestedVisibility & field.bit) != 0u;
        if (!field.present || requested == field.value) continue;
        if (field.valueBegin < sourceCursor || field.valueEnd < field.valueBegin ||
            field.valueEnd > originalLength) return false;
        const size_t prefixLength = field.valueBegin - sourceCursor;
        if (prefixLength > capacity - 1u - outputCursor) return false;
        for (size_t index = 0u; index < prefixLength; ++index)
            updated[outputCursor++] = original[sourceCursor + index];
        if (outputCursor >= capacity - 1u) return false;
        updated[outputCursor++] = requested ? '1' : '0';
        sourceCursor = field.valueEnd;
    }
    if (originalLength - sourceCursor > capacity - 1u - outputCursor) return false;
    for (size_t index = sourceCursor; index < originalLength; ++index)
        updated[outputCursor++] = original[index];

    for (size_t fieldIndex = 0u; fieldIndex < 4u; ++fieldIndex) {
        const DesktopIconConfigField& field = fields[fieldIndex];
        const bool requested = (requestedVisibility & field.bit) != 0u;
        if (field.present || requested == field.value) continue;
        const char* key = field.key;
        size_t keyLength = 0u;
        while (key[keyLength]) ++keyLength;
        const bool needsSeparator = outputCursor != 0u &&
            updated[outputCursor - 1u] != '\n' && updated[outputCursor - 1u] != '\r';
        const size_t required = (needsSeparator ? 1u : 0u) + keyLength + 3u;
        if (required > capacity - 1u - outputCursor) return false;
        if (needsSeparator) updated[outputCursor++] = '\n';
        for (size_t index = 0u; index < keyLength; ++index)
            updated[outputCursor++] = key[index];
        updated[outputCursor++] = '=';
        updated[outputCursor++] = requested ? '1' : '0';
        updated[outputCursor++] = '\n';
    }
    updated[outputCursor] = '\0';
    *outLength = outputCursor;
    return true;
}

static DesktopAppDataReadStatus loadConfigForUpdate(
    char* original, size_t capacity, size_t* originalLength,
    bool* originalMissing)
{
    if (!original || !originalLength || !originalMissing || capacity == 0u)
        return DesktopAppDataReadStatus::Invalid;
    *originalLength = 0u;
    *originalMissing = false;
    const DesktopAppDataReadStatus readStatus = readAppDataConfig(
        original, capacity, originalLength);
    if (readStatus != DesktopAppDataReadStatus::Missing) return readStatus;
    const size_t preambleLength = sizeof(kDesktopAppDataPreamble) - 1u;
    if (preambleLength >= capacity) return DesktopAppDataReadStatus::Invalid;
    for (size_t index = 0u; index < preambleLength; ++index)
        original[index] = kDesktopAppDataPreamble[index];
    original[preambleLength] = '\0';
    *originalLength = preambleLength;
    *originalMissing = true;
    return DesktopAppDataReadStatus::Loaded;
}

static bool removeNewConfigAndVerify()
{
    const vfs::Status unlinkStatus = vfs::unlink(kDesktopAppDataConfigPath);
    if (unlinkStatus != vfs::VFS_OK) {
        vfs::FileInfo info{};
        return vfs::stat(kDesktopAppDataConfigPath, &info) == vfs::VFS_ERR_NOT_FOUND;
    }
    vfs::FileInfo info{};
    return vfs::stat(kDesktopAppDataConfigPath, &info) == vfs::VFS_ERR_NOT_FOUND;
}

enum class WriteVerification : uint8_t {
    Verified,
    Unavailable,
    WriteFailed,
    ReadbackFailed
};

static WriteVerification writeAndVerify(const char* text, size_t length)
{
    const vfs::MountPoint* mount = vfs::get_mount(kDesktopAppDataConfigPath);
    if (!mount || !mount->active || mount->readOnly) return WriteVerification::Unavailable;
    const int32_t written = vfs::write_file(
        kDesktopAppDataConfigPath, text, static_cast<uint32_t>(length));
    if (written == vfs::VFS_ERR_NOT_MOUNT || written == vfs::VFS_ERR_READ_ONLY)
        return WriteVerification::Unavailable;
    if (written < 0 || static_cast<size_t>(written) != length) return WriteVerification::WriteFailed;
    char verify[kGradientConfigTextCapacity];
    size_t verifyLength = 0u;
    if (readAppDataConfig(verify, sizeof(verify), &verifyLength) !=
            DesktopAppDataReadStatus::Loaded ||
        verifyLength != length) return WriteVerification::ReadbackFailed;
    for (size_t index = 0u; index < length; ++index) {
        if (verify[index] != text[index]) return WriteVerification::ReadbackFailed;
    }
    return WriteVerification::Verified;
}

static void updateActualId(GradientPersistenceResult* output)
{
    char actual[kGradientConfigTextCapacity];
    size_t actualLength = 0u;
    if (readAppDataConfig(actual, sizeof(actual), &actualLength) !=
        DesktopAppDataReadStatus::Loaded) return;
    char id[kGradientPresetIdCapacity] = {};
    size_t valueCount = 0u;
    if (!extractWallpaperId(actual, actualLength, id, sizeof(id), nullptr, nullptr, &valueCount) ||
        valueCount != 1u || !validPresetId(id)) return;
    copyText(output->persistedId, id, sizeof(output->persistedId));
    output->flags |= kGradientPersistenceReadbackAvailable;
}

static void updateActualDesktopIconVisibility(
    DesktopIconPersistenceResult* output, uint32_t defaultVisibility)
{
    if (!output) return;
    output->flags &= ~kDesktopIconPersistenceReadbackAvailable;
    output->persistedVisibility = 0u;
    char actual[kGradientConfigTextCapacity];
    size_t actualLength = 0u;
    if (readAppDataConfig(actual, sizeof(actual), &actualLength) !=
        DesktopAppDataReadStatus::Loaded) return;
    DesktopIconConfigField fields[4]{};
    uint32_t visibility = 0u;
    if (!extractDesktopIconVisibility(actual, actualLength, &visibility,
                                      fields, 4u, defaultVisibility)) return;
    output->persistedVisibility = visibility;
    output->flags |= kDesktopIconPersistenceReadbackAvailable;
}

} // namespace

DesktopAppDataReadStatus readAppDataWallpaperId(
    char* output, size_t outputCapacity)
{
    if (!output || outputCapacity == 0u) return DesktopAppDataReadStatus::Invalid;
    output[0] = '\0';
    char config[kGradientConfigTextCapacity];
    size_t configLength = 0u;
    const DesktopAppDataReadStatus readStatus = readAppDataConfig(
        config, sizeof(config), &configLength);
    if (readStatus != DesktopAppDataReadStatus::Loaded) return readStatus;
    size_t valueCount = 0u;
    if (!extractWallpaperId(config, configLength, output, outputCapacity,
                            nullptr, nullptr, &valueCount)) {
        output[0] = '\0';
        return DesktopAppDataReadStatus::Invalid;
    }
    if (valueCount == 0u) return DesktopAppDataReadStatus::Missing;
    return validPresetId(output) ? DesktopAppDataReadStatus::Loaded
                                 : DesktopAppDataReadStatus::Invalid;
}

DesktopAppDataReadStatus readAppDataDesktopIconVisibility(
    uint32_t defaultVisibility, uint32_t* outputVisibility)
{
    if (!outputVisibility ||
        (defaultVisibility & ~kDesktopIconVisibilityKnownMask) != 0u)
        return DesktopAppDataReadStatus::Invalid;
    char config[kGradientConfigTextCapacity];
    size_t configLength = 0u;
    const DesktopAppDataReadStatus readStatus = readAppDataConfig(
        config, sizeof(config), &configLength);
    if (readStatus != DesktopAppDataReadStatus::Loaded) return readStatus;
    DesktopIconConfigField fields[4]{};
    uint32_t visibility = 0u;
    if (!extractDesktopIconVisibility(config, configLength, &visibility,
                                      fields, 4u, defaultVisibility))
        return DesktopAppDataReadStatus::Invalid;
    bool hasOverride = false;
    for (size_t index = 0u; index < 4u; ++index)
        hasOverride = hasOverride || fields[index].present;
    if (!hasOverride) return DesktopAppDataReadStatus::Missing;
    *outputVisibility = visibility;
    return DesktopAppDataReadStatus::Loaded;
}

bool readCanonicalWallpaperId(char* output, size_t outputCapacity)
{
    return readAppDataWallpaperId(output, outputCapacity) ==
        DesktopAppDataReadStatus::Loaded;
}

GradientPersistenceStatus persistBuiltInBackgroundId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output)
{
    if (!output) return GradientPersistenceStatus::InvalidConfiguration;
    *output = GradientPersistenceResult{};
    output->status = GradientPersistenceStatus::InvalidConfiguration;
    if (!validPresetId(expectedCurrentId) || !validPresetId(requestedId)) return output->status;

    char original[kGradientConfigTextCapacity];
    size_t originalLength = 0u;
    bool originalMissing = false;
    const DesktopAppDataReadStatus configStatus = loadConfigForUpdate(
        original, sizeof(original), &originalLength, &originalMissing);
    if (configStatus != DesktopAppDataReadStatus::Loaded) {
        output->status = configStatus == DesktopAppDataReadStatus::Unavailable
            ? GradientPersistenceStatus::Unavailable
            : GradientPersistenceStatus::InvalidConfiguration;
        return output->status;
    }

    char oldId[kGradientPresetIdCapacity] = {};
    size_t oldCount = 0u;
    if (!extractWallpaperId(original, originalLength, oldId, sizeof(oldId),
                            nullptr, nullptr, &oldCount)) {
        output->status = GradientPersistenceStatus::InvalidConfiguration;
        return output->status;
    }
    const bool emptyDefaultMatches = oldCount == 1u && oldId[0] == '\0' &&
        equalText(expectedCurrentId, kDesktopDefaultWallpaperId);
    if (oldCount == 1u && oldId[0] != '\0' && !validPresetId(oldId)) {
        output->status = GradientPersistenceStatus::InvalidConfiguration;
        return output->status;
    }
    if (oldCount == 1u && !equalText(oldId, expectedCurrentId) &&
        !emptyDefaultMatches) {
        output->status = GradientPersistenceStatus::Conflict;
        updateActualId(output);
        return output->status;
    }

    char updated[kGradientConfigTextCapacity];
    size_t updatedLength = 0u;
    if (!makeUpdatedConfig(original, originalLength, requestedId,
                           updated, sizeof(updated), &updatedLength)) {
        output->status = GradientPersistenceStatus::InvalidConfiguration;
        return output->status;
    }

    const WriteVerification commitVerification = writeAndVerify(updated, updatedLength);
    if (commitVerification == WriteVerification::Unavailable) {
        output->status = GradientPersistenceStatus::Unavailable;
        return output->status;
    }
    if (commitVerification == WriteVerification::Verified) {
        output->status = GradientPersistenceStatus::Ok;
        copyText(output->persistedId, requestedId, sizeof(output->persistedId));
        output->flags |= kGradientPersistenceReadbackAvailable;
        return output->status;
    }

    output->flags |= kGradientPersistenceRollbackAttempted;
    const bool restored = originalMissing
        ? removeNewConfigAndVerify()
        : writeAndVerify(original, originalLength) == WriteVerification::Verified;
    if (restored) output->flags |= kGradientPersistenceRollbackVerified;
    updateActualId(output);
    if (!restored) {
        output->status = GradientPersistenceStatus::RollbackFailed;
        return output->status;
    }
    output->status = commitVerification == WriteVerification::WriteFailed
        ? GradientPersistenceStatus::WriteFailedRolledBack
        : GradientPersistenceStatus::ReadbackFailedRolledBack;
    return output->status;
}

GradientPersistenceStatus persistBuiltInGradientId(
    const char* expectedCurrentId,
    const char* requestedId,
    GradientPersistenceResult* output)
{
    return persistBuiltInBackgroundId(expectedCurrentId, requestedId, output);
}

DesktopIconPersistenceStatus persistDesktopIconVisibility(
    uint32_t expectedVisibility,
    uint32_t requestedVisibility,
    DesktopIconPersistenceResult* output)
{
    if (!output) return DesktopIconPersistenceStatus::InvalidConfiguration;
    *output = DesktopIconPersistenceResult{};
    output->status = DesktopIconPersistenceStatus::InvalidConfiguration;
    if ((expectedVisibility & ~kDesktopIconVisibilityKnownMask) != 0u ||
        (requestedVisibility & ~kDesktopIconVisibilityKnownMask) != 0u) {
        return output->status;
    }

    char original[kGradientConfigTextCapacity];
    size_t originalLength = 0u;
    bool originalMissing = false;
    const DesktopAppDataReadStatus configStatus = loadConfigForUpdate(
        original, sizeof(original), &originalLength, &originalMissing);
    if (configStatus != DesktopAppDataReadStatus::Loaded) {
        output->status = configStatus == DesktopAppDataReadStatus::Unavailable
            ? DesktopIconPersistenceStatus::Unavailable
            : DesktopIconPersistenceStatus::InvalidConfiguration;
        return output->status;
    }

    DesktopIconConfigField fields[4]{};
    uint32_t currentVisibility = 0u;
    if (!extractDesktopIconVisibility(original, originalLength,
                                      &currentVisibility, fields, 4u,
                                      expectedVisibility)) {
        output->status = DesktopIconPersistenceStatus::InvalidConfiguration;
        return output->status;
    }
    output->persistedVisibility = currentVisibility;
    output->flags |= kDesktopIconPersistenceReadbackAvailable;
    if (currentVisibility != expectedVisibility) {
        output->status = DesktopIconPersistenceStatus::Conflict;
        return output->status;
    }
    if (currentVisibility == requestedVisibility) {
        output->status = DesktopIconPersistenceStatus::NoChange;
        return output->status;
    }

    char updated[kGradientConfigTextCapacity];
    size_t updatedLength = 0u;
    if (!makeUpdatedDesktopIconConfig(original, originalLength,
                                      requestedVisibility, fields, updated,
                                      sizeof(updated), &updatedLength)) {
        output->status = DesktopIconPersistenceStatus::InvalidConfiguration;
        return output->status;
    }

    const WriteVerification commitVerification =
        writeAndVerify(updated, updatedLength);
    if (commitVerification == WriteVerification::Unavailable) {
        output->status = DesktopIconPersistenceStatus::Unavailable;
        return output->status;
    }
    if (commitVerification == WriteVerification::Verified) {
        char verified[kGradientConfigTextCapacity];
        size_t verifiedLength = 0u;
        DesktopIconConfigField verifiedFields[4]{};
        uint32_t verifiedVisibility = 0u;
        if (readAppDataConfig(verified, sizeof(verified), &verifiedLength) !=
                DesktopAppDataReadStatus::Loaded ||
            verifiedLength != updatedLength ||
            !extractDesktopIconVisibility(verified, verifiedLength,
                                         &verifiedVisibility, verifiedFields, 4u,
                                         expectedVisibility) ||
            verifiedVisibility != requestedVisibility) {
            output->flags |= kDesktopIconPersistenceRollbackAttempted;
            const bool restored = originalMissing
                ? removeNewConfigAndVerify()
                : writeAndVerify(original, originalLength) == WriteVerification::Verified;
            if (restored) output->flags |= kDesktopIconPersistenceRollbackVerified;
            updateActualDesktopIconVisibility(output, expectedVisibility);
            output->status = restored
                ? DesktopIconPersistenceStatus::ReadbackFailedRolledBack
                : DesktopIconPersistenceStatus::RollbackFailed;
            return output->status;
        }
        output->status = DesktopIconPersistenceStatus::Ok;
        output->persistedVisibility = verifiedVisibility;
        output->flags |= kDesktopIconPersistenceReadbackAvailable;
        return output->status;
    }

    output->flags |= kDesktopIconPersistenceRollbackAttempted;
    const bool restored = originalMissing
        ? removeNewConfigAndVerify()
        : writeAndVerify(original, originalLength) == WriteVerification::Verified;
    if (restored) output->flags |= kDesktopIconPersistenceRollbackVerified;
    updateActualDesktopIconVisibility(output, expectedVisibility);
    if (!restored) {
        output->status = DesktopIconPersistenceStatus::RollbackFailed;
        return output->status;
    }
    output->status = commitVerification == WriteVerification::WriteFailed
        ? DesktopIconPersistenceStatus::WriteFailedRolledBack
        : DesktopIconPersistenceStatus::ReadbackFailedRolledBack;
    return output->status;
}

} // namespace desktop
} // namespace kernel
