#include "include/kernel/settings_native_provider.h"

#include "include/kernel/arch.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/version.h"
#include "../../display_configuration_command.h"
#include "../../display_configuration_service.h"

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

template <size_t Capacity>
static bool copyOptionalBoundedText(char (&destination)[Capacity], const char* source)
{
    if (!source || !source[0]) {
        destination[0] = '\0';
        return true;
    }
    return copyBoundedText(destination, source);
}

template <size_t Capacity>
static bool validOptionalText(const char (&text)[Capacity], bool available)
{
    return available ? validText(text) : text[0] == '\0';
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
                                        const char* osReleaseVersion,
                                        const char* kernelVersion,
                                        const char* architecture,
                                        uint64_t revision,
                                        SystemAboutSnapshotV2* output)
{
    if (!output) return SnapshotStatus::InvalidArgument;
    SystemAboutSnapshotV2 snapshot{};
    snapshot.structureSize = sizeof(SystemAboutSnapshotV2);
    snapshot.version = kSystemAboutSnapshotVersion;
    snapshot.revision = revision;
    snapshot.flags = kSystemAboutSnapshotAvailable;
    if (copyBoundedText(snapshot.productName, productName))
        snapshot.flags |= kSystemAboutProductNameAvailable;
    const bool osReleaseCopied = copyOptionalBoundedText(snapshot.osReleaseVersion, osReleaseVersion);
    if (osReleaseCopied &&
        snapshot.osReleaseVersion[0])
        snapshot.flags |= kSystemAboutOsReleaseVersionAvailable;
    if (copyBoundedText(snapshot.kernelVersion, kernelVersion))
        snapshot.flags |= kSystemAboutKernelVersionAvailable;
    if (copyBoundedText(snapshot.architecture, architecture))
        snapshot.flags |= kSystemAboutArchitectureAvailable;

    const uint32_t requiredFields = kSystemAboutProductNameAvailable |
        kSystemAboutKernelVersionAvailable | kSystemAboutArchitectureAvailable;
    if (revision == 0 || !osReleaseCopied ||
        (snapshot.flags & requiredFields) != requiredFields ||
        !validSystemAboutSnapshot(snapshot)) {
        *output = SystemAboutSnapshotV2{};
        output->structureSize = sizeof(SystemAboutSnapshotV2);
        output->version = kSystemAboutSnapshotVersion;
        output->revision = revision;
        return SnapshotStatus::Unavailable;
    }
    *output = snapshot;
    return SnapshotStatus::Ok;
}

bool validSystemAboutSnapshot(const SystemAboutSnapshotV2& snapshot)
{
    const uint32_t knownFlags = kSystemAboutSnapshotAvailable |
        kSystemAboutProductNameAvailable | kSystemAboutOsReleaseVersionAvailable |
        kSystemAboutKernelVersionAvailable | kSystemAboutArchitectureAvailable;
    const uint32_t requiredFields = kSystemAboutProductNameAvailable |
        kSystemAboutKernelVersionAvailable | kSystemAboutArchitectureAvailable;
    return snapshot.structureSize == sizeof(SystemAboutSnapshotV2) &&
        snapshot.version == kSystemAboutSnapshotVersion &&
        snapshot.revision != 0 &&
        (snapshot.flags & kSystemAboutSnapshotAvailable) != 0 &&
        (snapshot.flags & ~knownFlags) == 0 &&
        (snapshot.flags & requiredFields) == requiredFields &&
        validOptionalText(snapshot.productName,
            (snapshot.flags & kSystemAboutProductNameAvailable) != 0) &&
        validOptionalText(snapshot.osReleaseVersion,
            (snapshot.flags & kSystemAboutOsReleaseVersionAvailable) != 0) &&
        validOptionalText(snapshot.kernelVersion,
            (snapshot.flags & kSystemAboutKernelVersionAvailable) != 0) &&
        validOptionalText(snapshot.architecture,
            (snapshot.flags & kSystemAboutArchitectureAvailable) != 0);
}

