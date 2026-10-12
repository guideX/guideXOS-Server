#include "include/kernel/settings_native_provider.h"

#include "include/kernel/arch.h"
#include "include/kernel/version.h"

namespace kernel {
namespace settings_native {
namespace {

template <size_t Capacity>
static bool copyBoundedText(char (&destination)[Capacity], const char* source)
{
    if (!source || !source[0]) return false;
    size_t length = 0;
    while (source[length] && length < Capacity - 1u) ++length;
    if (length == 0 || source[length] != '\0') return false;
    for (size_t i = 0; i < length; ++i) destination[i] = source[i];
    destination[length] = '\0';
    return true;
}

template <size_t Capacity>
static bool validText(const char (&text)[Capacity])
{
    bool nonEmpty = false;
    for (size_t i = 0; i < Capacity; ++i) {
        if (text[i] == '\0') return nonEmpty;
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20u || c == 0x7Fu) return false;
        nonEmpty = true;
    }
    return false;
}

static bool textEquals(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

} // namespace

SnapshotStatus buildSystemAboutSnapshot(const char* productName,
                                        const char* kernelVersion,
                                        const char* architecture,
                                        uint64_t revision,
                                        SystemAboutSnapshotV1* output)
{
    if (!output) return SnapshotStatus::InvalidArgument;
    SystemAboutSnapshotV1 snapshot{};
    snapshot.structureSize = sizeof(SystemAboutSnapshotV1);
    snapshot.version = kSystemAboutSnapshotVersion;
    snapshot.revision = revision;
    snapshot.flags = kSystemAboutSnapshotAvailable;
    if (revision == 0 ||
        !copyBoundedText(snapshot.productName, productName) ||
        !copyBoundedText(snapshot.kernelVersion, kernelVersion) ||
        !copyBoundedText(snapshot.architecture, architecture) ||
        !validSystemAboutSnapshot(snapshot)) {
        *output = SystemAboutSnapshotV1{};
        output->structureSize = sizeof(SystemAboutSnapshotV1);
        output->version = kSystemAboutSnapshotVersion;
        output->revision = revision;
        return SnapshotStatus::Unavailable;
    }
    *output = snapshot;
    return SnapshotStatus::Ok;
}

bool validSystemAboutSnapshot(const SystemAboutSnapshotV1& snapshot)
{
    return snapshot.structureSize == sizeof(SystemAboutSnapshotV1) &&
        snapshot.version == kSystemAboutSnapshotVersion &&
        snapshot.revision != 0 &&
        snapshot.flags == kSystemAboutSnapshotAvailable &&
        validText(snapshot.productName) &&
        validText(snapshot.kernelVersion) &&
        validText(snapshot.architecture);
}

SnapshotStatus readSystemAboutSnapshot(
    const app::KernelApp* caller,
    const SystemAboutSnapshotRequestV1* request,
    void* output,
    size_t capacity,
    size_t* outSize,
    uint64_t* outRevision)
{
    if (outSize) *outSize = 0;
    if (outRevision) *outRevision = 0;
    if (!caller || !request || !output || !outSize || !outRevision) {
        return SnapshotStatus::InvalidArgument;
    }
    if (request->structureSize < sizeof(SystemAboutSnapshotRequestV1)) {
        return SnapshotStatus::InvalidArgument;
    }
    if (request->version != kSystemAboutSnapshotVersion) {
        return SnapshotStatus::UnsupportedVersion;
    }
    if (!textEquals(caller->getRuntimeAppId(), kSettingsRuntimeAppId)) {
        return SnapshotStatus::Unauthorized;
    }

    *outSize = sizeof(SystemAboutSnapshotV1);
    *outRevision = kSystemAboutSnapshotRevision;
    if (capacity < sizeof(SystemAboutSnapshotV1)) return SnapshotStatus::BufferTooSmall;
    if (request->knownRevision != 0 &&
        request->knownRevision != kSystemAboutSnapshotRevision) {
        return SnapshotStatus::StaleRevision;
    }

    SystemAboutSnapshotV1 snapshot{};
    const SnapshotStatus status = buildSystemAboutSnapshot(
        KERNEL_NAME,
        KERNEL_VERSION_STRING,
        arch::get_arch_name(),
        kSystemAboutSnapshotRevision,
        &snapshot);
    if (status != SnapshotStatus::Ok) return status;

    uint8_t* destination = static_cast<uint8_t*>(output);
    const uint8_t* source = reinterpret_cast<const uint8_t*>(&snapshot);
    for (size_t i = 0; i < sizeof(snapshot); ++i) destination[i] = source[i];
    return SnapshotStatus::Ok;
}

} // namespace settings_native
} // namespace kernel
