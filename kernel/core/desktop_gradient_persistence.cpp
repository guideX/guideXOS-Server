#include "kernel/desktop_gradient_persistence.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace desktop {
namespace {

static const char kDesktopConfigPath[] = "/desktop.cfg";
static uint8_t s_configBlockDeviceIndex = 0xFFu;

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

static bool readCanonicalConfig(char* output, size_t capacity, size_t* outLength)
{
    if (!output || capacity == 0u || !outLength) return false;
    *outLength = 0u;
    const vfs::MountPoint* mount = vfs::get_mount(kDesktopConfigPath);
    if (!mount && s_configBlockDeviceIndex != 0xFFu &&
        vfs::mount("/", s_configBlockDeviceIndex) != 0xFFu) {
        mount = vfs::get_mount(kDesktopConfigPath);
        if (mount) {
            serial::puts("[desktop-gradient] config mount restored from device=");
            serial::put_hex8(s_configBlockDeviceIndex);
            serial::putc('\n');
        }
    }
    if (!mount || mount->readOnly) return false;

    vfs::FileInfo info{};
    if (vfs::stat(kDesktopConfigPath, &info) != vfs::VFS_OK ||
        info.type != vfs::FILE_TYPE_REGULAR || info.size == 0u ||
        info.size >= capacity) return false;

    const int32_t read = vfs::read_file(
        kDesktopConfigPath, output, static_cast<uint32_t>(capacity - 1u));
    if (read <= 0 || static_cast<uint64_t>(read) != info.size) return false;
    const size_t length = static_cast<size_t>(read);
    for (size_t index = 0u; index < length; ++index) {
        if (output[index] == '\0') return false;
    }
    output[length] = '\0';
    *outLength = length;
    s_configBlockDeviceIndex = mount->blockDevIndex;
    return true;
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

enum class WriteVerification : uint8_t {
    Verified,
    WriteFailed,
    ReadbackFailed
};

static WriteVerification writeAndVerify(const char* text, size_t length)
{
    const int32_t written = vfs::write_file(
        kDesktopConfigPath, text, static_cast<uint32_t>(length));
    if (written < 0 || static_cast<size_t>(written) != length) return WriteVerification::WriteFailed;
    char verify[kGradientConfigTextCapacity];
    size_t verifyLength = 0u;
    if (!readCanonicalConfig(verify, sizeof(verify), &verifyLength) ||
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
    if (!readCanonicalConfig(actual, sizeof(actual), &actualLength)) return;
    char id[kGradientPresetIdCapacity] = {};
    size_t valueCount = 0u;
    if (!extractWallpaperId(actual, actualLength, id, sizeof(id), nullptr, nullptr, &valueCount) ||
        valueCount != 1u || !validPresetId(id)) return;
    copyText(output->persistedId, id, sizeof(output->persistedId));
    output->flags |= kGradientPersistenceReadbackAvailable;
}

} // namespace

void rememberDesktopGradientStorageDevice()
{
    const vfs::MountPoint* root = vfs::get_mount("/");
    if (root && root->active && !root->alias && !root->readOnly) {
        s_configBlockDeviceIndex = root->blockDevIndex;
    }
}

bool readCanonicalWallpaperId(char* output, size_t outputCapacity)
{
    if (!output || outputCapacity == 0u) return false;
    output[0] = '\0';
    char config[kGradientConfigTextCapacity];
    size_t configLength = 0u;
    if (!readCanonicalConfig(config, sizeof(config), &configLength)) return false;
    size_t valueCount = 0u;
    if (!extractWallpaperId(config, configLength, output, outputCapacity,
                            nullptr, nullptr, &valueCount) || valueCount != 1u) {
        output[0] = '\0';
        return false;
    }
    return validPresetId(output);
}

GradientPersistenceStatus persistBuiltInGradientId(
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
    if (!readCanonicalConfig(original, sizeof(original), &originalLength)) {
        output->status = GradientPersistenceStatus::Unavailable;
        return output->status;
    }

    char oldId[kGradientPresetIdCapacity] = {};
    size_t oldCount = 0u;
    if (!extractWallpaperId(original, originalLength, oldId, sizeof(oldId),
                            nullptr, nullptr, &oldCount)) {
        output->status = GradientPersistenceStatus::InvalidConfiguration;
        return output->status;
    }
    if (oldCount == 1u && !equalText(oldId, expectedCurrentId)) {
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
    if (commitVerification == WriteVerification::Verified) {
        output->status = GradientPersistenceStatus::Ok;
        copyText(output->persistedId, requestedId, sizeof(output->persistedId));
        output->flags |= kGradientPersistenceReadbackAvailable;
        return output->status;
    }

    output->flags |= kGradientPersistenceRollbackAttempted;
    const bool restored =
        writeAndVerify(original, originalLength) == WriteVerification::Verified;
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

} // namespace desktop
} // namespace kernel