SnapshotStatus readSystemAboutSnapshot(
    const app::KernelApp* caller,
    const SystemAboutSnapshotRequestV2* request,
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
    if (request->structureSize < sizeof(SystemAboutSnapshotRequestV2)) {
        return SnapshotStatus::InvalidArgument;
    }
    if (request->version != kSystemAboutSnapshotVersion) {
        return SnapshotStatus::UnsupportedVersion;
    }
    if (!textEquals(caller->getRuntimeAppId(), kSettingsRuntimeAppId)) {
        return SnapshotStatus::Unauthorized;
    }

    *outSize = sizeof(SystemAboutSnapshotV2);
    *outRevision = kSystemAboutSnapshotRevision;
    if (capacity < sizeof(SystemAboutSnapshotV2)) return SnapshotStatus::BufferTooSmall;
    if (request->knownRevision != 0 &&
        request->knownRevision != kSystemAboutSnapshotRevision) {
        return SnapshotStatus::StaleRevision;
    }

    SystemAboutSnapshotV2 snapshot{};
    const SnapshotStatus status = buildSystemAboutSnapshot(
        KERNEL_NAME,
        nullptr,
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

static bool validBitsPerPixel(uint32_t bitsPerPixel)
{
    return bitsPerPixel == 16u || bitsPerPixel == 24u || bitsPerPixel == 32u;
}

static uint32_t normalizePixelFormat(uint32_t pixelFormat)
{
    return pixelFormat <= static_cast<uint32_t>(framebuffer::PixelFormat::B8G8R8A8)
        ? pixelFormat
        : static_cast<uint32_t>(framebuffer::PixelFormat::Unknown);
}

static bool copyFixedText(char* destination, size_t capacity,
                          const char* source, size_t sourceCapacity)
{
    if (!destination || capacity == 0u || !source || sourceCapacity == 0u) return false;
    size_t length = 0u;
    while (length < sourceCapacity && source[length] != '\0') {
        const unsigned char character = static_cast<unsigned char>(source[length]);
        if (character < 0x20u || character == 0x7Fu) return false;
        ++length;
    }
    if (length == 0u || length == sourceCapacity || length >= capacity) return false;
    for (size_t index = 0u; index < length; ++index) destination[index] = source[index];
    destination[length] = '\0';
    return true;
}

static bool queryActiveDisplayConfiguration(
    gxos::display::DisplayConfigurationResponse& response,
    bool& topologyAvailable)
{
    topologyAvailable = false;
    gxos::display::DisplayConfigurationCommand request{};
    request.version = gxos::display::kDisplayConfigurationContractVersion;
    request.structureSize = sizeof(request);
    request.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    request.commandType = static_cast<uint32_t>(
        gxos::display::DisplayConfigurationCommandType::QueryActiveConfiguration);

    if (request.requestId == 0u) {
        return false;
    }

    const bool submitted = gxos::display::DisplayConfigurationService::submit(request, response);
    if (response.version != gxos::display::kDisplayConfigurationContractVersion ||
        response.structureSize < sizeof(response) || response.requestId != request.requestId ||
        response.commandType != request.commandType || response.completed == 0u ||
        response.activeConfigurationGeneration == 0u) {
        return false;
    }

    // The service returns its active generation even when it truthfully
    // reports that no output topology is available. That generation still
    // versions the combined framebuffer/topology snapshot. A busy or malformed
    // service response has no trustworthy generation and is rejected.
    if (!submitted || response.success == 0u) {
        return response.resultCode == static_cast<uint32_t>(
            gxos::display::DisplayConfigurationResultCode::BackendUnavailable);
    }

    const gxos::display::DisplayConfigurationSnapshot& active = response.activeConfiguration;
    topologyAvailable =
        active.version == gxos::display::kDisplayConfigurationContractVersion &&
        active.structureSize >= sizeof(active) && active.outputCount > 0u &&
        active.outputCount <= gxos::display::kDisplayConfigurationMaxOutputs;
    return true;
}

static void addActiveModeFields(
    const gxos::display::DisplayConfigurationResponse& response,
    DisplaySnapshotV1& snapshot)
{
    const gxos::display::DisplayConfigurationSnapshot& active = response.activeConfiguration;
    const uint32_t maxDimension = gxos::display::kDisplayConfigurationPersistenceMaxDimension;
    if (active.virtualDesktopWidth > 0 && active.virtualDesktopHeight > 0 &&
        static_cast<uint32_t>(active.virtualDesktopWidth) <= maxDimension &&
        static_cast<uint32_t>(active.virtualDesktopHeight) <= maxDimension) {
        snapshot.logicalDesktopWidth = static_cast<uint32_t>(active.virtualDesktopWidth);
        snapshot.logicalDesktopHeight = static_cast<uint32_t>(active.virtualDesktopHeight);
        snapshot.flags |= kDisplayLogicalDesktopWidthAvailable |
            kDisplayLogicalDesktopHeightAvailable;
    }

    if (active.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror) ||
        active.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend)) {
        snapshot.activeLayoutMode = active.mode;
        snapshot.flags |= kDisplayActiveLayoutModeAvailable;
    }

    for (uint32_t index = 0u; index < active.outputCount; ++index) {
        const gxos::display::DisplayConfigurationOutput& output = active.outputs[index];
        if (output.enabled == 0u || output.primary == 0u) continue;
        if (copyFixedText(snapshot.primaryModeId, sizeof(snapshot.primaryModeId),
                          output.modeId, sizeof(output.modeId))) {
            snapshot.flags |= kDisplayPrimaryModeIdAvailable;
        }
        break;
    }

    if (snapshot.flags & (kDisplayLogicalDesktopWidthAvailable |
                          kDisplayActiveLayoutModeAvailable |
                          kDisplayPrimaryModeIdAvailable)) {
        snapshot.status = DisplayInformationStatus::Available;
    }
}

SnapshotStatus buildDisplaySnapshot(bool framebufferAvailable,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t pitch,
                                    uint32_t bitsPerPixel,
                                    uint32_t pixelFormat,
                                    uint64_t revision,
                                    DisplaySnapshotV1* output)
{
    if (!output) return SnapshotStatus::InvalidArgument;
    if (revision == 0u) return SnapshotStatus::InvalidArgument;

    DisplaySnapshotV1 snapshot{};
    snapshot.structureSize = sizeof(DisplaySnapshotV1);
    snapshot.version = kDisplaySnapshotVersion;
    snapshot.revision = revision;
    snapshot.status = DisplayInformationStatus::Unavailable;

    if (!framebufferAvailable || width == 0u || height == 0u) {
        *output = snapshot;
        return SnapshotStatus::Ok;
    }

    snapshot.status = DisplayInformationStatus::Available;
    snapshot.flags = kDisplayFramebufferWidthAvailable |
        kDisplayFramebufferHeightAvailable | kDisplayPixelFormatAvailable;
    snapshot.framebufferWidth = width;
    snapshot.framebufferHeight = height;
    snapshot.pixelFormat = normalizePixelFormat(pixelFormat);

    if (validBitsPerPixel(bitsPerPixel)) {
        snapshot.bitsPerPixel = bitsPerPixel;
        snapshot.flags |= kDisplayBitsPerPixelAvailable;
        const uint64_t minimumPitch =
            (static_cast<uint64_t>(width) * bitsPerPixel + 7u) / 8u;
        if (pitch != 0u && static_cast<uint64_t>(pitch) >= minimumPitch) {
            snapshot.bytesPerScanline = pitch;
            snapshot.flags |= kDisplayPitchAvailable;
        }
    }

    if (!validDisplaySnapshot(snapshot)) return SnapshotStatus::Unavailable;
    *output = snapshot;
    return SnapshotStatus::Ok;
}

bool validDisplaySnapshot(const DisplaySnapshotV1& snapshot)
{
    const uint32_t knownFlags = kDisplayFramebufferWidthAvailable |
        kDisplayFramebufferHeightAvailable | kDisplayPixelFormatAvailable |
        kDisplayBitsPerPixelAvailable | kDisplayPitchAvailable |
        kDisplayLogicalDesktopWidthAvailable | kDisplayLogicalDesktopHeightAvailable |
        kDisplayActiveLayoutModeAvailable | kDisplayPrimaryModeIdAvailable;
    const uint32_t requiredFlags = kDisplayFramebufferWidthAvailable |
        kDisplayFramebufferHeightAvailable | kDisplayPixelFormatAvailable;
    if (snapshot.structureSize != sizeof(DisplaySnapshotV1) ||
        snapshot.version != kDisplaySnapshotVersion || snapshot.revision == 0u ||
        (snapshot.flags & ~knownFlags) != 0u) {
        return false;
    }

    const bool hasFramebufferWidth =
        (snapshot.flags & kDisplayFramebufferWidthAvailable) != 0u;
    const bool hasFramebufferHeight =
        (snapshot.flags & kDisplayFramebufferHeightAvailable) != 0u;
    const bool hasPixelFormat = (snapshot.flags & kDisplayPixelFormatAvailable) != 0u;
    if (hasFramebufferWidth != hasFramebufferHeight ||
        hasFramebufferWidth != hasPixelFormat) return false;
    if (hasFramebufferWidth) {
        if ((snapshot.flags & requiredFlags) != requiredFlags ||
            snapshot.framebufferWidth == 0u || snapshot.framebufferHeight == 0u ||
            snapshot.pixelFormat > static_cast<uint32_t>(framebuffer::PixelFormat::B8G8R8A8)) {
            return false;
        }
    } else if (snapshot.framebufferWidth != 0u || snapshot.framebufferHeight != 0u ||
               snapshot.pixelFormat != 0u) {
        return false;
    }

    const bool hasLogicalWidth =
        (snapshot.flags & kDisplayLogicalDesktopWidthAvailable) != 0u;
    const bool hasLogicalHeight =
        (snapshot.flags & kDisplayLogicalDesktopHeightAvailable) != 0u;
    if (hasLogicalWidth != hasLogicalHeight ||
        hasLogicalWidth != (snapshot.logicalDesktopWidth != 0u) ||
        hasLogicalHeight != (snapshot.logicalDesktopHeight != 0u)) return false;

    const bool hasLayoutMode =
        (snapshot.flags & kDisplayActiveLayoutModeAvailable) != 0u;
    if (hasLayoutMode != (snapshot.activeLayoutMode != 0u) ||
        (hasLayoutMode && snapshot.activeLayoutMode !=
            static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror) &&
         snapshot.activeLayoutMode !=
            static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend))) {
        return false;
    }
    const bool hasPrimaryModeId =
        (snapshot.flags & kDisplayPrimaryModeIdAvailable) != 0u;
    if (!validOptionalText(snapshot.primaryModeId, hasPrimaryModeId)) return false;

    if (snapshot.status == DisplayInformationStatus::Unavailable) {
        return snapshot.flags == 0u && snapshot.bitsPerPixel == 0u &&
            snapshot.bytesPerScanline == 0u && snapshot.logicalDesktopWidth == 0u &&
            snapshot.logicalDesktopHeight == 0u && snapshot.activeLayoutMode == 0u;
    }
    if (snapshot.status != DisplayInformationStatus::Available || snapshot.flags == 0u) {
        return false;
    }

    const bool hasBitsPerPixel = (snapshot.flags & kDisplayBitsPerPixelAvailable) != 0u;
    if (hasBitsPerPixel != (snapshot.bitsPerPixel != 0u) ||
        (hasBitsPerPixel && !validBitsPerPixel(snapshot.bitsPerPixel))) {
        return false;
    }
    const bool hasPitch = (snapshot.flags & kDisplayPitchAvailable) != 0u;
    if (hasPitch != (snapshot.bytesPerScanline != 0u)) return false;
    if (hasPitch) {
        if (!hasBitsPerPixel || !hasFramebufferWidth) return false;
        const uint64_t minimumPitch =
            (static_cast<uint64_t>(snapshot.framebufferWidth) * snapshot.bitsPerPixel + 7u) / 8u;
        if (static_cast<uint64_t>(snapshot.bytesPerScanline) < minimumPitch) return false;
    }
    return true;
}

SnapshotStatus readDisplaySnapshot(
    const app::KernelApp* caller,
    const DisplaySnapshotRequestV1* request,
    void* output,
    size_t capacity,
    size_t* outSize,
    uint64_t* outRevision)
{
    if (outSize) *outSize = 0u;
    if (outRevision) *outRevision = 0u;
    if (!caller || !request || !output || !outSize || !outRevision) {
        return SnapshotStatus::InvalidArgument;
    }
    if (request->structureSize < sizeof(DisplaySnapshotRequestV1)) {
        return SnapshotStatus::InvalidArgument;
    }
    if (request->version != kDisplaySnapshotVersion) {
        return SnapshotStatus::UnsupportedVersion;
    }
    if (!textEquals(caller->getRuntimeAppId(), kSettingsRuntimeAppId)) {
        return SnapshotStatus::Unauthorized;
    }

    *outSize = sizeof(DisplaySnapshotV1);
    if (capacity < sizeof(DisplaySnapshotV1)) return SnapshotStatus::BufferTooSmall;

    gxos::display::DisplayConfigurationResponse activeConfiguration{};
    bool topologyAvailable = false;
    if (!queryActiveDisplayConfiguration(activeConfiguration, topologyAvailable)) {
        return SnapshotStatus::Unavailable;
    }
    const uint64_t revision =
        static_cast<uint64_t>(activeConfiguration.activeConfigurationGeneration) + 1u;
    DisplaySnapshotV1 snapshot{};
    const SnapshotStatus status = buildDisplaySnapshot(
        framebuffer::is_available(),
        framebuffer::get_width(),
        framebuffer::get_height(),
        framebuffer::get_pitch(),
        framebuffer::get_bpp(),
        static_cast<uint32_t>(framebuffer::get_pixel_format()),
        revision,
        &snapshot);
    if (status != SnapshotStatus::Ok) return status;

    if (topologyAvailable) {
        addActiveModeFields(activeConfiguration, snapshot);
    }
    if (!validDisplaySnapshot(snapshot)) return SnapshotStatus::Unavailable;
    *outRevision = snapshot.revision;
    if (request->knownRevision != 0u && request->knownRevision != snapshot.revision) {
        return SnapshotStatus::StaleRevision;
    }

    uint8_t* destination = static_cast<uint8_t*>(output);
    const uint8_t* source = reinterpret_cast<const uint8_t*>(&snapshot);
    for (size_t index = 0u; index < sizeof(snapshot); ++index) {
        destination[index] = source[index];
    }
    return SnapshotStatus::Ok;
}

} // namespace settings_native
} // namespace kernel
