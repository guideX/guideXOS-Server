// VirtIO GPU Driver
//
// Diagnostic-only probe path for QEMU virtio-gpu discovery and
// single-scanout 2D test-pattern rendering.
//
// Safety boundaries:
// - PCI discovery and VirtIO config/queue access only behind the QEMU gate
// - QEMU-only 2D resource create, attach, scanout, transfer, and flush
// - No cursor queue setup
// - No physical Intel GPU support
// - No real hardware GPU BAR access
// - No 3D, virgl, Venus, blob, or unrestricted production compositor integration
// - No display hotplug
// - QEMU-only compositor desktop rendering is single-shot unless the explicit
//   bounded live QEMU proof gate is selected
// - No unbounded busy rendering loops or unlimited queue polling
// - No real hardware GPU/MMIO enablement
// REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/virtio_gpu.h"

#include "include/kernel/mmio.h"
#include "include/kernel/msi.h"
#include "include/kernel/pit.h"
#include "include/kernel/desktop.h"
#include "include/kernel/qemu_display_input_proof.h"
#include "include/kernel/system_font.h"
#include "include/kernel/serial_debug.h"
#include "../../virtio_gpu_display_backend.h"
#include "../../pixel_surface.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#define MEMORY_BARRIER() _ReadWriteBarrier()
#include <intrin.h>
#else
#define GXOS_MSVC_STUB 0
#define MEMORY_BARRIER() asm volatile ("" ::: "memory")
#endif

namespace kernel {
namespace virtio {
namespace gpu {

using namespace gxos::gui;
using namespace gxos::display;

namespace {

static const uint16_t kVirtioPciVendorId = PCI_VENDOR_ID;
static const uint16_t kVirtioGpuPciDeviceId = static_cast<uint16_t>(PCI_DEVICE_BASE_MODERN + DEVICE_GPU);
static const uint8_t kPciCapabilityVendorSpecific = 0x09;
static const uint8_t kPciCommandOffset = 0x04;
static const uint8_t kPciStatusOffset = 0x06;
static const uint8_t kPciRevisionOffset = 0x08;
static const uint8_t kPciHeaderTypeOffset = 0x0E;
static const uint8_t kPciBar0Offset = 0x10;
static const uint8_t kPciSubsystemVendorOffset = 0x2C;
static const uint8_t kPciSubsystemDeviceOffset = 0x2E;

static const uint32_t kProbeBusLimit = 8;
static const uint32_t kProbeDeviceLimit = 32;
static const uint32_t kProbeFunctionLimit = 8;
static const uint16_t kMaxControlQueueSize = 128;
static const uint16_t kMinControlQueueSize = 2;
static const uint32_t kStatusPollLimit = 100000;
static const uint32_t kResponseSpinLimit = 1000000;
static const uint32_t kDiagnosticBytesPerPixel = 4;
static const uint32_t kDiagnosticQemuMaxOutputsIntent = 2;
static const uint32_t kDiagnosticResourceId = 0x47584F53u; // "GXOS"
static const uint32_t kDiagnosticResourceIdSecondary = kDiagnosticResourceId + 1u;
static const size_t kDiagnosticBackingBytes = 16u * 1024u * 1024u;
static const uint32_t kDiagnosticBackingMaxMemEntries = 64u;
static const uint64_t kDiagnosticPageSizeBytes = 4096u;
static const uint32_t kQemuLogicalModeMinWidth = 800u;
static const uint32_t kQemuLogicalModeMinHeight = 600u;
static const uint32_t kQemuLogicalModeMaxWidth = 1280u;
static const uint32_t kQemuLogicalModeMaxHeight = 800u;
static const uint64_t kQemuLogicalModePerOutputBackingLimit = 16u * 1024u * 1024u;
static const uint64_t kQemuLogicalModeTotalBackingLimit = 32u * 1024u * 1024u;
static const uint32_t kFirstReplacementResourceId = 0x47584F90u;
static const uint16_t kInvalidDescriptorIndex = 0xFFFFu;
// The live path intentionally starts at a conservative 10 FPS cap.  PIT is
// configured at 100 Hz by the normal kernel boot path, so this interval is a
// hard minimum in scheduler ticks rather than a best-effort target.
static const uint32_t kLivePresentationFrameCap = 10u;
static const uint64_t kLivePresentationIntervalTicks = 10u;
static const uint32_t kLivePresentationBoundedAttemptLimit = 60u;
static const uint64_t kLivePresentationBoundedTimeLimitTicks = 800u;
static const uint32_t kLivePresentationOverlayPeriodAttempts = 2u;
static const uint32_t kLivePresentationFallbackFailureThreshold = 2u;
// The observer is intentionally capped at ten polls/second on the existing
// 100 Hz PIT path. It never installs a device-config interrupt in this pass.
static const uint64_t kDisplayEventPollIntervalTicks = 10u;
static const uint32_t kDisplayEventConfigReadRetries = 3u;
static const uint32_t kDisplayEventRescanRetryLimit = 3u;
static const uint64_t kDisplayEventFailedRescanBackoffTicks = 50u;
static const uint32_t kVirtioGpuKnownEventMask = VIRTIO_GPU_EVENT_DISPLAY;
// VIRTIO_F_VERSION_1 is required for the modern split-queue control path.
static const uint64_t kCommonCfgRequiredFeatureBits = FEATURE_VERSION_1;

struct PciCapability {
    uint8_t capId;
    uint8_t nextPtr;
    uint8_t capLen;
    uint8_t cfgType;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

struct PciRegion {
    bool found;
    bool present;
    bool mapped;
    uint8_t bar;
    uint64_t base;
    uint64_t mappedVirtual;
    uint32_t offset;
    uint32_t length;
    uint32_t rawBar;
};

enum class DisplayInfoOutcome : uint8_t {
    NotQueried = 0,
    Ok = 1,
    Failed = 2,
};

struct ModernTransport {
    bool present;
    bool modern;
    bool probeComplete;
    bool mmioMapped;
    bool mmioSanityReadsOk;
    bool featuresOk;
    bool controlQueueReady;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t revision;
    uint8_t headerType;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t progIf;
    uint16_t command;
    uint16_t status;
    uint16_t subsystemVendorId;
    uint16_t subsystemDeviceId;
    uint16_t vendorId;
    uint16_t deviceId;
    uint32_t deviceFeaturesLow;
    uint32_t deviceFeaturesHigh;
    uint64_t negotiatedFeatures;
    uint16_t queueSize;
    uint16_t queueNotifyOff;
    uint32_t notifyOffMultiplier;
    uint64_t mmioMappedVirtual;
    uint64_t mmioMappedPageCount;
    uint16_t mmioNumQueues;
    uint8_t mmioDeviceStatus;
    uint8_t mmioConfigGeneration;
    uint32_t mmioDeviceScanouts;
    uint32_t mmioDeviceCapsets;
    uint32_t displayInfoSlots;
    uint32_t enabledScanouts;
    uint32_t disabledScanouts;
    const char* mmioCacheMode;
    const char* mmioStopReason;
    PciRegion commonCfg;
    PciRegion notifyCfg;
    PciRegion isrCfg;
    PciRegion deviceCfg;
    PciRegion pciCfg;
    Virtqueue controlQueue;
};

struct MmioTransportProbeState {
    bool mmioMapped;
    bool sanityReadsOk;
    uint64_t mappedVirtual;
    uint64_t pageCount;
    uint16_t numQueues;
    uint8_t deviceStatus;
    uint8_t configGeneration;
    uint32_t deviceScanouts;
    uint32_t deviceCapsets;
    const char* cacheMode;
    const char* stopReason;
};

struct DeviceState {
    GpuDevice device;
    ModernTransport transport;
};

struct ProbeOutcome {
    bool valid;
    uint32_t candidateCount;
    bool initialized;
    bool featuresOk;
    bool controlQueueReady;
    DisplayInfoOutcome displayInfoOutcome;
    uint32_t scanoutSlots;
    uint32_t enabledScanoutCount;
    uint32_t disabledScanoutCount;
    uint32_t deviceConfigNumScanouts;
    uint32_t qemuMaxOutputsIntent;
    uint32_t enabledScanoutsAfter;
    bool resource2dReady;
    bool backingAttached;
    bool scanout0Set;
    bool transferOk;
    bool flushOk;
    bool resource2dReadySecondary;
    bool backingAttachedSecondary;
    bool scanout1Set;
    bool transfer1Ok;
    bool flush1Ok;
    bool distinctPatternsConfirmed;
    bool renderingTestPattern;
    const char* contentMode;
    const char* frameMode;
    bool continuousPresentationEnabled;
    const char* reason;
    const DeviceState* state;
    VirtioGpuOutputInventory outputInventory;
};

struct VirtioGpuProtocolDisplaySnapshot {
    DisplayInfo slots[MAX_SCANOUTS]{};
    uint32_t protocolSlotCount{0u};
    uint32_t enabledCount{0u};
    uint32_t disabledCount{0u};
    uint32_t deviceNumScanouts{0u};
    uint32_t deviceNumCapsets{0u};
    uint8_t responseValid{0u};
};

struct VirtioGpuDisplayEventObserver {
    bool initialized{false};
    bool enabled{false};
    uint64_t polls{0u};
    uint64_t coherentReads{0u};
    uint64_t incoherentReads{0u};
    uint64_t eventsObserved{0u};
    uint64_t displayEventsObserved{0u};
    uint64_t unknownEventBitsObserved{0u};
    uint64_t displayEventsProcessed{0u};
    uint64_t eventClearWrites{0u};
    uint64_t rescansSubmitted{0u};
    uint64_t rescansCoalesced{0u};
    uint64_t rescansSuccessful{0u};
    uint64_t rescansFailed{0u};
    uint64_t reassertions{0u};
    uint32_t lastEventsRead{0u};
    uint32_t lastEventsCleared{0u};
    uint8_t lastConfigGeneration{0u};
    uint64_t lastPollTick{0u};
    uint64_t nextPollTick{0u};
    uint64_t nextRetryTick{0u};
    uint32_t pollInterval{static_cast<uint32_t>(kDisplayEventPollIntervalTicks)};
    uint32_t pendingRescanRetries{0u};
    uint32_t topologyGeneration{0u};
    bool rescanInProgress{false};
    bool rescanPending{false};
    bool pendingTopologyChange{false};
    bool lastReasserted{false};
    bool injectionInProgress{false};
    char lastError[128]{"none"};
    char disabledReason[128]{"none"};
    gxos::display::VirtioGpuDetectedTopologySnapshot previousTopology{};
    gxos::display::VirtioGpuDetectedTopologySnapshot detectedTopology{};
    gxos::display::VirtioGpuDisplayTopologyChange pendingChange{};
};

struct DiagnosticPatternPalette {
    const char* name;
    uint32_t topLeft;
    uint32_t topRight;
    uint32_t bottomLeft;
    uint32_t bottomRight;
    uint32_t borderDark;
    uint32_t borderLight;
    uint32_t center;
};

struct DiagnosticBackingLayoutAudit {
    uint64_t backingVirtualBase;
    uint64_t totalBackingBytes;
    uint32_t totalPages;
    uint32_t totalMemEntries;
    uint32_t contiguousRunCount;
    uint64_t coveredBytes;
    bool physicalCoverageValid;
    uint64_t firstPhysicalStart;
    uint64_t firstPhysicalEnd;
    uint64_t lastPhysicalStart;
    uint64_t lastPhysicalEnd;
};

struct DiagnosticResourceState {
    const char* patternName;
    uint32_t resourceId;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t backingVirtual;
    uint64_t backingPhysical;
    uint64_t backingBytes;
    uint64_t backingPageCount;
    uint32_t memEntryCount;
    uint32_t contiguousRunCount;
    uint64_t coveredBytes;
    bool physicalCoverageValid;
    uint64_t patternChecksum;
    bool checksumValid;
    bool mirroredToFramebuffer;
    bool created;
    bool backingAttached;
    bool scanoutSet;
    bool transferOk;
    bool flushOk;
};

struct LivePresentationTargetDescriptor {
    uint32_t targetIndex{0};
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    int viewportOriginX{0};
    int viewportOriginY{0};
    int width{0};
    int height{0};
    bool primary{false};
};

// Persistent QEMU-only presentation state.  Transport/resource readiness is
// copied into this record after the one-shot proof, while compositor state
// remains owned by the normal desktop invalidation path.
struct LivePresentationBackendState {
    bool initialized{false};
    bool enabled{false};
    bool stopped{false};
    uint64_t frameSequence{0};
    uint64_t lastPresentedFrame{0};
    uint64_t lastDirtyGeneration{0};
    uint32_t framesAttempted{0};
    uint32_t eligibleAttempts{0};
    uint32_t boundedProofIterations{0};
    uint32_t presentationPolls{0};
    uint32_t dirtyFrames{0};
    uint32_t framesRendered{0};
    uint32_t framesSkippedClean{0};
    uint32_t rateLimitSkips{0};
    uint32_t target0TransferCount{0};
    uint32_t target0FlushCount{0};
    uint32_t target1TransferCount{0};
    uint32_t target1FlushCount{0};
    uint32_t target0Failures{0};
    uint32_t target1Failures{0};
    uint32_t target0FailureStreak{0};
    uint32_t target1FailureStreak{0};
    uint64_t presentationStartTicks{0};
    uint64_t lastPresentationTicks{0};
    uint32_t configuredFrameCap{kLivePresentationFrameCap};
    uint32_t boundedRunLimit{kLivePresentationBoundedAttemptLimit};
    uint64_t boundedTimeLimitTicks{kLivePresentationBoundedTimeLimitTicks};
    const char* fallbackMode{"static-patterns-retained"};
    const char* stoppedReason{"not-started"};
    bool fallbackActivated{false};
    bool fallbackTargetValid{false};
    uint32_t fallbackTarget{0};
    const char* fallbackReason{"none"};
    const char* fallbackResult{"not-used"};
    bool initialFrameReadyLogged{false};
    uint64_t initialTarget0Checksum{0};
    uint64_t initialTarget1Checksum{0};
    uint64_t finalTarget0Checksum{0};
    uint64_t finalTarget1Checksum{0};
    DeviceState* device{nullptr};
    DiagnosticResourceState resource0{};
    DiagnosticResourceState resource1{};
    LivePresentationTargetDescriptor target0{};
    LivePresentationTargetDescriptor target1{};
    uint8_t* backing0{nullptr};
    uint8_t* backing1{nullptr};
    uint64_t backingPhysical0{0};
    uint64_t backingPhysical1{0};
    uint64_t totalBackingBytes{0};
    uint64_t backingPageCount{0};
    uint32_t selectedWidth{0};
    uint32_t selectedHeight{0};
    uint32_t activeOutputCount{0};
    uint32_t virtualDesktopWidth{0};
    uint32_t virtualDesktopHeight{0};
    uint32_t bytesPerPixel{0};
    GpuFormat resourceFormat{FORMAT_B8G8R8X8_UNORM};
};

static bool s_initialized = false;
static DeviceState s_devices[4];
static int s_deviceCount = 0;
static ProbeOutcome s_probeOutcome{};
static LivePresentationBackendState s_livePresentation{};
static bool s_liveCommandLoggingSuppressed = false;
static bool s_displayConfigurationPresentationPaused = false;
static uint32_t s_nextReplacementResourceId = kFirstReplacementResourceId;
static uint32_t s_resourcesCreated = 2u;
static uint32_t s_resourcesCommitted = 2u;
static uint32_t s_resourcesRolledBack = 0u;
static uint32_t s_resourcesUnreferenced = 0u;
static uint32_t s_activeBackingAllocations = 2u;
static uint32_t s_cleanupFailures = 0u;
static uint32_t s_displayConfigurationMode = static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend);
static uint32_t s_displayConfigurationPrimaryOutput = 0u;
static gxos::display::DisplayConfigurationSnapshot s_detectedConfigurationSnapshot{};
static bool s_detectedConfigurationSnapshotReady = false;
static VirtioGpuDisplayEventObserver s_displayEventObserver{};
static uint64_t s_kernelPhysicalBase = 0x100000ULL;

#if defined(_MSC_VER)
__declspec(align(4096)) static uint8_t s_queueStorage[16384];
__declspec(align(4096)) static uint8_t s_commandBuffer[4096];
__declspec(align(4096)) static uint8_t s_responseBuffer[sizeof(RespDisplayInfo)];
__declspec(align(4096)) static uint8_t s_diagnosticBackingStorage0[kDiagnosticBackingBytes];
__declspec(align(4096)) static uint8_t s_diagnosticBackingStorage1[kDiagnosticBackingBytes];
__declspec(align(4096)) static uint8_t s_rebuildBackingStorage0[kDiagnosticBackingBytes];
__declspec(align(4096)) static uint8_t s_rebuildBackingStorage1[kDiagnosticBackingBytes];
#else
static uint8_t s_queueStorage[16384] __attribute__((aligned(4096)));
static uint8_t s_commandBuffer[4096] __attribute__((aligned(4096)));
static uint8_t s_responseBuffer[sizeof(RespDisplayInfo)] __attribute__((aligned(4096)));
static uint8_t s_diagnosticBackingStorage0[kDiagnosticBackingBytes] __attribute__((aligned(4096)));
static uint8_t s_diagnosticBackingStorage1[kDiagnosticBackingBytes] __attribute__((aligned(4096)));
// One bounded replacement slot per operational output. Old backing remains
// in the diagnostic slot until commit; the replacement slot is never exposed
// globally before its resource, backing, validation frame, and scanout bind
// have all succeeded.
static uint8_t s_rebuildBackingStorage0[kDiagnosticBackingBytes] __attribute__((aligned(4096)));
static uint8_t s_rebuildBackingStorage1[kDiagnosticBackingBytes] __attribute__((aligned(4096)));
#endif

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

struct VirtioGpuOutputRebuildPlan {
    uint32_t outputIdentity{0};
    uint32_t scanoutId{0};
    uint32_t oldResourceId{0};
    uint32_t newResourceId{0};
    uint32_t oldWidth{0};
    uint32_t oldHeight{0};
    uint32_t newWidth{0};
    uint32_t newHeight{0};
    uint64_t newBackingBytes{0};
    uint64_t newBackingPages{0};
    uint32_t newBackingMemEntries{0};
    int targetOriginX{0};
    int targetOriginY{0};
    bool prepared{false};
    bool attached{false};
    bool scanoutBound{false};
    bool scanoutUnbound{false};
    bool validationPresented{false};
    bool committed{false};
};

static bool checked_qemu_logical_backing_bytes(uint32_t width, uint32_t height, uint64_t& bytesOut)
{
    bytesOut = 0u;
    if (width < kQemuLogicalModeMinWidth || height < kQemuLogicalModeMinHeight ||
        width > kQemuLogicalModeMaxWidth || height > kQemuLogicalModeMaxHeight) return false;
    const uint64_t w = width;
    const uint64_t h = height;
    if (w > 0xFFFFFFFFFFFFFFFFULL / h) return false;
    const uint64_t pixels = w * h;
    if (pixels > 0xFFFFFFFFFFFFFFFFULL / kDiagnosticBytesPerPixel) return false;
    const uint64_t bytes = pixels * kDiagnosticBytesPerPixel;
    if (bytes == 0u || bytes > kQemuLogicalModePerOutputBackingLimit || bytes > kDiagnosticBackingBytes) return false;
    bytesOut = bytes;
    return true;
}

static const char* qemu_logical_mode_id(uint32_t width, uint32_t height)
{
    if (width == 1280u && height == 800u) return "qemu-1280x800";
    if (width == 1024u && height == 768u) return "qemu-1024x768";
    if (width == 800u && height == 600u) return "qemu-800x600";
    return nullptr;
}

static bool resource_id_in_use(uint32_t id, const DiagnosticResourceState& resource0,
                               const DiagnosticResourceState& resource1, uint32_t pending0,
                               uint32_t pending1)
{
    return id == 0u || id == kDiagnosticResourceId || id == kDiagnosticResourceIdSecondary ||
        id == resource0.resourceId || id == resource1.resourceId || id == pending0 || id == pending1;
}

static uint32_t allocate_replacement_resource_id(const DiagnosticResourceState& resource0,
                                                 const DiagnosticResourceState& resource1,
                                                 uint32_t pending0, uint32_t pending1)
{
    // Bounded probe: the replacement namespace is intentionally finite and
    // never wraps into currently visible or diagnostic resource IDs.
    for (uint32_t attempt = 0u; attempt < 1024u; ++attempt) {
        uint32_t candidate = s_nextReplacementResourceId++;
        if (candidate == 0u) candidate = s_nextReplacementResourceId++;
        if (!resource_id_in_use(candidate, resource0, resource1, pending0, pending1)) return candidate;
    }
    return 0u;
}

static void serial_put_u32_decimal(uint32_t value)
{
    char buffer[11];
    int index = 10;
    buffer[index] = '\0';

    do {
        buffer[--index] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && index > 0);

    kernel::serial::puts(&buffer[index]);
}

static void serial_put_u64_decimal(uint64_t value)
{
    char buffer[21];
    int index = 20;
    buffer[index] = '\0';

    do {
        buffer[--index] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && index > 0);

    kernel::serial::puts(&buffer[index]);
}

static void write_u64_decimal(char* buffer, size_t bufferSize, uint64_t value)
{
    if (buffer == nullptr || bufferSize < 2u) {
        return;
    }

    size_t index = bufferSize - 1u;
    buffer[index] = '\0';
    do {
        if (index == 0u) {
            break;
        }
        buffer[--index] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);

    if (index != 0u) {
        size_t out = 0u;
        while (index < bufferSize) {
            buffer[out++] = buffer[index++];
        }
    }
}

static bool text_contains(const char* text, const char* needle)
{
    if (text == nullptr || needle == nullptr || needle[0] == '\0') {
        return false;
    }
    for (const char* start = text; *start != '\0'; ++start) {
        const char* a = start;
        const char* b = needle;
        while (*a != '\0' && *b != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t dma_address(const void* ptr)
{
    const uint64_t virt = reinterpret_cast<uint64_t>(ptr);
    if (virt >= 0x100000ULL) {
        return s_kernelPhysicalBase + (virt - 0x100000ULL);
    }

    return virt;
}

static bool add_u64_overflow(uint64_t a, uint64_t b, uint64_t* out)
{
    if (out == nullptr) {
        return true;
    }

    if (b > (~0ULL - a)) {
        *out = 0;
        return true;
    }

    *out = a + b;
    return false;
}

static bool mul_u64_overflow(uint64_t a, uint64_t b, uint64_t* out)
{
    if (out == nullptr) {
        return true;
    }

    if (a != 0u && b > (~0ULL / a)) {
        *out = 0;
        return true;
    }

    *out = a * b;
    return false;
}

static const char* gpu_format_name(GpuFormat format)
{
    switch (format) {
    case FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case FORMAT_B8G8R8X8_UNORM:
        return "B8G8R8X8_UNORM";
    case FORMAT_A8R8G8B8_UNORM:
        return "A8R8G8B8_UNORM";
    case FORMAT_X8R8G8B8_UNORM:
        return "X8R8G8B8_UNORM";
    case FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case FORMAT_X8B8G8R8_UNORM:
        return "X8B8G8R8_UNORM";
    case FORMAT_A8B8G8R8_UNORM:
        return "A8B8G8R8_UNORM";
    case FORMAT_R8G8B8X8_UNORM:
        return "R8G8B8X8_UNORM";
    default:
        return "unknown";
    }
}

static bool gpu_format_bytes_per_pixel(GpuFormat format, uint32_t* bytesPerPixelOut)
{
    if (bytesPerPixelOut == nullptr) {
        return false;
    }

    switch (format) {
    case FORMAT_B8G8R8A8_UNORM:
    case FORMAT_B8G8R8X8_UNORM:
    case FORMAT_A8R8G8B8_UNORM:
    case FORMAT_X8R8G8B8_UNORM:
    case FORMAT_R8G8B8A8_UNORM:
    case FORMAT_X8B8G8R8_UNORM:
    case FORMAT_A8B8G8R8_UNORM:
    case FORMAT_R8G8B8X8_UNORM:
        *bytesPerPixelOut = 4u;
        return true;
    default:
        *bytesPerPixelOut = 0u;
        return false;
    }
}

static uint32_t make_bgrx(uint8_t red, uint8_t green, uint8_t blue)
{
    return (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(green) << 8) |
           static_cast<uint32_t>(blue);
}

static const DiagnosticPatternPalette& diagnostic_pattern_palette(uint32_t scanoutId)
{
    static const DiagnosticPatternPalette kScanout0Palette = {
        "scanout0-blue-cyan",
        make_bgrx(0x20u, 0x58u, 0xE8u),
        make_bgrx(0x00u, 0xD8u, 0xF0u),
        make_bgrx(0x38u, 0x78u, 0xFFu),
        make_bgrx(0x90u, 0xF8u, 0xFFu),
        make_bgrx(0x10u, 0x18u, 0x48u),
        make_bgrx(0x78u, 0xD8u, 0xFFu),
        make_bgrx(0xFFu, 0xFFu, 0xFFu),
    };
    static const DiagnosticPatternPalette kScanout1Palette = {
        "scanout1-red-orange",
        make_bgrx(0xE0u, 0x40u, 0x18u),
        make_bgrx(0xFFu, 0x88u, 0x18u),
        make_bgrx(0xB0u, 0x18u, 0x10u),
        make_bgrx(0xFFu, 0xC0u, 0x48u),
        make_bgrx(0x58u, 0x10u, 0x00u),
        make_bgrx(0xFFu, 0xB0u, 0x60u),
        make_bgrx(0xFFu, 0xFFu, 0xFFu),
    };

    return scanoutId == 1u ? kScanout1Palette : kScanout0Palette;
}

static void fill_diagnostic_pattern(uint32_t* pixels,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t stridePixels,
                                    const DiagnosticPatternPalette& palette)
{
    if (pixels == nullptr || width == 0u || height == 0u || stridePixels < width) {
        return;
    }

    const uint32_t border = 8u;
    const uint32_t midX = width / 2u;
    const uint32_t midY = height / 2u;
    const uint32_t centerLeft = width / 4u;
    const uint32_t centerRight = width - centerLeft;
    const uint32_t centerTop = height / 4u;
    const uint32_t centerBottom = height - centerTop;

    for (uint32_t y = 0u; y < height; ++y) {
        uint32_t* row = pixels + (static_cast<size_t>(y) * stridePixels);
        for (uint32_t x = 0u; x < width; ++x) {
            uint32_t color = 0u;
            if (x < border || y < border || x >= (width - border) || y >= (height - border)) {
                const bool checker = (((x / 8u) + (y / 8u)) & 1u) == 0u;
                color = checker ? palette.borderDark : palette.borderLight;
            } else if (x >= centerLeft && x < centerRight && y >= centerTop && y < centerBottom) {
                color = palette.center;
            } else if (x < midX && y < midY) {
                color = palette.topLeft;
            } else if (x >= midX && y < midY) {
                color = palette.topRight;
            } else if (x < midX && y >= midY) {
                color = palette.bottomLeft;
            } else {
                color = palette.bottomRight;
            }

            row[x] = color;
        }
    }
}

static uint64_t checksum_diagnostic_pattern(const uint8_t* bytes, size_t byteCount)
{
    if (bytes == nullptr) {
        return 0u;
    }

    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= 1099511628211ULL;
    }

    return hash;
}

static bool build_diagnostic_backing_layout(uint8_t* backingBase,
                                            uint64_t totalBackingBytes,
                                            MemEntry* entriesOut,
                                            uint32_t entryCapacity,
                                            DiagnosticBackingLayoutAudit* auditOut)
{
    if (backingBase == nullptr || entriesOut == nullptr || auditOut == nullptr) {
        return false;
    }

    memzero(auditOut, sizeof(*auditOut));
    auditOut->backingVirtualBase = reinterpret_cast<uint64_t>(backingBase);
    auditOut->totalBackingBytes = totalBackingBytes;

    if (totalBackingBytes == 0u) {
        return false;
    }

    const uint64_t totalPages64 = align_up(totalBackingBytes, kDiagnosticPageSizeBytes) / kDiagnosticPageSizeBytes;
    if (totalPages64 == 0u || totalPages64 > static_cast<uint64_t>(~0u)) {
        return false;
    }

    uint32_t entryCount = 0u;
    uint32_t contiguousRunCount = 0u;
    uint64_t coveredBytes = 0u;
    bool physicalCoverageValid = true;
    bool runOpen = false;
    uint64_t runStartPhysical = 0u;
    uint64_t runEndPhysical = 0u;
    uint64_t runLength = 0u;
    uint64_t firstPhysicalStart = 0u;
    uint64_t firstPhysicalEnd = 0u;
    uint64_t lastPhysicalStart = 0u;
    uint64_t lastPhysicalEnd = 0u;

    for (uint64_t pageIndex = 0u; pageIndex < totalPages64; ++pageIndex) {
        const uint64_t pageOffset = pageIndex * kDiagnosticPageSizeBytes;
        if (pageOffset >= totalBackingBytes) {
            break;
        }

        uint64_t remainingBytes = totalBackingBytes - pageOffset;
        const uint64_t chunkBytes = remainingBytes < kDiagnosticPageSizeBytes ? remainingBytes : kDiagnosticPageSizeBytes;
        if (chunkBytes == 0u) {
            physicalCoverageValid = false;
            break;
        }

        uint8_t* pageVirtual = backingBase + static_cast<size_t>(pageOffset);
        const uint64_t physicalStart = dma_address(pageVirtual);
        if (physicalStart == 0u) {
            physicalCoverageValid = false;
            break;
        }

        uint64_t physicalEnd = 0u;
        if (add_u64_overflow(physicalStart, chunkBytes - 1u, &physicalEnd)) {
            physicalCoverageValid = false;
            break;
        }

        if (!runOpen) {
            runOpen = true;
            runStartPhysical = physicalStart;
            runEndPhysical = physicalEnd;
            runLength = chunkBytes;
            firstPhysicalStart = physicalStart;
            firstPhysicalEnd = physicalEnd;
        } else {
            uint64_t expectedPhysicalStart = 0u;
            if (!add_u64_overflow(runEndPhysical, 1u, &expectedPhysicalStart) && physicalStart == expectedPhysicalStart) {
                if (add_u64_overflow(runLength, chunkBytes, &runLength)) {
                    physicalCoverageValid = false;
                    break;
                }
                runEndPhysical = physicalEnd;
            } else {
                if (runLength == 0u) {
                    physicalCoverageValid = false;
                    break;
                }

                if (entryCount >= entryCapacity) {
                    physicalCoverageValid = false;
                    break;
                }

                entriesOut[entryCount].addr = runStartPhysical;
                entriesOut[entryCount].length = static_cast<uint32_t>(runLength);
                entriesOut[entryCount].padding = 0u;
                ++entryCount;
                ++contiguousRunCount;
                lastPhysicalStart = runStartPhysical;
                lastPhysicalEnd = runEndPhysical;

                runStartPhysical = physicalStart;
                runEndPhysical = physicalEnd;
                runLength = chunkBytes;
            }
        }

        if (add_u64_overflow(coveredBytes, chunkBytes, &coveredBytes)) {
            physicalCoverageValid = false;
            break;
        }
    }

    if (runOpen && physicalCoverageValid) {
        if (runLength == 0u || entryCount >= entryCapacity) {
            physicalCoverageValid = false;
        } else {
            entriesOut[entryCount].addr = runStartPhysical;
            entriesOut[entryCount].length = static_cast<uint32_t>(runLength);
            entriesOut[entryCount].padding = 0u;
            ++entryCount;
            ++contiguousRunCount;
            lastPhysicalStart = runStartPhysical;
            lastPhysicalEnd = runEndPhysical;
        }
    }

    auditOut->totalPages = static_cast<uint32_t>(totalPages64);
    auditOut->totalMemEntries = entryCount;
    auditOut->contiguousRunCount = contiguousRunCount;
    auditOut->coveredBytes = coveredBytes;
    auditOut->physicalCoverageValid = physicalCoverageValid && (coveredBytes == totalBackingBytes) && (entryCount > 0u);
    auditOut->firstPhysicalStart = firstPhysicalStart;
    auditOut->firstPhysicalEnd = firstPhysicalEnd;
    auditOut->lastPhysicalStart = lastPhysicalStart;
    auditOut->lastPhysicalEnd = lastPhysicalEnd;

    return auditOut->physicalCoverageValid;
}

static void log_diagnostic_backing_layout(const DiagnosticBackingLayoutAudit& audit)
{
    kernel::serial::puts("[VIRTIO-GPU] Diagnostic backing layout backingVirtualBase=0x");
    kernel::serial::put_hex64(audit.backingVirtualBase);
    kernel::serial::puts(" totalBackingBytes=");
    serial_put_u64_decimal(audit.totalBackingBytes);
    kernel::serial::puts(" totalPages=");
    serial_put_u32_decimal(audit.totalPages);
    kernel::serial::puts(" totalMemEntries=");
    serial_put_u32_decimal(audit.totalMemEntries);
    kernel::serial::puts(" contiguousRunCount=");
    serial_put_u32_decimal(audit.contiguousRunCount);
    kernel::serial::puts(" coveredBytes=");
    serial_put_u64_decimal(audit.coveredBytes);
    kernel::serial::puts(" physicalCoverageValid=");
    kernel::serial::puts(audit.physicalCoverageValid ? "yes" : "no");
    kernel::serial::puts(" firstPhysRange=0x");
    kernel::serial::put_hex64(audit.firstPhysicalStart);
    kernel::serial::puts("-0x");
    kernel::serial::put_hex64(audit.firstPhysicalEnd);
    kernel::serial::puts(" lastPhysRange=0x");
    kernel::serial::put_hex64(audit.lastPhysicalStart);
    kernel::serial::puts("-0x");
    kernel::serial::put_hex64(audit.lastPhysicalEnd);
    kernel::serial::putc('\n');
}

static VirtioGpuScanoutState make_scanout_state(
    uint32_t scanoutId,
    const DisplayInfo& preferred,
    const DiagnosticResourceState& resource,
    bool connectorEnabled,
    int assignedX,
    int assignedY,
    int assignedWidth,
    int assignedHeight,
    bool presentationConfirmed,
    const char* lastCommandStatus,
    bool primary)
{
    VirtioGpuScanoutState state;
    state.scanoutId = scanoutId;
    state.resourceId = resource.resourceId;
    state.connectorEnabled = connectorEnabled;
    state.preferredX = static_cast<int>(preferred.x);
    state.preferredY = static_cast<int>(preferred.y);
    state.preferredWidth = static_cast<int>(preferred.width);
    state.preferredHeight = static_cast<int>(preferred.height);
    state.assignedX = assignedX;
    state.assignedY = assignedY;
    state.assignedWidth = assignedWidth;
    state.assignedHeight = assignedHeight;
    state.resourceBound = resource.scanoutSet;
    state.backingAttached = resource.backingAttached;
    state.transferReady = resource.transferOk;
    state.presentReady = resource.flushOk;
    state.presentationConfirmed = presentationConfirmed;
    state.primary = primary;
    state.active = resource.scanoutSet && resource.backingAttached && resource.transferOk && resource.flushOk;
    state.backingVirtualAddress = resource.backingVirtual;
    state.backingByteCount = resource.backingBytes;
    state.backingMemEntryCount = resource.memEntryCount;
    state.patternChecksum = resource.patternChecksum;
    state.lastCommandStatus = lastCommandStatus != nullptr ? lastCommandStatus : "";
    return state;
}

static VirtioGpuOutputInventory build_output_inventory(
    const DisplayInfo& scanout0Preferred,
    const DisplayInfo& scanout1Initial,
    const DiagnosticResourceState& resource1,
    const DiagnosticResourceState& resource2,
    uint32_t deviceConfigNumScanouts,
    bool scanout0PresentationConfirmed,
    bool scanout1PresentationConfirmed,
    uint32_t selectedWidth,
    uint32_t selectedHeight)
{
    FixedList<VirtioGpuScanoutState, kVirtioGpuMaxOutputs> scanouts;
    if (resource1.resourceId != 0u) {
        scanouts.push_back(make_scanout_state(0u,
            scanout0Preferred,
            resource1,
            scanout0Preferred.enabled,
            0,
            0,
            static_cast<int>(selectedWidth),
            static_cast<int>(selectedHeight),
            scanout0PresentationConfirmed,
            resource1.flushOk ? "RESOURCE_FLUSH result=ok" : "RESOURCE_FLUSH blocked",
            true));
    }
    if (resource2.resourceId != 0u) {
        scanouts.push_back(make_scanout_state(1u,
            scanout1Initial,
            resource2,
            scanout1Initial.enabled,
            static_cast<int>(selectedWidth),
            0,
            static_cast<int>(selectedWidth),
            static_cast<int>(selectedHeight),
            scanout1PresentationConfirmed,
            resource2.flushOk ? "RESOURCE_FLUSH result=ok" : "RESOURCE_FLUSH blocked",
            false));
    }

    return VirtioGpuDisplayBackend::getVirtioGpuOutputInventory(scanouts, deviceConfigNumScanouts);
}

struct CompositorFrameTargetResult {
    uint32_t targetIndex{0};
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    bool renderOk{false};
    bool transferOk{false};
    bool flushOk{false};
    bool fallbackUsed{false};
    bool backingValid{false};
    bool conversionRequired{false};
    uint64_t renderedByteCount{0};
    uint64_t checksum{0};
    const char* blocker{nullptr};
};

static PixelFormatKind pixel_format_from_gpu_format(GpuFormat format)
{
    switch (format) {
    case FORMAT_B8G8R8X8_UNORM:
        return PixelFormatKind::B8G8R8X8_UNORM;
    case FORMAT_B8G8R8A8_UNORM:
        return PixelFormatKind::B8G8R8A8_UNORM;
    case FORMAT_X8R8G8B8_UNORM:
        return PixelFormatKind::X8R8G8B8_UNORM;
    case FORMAT_A8R8G8B8_UNORM:
        return PixelFormatKind::A8R8G8B8_UNORM;
    case FORMAT_R8G8B8A8_UNORM:
        return PixelFormatKind::R8G8B8A8_UNORM;
    case FORMAT_X8B8G8R8_UNORM:
        return PixelFormatKind::X8B8G8R8_UNORM;
    case FORMAT_A8B8G8R8_UNORM:
        return PixelFormatKind::A8B8G8R8_UNORM;
    case FORMAT_R8G8B8X8_UNORM:
        return PixelFormatKind::R8G8B8X8_UNORM;
    default:
        return PixelFormatKind::Unknown;
    }
}

static bool pixel_surface_is_direct_bgrx(const PixelSurface& surface)
{
    return pixelFormatIsBgrxLike(surface.pixelFormat);
}

static void surface_put_pixel(PixelSurface& surface, int localX, int localY, uint32_t color)
{
    if (surface.pixelPointer == nullptr || localX < 0 || localY < 0) {
        return;
    }
    if (localX >= static_cast<int>(surface.width) || localY >= static_cast<int>(surface.height)) {
        return;
    }

    const uint32_t stridePixels = surface.pitchBytes / 4u;
    uint32_t* const row = surface.pixelPointer + (static_cast<size_t>(localY) * static_cast<size_t>(stridePixels));
    row[static_cast<size_t>(localX)] = pixelSurfaceConvertBgrxToFormat(color, surface.pixelFormat);
}

static void surface_fill_rect_local(PixelSurface& surface, const PixelRect& localRect, uint32_t color)
{
    if (surface.pixelPointer == nullptr || !localRect.isValid()) {
        return;
    }

    const PixelRect clip = intersectPixelRect(localRect, surface.clipRect);
    if (!clip.isValid()) {
        return;
    }

    const uint32_t packed = pixelSurfaceConvertBgrxToFormat(color, surface.pixelFormat);
    const uint32_t stridePixels = surface.pitchBytes / 4u;
    for (int y = clip.top; y < clip.bottom; ++y) {
        uint32_t* const row = surface.pixelPointer + (static_cast<size_t>(y) * static_cast<size_t>(stridePixels));
        for (int x = clip.left; x < clip.right; ++x) {
            row[static_cast<size_t>(x)] = packed;
        }
    }
}

static void surface_fill_rect_global(PixelSurface& surface, int globalX, int globalY, int width, int height, uint32_t color)
{
    PixelRect local{};
    if (!pixelSurfaceLocalRectFromGlobal(surface, globalX, globalY, width, height, &local)) {
        return;
    }
    surface_fill_rect_local(surface, local, color);
}

static void surface_draw_rect_global(PixelSurface& surface, int globalX, int globalY, int width, int height, uint32_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    surface_fill_rect_global(surface, globalX, globalY, width, 1, color);
    surface_fill_rect_global(surface, globalX, globalY + height - 1, width, 1, color);
    surface_fill_rect_global(surface, globalX, globalY, 1, height, color);
    surface_fill_rect_global(surface, globalX + width - 1, globalY, 1, height, color);
}

static void surface_draw_text_global(PixelSurface& surface, int globalX, int globalY, const char* text, uint32_t color, FontRole role)
{
    if (text == nullptr || text[0] == '\0' || surface.pixelPointer == nullptr) {
        return;
    }

    SystemFont::EnsureInitialized();
    const int localX = globalX - surface.viewportOriginX;
    const int localY = globalY - surface.viewportOriginY;
    SystemFont::DrawTextToBuffer(surface.pixelPointer,
                                 static_cast<int>(surface.pitchBytes),
                                 static_cast<int>(surface.width),
                                 static_cast<int>(surface.height),
                                 localX,
                                 localY,
                                 text,
                                 -1,
                                 color,
                                 role);
}

static void fill_virtual_desktop_background(PixelSurface& surface, uint32_t desktopWidth, uint32_t desktopHeight)
{
    if (surface.pixelPointer == nullptr || surface.width == 0u || surface.height == 0u) {
        return;
    }

    const uint32_t leftRed = 0x26u;
    const uint32_t leftGreen = 0x40u;
    const uint32_t leftBlue = 0x56u;
    const uint32_t rightRed = 0x52u;
    const uint32_t rightGreen = 0x70u;
    const uint32_t rightBlue = 0x88u;
    const uint32_t denomX = desktopWidth > 1u ? desktopWidth - 1u : 1u;
    const uint32_t denomY = desktopHeight > 1u ? desktopHeight - 1u : 1u;
    const uint32_t stridePixels = surface.pitchBytes / 4u;

    for (uint32_t y = 0u; y < surface.height; ++y) {
        const uint32_t globalY = static_cast<uint32_t>(surface.viewportOriginY) + y;
        const uint32_t shade = (globalY * 18u) / denomY;
        uint32_t* const row = surface.pixelPointer + (static_cast<size_t>(y) * static_cast<size_t>(stridePixels));
        for (uint32_t x = 0u; x < surface.width; ++x) {
            const uint32_t globalX = static_cast<uint32_t>(surface.viewportOriginX) + x;
            const uint32_t blend = (globalX * 255u) / denomX;
            const uint32_t red = leftRed + ((rightRed - leftRed) * blend) / 255u;
            const uint32_t green = leftGreen + ((rightGreen - leftGreen) * blend) / 255u;
            const uint32_t blue = leftBlue + ((rightBlue - leftBlue) * blend) / 255u;
            const uint32_t shadedRed = red > shade ? red - shade : 0u;
            const uint32_t shadedGreen = green > shade ? green - shade : 0u;
            const uint32_t shadedBlue = blue > shade ? blue - shade : 0u;
            row[x] = pixelSurfaceConvertBgrxToFormat(pixelSurfacePackBgrx(static_cast<uint8_t>(shadedRed),
                                                                          static_cast<uint8_t>(shadedGreen),
                                                                          static_cast<uint8_t>(shadedBlue)),
                                                     surface.pixelFormat);
        }
    }
}

static void draw_desktop_windows(PixelSurface& surface, bool primaryTaskbarVisible)
{
    surface_fill_rect_global(surface, 96, 128, 540, 308, 0x002E3540);
    surface_fill_rect_global(surface, 96, 128, 540, 28, 0x004B77A4);
    surface_draw_rect_global(surface, 96, 128, 540, 308, 0x00D9E5F2);
    surface_draw_text_global(surface, 112, 135, "guideXOS desktop", 0x00FFFFFF, FontRole::Title);
    surface_fill_rect_global(surface, 124, 178, 128, 92, 0x00446D8A);
    surface_draw_rect_global(surface, 124, 178, 128, 92, 0x00D5E6F0);
    surface_fill_rect_global(surface, 270, 178, 220, 36, 0x003B4C59);
    surface_fill_rect_global(surface, 270, 220, 220, 36, 0x00475A68);
    surface_fill_rect_global(surface, 270, 262, 220, 36, 0x00526678);
    surface_draw_text_global(surface, 280, 186, "primary workspace", 0x00F0F6FC, FontRole::Small);

    surface_fill_rect_global(surface, 1032, 156, 632, 248, 0x002C4034);
    surface_fill_rect_global(surface, 1032, 156, 632, 28, 0x005A8A56);
    surface_draw_rect_global(surface, 1032, 156, 632, 248, 0x00D2E8D0);
    surface_draw_text_global(surface, 1048, 163, "spanning monitor bridge", 0x00F0FFF0, FontRole::Title);
    surface_fill_rect_global(surface, 1072, 206, 210, 74, 0x003D5A43);
    surface_fill_rect_global(surface, 1300, 206, 210, 74, 0x00456A4B);
    surface_fill_rect_global(surface, 1528, 206, 108, 74, 0x00548258);
    surface_draw_text_global(surface, 1078, 224, "clip proof", 0x00E0F2E0, FontRole::SmallBold);

    if (!primaryTaskbarVisible) {
        surface_fill_rect_global(surface, 1408, 64, 286, 182, 0x00383A2B);
        surface_fill_rect_global(surface, 1408, 64, 286, 28, 0x00A16C2C);
        surface_draw_rect_global(surface, 1408, 64, 286, 182, 0x00F7E0C4);
        surface_draw_text_global(surface, 1420, 70, "secondary output", 0x00FFF6E8, FontRole::Title);
        surface_fill_rect_global(surface, 1432, 110, 102, 58, 0x00546A34);
        surface_fill_rect_global(surface, 1548, 110, 120, 58, 0x00507A52);
        surface_draw_text_global(surface, 1438, 128, "no taskbar", 0x00FFF1D8, FontRole::SmallBold);
    }

    if (primaryTaskbarVisible) {
        const int taskbarTop = static_cast<int>(surface.height) - 40;
        const int taskbarGlobalTop = surface.viewportOriginY + taskbarTop;
        surface_fill_rect_global(surface, surface.viewportOriginX, taskbarGlobalTop, static_cast<int>(surface.width), 40, 0x00313544);
        surface_fill_rect_global(surface, surface.viewportOriginX + 10, taskbarGlobalTop + 6, 34, 24, 0x004C74A4);
        surface_draw_rect_global(surface, surface.viewportOriginX + 10, taskbarGlobalTop + 6, 34, 24, 0x00FFFFFF);
        surface_draw_text_global(surface, surface.viewportOriginX + 18, taskbarGlobalTop + 11, "S", 0x00FFFFFF, FontRole::SmallBold);
        surface_fill_rect_global(surface, surface.viewportOriginX + 58, taskbarGlobalTop + 6, 144, 24, 0x003C4658);
        surface_draw_rect_global(surface, surface.viewportOriginX + 58, taskbarGlobalTop + 6, 144, 24, 0x00FFFFFF);
        surface_draw_text_global(surface, surface.viewportOriginX + 68, taskbarGlobalTop + 11, "guideXOS", 0x00E6EEF6, FontRole::Small);
    }
}

static void draw_live_diagnostic_overlay(
    PixelSurface& surface,
    const DisplayRenderTarget& target,
    uint64_t frameSequence)
{
    // QEMU-only proof marker: a small moving rectangle and frame label in the
    // work area.  It is clipped through the target viewport and never touches
    // the primary-only taskbar at the bottom of the target.
    const int slot = static_cast<int>(frameSequence % 8u);
    const int markerX = surface.viewportOriginX + 24 + (slot * 22);
    const int markerY = surface.viewportOriginY + 78;
    const uint32_t markerColor = target.primary ? 0x00A8E6FF : 0x00FFD070;
    surface_fill_rect_global(surface, markerX, markerY, 16, 12, markerColor);
    surface_draw_rect_global(surface, markerX, markerY, 16, 12, 0x00FFFFFF);

    char frameText[24]{};
    write_u64_decimal(frameText, sizeof(frameText), frameSequence);
    char label[32] = "LIVE ";
    size_t labelIndex = 5u;
    for (size_t textIndex = 0u; frameText[textIndex] != '\0' && labelIndex < sizeof(label) - 1u; ++textIndex) {
        label[labelIndex++] = frameText[textIndex];
    }
    label[labelIndex] = '\0';
    surface_draw_text_global(surface,
                             surface.viewportOriginX + 48,
                             surface.viewportOriginY + 80,
                             label,
                             0x00FFFFFF,
                             FontRole::SmallBold);
}

static void draw_qemu_input_proof_overlay(PixelSurface& surface)
{
    const qemu_display_input_proof::State* proof =
        qemu_display_input_proof::state();
    if (proof == nullptr || !proof->initialized) return;

    // QEMU-only software proof window. It is painted into the ordinary
    // compositor target backing store; no virtio-gpu cursor queue or hardware
    // cursor command is used.
    const uint32_t shadow = 0x00101018;
    const uint32_t body = 0x002A3038;
    const uint32_t title = proof->clickFocus ? 0x00416D9A : 0x002C3540;
    const uint32_t border = proof->windowDominantMonitor == 2
        ? 0x00E2C46B : 0x008CC8F0;
    surface_fill_rect_global(surface, proof->windowX + 4, proof->windowY + 4,
                             proof->windowW, proof->windowH, shadow);
    surface_fill_rect_global(surface, proof->windowX, proof->windowY,
                             proof->windowW, proof->windowH, body);
    surface_draw_rect_global(surface, proof->windowX, proof->windowY,
                             proof->windowW, proof->windowH, border);
    surface_fill_rect_global(surface, proof->windowX + 1, proof->windowY + 1,
                             proof->windowW - 2, 28, title);
    surface_draw_text_global(surface, proof->windowX + 14, proof->windowY + 8,
                             "QEMU INPUT PROOF", 0x00FFFFFF, FontRole::Title);
    surface_draw_text_global(surface, proof->windowX + 22, proof->windowY + 62,
                             "ordinary window-manager drag state", 0x00E9F2F8,
                             FontRole::SmallBold);
    surface_draw_text_global(surface, proof->windowX + 22, proof->windowY + 86,
                             "move this window across the scanout boundary", 0x00D2E4EF,
                             FontRole::Small);
    surface_draw_text_global(surface, proof->windowX + 22, proof->windowY + 122,
                             proof->dragCrossedBoundary ? "boundary crossed" : "waiting for boundary",
                             proof->dragCrossedBoundary ? 0x00FFE3A6 : 0x00B7DDF4,
                             FontRole::SmallBold);
    surface_draw_text_global(surface, proof->windowX + 22, proof->windowY + 148,
                             proof->windowDominantMonitor == 2 ? "dominant monitor: 2" : "dominant monitor: 1",
                             0x00F0F6FC, FontRole::Small);

    const int cursorX = proof->cursorX;
    const int cursorY = proof->cursorY;
    surface_fill_rect_global(surface, cursorX, cursorY, 3, 18, 0x00000000);
    surface_fill_rect_global(surface, cursorX + 2, cursorY + 2, 8, 2, 0x00FFFFFF);
    surface_fill_rect_global(surface, cursorX + 2, cursorY + 4, 6, 2, 0x00FFFFFF);
    surface_fill_rect_global(surface, cursorX + 2, cursorY + 6, 4, 2, 0x00FFFFFF);
    surface_fill_rect_global(surface, cursorX + 2, cursorY + 8, 2, 7, 0x00FFFFFF);
    surface_draw_rect_global(surface, cursorX, cursorY, 11, 18, 0x00000000);
}

static bool render_guide_xos_desktop_snapshot(
    PixelSurface& surface,
    const DisplayRenderTarget& target,
    uint32_t virtualDesktopWidth,
    uint32_t virtualDesktopHeight,
    bool* conversionRequiredOut,
    const char** blockerOut,
    uint64_t diagnosticFrameSequence = 0u,
    bool liveDiagnosticOverlay = false)
{
    if (blockerOut != nullptr) {
        *blockerOut = nullptr;
    }
    if (conversionRequiredOut != nullptr) {
        *conversionRequiredOut = false;
    }

    if (!surface.isValid()) {
        if (blockerOut != nullptr) {
            *blockerOut = "pixel surface is invalid";
        }
        return false;
    }
    if (!pixelSurfaceCanCoverBacking(surface)) {
        if (blockerOut != nullptr) {
            *blockerOut = "backing store is smaller than pitch x height";
        }
        return false;
    }
    if (surface.pixelFormat == PixelFormatKind::Unknown) {
        if (blockerOut != nullptr) {
            *blockerOut = "pixel format is unknown";
        }
        return false;
    }

    const PixelFormatKind compositorFormat = PixelFormatKind::B8G8R8X8_UNORM;
    const bool conversionRequired = pixelSurfaceRequiresConversion(compositorFormat, surface.pixelFormat);
    if (conversionRequiredOut != nullptr) {
        *conversionRequiredOut = conversionRequired;
    }

    const uint32_t desktopWidth = virtualDesktopWidth > 0u ? virtualDesktopWidth : static_cast<uint32_t>(surface.width);
    const uint32_t desktopHeight = virtualDesktopHeight > 0u ? virtualDesktopHeight : static_cast<uint32_t>(surface.height);

    fill_virtual_desktop_background(surface, desktopWidth, desktopHeight);
    draw_desktop_windows(surface, target.primary);
    draw_qemu_input_proof_overlay(surface);
    surface_draw_text_global(surface,
                             surface.viewportOriginX + 20,
                             surface.viewportOriginY + 28,
                             target.primary ? "PRIMARY MONITOR" : "SECONDARY MONITOR",
                             0x00F7FBFF,
                             FontRole::SmallBold);
    surface_draw_text_global(surface,
                             surface.viewportOriginX + 20,
                             surface.viewportOriginY + 48,
                             target.primary ? "taskbar visible" : "taskbar suppressed",
                             0x00D8E8F0,
                             FontRole::Small);
    if (liveDiagnosticOverlay) {
        draw_live_diagnostic_overlay(surface, target, diagnosticFrameSequence);
    }
    return true;
}

static bool issue_transfer_to_host_2d(DeviceState& state,
                                      DiagnosticResourceState& resource,
                                      uint32_t scanoutId,
                                      const char** failureReasonOut,
                                      bool* completionKnownOut);

static bool issue_resource_flush(DeviceState& state,
                                 DiagnosticResourceState& resource,
                                 uint32_t scanoutId,
                                 const char** failureReasonOut,
                                 bool* completionKnownOut);

static CompositorFrameTargetResult present_target_once(
    DeviceState& state,
    const DisplayRenderTarget& target,
    DiagnosticResourceState& resource,
    uint8_t* backingBase,
    uint64_t backingPhysical,
    uint64_t totalBackingBytes,
    const DiagnosticPatternPalette& fallbackPalette,
    GpuFormat resourceFormat,
    uint32_t selectedWidth,
    uint32_t selectedHeight,
    uint32_t bytesPerPixel,
    uint64_t backingPageCount,
    uint64_t diagnosticFrameSequence = 0u,
    bool liveDiagnosticOverlay = false,
    bool verbose = true)
{
    CompositorFrameTargetResult result;
    result.targetIndex = target.targetIndex;
    result.scanoutId = target.scanoutId;
    result.resourceId = target.resourceId;

    if (backingBase == nullptr || target.width <= 0 || target.height <= 0) {
        result.blocker = "target backing is unavailable";
        return result;
    }

    PixelSurface surface{};
    surface.pixelPointer = reinterpret_cast<uint32_t*>(backingBase);
    surface.width = static_cast<uint32_t>(target.width);
    surface.height = static_cast<uint32_t>(target.height);
    const uint32_t resourceWidth = resource.width != 0u ? resource.width : static_cast<uint32_t>(target.width);
    const uint32_t resourceHeight = resource.height != 0u ? resource.height : static_cast<uint32_t>(target.height);
    const uint64_t actualBackingBytes = resource.backingBytes != 0u ? resource.backingBytes : totalBackingBytes;
    surface.pitchBytes = resourceWidth * bytesPerPixel;
    surface.bytesPerPixel = static_cast<uint8_t>(bytesPerPixel);
    surface.pixelFormat = pixel_format_from_gpu_format(resourceFormat);
    surface.viewportOriginX = target.viewportOriginX;
    surface.viewportOriginY = target.viewportOriginY;
    surface.targetIndex = target.targetIndex;
    surface.monitorId = target.monitorId;
    surface.scanoutId = target.scanoutId;
    surface.primary = target.primary;
    surface.taskbarVisible = target.primary;
    surface.clipRect = makePixelRect(0, 0, static_cast<int>(surface.width), static_cast<int>(surface.height));
    surface.backingByteCount = actualBackingBytes;

    result.backingValid = pixelSurfaceCanCoverBacking(surface);
    result.renderedByteCount = pixelSurfaceExpectedByteCount(surface);
    const PixelFormatKind compositorFormat = PixelFormatKind::B8G8R8X8_UNORM;
    const PixelFormatKind resourcePixelFormat = surface.pixelFormat;
    result.conversionRequired = pixelSurfaceRequiresConversion(compositorFormat, resourcePixelFormat);

    if (!result.backingValid) {
        result.blocker = "backing store is smaller than pitch x height";
        return result;
    }

    memzero(backingBase, static_cast<size_t>(actualBackingBytes));
    fill_diagnostic_pattern(surface.pixelPointer,
                            surface.width,
                            surface.height,
                            resourceWidth,
                            fallbackPalette);

    bool conversionRequired = false;
    const char* renderBlocker = nullptr;
    const uint32_t virtualDesktopWidth = selectedWidth > resourceWidth ? selectedWidth : resourceWidth * 2u;
    const uint32_t virtualDesktopHeight = selectedHeight > resourceHeight ? selectedHeight : resourceHeight;
    result.renderOk = render_guide_xos_desktop_snapshot(surface,
                                                        target,
                                                         virtualDesktopWidth,
                                                         virtualDesktopHeight,
                                                         &conversionRequired,
                                                         &renderBlocker,
                                                         diagnosticFrameSequence,
                                                         liveDiagnosticOverlay);
    result.conversionRequired = conversionRequired;
    auto restore_fallback_pattern = [&]() {
        fill_diagnostic_pattern(surface.pixelPointer,
                                surface.width,
                                surface.height,
                                resourceWidth,
                                fallbackPalette);
        result.checksum = checksum_diagnostic_pattern(reinterpret_cast<const uint8_t*>(surface.pixelPointer),
                                                      static_cast<size_t>(pixelSurfaceExpectedByteCount(surface)));
        result.fallbackUsed = true;
        result.renderOk = false;
        resource.patternChecksum = result.checksum;
        resource.checksumValid = true;
        resource.patternName = fallbackPalette.name;
    };

    if (result.renderOk) {
        result.checksum = checksum_diagnostic_pattern(reinterpret_cast<const uint8_t*>(surface.pixelPointer),
                                                      static_cast<size_t>(pixelSurfaceExpectedByteCount(surface)));
    } else {
        result.blocker = renderBlocker != nullptr ? renderBlocker : "compositor render failed";
        restore_fallback_pattern();
    }

    if (!result.renderOk) {
        if (verbose) {
        kernel::serial::puts("[VIRTIO-GPU] compositor target plan target=");
        serial_put_u32_decimal(target.targetIndex);
        kernel::serial::puts(" scanoutId=");
        serial_put_u32_decimal(target.scanoutId);
        kernel::serial::puts(" resourceId=0x");
        kernel::serial::put_hex32(target.resourceId);
        kernel::serial::puts(" viewport=");
        kernel::serial::puts(virtioGpuGeometrySummary(target.viewportOriginX, target.viewportOriginY, target.width, target.height));
        kernel::serial::puts(" compositorPixelFormat=");
        kernel::serial::puts(pixelFormatKindName(compositorFormat));
        kernel::serial::puts(" resourcePixelFormat=");
        kernel::serial::puts(pixelFormatKindName(resourcePixelFormat));
        kernel::serial::puts(" conversionRequired=");
        kernel::serial::puts(result.conversionRequired ? "yes" : "no");
        kernel::serial::puts(" stride=");
        serial_put_u64_decimal(surface.pitchBytes);
        kernel::serial::puts(" totalBytes=");
        serial_put_u64_decimal(pixelSurfaceExpectedByteCount(surface));
        kernel::serial::puts(" backingBytes=");
        serial_put_u64_decimal(totalBackingBytes);
        kernel::serial::puts(" backingValid=");
        kernel::serial::puts(result.backingValid ? "yes" : "no");
        kernel::serial::putc('\n');

        kernel::serial::puts("[VIRTIO-GPU] compositor target result target=");
        serial_put_u32_decimal(target.targetIndex);
        kernel::serial::puts(" scanoutId=");
        serial_put_u32_decimal(target.scanoutId);
        kernel::serial::puts(" resourceId=0x");
        kernel::serial::put_hex32(target.resourceId);
        kernel::serial::puts(" viewport=");
        kernel::serial::puts(virtioGpuGeometrySummary(target.viewportOriginX, target.viewportOriginY, target.width, target.height));
        kernel::serial::puts(" renderedByteCount=");
        serial_put_u64_decimal(result.renderedByteCount);
        kernel::serial::puts(" checksum=0x");
        kernel::serial::put_hex64(result.checksum);
        kernel::serial::puts(" transfer=");
        kernel::serial::puts(result.transferOk ? "ok" : "failed");
        kernel::serial::puts(" flush=");
        kernel::serial::puts(result.flushOk ? "ok" : "failed");
        kernel::serial::puts(" contentMode=fallback-patterns-after-failure");
        kernel::serial::puts(" fallbackPatterns=yes");
        kernel::serial::puts(" reason=");
        kernel::serial::puts(result.blocker != nullptr ? result.blocker : "compositor render failed");
        kernel::serial::putc('\n');
        }

        (void)backingPhysical;
        (void)backingPageCount;
        return result;
    }

    if (verbose) {
    kernel::serial::puts("[VIRTIO-GPU] compositor target plan target=");
    serial_put_u32_decimal(target.targetIndex);
    kernel::serial::puts(" scanoutId=");
    serial_put_u32_decimal(target.scanoutId);
    kernel::serial::puts(" resourceId=0x");
    kernel::serial::put_hex32(target.resourceId);
    kernel::serial::puts(" viewport=");
    kernel::serial::puts(virtioGpuGeometrySummary(target.viewportOriginX, target.viewportOriginY, target.width, target.height));
    kernel::serial::puts(" compositorPixelFormat=");
    kernel::serial::puts(pixelFormatKindName(compositorFormat));
    kernel::serial::puts(" resourcePixelFormat=");
    kernel::serial::puts(pixelFormatKindName(resourcePixelFormat));
    kernel::serial::puts(" conversionRequired=");
    kernel::serial::puts(result.conversionRequired ? "yes" : "no");
    kernel::serial::puts(" stride=");
    serial_put_u64_decimal(surface.pitchBytes);
    kernel::serial::puts(" totalBytes=");
    serial_put_u64_decimal(pixelSurfaceExpectedByteCount(surface));
    kernel::serial::puts(" backingBytes=");
    serial_put_u64_decimal(totalBackingBytes);
    kernel::serial::puts(" backingValid=");
    kernel::serial::puts(result.backingValid ? "yes" : "no");
    kernel::serial::putc('\n');
    }

    const char* commandReason = nullptr;
    bool commandCompleted = false;
    if (!issue_transfer_to_host_2d(state, resource, target.scanoutId, &commandReason, &commandCompleted)) {
        result.transferOk = false;
        result.blocker = commandReason != nullptr ? commandReason : "TRANSFER_TO_HOST_2D failed";
        restore_fallback_pattern();
        if (verbose) {
        kernel::serial::puts("[VIRTIO-GPU] compositor target result target=");
        serial_put_u32_decimal(target.targetIndex);
        kernel::serial::puts(" scanoutId=");
        serial_put_u32_decimal(target.scanoutId);
        kernel::serial::puts(" resourceId=0x");
        kernel::serial::put_hex32(target.resourceId);
        kernel::serial::puts(" viewport=");
        kernel::serial::puts(virtioGpuGeometrySummary(target.viewportOriginX, target.viewportOriginY, target.width, target.height));
        kernel::serial::puts(" renderedByteCount=");
        serial_put_u64_decimal(result.renderedByteCount);
        kernel::serial::puts(" checksum=0x");
        kernel::serial::put_hex64(result.checksum);
        kernel::serial::puts(" transfer=");
        kernel::serial::puts(result.transferOk ? "ok" : "failed");
        kernel::serial::puts(" flush=");
        kernel::serial::puts(result.flushOk ? "ok" : "failed");
        kernel::serial::puts(" contentMode=fallback-patterns-after-failure");
        kernel::serial::puts(" fallbackPatterns=yes");
        kernel::serial::puts(" reason=");
        kernel::serial::puts(result.blocker);
        kernel::serial::putc('\n');
        }
        return result;
    }
    result.transferOk = true;

    if (!issue_resource_flush(state, resource, target.scanoutId, &commandReason, &commandCompleted)) {
        result.flushOk = false;
        result.blocker = commandReason != nullptr ? commandReason : "RESOURCE_FLUSH failed";
        restore_fallback_pattern();
        if (verbose) {
        kernel::serial::puts("[VIRTIO-GPU] compositor target result target=");
        serial_put_u32_decimal(target.targetIndex);
        kernel::serial::puts(" scanoutId=");
        serial_put_u32_decimal(target.scanoutId);
        kernel::serial::puts(" resourceId=0x");
        kernel::serial::put_hex32(target.resourceId);
        kernel::serial::puts(" viewport=");
        kernel::serial::puts(virtioGpuGeometrySummary(target.viewportOriginX, target.viewportOriginY, target.width, target.height));
        kernel::serial::puts(" renderedByteCount=");
        serial_put_u64_decimal(result.renderedByteCount);
        kernel::serial::puts(" checksum=0x");
        kernel::serial::put_hex64(result.checksum);
        kernel::serial::puts(" transfer=");
        kernel::serial::puts(result.transferOk ? "ok" : "failed");
        kernel::serial::puts(" flush=");
        kernel::serial::puts(result.flushOk ? "ok" : "failed");
        kernel::serial::puts(" contentMode=fallback-patterns-after-failure");
        kernel::serial::puts(" fallbackPatterns=yes");
        kernel::serial::puts(" reason=");
        kernel::serial::puts(result.blocker);
        kernel::serial::putc('\n');
        }
        return result;
    }
    result.flushOk = true;

    resource.patternChecksum = result.checksum;
    resource.checksumValid = true;
    resource.transferOk = true;
    resource.flushOk = true;
    resource.patternName = "compositor-single-frame";

    if (verbose) {
    kernel::serial::puts("[VIRTIO-GPU] compositor target result target=");
    serial_put_u32_decimal(target.targetIndex);
    kernel::serial::puts(" scanoutId=");
    serial_put_u32_decimal(target.scanoutId);
    kernel::serial::puts(" resourceId=0x");
    kernel::serial::put_hex32(target.resourceId);
    kernel::serial::puts(" viewport=");
    kernel::serial::puts(virtioGpuGeometrySummary(target.viewportOriginX, target.viewportOriginY, target.width, target.height));
    kernel::serial::puts(" renderedByteCount=");
    serial_put_u64_decimal(result.renderedByteCount);
    kernel::serial::puts(" checksum=0x");
    kernel::serial::put_hex64(result.checksum);
    kernel::serial::puts(" transfer=");
    kernel::serial::puts(result.transferOk ? "ok" : "failed");
    kernel::serial::puts(" flush=");
    kernel::serial::puts(result.flushOk ? "ok" : "failed");
    kernel::serial::puts(" contentMode=");
    kernel::serial::puts(result.renderOk ? "compositor-single-frame" : "fallback-patterns-after-failure");
    kernel::serial::puts(" fallbackPatterns=");
    kernel::serial::puts(result.renderOk ? "no" : "yes");
    if (result.blocker != nullptr && result.blocker[0] != '\0') {
        kernel::serial::puts(" reason=");
        kernel::serial::puts(result.blocker);
    }
    kernel::serial::putc('\n');
    }

    (void)backingPhysical;
    (void)backingPageCount;
    return result;
}

static void store_live_target_descriptor(LivePresentationTargetDescriptor& descriptor,
                                         const DisplayRenderTarget& target)
{
    descriptor.targetIndex = target.targetIndex;
    descriptor.scanoutId = target.scanoutId;
    descriptor.resourceId = target.resourceId;
    descriptor.viewportOriginX = target.viewportOriginX;
    descriptor.viewportOriginY = target.viewportOriginY;
    descriptor.width = target.width;
    descriptor.height = target.height;
    descriptor.primary = target.primary;
}

static DisplayRenderTarget make_live_target(const LivePresentationTargetDescriptor& descriptor)
{
    DisplayRenderTarget target{};
    target.targetIndex = descriptor.targetIndex;
    target.targetId = descriptor.targetIndex == 1u ? "virtio-gpu-target-1" : "virtio-gpu-target-2";
    target.source = "virtio-gpu";
    target.monitorId = descriptor.targetIndex == 1u ? 1u : 2u;
    target.monitorName = descriptor.targetIndex == 1u ? "Virtio GPU Output 0" : "Virtio GPU Output 1";
    target.scanoutId = descriptor.scanoutId;
    target.resourceId = descriptor.resourceId;
    target.viewportOriginX = descriptor.viewportOriginX;
    target.viewportOriginY = descriptor.viewportOriginY;
    target.width = descriptor.width;
    target.height = descriptor.height;
    target.framebufferRect = DisplayRect{ 0, 0, descriptor.width, descriptor.height };
    target.preferredX = descriptor.viewportOriginX;
    target.preferredY = descriptor.viewportOriginY;
    target.preferredWidth = descriptor.width;
    target.preferredHeight = descriptor.height;
    target.assignedX = descriptor.viewportOriginX;
    target.assignedY = descriptor.viewportOriginY;
    target.assignedWidth = descriptor.width;
    target.assignedHeight = descriptor.height;
    target.primary = descriptor.primary;
    target.active = true;
    target.backedByHostedFramebuffer = false;
    target.backedByOutputResource = true;
    target.connectorEnabled = true;
    target.resourceBound = true;
    target.backingAttached = true;
    target.transferReady = true;
    target.presentReady = true;
    target.presentationConfirmed = true;
    target.syntheticHosted = false;
    return target;
}

// Repaint one complete target with the known-good static pattern after a
// bounded streak of live command failures.  This is deliberately target-local
// and keeps the other output's resource and scanout untouched.
static bool repaint_static_fallback_target(DeviceState& state,
                                           DiagnosticResourceState& resource,
                                           uint32_t scanoutId,
                                           uint8_t* backingBase,
                                           uint32_t width,
                                           uint32_t height,
                                           uint64_t backingBytes,
                                           const DiagnosticPatternPalette& palette,
                                           uint64_t* checksumOut)
{
    if (backingBase == nullptr || width == 0u || height == 0u || backingBytes == 0u) {
        return false;
    }

    memzero(backingBase, static_cast<size_t>(backingBytes));
    fill_diagnostic_pattern(reinterpret_cast<uint32_t*>(backingBase), width, height, width, palette);
    const uint64_t checksum = checksum_diagnostic_pattern(backingBase, static_cast<size_t>(backingBytes));
    const char* failureReason = nullptr;
    bool completionKnown = false;
    if (!issue_transfer_to_host_2d(state, resource, scanoutId, &failureReason, &completionKnown)) {
        return false;
    }
    if (!issue_resource_flush(state, resource, scanoutId, &failureReason, &completionKnown)) {
        return false;
    }

    resource.patternName = palette.name;
    resource.patternChecksum = checksum;
    resource.checksumValid = true;
    resource.transferOk = true;
    resource.flushOk = true;
    if (checksumOut != nullptr) {
        *checksumOut = checksum;
    }
    return true;
}

static void log_compositor_proof_line(
    const CompositorFrameTargetResult& target0Result,
    const CompositorFrameTargetResult& target1Result,
    uint32_t virtualDesktopWidth,
    uint32_t virtualDesktopHeight,
    bool taskbarPrimaryOnlyConfirmed,
    const char* reason)
{
    const bool success = target0Result.renderOk
        && target0Result.transferOk
        && target0Result.flushOk
        && target1Result.renderOk
        && target1Result.transferOk
        && target1Result.flushOk;

    kernel::serial::puts("[VIRTIO-GPU] VirtioGPU compositor proof: outputs=2 targets=2 frameMode=single-shot ");
    if (success) {
        kernel::serial::puts("target0Render=ok target0Transfer=ok target0Flush=ok target1Render=ok target1Transfer=ok target1Flush=ok virtualDesktop=");
        serial_put_u32_decimal(virtualDesktopWidth);
        kernel::serial::puts("x");
        serial_put_u32_decimal(virtualDesktopHeight);
        kernel::serial::puts(" taskbarPrimaryOnly=");
        kernel::serial::puts(taskbarPrimaryOnlyConfirmed ? "yes" : "no");
        kernel::serial::puts(" continuousPresentation=disabled");
        kernel::serial::putc('\n');
        return;
    }

    kernel::serial::puts("target0Render=");
    kernel::serial::puts(target0Result.renderOk ? "ok" : "failed");
    kernel::serial::puts(" target0Transfer=");
    kernel::serial::puts(target0Result.transferOk ? "ok" : "failed");
    kernel::serial::puts(" target0Flush=");
    kernel::serial::puts(target0Result.flushOk ? "ok" : "failed");
    kernel::serial::puts(" target1Render=");
    kernel::serial::puts(target1Result.renderOk ? "ok" : "failed");
    kernel::serial::puts(" target1Transfer=");
    kernel::serial::puts(target1Result.transferOk ? "ok" : "failed");
    kernel::serial::puts(" target1Flush=");
    kernel::serial::puts(target1Result.flushOk ? "ok" : "failed");
    kernel::serial::puts(" fallbackPatterns=");
    kernel::serial::puts((!target0Result.renderOk || !target1Result.renderOk) ? "yes" : "no");
    kernel::serial::puts(" reason=");
    if (reason != nullptr && reason[0] != '\0') {
        kernel::serial::puts(reason);
    } else if (!target0Result.renderOk && target0Result.blocker != nullptr) {
        kernel::serial::puts(target0Result.blocker);
    } else if (!target1Result.renderOk && target1Result.blocker != nullptr) {
        kernel::serial::puts(target1Result.blocker);
    } else {
        kernel::serial::puts("compositor frame presentation failed");
    }
    kernel::serial::putc('\n');
}

static inline void mmio_write8(uint64_t addr, uint8_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    volatile uint8_t* ptr = reinterpret_cast<volatile uint8_t*>(addr);
    *ptr = value;
    MEMORY_BARRIER();
#endif
}

static inline void mmio_write16(uint64_t addr, uint16_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    volatile uint16_t* ptr = reinterpret_cast<volatile uint16_t*>(addr);
    *ptr = value;
    MEMORY_BARRIER();
#endif
}

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
#if GXOS_MSVC_STUB
    (void)addr;
    (void)value;
#else
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr);
    *ptr = value;
    MEMORY_BARRIER();
#endif
}

static inline void mmio_write64(uint64_t addr, uint64_t value)
{
    mmio_write32(addr, static_cast<uint32_t>(value & 0xFFFFFFFFu));
    mmio_write32(addr + 4, static_cast<uint32_t>(value >> 32));
}

static inline uint8_t mmio_read8(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    volatile uint8_t* ptr = reinterpret_cast<volatile uint8_t*>(addr);
    uint8_t value = *ptr;
    MEMORY_BARRIER();
    return value;
#endif
}

static inline uint16_t mmio_read16(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    volatile uint16_t* ptr = reinterpret_cast<volatile uint16_t*>(addr);
    uint16_t value = *ptr;
    MEMORY_BARRIER();
    return value;
#endif
}

static inline uint32_t mmio_read32(uint64_t addr)
{
#if GXOS_MSVC_STUB
    (void)addr;
    return 0;
#else
    volatile uint32_t* ptr = reinterpret_cast<volatile uint32_t*>(addr);
    uint32_t value = *ptr;
    MEMORY_BARRIER();
    return value;
#endif
}

static inline uint64_t mmio_read64(uint64_t addr)
{
    const uint64_t low = mmio_read32(addr);
    const uint64_t high = mmio_read32(addr + 4);
    return low | (high << 32);
}

static uint32_t le32_to_cpu(uint32_t value)
{
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return ((value & 0x000000FFu) << 24u) |
           ((value & 0x0000FF00u) << 8u) |
           ((value & 0x00FF0000u) >> 8u) |
           ((value & 0xFF000000u) >> 24u);
#else
    return value;
#endif
}

static uint32_t cpu_to_le32(uint32_t value)
{
    return le32_to_cpu(value);
}

static void copy_event_text(char* destination, uint32_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0u) return;
    uint32_t index = 0u;
    if (source != nullptr) {
        while (source[index] != '\0' && index + 1u < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
    while (++index < capacity) destination[index] = '\0';
}

static void copy_event_identity(char* destination, const char* prefix, uint32_t ordinal)
{
    if (destination == nullptr) return;
    destination[0] = '\0';
    if (prefix == nullptr) return;
    uint32_t position = 0u;
    while (prefix[position] != '\0' && position + 1u < gxos::display::kVirtioGpuDisplayEventIdentityBytes) {
        destination[position] = prefix[position];
        ++position;
    }
    char digits[11];
    uint32_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + (ordinal % 10u));
        ordinal /= 10u;
    } while (ordinal != 0u && count < sizeof(digits));
    while (count > 0u && position + 1u < gxos::display::kVirtioGpuDisplayEventIdentityBytes) {
        destination[position++] = digits[--count];
    }
    destination[position] = '\0';
}

static bool text_equal_bounded(const char* left, const char* right, uint32_t capacity)
{
    if (left == nullptr || right == nullptr) return left == right;
    for (uint32_t i = 0u; i < capacity; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return true;
}

// Forward declarations for the existing modern PCI capability addressors.
static uint64_t common_cfg_addr(const ModernTransport& transport, uint32_t fieldOffset);
static uint64_t device_cfg_addr(const ModernTransport& transport, uint32_t fieldOffset);

static bool read_virtio_gpu_config_snapshot_internal(DeviceState& state,
                                                       VirtioGpuConfigSnapshot& snapshot)
{
    snapshot = VirtioGpuConfigSnapshot{};
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    (void)state;
    copy_event_text(snapshot.failureReason, sizeof(snapshot.failureReason), "QEMU-only probe gate is disabled");
    return false;
#else
    ModernTransport& transport = state.transport;
    if (!state.device.initialized || !transport.modern || !transport.mmioMapped ||
        !transport.deviceCfg.present || !transport.commonCfg.present) {
        copy_event_text(snapshot.failureReason, sizeof(snapshot.failureReason), "modern mapped device configuration is unavailable");
        return false;
    }

    for (uint32_t attempt = 0u; attempt <= kDisplayEventConfigReadRetries; ++attempt) {
        const uint8_t first = mmio_read8(common_cfg_addr(transport, pci::COMMON_CFG_GEN));
        const uint32_t eventsRead = le32_to_cpu(mmio_read32(device_cfg_addr(transport, DEVICE_CONFIG_EVENTS_READ)));
        const uint32_t numScanouts = le32_to_cpu(mmio_read32(device_cfg_addr(transport, DEVICE_CONFIG_NUM_SCANOUTS)));
        const uint32_t numCapsets = le32_to_cpu(mmio_read32(device_cfg_addr(transport, DEVICE_CONFIG_NUM_CAPSETS)));
        const uint8_t final = mmio_read8(common_cfg_addr(transport, pci::COMMON_CFG_GEN));
        snapshot.firstGeneration = first;
        snapshot.finalGeneration = final;
        snapshot.retryCount = static_cast<uint8_t>(attempt);
        snapshot.eventsRead = eventsRead;
        snapshot.numScanouts = numScanouts;
        snapshot.numCapsets = numCapsets;
        if (first == final) {
            snapshot.coherent = 1u;
            snapshot.failureReason[0] = '\0';
            transport.mmioConfigGeneration = final;
            transport.mmioDeviceScanouts = numScanouts;
            transport.mmioDeviceCapsets = numCapsets;
            return true;
        }
    }

    copy_event_text(snapshot.failureReason, sizeof(snapshot.failureReason),
                    "config_generation changed across bounded device-config read");
    return false;
#endif
}

static const char* capability_name(uint8_t cfgType)
{
    switch (cfgType) {
    case pci::CAP_COMMON_CFG:
        return "common";
    case pci::CAP_NOTIFY_CFG:
        return "notify";
    case pci::CAP_ISR_CFG:
        return "isr";
    case pci::CAP_DEVICE_CFG:
        return "device";
    case pci::CAP_PCI_CFG:
        return "pci";
    default:
        return "unknown";
    }
}

static const char* region_status_name(const PciRegion& region)
{
    if (region.present) {
        return "resolved";
    }

    if (region.found) {
        return "malformed";
    }

    return "absent";
}

static const char* bar_kind_name(uint8_t barIndex, uint32_t rawBar)
{
    if (barIndex > 5) {
        return "invalid-index";
    }

    if (rawBar == 0u || rawBar == 0xFFFFFFFFu) {
        return "unassigned";
    }

    if ((rawBar & 0x1u) != 0u) {
        return "io";
    }

    switch ((rawBar >> 1) & 0x3u) {
    case 0:
        return "mmio32";
    case 2:
        return "mmio64";
    default:
        return "mmio-reserved";
    }
}

static const char* transport_kind_name(const ModernTransport& transport)
{
    if (transport.vendorId != kVirtioPciVendorId) {
        return "unknown";
    }

    if (transport.deviceId < PCI_DEVICE_BASE_MODERN) {
        return "legacy";
    }

    if (transport.commonCfg.present &&
        transport.notifyCfg.present &&
        transport.isrCfg.present &&
        transport.deviceCfg.present) {
        return "modern";
    }

    return "transitional";
}

static const char* transport_blocker_reason(const ModernTransport& transport)
{
    if (transport.commonCfg.present &&
        transport.notifyCfg.present &&
        transport.isrCfg.present &&
        transport.deviceCfg.present) {
        return nullptr;
    }

    if (transport.pciCfg.found &&
        !transport.commonCfg.found &&
        !transport.notifyCfg.found &&
        !transport.isrCfg.found &&
        !transport.deviceCfg.found) {
        return "only cfg_type=0x05 pci capability observed";
    }

    if (!transport.commonCfg.present) {
        if (transport.commonCfg.found) {
            return "common config capability malformed";
        }
        return "common config capability absent";
    }

    if (!transport.notifyCfg.present) {
        if (transport.notifyCfg.found) {
            return "notify config capability malformed";
        }
        return "notify config capability absent";
    }

    if (!transport.isrCfg.present) {
        if (transport.isrCfg.found) {
            return "isr config capability malformed";
        }
        return "isr config capability absent";
    }

    if (!transport.deviceCfg.present) {
        if (transport.deviceCfg.found) {
            return "device config capability malformed";
        }
        return "device config capability absent";
    }

    return "required modern transport capabilities unresolved";
}

enum class MmioRegionKind : uint8_t {
    Common = 0,
    Notify = 1,
    Isr = 2,
    Device = 3,
    Pci = 4,
};

static const char* mmio_region_label(MmioRegionKind kind)
{
    switch (kind) {
    case MmioRegionKind::Common:
        return "common";
    case MmioRegionKind::Notify:
        return "notify";
    case MmioRegionKind::Isr:
        return "isr";
    case MmioRegionKind::Device:
        return "device";
    case MmioRegionKind::Pci:
        return "pci";
    default:
        return "unknown";
    }
}

static const char* mmio_region_blocker_reason(MmioRegionKind kind,
                                              const kernel::mmio::MappingReport& report)
{
    if (report.length == 0) {
        switch (kind) {
        case MmioRegionKind::Common:
            return "common config MMIO length is zero";
        case MmioRegionKind::Notify:
            return "notify config MMIO length is zero";
        case MmioRegionKind::Isr:
            return "isr config MMIO length is zero";
        case MmioRegionKind::Device:
            return "device config MMIO length is zero";
        case MmioRegionKind::Pci:
            return "pci config MMIO length is zero";
        default:
            return "MMIO length is zero";
        }
    }

    if ((report.flags & (kernel::mmio::MAP_FLAG_NON_USER | kernel::mmio::MAP_FLAG_NO_EXEC)) !=
        (kernel::mmio::MAP_FLAG_NON_USER | kernel::mmio::MAP_FLAG_NO_EXEC)) {
        switch (kind) {
        case MmioRegionKind::Common:
            return "common config MMIO mappings must be kernel-only, no-executable, and uncached";
        case MmioRegionKind::Notify:
            return "notify config MMIO mappings must be kernel-only, no-executable, and uncached";
        case MmioRegionKind::Isr:
            return "isr config MMIO mappings must be kernel-only, no-executable, and uncached";
        case MmioRegionKind::Device:
            return "device config MMIO mappings must be kernel-only, no-executable, and uncached";
        case MmioRegionKind::Pci:
            return "pci config MMIO mappings must be kernel-only, no-executable, and uncached";
        default:
            return "MMIO mappings must be kernel-only, no-executable, and uncached";
        }
    }

    if (report.cacheAttributesRequested && !report.cacheAttributesSupported) {
        switch (kind) {
        case MmioRegionKind::Common:
            return "common config MMIO cache mode is not supported yet";
        case MmioRegionKind::Notify:
            return "notify config MMIO cache mode is not supported yet";
        case MmioRegionKind::Isr:
            return "isr config MMIO cache mode is not supported yet";
        case MmioRegionKind::Device:
            return "device config MMIO cache mode is not supported yet";
        case MmioRegionKind::Pci:
            return "pci config MMIO cache mode is not supported yet";
        default:
            return "MMIO cache mode is not supported yet";
        }
    }

    if (!report.withinSafeDirectMap) {
        switch (kind) {
        case MmioRegionKind::Common:
            return "common config MMIO range is not yet mapped in the reserved window";
        case MmioRegionKind::Notify:
            return "notify config MMIO range is not yet mapped in the reserved window";
        case MmioRegionKind::Isr:
            return "isr config MMIO range is not yet mapped in the reserved window";
        case MmioRegionKind::Device:
            return "device config MMIO range is not yet mapped in the reserved window";
        case MmioRegionKind::Pci:
            return "pci config MMIO range is not yet mapped in the reserved window";
        default:
            return "MMIO range is not yet mapped in the reserved window";
        }
    }

    switch (kind) {
    case MmioRegionKind::Common:
        return "common config MMIO mapping completed unexpectedly";
    case MmioRegionKind::Notify:
        return "notify config MMIO mapping completed unexpectedly";
    case MmioRegionKind::Isr:
        return "isr config MMIO mapping completed unexpectedly";
    case MmioRegionKind::Device:
        return "device config MMIO mapping completed unexpectedly";
    case MmioRegionKind::Pci:
        return "pci config MMIO mapping completed unexpectedly";
    default:
        return "MMIO mapping completed unexpectedly";
    }
}

static bool region_physical_base(const PciRegion& region, uint64_t* physicalBaseOut)
{
    if (physicalBaseOut == nullptr) {
        return false;
    }

    if (region.base > (~0ULL - region.offset)) {
        *physicalBaseOut = 0;
        return false;
    }

    *physicalBaseOut = region.base + region.offset;
    return true;
}

static uint64_t region_mmio_addr(const PciRegion& region, uint32_t fieldOffset)
{
    if (region.mapped) {
        return region.mappedVirtual + fieldOffset;
    }

    return region.base + region.offset + fieldOffset;
}

static void log_mmio_mapping_report(MmioRegionKind kind,
                                    const kernel::mmio::MappingReport& report,
                                    bool mappingSucceeded,
                                    uint64_t mappedVirtual,
                                    bool qemuProbeOnly)
{
    kernel::serial::puts("[VIRTIO-GPU] MMIO mapping report ");
    kernel::serial::puts(mmio_region_label(kind));
    kernel::serial::puts(" requestBase=0x");
    kernel::serial::put_hex64(report.physicalBase);
    kernel::serial::puts(" requestLength=0x");
    kernel::serial::put_hex64(report.length);
    kernel::serial::puts(" alignedBase=0x");
    kernel::serial::put_hex64(report.alignedBase);
    kernel::serial::puts(" alignedLength=0x");
    kernel::serial::put_hex64(report.alignedLength);
    kernel::serial::puts(" mappedLength=0x");
    kernel::serial::put_hex64(report.mappedLength);
    kernel::serial::puts(" pages=");
    serial_put_u64_decimal(report.pageCount);
    kernel::serial::puts(" kernelVirtualBase=");
    if (mappingSucceeded) {
        kernel::serial::puts("0x");
        kernel::serial::put_hex64(report.kernelVirtualBase);
    } else {
        kernel::serial::puts("n/a");
    }
    kernel::serial::puts(" mappedVirtual=");
    if (mappingSucceeded) {
        kernel::serial::puts("0x");
        kernel::serial::put_hex64(mappedVirtual);
    } else {
        kernel::serial::puts("n/a");
    }
    kernel::serial::puts(" flags=0x");
    kernel::serial::put_hex32(report.flags);
    kernel::serial::puts(" nonUser=");
    kernel::serial::puts((report.flags & kernel::mmio::MAP_FLAG_NON_USER) != 0u ? "yes" : "no");
    kernel::serial::puts(" noExec=");
    kernel::serial::puts((report.flags & kernel::mmio::MAP_FLAG_NO_EXEC) != 0u ? "yes" : "no");
    kernel::serial::puts(" uncached=");
    kernel::serial::puts((report.flags & kernel::mmio::MAP_FLAG_UNCACHED) != 0u ? "yes" : "no");
    kernel::serial::puts(" cacheAttrs=");
    if (report.cacheAttributesRequested) {
        kernel::serial::puts(report.cacheAttributesSupported ? "ok" : "blocked");
    } else {
        kernel::serial::puts("off");
    }
    kernel::serial::puts(" cacheMode=");
    kernel::serial::puts(report.cacheMode != nullptr ? report.cacheMode : "n/a");
    kernel::serial::puts(" qemuProbeOnly=");
    kernel::serial::puts(qemuProbeOnly ? "yes" : "no");
    kernel::serial::puts(" pageAligned=");
    kernel::serial::puts(report.pageAligned ? "yes" : "no");
    kernel::serial::puts(" windowEligible=");
    kernel::serial::puts(report.withinSafeDirectMap ? "yes" : "no");
    kernel::serial::puts(" requiresNewPageTableEntries=");
    kernel::serial::puts(report.requiresNewPageTableEntries ? "yes" : "no");
    kernel::serial::puts(" success=");
    kernel::serial::puts(report.success ? "yes" : "no");
    kernel::serial::puts(" reason=");
    kernel::serial::puts(report.reason != nullptr ? report.reason : "n/a");
    kernel::serial::puts(" nextFeature=");
    kernel::serial::puts(report.nextKernelFeature != nullptr ? report.nextKernelFeature : "n/a");
    kernel::serial::putc('\n');
}

static const char* transport_mmio_blocker_reason(ModernTransport& transport,
                                                 MmioTransportProbeState* mappingOut = nullptr,
                                                 kernel::mmio::MappingReport* blockerReportOut = nullptr)
{
    struct RegionDescriptor {
        MmioRegionKind kind;
        const PciRegion* region;
    };

    const RegionDescriptor regions[] = {
        { MmioRegionKind::Common, &transport.commonCfg },
        { MmioRegionKind::Notify, &transport.notifyCfg },
        { MmioRegionKind::Isr, &transport.isrCfg },
        { MmioRegionKind::Device, &transport.deviceCfg },
    };

    const char* blockerReason = nullptr;
    if (blockerReportOut != nullptr) {
        *blockerReportOut = kernel::mmio::MappingReport{};
    }
    if (mappingOut != nullptr) {
        *mappingOut = MmioTransportProbeState{};
        mappingOut->stopReason = "transport writes intentionally disabled";
    }

    uint64_t totalUniquePages = 0;
    uint64_t commonVirtual = 0;
    const char* cacheMode = nullptr;
    bool summarySuccess = true;
    bool allSanityReadsOk = false;
    uint16_t numQueues = 0;
    uint8_t deviceStatus = 0;
    uint8_t configGeneration = 0;
    uint32_t deviceScanouts = 0;
    uint32_t deviceCapsets = 0;

    for (const RegionDescriptor& entry : regions) {
        const PciRegion& region = *entry.region;
        if (!region.present) {
            continue;
        }

        kernel::mmio::MappingReport report{};
        uint64_t physicalBase = 0;
        const bool baseValid = region_physical_base(region, &physicalBase);
        const uint32_t mappingFlags = kernel::mmio::MAP_FLAG_NON_USER |
                                      kernel::mmio::MAP_FLAG_NO_EXEC |
                                      kernel::mmio::MAP_FLAG_UNCACHED;
        bool mapped = false;
        uint64_t mappedVirtual = 0;

        if (baseValid) {
            mapped = kernel::mmio::mapForDevice(physicalBase, region.length, &mappedVirtual, &report, mappingFlags);
        } else {
            report.physicalBase = 0;
            report.length = region.length;
            report.alignedBase = 0;
            report.alignedLength = 0;
            report.mappedLength = 0;
            report.pageCount = 0;
            report.safeDirectMapCeiling = kernel::mmio::MMIO_WINDOW_LIMIT;
            report.kernelVirtualBase = 0;
            report.mappedVirtual = 0;
            report.pageOffset = 0;
            report.flags = mappingFlags;
            report.pageAligned = false;
            report.withinSafeDirectMap = false;
            report.requiresPageRounding = true;
            report.requiresNewPageTableEntries = true;
            report.cacheAttributesRequested = false;
            report.cacheAttributesSupported = false;
            report.success = false;
            report.cacheMode = "n/a";
            report.reason = "MMIO base overflows address space";
            report.nextKernelFeature = "overflow-safe MMIO range validation";
        }
        log_mmio_mapping_report(entry.kind, report, mapped, mappedVirtual, true);

        if (blockerReason == nullptr) {
            if (!baseValid) {
                switch (entry.kind) {
                case MmioRegionKind::Common:
                    blockerReason = "common config MMIO base overflows address space";
                    break;
                case MmioRegionKind::Notify:
                    blockerReason = "notify config MMIO base overflows address space";
                    break;
                case MmioRegionKind::Isr:
                    blockerReason = "isr config MMIO base overflows address space";
                    break;
                case MmioRegionKind::Device:
                    blockerReason = "device config MMIO base overflows address space";
                    break;
                case MmioRegionKind::Pci:
                    blockerReason = "pci config MMIO base overflows address space";
                    break;
                default:
                    blockerReason = "MMIO base overflows address space";
                    break;
                }
                if (blockerReportOut != nullptr) {
                    *blockerReportOut = report;
                }
            } else if (!mapped) {
                blockerReason = mmio_region_blocker_reason(entry.kind, report);
                if (blockerReason != nullptr && blockerReportOut != nullptr) {
                    *blockerReportOut = report;
                }
            } else {
                if (report.requiresNewPageTableEntries) {
                    totalUniquePages += report.pageCount;
                }
                if (entry.kind == MmioRegionKind::Common) {
                    commonVirtual = mappedVirtual;
                }
                if (report.cacheMode != nullptr && cacheMode == nullptr) {
                    cacheMode = report.cacheMode;
                }
            }
        }

        if (mapped) {
            if (entry.region == &transport.commonCfg) {
                transport.commonCfg.mapped = true;
                transport.commonCfg.mappedVirtual = mappedVirtual;
            } else if (entry.region == &transport.notifyCfg) {
                transport.notifyCfg.mapped = true;
                transport.notifyCfg.mappedVirtual = mappedVirtual;
            } else if (entry.region == &transport.isrCfg) {
                transport.isrCfg.mapped = true;
                transport.isrCfg.mappedVirtual = mappedVirtual;
            } else if (entry.region == &transport.deviceCfg) {
                transport.deviceCfg.mapped = true;
                transport.deviceCfg.mappedVirtual = mappedVirtual;
            }
        } else {
            summarySuccess = false;
        }
    }

    if (mappingOut != nullptr && summarySuccess) {
        kernel::serial::puts("[VIRTIO-GPU] MMIO sanity reads begin\n");
        if (transport.commonCfg.mapped) {
            numQueues = mmio_read16(region_mmio_addr(transport.commonCfg, pci::COMMON_NUM_QUEUES));
            deviceStatus = mmio_read8(region_mmio_addr(transport.commonCfg, pci::COMMON_STATUS));
            configGeneration = mmio_read8(region_mmio_addr(transport.commonCfg, pci::COMMON_CFG_GEN));
        }
        kernel::serial::puts("[VIRTIO-GPU] MMIO sanity reads done\n");
        allSanityReadsOk = transport.commonCfg.mapped;
        mappingOut->mmioMapped = true;
        mappingOut->sanityReadsOk = allSanityReadsOk;
        mappingOut->mappedVirtual = commonVirtual;
        mappingOut->pageCount = totalUniquePages;
        mappingOut->numQueues = numQueues;
        mappingOut->deviceStatus = deviceStatus;
        mappingOut->configGeneration = configGeneration;
        mappingOut->deviceScanouts = deviceScanouts;
        mappingOut->deviceCapsets = deviceCapsets;
        mappingOut->cacheMode = cacheMode != nullptr ? cacheMode : "uc(pcd+pwt)";
        mappingOut->stopReason = "transport writes intentionally disabled";
    }

    transport.mmioMapped = summarySuccess;
    transport.mmioSanityReadsOk = allSanityReadsOk;
    transport.mmioMappedVirtual = commonVirtual;
    transport.mmioMappedPageCount = totalUniquePages;
    transport.mmioNumQueues = numQueues;
    transport.mmioDeviceStatus = deviceStatus;
    transport.mmioConfigGeneration = configGeneration;
    transport.mmioDeviceScanouts = deviceScanouts;
    transport.mmioDeviceCapsets = deviceCapsets;
    transport.mmioCacheMode = summarySuccess ? (cacheMode != nullptr ? cacheMode : "uc(pcd+pwt)") : "n/a";
    if (summarySuccess) {
        transport.mmioStopReason = "transport writes intentionally disabled";
    }

    return blockerReason;
}

static void print_capability_inventory(const ModernTransport& transport)
{
    kernel::serial::puts("common=");
    kernel::serial::puts(region_status_name(transport.commonCfg));
    kernel::serial::puts(" notify=");
    kernel::serial::puts(region_status_name(transport.notifyCfg));
    kernel::serial::puts(" isr=");
    kernel::serial::puts(region_status_name(transport.isrCfg));
    kernel::serial::puts(" device=");
    kernel::serial::puts(region_status_name(transport.deviceCfg));
    kernel::serial::puts(" pci=");
    kernel::serial::puts(region_status_name(transport.pciCfg));
}

static void log_capability_inventory_line(const ModernTransport& transport)
{
    kernel::serial::puts("[VIRTIO-GPU] Capability inventory ");
    print_capability_inventory(transport);
    kernel::serial::putc('\n');
}

static void log_init_step(const char* step)
{
    kernel::serial::puts("[VIRTIO-GPU] Init step: ");
    kernel::serial::puts(step);
    kernel::serial::putc('\n');
}

static void record_probe_outcome(const DeviceState& state, bool initialized,
                                 DisplayInfoOutcome displayInfoOutcome,
                                 uint32_t enabledScanoutCount,
                                 uint32_t disabledScanoutCount,
                                 uint32_t scanoutSlots,
                                 const char* reason)
{
    s_probeOutcome.valid = true;
    s_probeOutcome.candidateCount = 1;
    s_probeOutcome.initialized = initialized;
    s_probeOutcome.featuresOk = state.transport.featuresOk;
    s_probeOutcome.controlQueueReady = state.transport.controlQueueReady;
    s_probeOutcome.displayInfoOutcome = displayInfoOutcome;
    s_probeOutcome.scanoutSlots = scanoutSlots;
    s_probeOutcome.enabledScanoutCount = enabledScanoutCount;
    s_probeOutcome.disabledScanoutCount = disabledScanoutCount;
    s_probeOutcome.reason = reason;
    s_probeOutcome.state = &state;
}

static void print_probe_outcome()
{
    const ModernTransport* transport = nullptr;
    if (s_probeOutcome.valid && s_probeOutcome.state != nullptr) {
        transport = &s_probeOutcome.state->transport;
    }

    kernel::serial::puts("[VIRTIO-GPU] Probe complete: devices=");
    serial_put_u32_decimal(s_probeOutcome.valid ? s_probeOutcome.candidateCount : 0u);
    kernel::serial::puts(" initialized=");
    kernel::serial::puts((s_probeOutcome.valid && s_probeOutcome.initialized) ? "1" : "0");
    kernel::serial::puts(" transport=");
    if (transport != nullptr) {
        kernel::serial::puts(transport_kind_name(*transport));
    } else {
        kernel::serial::puts("unknown");
    }
    kernel::serial::puts(" mmioMapped=");
    if (transport != nullptr) {
        kernel::serial::puts(transport->mmioMapped ? "yes" : "no");
    } else {
        kernel::serial::puts("no");
    }
    kernel::serial::puts(" mappingVirtual=");
    if (transport != nullptr && transport->mmioMapped) {
        kernel::serial::puts("0x");
        kernel::serial::put_hex64(transport->mmioMappedVirtual);
    } else {
        kernel::serial::puts("n/a");
    }
    kernel::serial::puts(" pageCount=");
    if (transport != nullptr && transport->mmioMapped) {
        serial_put_u64_decimal(transport->mmioMappedPageCount);
    } else {
        kernel::serial::puts("0");
    }
    kernel::serial::puts(" cacheMode=");
    if (transport != nullptr && transport->mmioCacheMode != nullptr) {
        kernel::serial::puts(transport->mmioCacheMode);
    } else {
        kernel::serial::puts("n/a");
    }
    kernel::serial::puts(" sanityReads=");
    if (transport != nullptr) {
        kernel::serial::puts(transport->mmioSanityReadsOk ? "ok" : "blocked");
    } else {
        kernel::serial::puts("blocked");
    }
    kernel::serial::puts(" featuresOk=");
    if (transport != nullptr) {
        kernel::serial::puts(s_probeOutcome.featuresOk ? "yes" : "no");
    } else {
        kernel::serial::puts("no");
    }
    kernel::serial::puts(" controlq=");
    if (transport != nullptr) {
        kernel::serial::puts(s_probeOutcome.controlQueueReady ? "ready" : "blocked");
    } else {
        kernel::serial::puts("blocked");
    }
    kernel::serial::puts(" caps=");
    if (transport != nullptr) {
        print_capability_inventory(*transport);
    } else {
        kernel::serial::puts("common=absent notify=absent isr=absent device=absent pci=absent");
    }
    kernel::serial::puts(" displayInfo=");
    switch (s_probeOutcome.displayInfoOutcome) {
    case DisplayInfoOutcome::Ok:
        kernel::serial::puts("ok");
        break;
    case DisplayInfoOutcome::Failed:
        kernel::serial::puts("failed");
        break;
    case DisplayInfoOutcome::NotQueried:
    default:
        kernel::serial::puts("not-queried");
        break;
    }
    kernel::serial::puts(" scanoutSlots=");
    serial_put_u32_decimal(s_probeOutcome.scanoutSlots);
    kernel::serial::puts(" deviceConfigNumScanouts=");
    serial_put_u32_decimal(s_probeOutcome.deviceConfigNumScanouts);
    kernel::serial::puts(" qemuMaxOutputsIntent=");
    serial_put_u32_decimal(s_probeOutcome.qemuMaxOutputsIntent);
    kernel::serial::puts(" enabledScanoutsBefore=");
    serial_put_u32_decimal(s_probeOutcome.enabledScanoutCount);
    kernel::serial::puts(" disabledScanoutsBefore=");
    serial_put_u32_decimal(s_probeOutcome.disabledScanoutCount);
    kernel::serial::puts(" enabledScanoutsAfter=");
    serial_put_u32_decimal(s_probeOutcome.enabledScanoutsAfter);
    kernel::serial::puts(" resource2d=");
    kernel::serial::puts(s_probeOutcome.resource2dReady ? "ready" : "blocked");
    kernel::serial::puts(" backing=");
    kernel::serial::puts(s_probeOutcome.backingAttached ? "attached" : "blocked");
    kernel::serial::puts(" scanout0=");
    kernel::serial::puts(s_probeOutcome.scanout0Set ? "set" : "blocked");
    kernel::serial::puts(" transfer=");
    kernel::serial::puts(s_probeOutcome.transferOk ? "ok" : "blocked");
    kernel::serial::puts(" flush=");
    kernel::serial::puts(s_probeOutcome.flushOk ? "ok" : "blocked");
    kernel::serial::puts(" resource2dSecondary=");
    kernel::serial::puts(s_probeOutcome.resource2dReadySecondary ? "ready" : "blocked");
    kernel::serial::puts(" backingSecondary=");
    kernel::serial::puts(s_probeOutcome.backingAttachedSecondary ? "attached" : "blocked");
    kernel::serial::puts(" scanout1=");
    kernel::serial::puts(s_probeOutcome.scanout1Set ? "set" : "blocked");
    kernel::serial::puts(" transfer1=");
    kernel::serial::puts(s_probeOutcome.transfer1Ok ? "ok" : "blocked");
    kernel::serial::puts(" flush1=");
    kernel::serial::puts(s_probeOutcome.flush1Ok ? "ok" : "blocked");
    kernel::serial::puts(" distinctPatterns=");
    kernel::serial::puts(s_probeOutcome.distinctPatternsConfirmed ? "yes" : "no");
    kernel::serial::puts(" qemuTwoUsableScanouts=");
    kernel::serial::puts((s_probeOutcome.deviceConfigNumScanouts >= 2u && s_probeOutcome.enabledScanoutsAfter >= 2u) ? "yes" : "no");
    kernel::serial::puts(" contentMode=");
    kernel::serial::puts(s_probeOutcome.contentMode != nullptr ? s_probeOutcome.contentMode : "diagnostic-patterns");
    kernel::serial::puts(" frameMode=");
    kernel::serial::puts(s_probeOutcome.frameMode != nullptr ? s_probeOutcome.frameMode : "single-shot");
    kernel::serial::puts(" continuousPresentation=");
    kernel::serial::puts(s_probeOutcome.continuousPresentationEnabled ? "enabled" : "disabled");
    kernel::serial::puts(" rendering=");
    if (s_probeOutcome.distinctPatternsConfirmed) {
        kernel::serial::puts("dual-output-test-pattern");
    } else {
        kernel::serial::puts(s_probeOutcome.renderingTestPattern ? "test-pattern-single-output" : "disabled");
    }
    if (s_probeOutcome.reason != nullptr && s_probeOutcome.reason[0] != '\0') {
        kernel::serial::puts(" reason=");
        kernel::serial::puts(s_probeOutcome.reason);
    }
    kernel::serial::putc('\n');

    if (s_probeOutcome.outputInventory.outputCount > 0u) {
        kernel::serial::puts("[VIRTIO-GPU] ");
        kernel::serial::puts(virtioGpuOutputInventorySummary(s_probeOutcome.outputInventory));
        kernel::serial::putc('\n');

        for (size_t i = 0; i < s_probeOutcome.outputInventory.outputs.size(); ++i) {
            const VirtioGpuScanoutState& output = s_probeOutcome.outputInventory.outputs[i];
            const DisplayMonitorDescriptor* monitor = i < s_probeOutcome.outputInventory.monitors.size()
                ? &s_probeOutcome.outputInventory.monitors[i]
                : nullptr;
            const DisplayViewport* viewport = i < s_probeOutcome.outputInventory.viewports.size()
                ? &s_probeOutcome.outputInventory.viewports[i]
                : nullptr;
            const DisplayRenderTarget* target = i < s_probeOutcome.outputInventory.renderTargets.size()
                ? &s_probeOutcome.outputInventory.renderTargets[i]
                : nullptr;

            if (monitor != nullptr) {
                kernel::serial::puts("[VIRTIO-GPU] ");
                kernel::serial::puts(virtioGpuOutputSummaryLine(output, *monitor));
                kernel::serial::putc('\n');

                kernel::serial::puts("[VIRTIO-GPU] ");
                kernel::serial::puts(virtioGpuMonitorSummaryLine(*monitor));
                kernel::serial::putc('\n');
            }

            if (viewport != nullptr) {
                kernel::serial::puts("[VIRTIO-GPU] viewport[");
                serial_put_u32_decimal(static_cast<uint32_t>(i));
                kernel::serial::puts("]: ");
                kernel::serial::puts(viewport->summary());
                kernel::serial::putc('\n');
            }

            if (target != nullptr) {
                kernel::serial::puts("[VIRTIO-GPU] ");
                kernel::serial::puts(virtioGpuRenderTargetSummaryLine(*target));
                kernel::serial::putc('\n');
            }
        }
    }
}

static DeviceState* find_state(GpuDevice* device)
{
    if (device == nullptr) {
        return nullptr;
    }

    for (int i = 0; i < s_deviceCount; ++i) {
        if (&s_devices[i].device == device) {
            return &s_devices[i];
        }
    }

    return nullptr;
}

static DeviceState* reserve_state()
{
    if (s_deviceCount >= static_cast<int>(sizeof(s_devices) / sizeof(s_devices[0]))) {
        return nullptr;
    }
    return &s_devices[s_deviceCount];
}

static uint16_t choose_queue_size(uint16_t queueMax)
{
    if (queueMax < kMinControlQueueSize) {
        return 0;
    }

    uint16_t chosen = kMinControlQueueSize;
    while ((chosen << 1) <= queueMax && chosen < kMaxControlQueueSize) {
        chosen = static_cast<uint16_t>(chosen << 1);
    }

    if (chosen > kMaxControlQueueSize) {
        chosen = kMaxControlQueueSize;
    }

    return chosen;
}

static bool layout_control_queue(Virtqueue* queue, uint16_t queueSize)
{
    if (queue == nullptr || queueSize < kMinControlQueueSize) {
        return false;
    }

    memzero(&s_queueStorage[0], sizeof(s_queueStorage));
    memzero(queue, sizeof(Virtqueue));

    const uint64_t base = reinterpret_cast<uint64_t>(&s_queueStorage[0]);
    const uint64_t desc = base;
    const uint64_t avail = desc + queueSize * sizeof(VringDesc);
    const uint64_t used = align_up(avail + sizeof(uint16_t) * (3 + queueSize), 4096);
    const uint64_t end = used + sizeof(uint16_t) * 3 + queueSize * sizeof(VringUsedElem);

    if (end > base + sizeof(s_queueStorage)) {
        return false;
    }

    queue->size = queueSize;
    queue->index = 0;
    queue->desc = reinterpret_cast<VringDesc*>(desc);
    queue->avail = reinterpret_cast<VringAvail*>(avail);
    queue->used = reinterpret_cast<VringUsed*>(used);
    queue->lastUsedIdx = 0;
    queue->freeHead = 0;
    queue->numFree = queueSize;
    queue->notifyOffset = 0;
    queue->descPhys = dma_address(reinterpret_cast<void*>(desc));
    queue->availPhys = dma_address(reinterpret_cast<void*>(avail));
    queue->usedPhys = dma_address(reinterpret_cast<void*>(used));

    for (uint16_t index = 0; index < queueSize; ++index) {
        queue->desc[index].addr = 0;
        queue->desc[index].len = 0;
        queue->desc[index].flags = 0;
        queue->desc[index].next = (index + 1u < queueSize) ? static_cast<uint16_t>(index + 1u) : kInvalidDescriptorIndex;
    }

    return true;
}

static bool read_bar_base(uint8_t bus, uint8_t device, uint8_t function, uint8_t barIndex,
                          uint32_t* rawBarOut, uint64_t* baseOut)
{
    if (baseOut == nullptr || rawBarOut == nullptr || barIndex > 5) {
        if (rawBarOut != nullptr) {
            *rawBarOut = 0xFFFFFFFFu;
        }
        return false;
    }

    const uint8_t offset = static_cast<uint8_t>(kPciBar0Offset + (barIndex * 4));
    const uint32_t barLow = msi::pci_config_read32(bus, device, function, offset);
    *rawBarOut = barLow;
    if (barLow == 0 || barLow == 0xFFFFFFFFu) {
        return false;
    }

    if (barLow & 0x1u) {
        return false;
    }

    uint64_t base = barLow & 0xFFFFFFF0u;
    const uint8_t type = static_cast<uint8_t>((barLow >> 1) & 0x3u);
    if (type == 2) {
        const uint32_t barHigh = msi::pci_config_read32(bus, device, function, static_cast<uint8_t>(offset + 4));
        base |= (static_cast<uint64_t>(barHigh) << 32);
    }

    *baseOut = base;
    return base != 0;
}

static void log_capability_entry(uint8_t capPtr, const PciCapability& cap, uint32_t rawBar,
                                 uint64_t base, const char* status)
{
    kernel::serial::puts("[VIRTIO-GPU] PCI cap capPtr=0x");
    kernel::serial::put_hex8(capPtr);
    kernel::serial::puts(" capId=0x");
    kernel::serial::put_hex8(cap.capId);
    kernel::serial::puts(" next=0x");
    kernel::serial::put_hex8(cap.nextPtr);
    kernel::serial::puts(" cfgType=0x");
    kernel::serial::put_hex8(cap.cfgType);
    kernel::serial::puts(" type=");
    kernel::serial::puts(capability_name(cap.cfgType));
    kernel::serial::puts(" bar=");
    kernel::serial::put_hex8(cap.bar);
    kernel::serial::puts(" offset=0x");
    kernel::serial::put_hex32(cap.offset);
    kernel::serial::puts(" length=0x");
    kernel::serial::put_hex32(cap.length);
    kernel::serial::puts(" rawBar=0x");
    kernel::serial::put_hex32(rawBar);
    kernel::serial::puts(" base=0x");
    kernel::serial::put_hex64(base);
    kernel::serial::puts(" barKind=");
    kernel::serial::puts(bar_kind_name(cap.bar, rawBar));
    kernel::serial::puts(" status=");
    kernel::serial::puts(status);
    kernel::serial::putc('\n');
}

static void log_pci_candidate(uint8_t bus, uint8_t device, uint8_t function,
                              uint16_t vendorId, uint16_t deviceId,
                              uint8_t revision, uint8_t classCode,
                              uint8_t subclass, uint8_t progIf,
                              uint8_t headerType, uint16_t command,
                              uint16_t status, uint16_t subsystemVendorId,
                              uint16_t subsystemDeviceId)
{
    kernel::serial::puts("[VIRTIO-GPU] PCI candidate ");
    kernel::serial::put_hex8(bus);
    kernel::serial::putc(':');
    kernel::serial::put_hex8(device);
    kernel::serial::putc('.');
    kernel::serial::put_hex8(function);
    kernel::serial::puts(" vendor=0x");
    kernel::serial::put_hex16(vendorId);
    kernel::serial::puts(" device=0x");
    kernel::serial::put_hex16(deviceId);
    kernel::serial::puts(" revision=0x");
    kernel::serial::put_hex8(revision);
    kernel::serial::puts(" class=0x");
    kernel::serial::put_hex8(classCode);
    kernel::serial::puts(" subclass=0x");
    kernel::serial::put_hex8(subclass);
    kernel::serial::puts(" progIf=0x");
    kernel::serial::put_hex8(progIf);
    kernel::serial::puts(" header=0x");
    kernel::serial::put_hex8(headerType);
    kernel::serial::puts(" command=0x");
    kernel::serial::put_hex16(command);
    kernel::serial::puts(" status=0x");
    kernel::serial::put_hex16(status);
    kernel::serial::puts(" subsystem=0x");
    kernel::serial::put_hex16(subsystemVendorId);
    kernel::serial::putc(':');
    kernel::serial::put_hex16(subsystemDeviceId);
    kernel::serial::putc('\n');
}

static bool parse_virtio_regions(ModernTransport* transport)
{
    if (transport == nullptr) {
        return false;
    }

    const uint8_t bus = transport->bus;
    const uint8_t device = transport->device;
    const uint8_t function = transport->function;

    if (!(msi::pci_config_read16(bus, device, function, 0x06) & 0x10)) {
        kernel::serial::puts("[VIRTIO-GPU] PCI capability list absent\n");
        return false;
    }

    uint8_t capPtr = static_cast<uint8_t>(msi::pci_config_read8(bus, device, function, 0x34) & 0xFCu);
    int guard = 64;
    uint32_t capabilityCount = 0;
    uint32_t vendorSpecificCount = 0;

    while (capPtr != 0 && guard-- > 0) {
        PciCapability cap{};
        cap.capId = msi::pci_config_read8(bus, device, function, capPtr);
        cap.nextPtr = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 1)) & 0xFCu;
        cap.capLen = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 2));
        cap.cfgType = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 3));
        cap.bar = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 4));
        cap.padding[0] = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 5));
        cap.padding[1] = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 6));
        cap.padding[2] = msi::pci_config_read8(bus, device, function, static_cast<uint8_t>(capPtr + 7));
        cap.offset = msi::pci_config_read32(bus, device, function, static_cast<uint8_t>(capPtr + 8));
        cap.length = msi::pci_config_read32(bus, device, function, static_cast<uint8_t>(capPtr + 12));
        ++capabilityCount;

        if (cap.capId != kPciCapabilityVendorSpecific) {
            kernel::serial::puts("[VIRTIO-GPU] PCI cap capPtr=0x");
            kernel::serial::put_hex8(capPtr);
            kernel::serial::puts(" capId=0x");
            kernel::serial::put_hex8(cap.capId);
            kernel::serial::puts(" next=0x");
            kernel::serial::put_hex8(cap.nextPtr);
            kernel::serial::puts(" status=ignored\n");
            capPtr = cap.nextPtr;
            continue;
        }

        ++vendorSpecificCount;

        PciRegion* region = nullptr;
        switch (cap.cfgType) {
        case pci::CAP_COMMON_CFG:
            region = &transport->commonCfg;
            break;
        case pci::CAP_NOTIFY_CFG:
            region = &transport->notifyCfg;
            break;
        case pci::CAP_ISR_CFG:
            region = &transport->isrCfg;
            break;
        case pci::CAP_DEVICE_CFG:
            region = &transport->deviceCfg;
            break;
        case pci::CAP_PCI_CFG:
            region = &transport->pciCfg;
            break;
        default:
            break;
        }

        uint32_t rawBar = 0xFFFFFFFFu;
        uint64_t base = 0;
        const bool resolved = read_bar_base(bus, device, function, cap.bar, &rawBar, &base);

        if (region != nullptr) {
            region->found = true;
            region->bar = cap.bar;
            region->offset = cap.offset;
            region->length = cap.length;
            region->rawBar = rawBar;
            if (resolved && !region->present) {
                region->present = true;
                region->base = base;
            }
        }

        if (cap.cfgType == pci::CAP_NOTIFY_CFG) {
            transport->notifyOffMultiplier = msi::pci_config_read32(bus, device, function, static_cast<uint8_t>(capPtr + 16));
            kernel::serial::puts("[VIRTIO-GPU] notify multiplier=0x");
            kernel::serial::put_hex32(transport->notifyOffMultiplier);
            kernel::serial::putc('\n');
        }

        const char* status = "skipped";
        if (region != nullptr) {
            status = resolved ? "resolved" : "malformed";
        }

        log_capability_entry(capPtr, cap, rawBar, base, status);
        capPtr = cap.nextPtr;
    }

    if (guard <= 0 && capPtr != 0) {
        kernel::serial::puts("[VIRTIO-GPU] PCI capability walk stopped at guard limit\n");
    }

    log_capability_inventory_line(*transport);
    kernel::serial::puts("[VIRTIO-GPU] PCI capability walk complete caps=");
    serial_put_u32_decimal(capabilityCount);
    kernel::serial::puts(" vendorSpecific=");
    serial_put_u32_decimal(vendorSpecificCount);
    kernel::serial::putc('\n');

    return transport->commonCfg.present &&
           transport->notifyCfg.present &&
           transport->isrCfg.present &&
           transport->deviceCfg.present;
}

static uint64_t common_cfg_addr(const ModernTransport& transport, uint32_t fieldOffset)
{
    return region_mmio_addr(transport.commonCfg, fieldOffset);
}

static uint64_t notify_cfg_addr(const ModernTransport& transport, uint32_t fieldOffset)
{
    return region_mmio_addr(transport.notifyCfg, fieldOffset);
}

static uint64_t device_cfg_addr(const ModernTransport& transport, uint32_t fieldOffset)
{
    return region_mmio_addr(transport.deviceCfg, fieldOffset);
}

static void log_status_transition(const char* label, uint8_t writtenStatus, uint8_t readbackStatus)
{
    kernel::serial::puts("[VIRTIO-GPU] Status ");
    kernel::serial::puts(label);
    kernel::serial::puts(" write=0x");
    kernel::serial::put_hex8(writtenStatus);
    kernel::serial::puts(" readback=0x");
    kernel::serial::put_hex8(readbackStatus);
    kernel::serial::putc('\n');
}

static bool reset_transport(ModernTransport& transport)
{
    log_init_step("reset_device begin");
    const uint64_t statusAddr = common_cfg_addr(transport, pci::COMMON_STATUS);
    mmio_write8(statusAddr, 0);
    uint8_t readback = 0xFFu;
    uint32_t spins = 0;
    for (; spins < kStatusPollLimit; ++spins) {
        readback = mmio_read8(statusAddr);
        if (readback == 0) {
            break;
        }
    }

    log_status_transition("reset", 0, readback);
    kernel::serial::puts("[VIRTIO-GPU] Reset poll spins=");
    serial_put_u32_decimal(spins);
    kernel::serial::puts(" timeout=");
    kernel::serial::puts(readback == 0 ? "no" : "yes");
    kernel::serial::putc('\n');

    transport.mmioDeviceStatus = readback;
    if (readback != 0) {
        transport.mmioStopReason = "device status did not clear after reset";
    }
    return readback == 0;
}

static uint64_t read_device_features(ModernTransport& transport)
{
    const uint64_t featureSelectAddr = common_cfg_addr(transport, pci::COMMON_DFSELECT);
    const uint64_t featureAddr = common_cfg_addr(transport, pci::COMMON_DF);

    mmio_write32(featureSelectAddr, 0);
    const uint64_t low = mmio_read32(featureAddr);

    mmio_write32(featureSelectAddr, 1);
    const uint64_t high = mmio_read32(featureAddr);

    transport.deviceFeaturesLow = static_cast<uint32_t>(low);
    transport.deviceFeaturesHigh = static_cast<uint32_t>(high);
    return low | (high << 32);
}

static void write_driver_features(ModernTransport& transport, uint64_t features)
{
    const uint64_t featureSelectAddr = common_cfg_addr(transport, pci::COMMON_GFSELECT);
    const uint64_t featureAddr = common_cfg_addr(transport, pci::COMMON_GF);

    mmio_write32(featureSelectAddr, 0);
    mmio_write32(featureAddr, static_cast<uint32_t>(features));

    mmio_write32(featureSelectAddr, 1);
    mmio_write32(featureAddr, static_cast<uint32_t>(features >> 32));
}

static uint8_t read_status(const ModernTransport& transport)
{
    return mmio_read8(common_cfg_addr(transport, pci::COMMON_STATUS));
}

static void write_status(ModernTransport& transport, uint8_t status)
{
    mmio_write8(common_cfg_addr(transport, pci::COMMON_STATUS), status);
}

static bool set_status_and_verify(ModernTransport& transport, uint8_t bits, const char* label)
{
    const uint8_t current = read_status(transport);
    const uint8_t updated = static_cast<uint8_t>(current | bits);
    write_status(transport, updated);
    const uint8_t readback = read_status(transport);
    log_status_transition(label, updated, readback);
    transport.mmioDeviceStatus = readback;
    return (readback & bits) == bits;
}

static void mark_device_failed(ModernTransport& transport, const char* reason)
{
    transport.mmioStopReason = reason;
    const uint8_t current = read_status(transport);
    const uint8_t updated = static_cast<uint8_t>(current | STATUS_FAILED);
    write_status(transport, updated);
    const uint8_t readback = read_status(transport);
    log_status_transition("FAILED", updated, readback);
    transport.mmioDeviceStatus = readback;
}

static bool negotiate_features(ModernTransport& transport)
{
    log_init_step("feature negotiation begin");

    const uint64_t deviceFeatures = read_device_features(transport);
    const uint64_t requestedFeatures = kCommonCfgRequiredFeatureBits;
    const uint64_t recognizedFeatures = deviceFeatures & requestedFeatures;
    const uint64_t rejectedFeatures = deviceFeatures & ~requestedFeatures;

    kernel::serial::puts("[VIRTIO-GPU] Feature bitmap rawLow=0x");
    kernel::serial::put_hex32(transport.deviceFeaturesLow);
    kernel::serial::puts(" rawHigh=0x");
    kernel::serial::put_hex32(transport.deviceFeaturesHigh);
    kernel::serial::puts(" raw=0x");
    kernel::serial::put_hex64(deviceFeatures);
    kernel::serial::putc('\n');

    kernel::serial::puts("[VIRTIO-GPU] Feature bitmap recognized=0x");
    kernel::serial::put_hex64(recognizedFeatures);
    kernel::serial::puts(" requested=0x");
    kernel::serial::put_hex64(requestedFeatures);
    kernel::serial::puts(" rejected=0x");
    kernel::serial::put_hex64(rejectedFeatures);
    kernel::serial::putc('\n');

    kernel::serial::puts("[VIRTIO-GPU] VIRTIO_F_VERSION_1 required=yes\n");

    if ((deviceFeatures & FEATURE_VERSION_1) == 0u) {
        mark_device_failed(transport, "VIRTIO_F_VERSION_1 missing from device feature bitmap");
        return false;
    }

    write_driver_features(transport, requestedFeatures);
    transport.negotiatedFeatures = requestedFeatures;

    if (!set_status_and_verify(transport, static_cast<uint8_t>(read_status(transport) | STATUS_FEATURES_OK),
                               "FEATURES_OK")) {
        mark_device_failed(transport, "device rejected FEATURES_OK after feature negotiation");
        return false;
    }

    const uint8_t negotiatedStatus = read_status(transport);
    if ((negotiatedStatus & STATUS_FEATURES_OK) == 0u) {
        kernel::serial::puts("[VIRTIO-GPU] FEATURES_OK readback cleared by device status=0x");
        kernel::serial::put_hex8(negotiatedStatus);
        kernel::serial::putc('\n');
        mark_device_failed(transport, "device cleared FEATURES_OK after feature negotiation");
        return false;
    }

    transport.featuresOk = true;
    kernel::serial::puts("[VIRTIO-GPU] Feature negotiation status=ok negotiated=0x");
    kernel::serial::put_hex64(requestedFeatures);
    kernel::serial::puts(" deviceFeatures=0x");
    kernel::serial::put_hex64(deviceFeatures);
    kernel::serial::puts(" rejected=0x");
    kernel::serial::put_hex64(rejectedFeatures);
    kernel::serial::putc('\n');
    return true;
}

static bool resolve_queue_notify_address(const ModernTransport& transport,
                                         uint64_t* notifyAddrOut,
                                         uint64_t* notifyOffsetBytesOut,
                                         const char** reasonOut)
{
    if (notifyAddrOut == nullptr || notifyOffsetBytesOut == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = "notify address resolver received a null output pointer";
        }
        return false;
    }

    if (!transport.notifyCfg.present) {
        if (reasonOut != nullptr) {
            *reasonOut = "notify config capability is unavailable";
        }
        return false;
    }

    if (transport.notifyOffMultiplier == 0u) {
        if (reasonOut != nullptr) {
            *reasonOut = "notify_off_multiplier is zero";
        }
        return false;
    }

    const uint64_t notifyOffsetBytes =
        static_cast<uint64_t>(transport.queueNotifyOff) *
        static_cast<uint64_t>(transport.notifyOffMultiplier);
    if (transport.queueNotifyOff != 0u &&
        (notifyOffsetBytes / static_cast<uint64_t>(transport.queueNotifyOff)) !=
            static_cast<uint64_t>(transport.notifyOffMultiplier)) {
        if (reasonOut != nullptr) {
            *reasonOut = "queue notify offset overflows address space";
        }
        return false;
    }

    uint64_t notifyBase = 0;
    if (transport.notifyCfg.mapped) {
        notifyBase = transport.notifyCfg.mappedVirtual;
    } else {
        if (transport.notifyCfg.base > (~0ULL - static_cast<uint64_t>(transport.notifyCfg.offset))) {
            if (reasonOut != nullptr) {
                *reasonOut = "notify config base overflows address space";
            }
            return false;
        }
        notifyBase = transport.notifyCfg.base + static_cast<uint64_t>(transport.notifyCfg.offset);
    }

    if (notifyBase > (~0ULL - notifyOffsetBytes)) {
        if (reasonOut != nullptr) {
            *reasonOut = "notify register address overflows address space";
        }
        return false;
    }

    if (notifyOffsetBytes > (~0ULL - static_cast<uint64_t>(sizeof(uint16_t)))) {
        if (reasonOut != nullptr) {
            *reasonOut = "notify register size overflows address space";
        }
        return false;
    }

    if (transport.notifyCfg.length != 0u &&
        notifyOffsetBytes + static_cast<uint64_t>(sizeof(uint16_t)) > transport.notifyCfg.length) {
        if (reasonOut != nullptr) {
            *reasonOut = "notify register address exceeds mapped BAR range";
        }
        return false;
    }

    *notifyOffsetBytesOut = notifyOffsetBytes;
    *notifyAddrOut = notifyBase + notifyOffsetBytes;
    return true;
}

static bool setup_control_queue(ModernTransport& transport)
{
    const uint64_t queueSelectAddr = common_cfg_addr(transport, pci::COMMON_Q_SELECT);
    const uint64_t queueSizeAddr = common_cfg_addr(transport, pci::COMMON_Q_SIZE);
    const uint64_t queueDescAddr = common_cfg_addr(transport, pci::COMMON_Q_DESC);
    const uint64_t queueAvailAddr = common_cfg_addr(transport, pci::COMMON_Q_AVAIL);
    const uint64_t queueUsedAddr = common_cfg_addr(transport, pci::COMMON_Q_USED);
    const uint64_t queueEnableAddr = common_cfg_addr(transport, pci::COMMON_Q_ENABLE);
    const uint64_t queueNotifyOffAddr = common_cfg_addr(transport, pci::COMMON_Q_NOTIFY_OFF);

    log_init_step("control queue begin");
    mmio_write16(queueSelectAddr, 0);

    const uint16_t queueEnableBefore = mmio_read16(queueEnableAddr);
    if (queueEnableBefore != 0u) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue already enabled before guest configuration\n");
        transport.mmioStopReason = "control queue already enabled before guest configuration";
        return false;
    }

    const uint16_t queueCount = mmio_read16(common_cfg_addr(transport, pci::COMMON_NUM_QUEUES));
    const uint16_t queueMax = mmio_read16(queueSizeAddr);
    transport.mmioNumQueues = queueCount;
    kernel::serial::puts("[VIRTIO-GPU] Common config queueCount=");
    serial_put_u32_decimal(queueCount);
    kernel::serial::puts(" queueMax=");
    serial_put_u32_decimal(queueMax);
    kernel::serial::putc('\n');

    if (queueMax < kMinControlQueueSize) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue unavailable\n");
        transport.mmioStopReason = "control queue unavailable";
        return false;
    }

    const uint16_t queueSize = choose_queue_size(queueMax);
    if (queueSize < kMinControlQueueSize) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue size too small\n");
        transport.mmioStopReason = "control queue size too small";
        return false;
    }

    if (!layout_control_queue(&transport.controlQueue, queueSize)) {
        kernel::serial::puts("[VIRTIO-GPU] Failed to lay out control queue\n");
        transport.mmioStopReason = "failed to lay out control queue";
        return false;
    }

    transport.queueSize = queueSize;
    transport.controlQueue.index = 0;
    transport.controlQueue.notifyOffset = 0;

    mmio_write16(queueSizeAddr, queueSize);
    mmio_write64(queueDescAddr, transport.controlQueue.descPhys);
    mmio_write64(queueAvailAddr, transport.controlQueue.availPhys);
    mmio_write64(queueUsedAddr, transport.controlQueue.usedPhys);
    const uint16_t queueNotifyOff = mmio_read16(queueNotifyOffAddr);
    transport.queueNotifyOff = queueNotifyOff;
    transport.controlQueue.notifyOffset = queueNotifyOff;

    uint64_t notifyAddr = 0;
    uint64_t notifyOffsetBytes = 0;
    const char* notifyReason = nullptr;
    if (!resolve_queue_notify_address(transport, &notifyAddr, &notifyOffsetBytes, &notifyReason)) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue notify validation blocked: ");
        kernel::serial::puts(notifyReason != nullptr ? notifyReason : "n/a");
        kernel::serial::putc('\n');
        transport.mmioStopReason = notifyReason != nullptr ? notifyReason : "control queue notify validation blocked";
        return false;
    }

    mmio_write16(queueEnableAddr, 1);

    const uint16_t queueEnableAfter = mmio_read16(queueEnableAddr);

    if (queueEnableAfter == 0u) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue enable readback is still 0\n");
        transport.mmioStopReason = "control queue enable readback is still 0";
        return false;
    }

    kernel::serial::puts("[VIRTIO-GPU] Control queue ready size=");
    serial_put_u32_decimal(queueSize);
    kernel::serial::puts(" queueEnable=");
    kernel::serial::puts(queueEnableAfter != 0u ? "yes" : "no");
    kernel::serial::puts(" queueNotifyOff=");
    serial_put_u32_decimal(queueNotifyOff);
    kernel::serial::puts(" notifyOffMultiplier=");
    serial_put_u32_decimal(transport.notifyOffMultiplier);
    kernel::serial::puts(" notifyOffsetBytes=");
    serial_put_u64_decimal(notifyOffsetBytes);
    kernel::serial::puts(" notifyAddr=0x");
    kernel::serial::put_hex64(notifyAddr);
    kernel::serial::puts(" descVirt=0x");
    kernel::serial::put_hex64(reinterpret_cast<uint64_t>(transport.controlQueue.desc));
    kernel::serial::puts(" desc=0x");
    kernel::serial::put_hex64(transport.controlQueue.descPhys);
    kernel::serial::puts(" availVirt=0x");
    kernel::serial::put_hex64(reinterpret_cast<uint64_t>(transport.controlQueue.avail));
    kernel::serial::puts(" avail=0x");
    kernel::serial::put_hex64(transport.controlQueue.availPhys);
    kernel::serial::puts(" usedVirt=0x");
    kernel::serial::put_hex64(reinterpret_cast<uint64_t>(transport.controlQueue.used));
    kernel::serial::puts(" used=0x");
    kernel::serial::put_hex64(transport.controlQueue.usedPhys);
    kernel::serial::puts(" alignment=4096");
    kernel::serial::putc('\n');

    transport.controlQueueReady = queueEnableAfter != 0u;
    return true;
}

static bool queue_notify(ModernTransport& transport, uint16_t queueIndex)
{
    uint64_t notifyAddr = 0;
    uint64_t notifyOffsetBytes = 0;
    const char* reason = nullptr;
    if (!resolve_queue_notify_address(transport, &notifyAddr, &notifyOffsetBytes, &reason)) {
        kernel::serial::puts("[VIRTIO-GPU] Notify address blocked: ");
        kernel::serial::puts(reason != nullptr ? reason : "n/a");
        kernel::serial::putc('\n');
        return false;
    }

    (void)notifyOffsetBytes;
    mmio_write16(notifyAddr, queueIndex);
    return true;
}

static bool queue_alloc_descriptor(Virtqueue& queue, uint16_t* descriptorIndexOut)
{
    if (descriptorIndexOut == nullptr || queue.numFree == 0u || queue.freeHead == kInvalidDescriptorIndex) {
        return false;
    }

    const uint16_t descriptorIndex = queue.freeHead;
    if (descriptorIndex >= queue.size) {
        return false;
    }

    queue.freeHead = queue.desc[descriptorIndex].next;
    queue.desc[descriptorIndex].next = kInvalidDescriptorIndex;
    queue.desc[descriptorIndex].flags = 0;
    queue.desc[descriptorIndex].len = 0;
    queue.desc[descriptorIndex].addr = 0;
    queue.numFree = static_cast<uint16_t>(queue.numFree - 1u);
    *descriptorIndexOut = descriptorIndex;
    return true;
}

static void queue_release_descriptor(Virtqueue& queue, uint16_t descriptorIndex)
{
    if (descriptorIndex >= queue.size) {
        return;
    }

    queue.desc[descriptorIndex].addr = 0;
    queue.desc[descriptorIndex].len = 0;
    queue.desc[descriptorIndex].flags = 0;
    queue.desc[descriptorIndex].next = queue.freeHead;
    queue.freeHead = descriptorIndex;
    if (queue.numFree < queue.size) {
        queue.numFree = static_cast<uint16_t>(queue.numFree + 1u);
    }
}

static bool wait_for_control_completion(Virtqueue& queue,
                                        uint16_t expectedUsedIdx,
                                        uint16_t headDescriptor,
                                        const char* commandName,
                                        uint16_t* usedLengthOut,
                                        const VringUsedElem** usedElemOut,
                                        bool* completionKnownOut,
                                        const char** failureReasonOut)
{
    if (usedLengthOut != nullptr) {
        *usedLengthOut = 0;
    }
    if (usedElemOut != nullptr) {
        *usedElemOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    uint32_t spin = 0;
    while (queue.used->idx == expectedUsedIdx && spin < kResponseSpinLimit) {
        MEMORY_BARRIER();
        ++spin;
    }

    if (queue.used->idx == expectedUsedIdx) {
        if (!s_liveCommandLoggingSuppressed) {
            kernel::serial::puts("[VIRTIO-GPU] ");
            kernel::serial::puts(commandName != nullptr ? commandName : "control command");
            kernel::serial::puts(" timed out usedIdx=");
            serial_put_u32_decimal(expectedUsedIdx);
            kernel::serial::puts(" spins=");
            serial_put_u32_decimal(spin);
            kernel::serial::putc('\n');
        }
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control command timed out";
        }
        return false;
    }

    const uint16_t completedUsedIdx = queue.used->idx;
    const VringUsedElem& usedElem = queue.used->ring[expectedUsedIdx % queue.size];
    if (usedElemOut != nullptr) {
        *usedElemOut = &usedElem;
    }
    if (usedLengthOut != nullptr) {
        *usedLengthOut = static_cast<uint16_t>(usedElem.len);
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = (usedElem.id == headDescriptor);
    }

    if (!s_liveCommandLoggingSuppressed) {
        kernel::serial::puts("[VIRTIO-GPU] ");
        kernel::serial::puts(commandName != nullptr ? commandName : "control command");
        kernel::serial::puts(" completion usedIdx=");
        serial_put_u32_decimal(completedUsedIdx);
        kernel::serial::puts(" usedLen=");
        serial_put_u32_decimal(usedElem.len);
        kernel::serial::puts(" headDescriptor=");
        serial_put_u32_decimal(usedElem.id);
        kernel::serial::putc('\n');
    }

    if (usedElem.id != headDescriptor) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control command completed on unexpected descriptor";
        }
        return false;
    }

    return true;
}

static bool submit_control_command_sync(ModernTransport& transport,
                                        const char* commandName,
                                        uint32_t commandType,
                                        const void* request,
                                        size_t requestLen,
                                        void* response,
                                        size_t responseLen,
                                        uint32_t expectedResponseType,
                                        const char** failureReasonOut,
                                        bool* completionKnownOut)
{
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    Virtqueue& queue = transport.controlQueue;
    if (queue.desc == nullptr || queue.avail == nullptr || queue.used == nullptr || queue.size < kMinControlQueueSize) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control queue is not ready";
        }
        return false;
    }

    if (request == nullptr || response == nullptr || requestLen < sizeof(CtrlHeader) || responseLen < sizeof(CtrlHeader)) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control command buffers are invalid";
        }
        return false;
    }

    const CtrlHeader* requestHeader = reinterpret_cast<const CtrlHeader*>(request);
    if (requestHeader->type != commandType) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control command type mismatch";
        }
        return false;
    }

    if (queue.numFree < 2u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control queue has insufficient free descriptors";
        }
        return false;
    }

    uint16_t requestDescriptor = kInvalidDescriptorIndex;
    uint16_t responseDescriptor = kInvalidDescriptorIndex;
    if (!queue_alloc_descriptor(queue, &requestDescriptor) ||
        !queue_alloc_descriptor(queue, &responseDescriptor)) {
        if (requestDescriptor != kInvalidDescriptorIndex) {
            queue_release_descriptor(queue, requestDescriptor);
        }
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control queue descriptor allocation failed";
        }
        return false;
    }

    memzero(response, responseLen);
    queue.desc[requestDescriptor].addr = dma_address(request);
    queue.desc[requestDescriptor].len = static_cast<uint32_t>(requestLen);
    queue.desc[requestDescriptor].flags = VRING_DESC_F_NEXT;
    queue.desc[requestDescriptor].next = responseDescriptor;
    queue.desc[responseDescriptor].addr = dma_address(response);
    queue.desc[responseDescriptor].len = static_cast<uint32_t>(responseLen);
    queue.desc[responseDescriptor].flags = VRING_DESC_F_WRITE;
    queue.desc[responseDescriptor].next = 0;

    const uint16_t expectedUsedIdx = queue.lastUsedIdx;
    const uint16_t slot = static_cast<uint16_t>(queue.avail->idx % queue.size);
    queue.avail->ring[slot] = requestDescriptor;
    MEMORY_BARRIER();
    queue.avail->idx = static_cast<uint16_t>(queue.avail->idx + 1u);
    MEMORY_BARRIER();

    if (!queue_notify(transport, 0)) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "notify address validation failed";
        }
        return false;
    }

    const VringUsedElem* usedElem = nullptr;
    uint16_t usedLen = 0;
    bool completionKnown = false;
    if (!wait_for_control_completion(queue,
                                     expectedUsedIdx,
                                     requestDescriptor,
                                     commandName,
                                     &usedLen,
                                     &usedElem,
                                     &completionKnown,
                                     failureReasonOut)) {
        if (completionKnown && responseDescriptor != kInvalidDescriptorIndex) {
            queue_release_descriptor(queue, responseDescriptor);
            queue_release_descriptor(queue, requestDescriptor);
            queue.lastUsedIdx = queue.used->idx;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    if (usedLen < responseLen) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "control command returned a short response";
        }
        if (responseDescriptor != kInvalidDescriptorIndex) {
            queue_release_descriptor(queue, responseDescriptor);
            queue_release_descriptor(queue, requestDescriptor);
            queue.lastUsedIdx = queue.used->idx;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = true;
        }
        return false;
    }

    const CtrlHeader* responseHeader = reinterpret_cast<const CtrlHeader*>(response);
    if (responseHeader->type != expectedResponseType) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "unexpected control response type";
        }
        if (responseDescriptor != kInvalidDescriptorIndex) {
            queue_release_descriptor(queue, responseDescriptor);
            queue_release_descriptor(queue, requestDescriptor);
            queue.lastUsedIdx = queue.used->idx;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = true;
        }
        return false;
    }

    if (responseDescriptor != kInvalidDescriptorIndex) {
        queue_release_descriptor(queue, responseDescriptor);
        queue_release_descriptor(queue, requestDescriptor);
        queue.lastUsedIdx = queue.used->idx;
    }

    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }

    return true;
}

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
static void log_display_info_snapshot(const DeviceState& state,
                                      const char* phaseLabel,
                                      const ModernTransport& transport,
                                      uint32_t deviceConfigScanouts,
                                      uint32_t deviceConfigCapsets,
                                      uint32_t qemuMaxOutputsIntent,
                                      bool logDeviceConfigLine)
{
    if (logDeviceConfigLine) {
        kernel::serial::puts("[VIRTIO-GPU] ");
        if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
            kernel::serial::puts(phaseLabel);
            kernel::serial::puts(" ");
        }
        kernel::serial::puts("Device config numScanouts=");
        serial_put_u32_decimal(deviceConfigScanouts);
        kernel::serial::puts(" numCapsets=");
        serial_put_u32_decimal(deviceConfigCapsets);
        kernel::serial::putc('\n');
    }

    kernel::serial::puts("[VIRTIO-GPU] ");
    if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
        kernel::serial::puts(phaseLabel);
        kernel::serial::puts(" ");
    }
    kernel::serial::puts("GET_DISPLAY_INFO protocolSlots=");
    serial_put_u32_decimal(transport.displayInfoSlots);
    kernel::serial::puts(" enabledScanouts=");
    serial_put_u32_decimal(transport.enabledScanouts);
    kernel::serial::puts(" disabledScanouts=");
    serial_put_u32_decimal(transport.disabledScanouts);
    kernel::serial::puts(" deviceConfigNumScanouts=");
    serial_put_u32_decimal(deviceConfigScanouts);
    kernel::serial::puts(" qemuMaxOutputsIntent=");
    serial_put_u32_decimal(qemuMaxOutputsIntent);
    kernel::serial::putc('\n');

    for (uint32_t i = 0; i < MAX_SCANOUTS; ++i) {
        const DisplayInfo& mode = state.device.displays[i];
        kernel::serial::puts("[VIRTIO-GPU] ");
        if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
            kernel::serial::puts(phaseLabel);
            kernel::serial::puts(" ");
        }
        kernel::serial::puts("scanout[");
        serial_put_u32_decimal(i);
        kernel::serial::puts("] enabled=");
        kernel::serial::puts(mode.enabled ? "yes" : "no");
        kernel::serial::puts(" x=");
        serial_put_u32_decimal(mode.x);
        kernel::serial::puts(" y=");
        serial_put_u32_decimal(mode.y);
        kernel::serial::puts(" width=");
        serial_put_u32_decimal(mode.width);
        kernel::serial::puts(" height=");
        serial_put_u32_decimal(mode.height);
        kernel::serial::putc('\n');
    }
}

static bool submit_display_info_request(DeviceState& state,
                                        const char* phaseLabel,
                                        bool logDeviceConfigLine)
{
    ModernTransport& transport = state.transport;
    Virtqueue& queue = transport.controlQueue;

    if (queue.desc == nullptr || queue.avail == nullptr || queue.used == nullptr || queue.size < kMinControlQueueSize) {
        kernel::serial::puts("[VIRTIO-GPU] GET_DISPLAY_INFO blocked: control queue is not ready\n");
        transport.mmioStopReason = "control queue is not ready";
        return false;
    }

    kernel::serial::puts("[VIRTIO-GPU] ");
    if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
        kernel::serial::puts(phaseLabel);
        kernel::serial::puts(" ");
    }
    kernel::serial::puts("GET_DISPLAY_INFO begin\n");

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));

    CtrlHeader* request = reinterpret_cast<CtrlHeader*>(&s_commandBuffer[0]);
    request->type = CMD_GET_DISPLAY_INFO;
    request->flags = 0;
    request->fenceId = 0;
    request->ctxId = 0;
    request->padding = 0;

    const char* failureReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     phaseLabel != nullptr ? phaseLabel : "GET_DISPLAY_INFO",
                                     CMD_GET_DISPLAY_INFO,
                                     request,
                                     sizeof(CtrlHeader),
                                     &s_responseBuffer[0],
                                     sizeof(RespDisplayInfo),
                                     RESP_OK_DISPLAY_INFO,
                                     &failureReason,
                                     &completionKnown)) {
        kernel::serial::puts("[VIRTIO-GPU] ");
        if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
            kernel::serial::puts(phaseLabel);
            kernel::serial::puts(" ");
        }
        kernel::serial::puts("GET_DISPLAY_INFO failed: ");
        kernel::serial::puts(failureReason != nullptr ? failureReason : "n/a");
        kernel::serial::putc('\n');
        transport.mmioStopReason = failureReason != nullptr ? failureReason : "GET_DISPLAY_INFO failed";
        return false;
    }

    const RespDisplayInfo* response = reinterpret_cast<const RespDisplayInfo*>(&s_responseBuffer[0]);
    kernel::serial::puts("[VIRTIO-GPU] ");
    if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
        kernel::serial::puts(phaseLabel);
        kernel::serial::puts(" ");
    }
    kernel::serial::puts("GET_DISPLAY_INFO response type=0x");
    kernel::serial::put_hex32(response->header.type);
    kernel::serial::putc('\n');

    if (response->header.type != RESP_OK_DISPLAY_INFO) {
        kernel::serial::puts("[VIRTIO-GPU] ");
        if (phaseLabel != nullptr && phaseLabel[0] != '\0') {
            kernel::serial::puts(phaseLabel);
            kernel::serial::puts(" ");
        }
        kernel::serial::puts("GET_DISPLAY_INFO returned unexpected response type\n");
        transport.mmioStopReason = "GET_DISPLAY_INFO returned unexpected response type";
        return false;
    }

    uint32_t deviceConfigScanouts = transport.mmioDeviceScanouts;
    uint32_t deviceConfigCapsets = transport.mmioDeviceCapsets;
    if (logDeviceConfigLine && transport.deviceCfg.present) {
        deviceConfigScanouts = mmio_read32(device_cfg_addr(transport, 0x08));
        deviceConfigCapsets = mmio_read32(device_cfg_addr(transport, 0x0C));
        transport.mmioDeviceScanouts = deviceConfigScanouts;
        transport.mmioDeviceCapsets = deviceConfigCapsets;
    }

    transport.displayInfoSlots = MAX_SCANOUTS;
    transport.enabledScanouts = 0;
    transport.disabledScanouts = 0;

    for (uint32_t i = 0; i < MAX_SCANOUTS; ++i) {
        const DisplayOne& mode = response->pmodes[i];
        state.device.displays[i].x = mode.rect.x;
        state.device.displays[i].y = mode.rect.y;
        state.device.displays[i].width = mode.rect.width;
        state.device.displays[i].height = mode.rect.height;
        state.device.displays[i].enabled = mode.enabled != 0u;
        if (mode.enabled != 0u) {
            ++transport.enabledScanouts;
        } else {
            ++transport.disabledScanouts;
        }
    }

    state.device.numScanouts = transport.enabledScanouts;
    state.device.features = transport.negotiatedFeatures;
    log_display_info_snapshot(state,
                              phaseLabel,
                              transport,
                              deviceConfigScanouts,
                              deviceConfigCapsets,
                              kDiagnosticQemuMaxOutputsIntent,
                              logDeviceConfigLine);

    transport.mmioStopReason = "GET_DISPLAY_INFO milestone complete";
    return true;
}

// Event rescans use the same bounded control queue and GET_DISPLAY_INFO
// command as probe/presentation, but parse into a detached snapshot.  This is
// deliberately separate from submit_display_info_request: the observer must
// not overwrite the active device/inventory state before the diff is
// published.
static bool submit_display_info_snapshot_request(DeviceState& state,
                                                  const char* phaseLabel,
                                                  VirtioGpuProtocolDisplaySnapshot& snapshot)
{
    snapshot = VirtioGpuProtocolDisplaySnapshot{};
    ModernTransport& transport = state.transport;
    Virtqueue& queue = transport.controlQueue;
    if (queue.desc == nullptr || queue.avail == nullptr || queue.used == nullptr || queue.size < kMinControlQueueSize) {
        copy_event_text(s_displayEventObserver.lastError,
                        sizeof(s_displayEventObserver.lastError),
                        "GET_DISPLAY_INFO control queue is not ready");
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    CtrlHeader* request = reinterpret_cast<CtrlHeader*>(&s_commandBuffer[0]);
    request->type = CMD_GET_DISPLAY_INFO;
    request->flags = 0u;
    request->fenceId = 0u;
    request->ctxId = 0u;
    request->padding = 0u;

    const char* failureReason = nullptr;
    bool completionKnown = false;
    const bool submitted = submit_control_command_sync(
        transport,
        phaseLabel != nullptr ? phaseLabel : "GET_DISPLAY_INFO rescan",
        CMD_GET_DISPLAY_INFO,
        request,
        sizeof(CtrlHeader),
        &s_responseBuffer[0],
        sizeof(RespDisplayInfo),
        RESP_OK_DISPLAY_INFO,
        &failureReason,
        &completionKnown);
    (void)completionKnown;
    if (!submitted) {
        copy_event_text(s_displayEventObserver.lastError,
                        sizeof(s_displayEventObserver.lastError),
                        failureReason != nullptr ? failureReason : "GET_DISPLAY_INFO failed");
        kernel::serial::puts("[VIRTIO-GPU] VirtioGPU display rescan: result=failed reason=");
        kernel::serial::puts(failureReason != nullptr ? failureReason : "GET_DISPLAY_INFO failed");
        kernel::serial::putc('\n');
        return false;
    }

    const RespDisplayInfo* response = reinterpret_cast<const RespDisplayInfo*>(&s_responseBuffer[0]);
    if (response->header.type != RESP_OK_DISPLAY_INFO) {
        copy_event_text(s_displayEventObserver.lastError,
                        sizeof(s_displayEventObserver.lastError),
                        "GET_DISPLAY_INFO response validation failed");
        return false;
    }

    snapshot.protocolSlotCount = MAX_SCANOUTS;
    snapshot.deviceNumScanouts = transport.mmioDeviceScanouts;
    snapshot.deviceNumCapsets = transport.mmioDeviceCapsets;
    for (uint32_t i = 0u; i < MAX_SCANOUTS; ++i) {
        const DisplayOne& mode = response->pmodes[i];
        DisplayInfo& output = snapshot.slots[i];
        output.x = mode.rect.x;
        output.y = mode.rect.y;
        output.width = mode.rect.width;
        output.height = mode.rect.height;
        output.enabled = mode.enabled != 0u;
        if (output.enabled) ++snapshot.enabledCount;
        else ++snapshot.disabledCount;
    }
    snapshot.responseValid = 1u;
    kernel::serial::puts("[VIRTIO-GPU] VirtioGPU display rescan: GET_DISPLAY_INFO result=success protocolSlots=");
    serial_put_u32_decimal(snapshot.protocolSlotCount);
    kernel::serial::puts(" deviceNumScanouts=");
    serial_put_u32_decimal(snapshot.deviceNumScanouts);
    kernel::serial::puts(" connectorEnabled=");
    serial_put_u32_decimal(snapshot.enabledCount);
    kernel::serial::putc('\n');
    for (uint32_t i = 0u; i < snapshot.protocolSlotCount; ++i) {
        const DisplayInfo& output = snapshot.slots[i];
        kernel::serial::puts("[VIRTIO-GPU] VirtioGPU display rescan: scanout=");
        serial_put_u32_decimal(i);
        kernel::serial::puts(" connectorEnabled=");
        kernel::serial::puts(output.enabled ? "yes" : "no");
        kernel::serial::puts(" rect=");
        serial_put_u32_decimal(output.x);
        kernel::serial::putc(',');
        serial_put_u32_decimal(output.y);
        kernel::serial::putc(' ');
        serial_put_u32_decimal(output.width);
        kernel::serial::putc('x');
        serial_put_u32_decimal(output.height);
        kernel::serial::putc('\n');
    }
    return true;
}

static const VirtioGpuScanoutState* active_scanout_state(uint32_t scanoutId)
{
    for (uint32_t i = 0u; i < s_probeOutcome.outputInventory.outputs.size(); ++i) {
        const VirtioGpuScanoutState& output = s_probeOutcome.outputInventory.outputs[i];
        if (output.scanoutId == scanoutId) return &output;
    }
    return nullptr;
}

static bool protocol_slot_reported(const DisplayInfo& slot)
{
    return slot.enabled || slot.width != 0u || slot.height != 0u;
}

static void fill_detected_output(VirtioGpuDetectedOutput& detected,
                                 uint32_t scanoutId,
                                 const DisplayInfo& protocol,
                                 const VirtioGpuScanoutState* active)
{
    detected = VirtioGpuDetectedOutput{};
    detected.scanoutId = scanoutId;
    detected.reported = protocol_slot_reported(protocol) ? 1u : 0u;
    detected.connectorEnabled = protocol.enabled ? 1u : 0u;
    detected.reportedX = static_cast<int32_t>(protocol.x);
    detected.reportedY = static_cast<int32_t>(protocol.y);
    detected.reportedWidth = static_cast<int32_t>(protocol.width);
    detected.reportedHeight = static_cast<int32_t>(protocol.height);
    copy_event_identity(detected.stableIdentity, "display-", scanoutId + 1u);
    if (active == nullptr) return;

    detected.resourceId = active->resourceId;
    detected.operational = active->active || active->isOperational(s_probeOutcome.deviceConfigNumScanouts) ? 1u : 0u;
    detected.presentationReady = active->presentReady ? 1u : 0u;
    detected.assignedX = active->assignedX;
    detected.assignedY = active->assignedY;
    detected.assignedWidth = active->assignedWidth;
    detected.assignedHeight = active->assignedHeight;
    detected.currentModeWidth = active->assignedWidth > 0 ? static_cast<uint32_t>(active->assignedWidth) : 0u;
    detected.currentModeHeight = active->assignedHeight > 0 ? static_cast<uint32_t>(active->assignedHeight) : 0u;
    copy_event_text(detected.currentModeId,
                    sizeof(detected.currentModeId),
                    qemu_logical_mode_id(detected.currentModeWidth, detected.currentModeHeight));
}

static bool build_detected_topology_snapshot(
    DeviceState& state,
    const VirtioGpuConfigSnapshot& config,
    const VirtioGpuProtocolDisplaySnapshot* protocol,
    VirtioGpuDetectedTopologySnapshot& snapshot)
{
    snapshot = VirtioGpuDetectedTopologySnapshot{};
    snapshot.version = gxos::display::kVirtioGpuDisplayEventRecordVersion;
    snapshot.sourceBackend = 1u;
    snapshot.configGeneration = config.finalGeneration;
    snapshot.numScanouts = config.numScanouts;
    snapshot.observedTick = kernel::pit::ticks();
    copy_event_text(snapshot.sourceBackendName, sizeof(snapshot.sourceBackendName), "virtio-gpu");
    copy_event_text(snapshot.deviceIdentity, sizeof(snapshot.deviceIdentity), "gpu0");

    if (snapshot.numScanouts > gxos::display::kVirtioGpuDisplayEventMaxScanouts) {
        s_displayEventObserver.lastError[0] = '\0';
        copy_event_text(s_displayEventObserver.lastError,
                        sizeof(s_displayEventObserver.lastError),
                        "device num_scanouts exceeds bounded topology capacity");
        return false;
    }

    snapshot.outputCount = snapshot.numScanouts;
    for (uint32_t i = 0u; i < snapshot.outputCount; ++i) {
        DisplayInfo slot{};
        if (protocol != nullptr && i < protocol->protocolSlotCount) {
            slot = protocol->slots[i];
        } else if (i < MAX_SCANOUTS) {
            slot = state.device.displays[i];
        }
        fill_detected_output(snapshot.outputs[i], i, slot, active_scanout_state(i));
    }
    return true;
}

static const VirtioGpuDetectedOutput* topology_output(
    const VirtioGpuDetectedTopologySnapshot& snapshot,
    const char* identity)
{
    for (uint32_t i = 0u; i < snapshot.outputCount && i < gxos::display::kVirtioGpuDisplayEventMaxScanouts; ++i) {
        if (text_equal_bounded(snapshot.outputs[i].stableIdentity,
                               identity,
                               gxos::display::kVirtioGpuDisplayEventIdentityBytes)) {
            return &snapshot.outputs[i];
        }
    }
    return nullptr;
}

static void add_topology_identity(char destination[][gxos::display::kVirtioGpuDisplayEventIdentityBytes],
                                  uint32_t& count,
                                  const char* identity)
{
    if (identity == nullptr || count >= gxos::display::kVirtioGpuDisplayEventMaxScanouts) return;
    copy_event_text(destination[count], gxos::display::kVirtioGpuDisplayEventIdentityBytes, identity);
    ++count;
}

static bool detected_output_geometry_equal(const VirtioGpuDetectedOutput& left,
                                           const VirtioGpuDetectedOutput& right)
{
    return left.reported == right.reported &&
        left.reportedX == right.reportedX && left.reportedY == right.reportedY &&
        left.reportedWidth == right.reportedWidth && left.reportedHeight == right.reportedHeight;
}

static bool detected_output_runtime_equal(const VirtioGpuDetectedOutput& left,
                                          const VirtioGpuDetectedOutput& right)
{
    return left.operational == right.operational &&
        left.presentationReady == right.presentationReady &&
        left.resourceId == right.resourceId &&
        left.assignedX == right.assignedX && left.assignedY == right.assignedY &&
        left.assignedWidth == right.assignedWidth && left.assignedHeight == right.assignedHeight;
}

static VirtioGpuDisplayTopologyChange diff_detected_topologies(
    const VirtioGpuDetectedTopologySnapshot& oldTopology,
    const VirtioGpuDetectedTopologySnapshot& newTopology,
    bool injectedEvent,
    bool reasserted,
    bool genuineDeviceEvent)
{
    VirtioGpuDisplayTopologyChange change{};
    change.version = gxos::display::kVirtioGpuDisplayEventRecordVersion;
    change.oldGeneration = oldTopology.configGeneration;
    change.newGeneration = newTopology.configGeneration;
    change.oldScanoutCount = oldTopology.numScanouts;
    change.newScanoutCount = newTopology.numScanouts;
    change.injectedEvent = injectedEvent ? 1u : 0u;
    change.reasserted = reasserted ? 1u : 0u;
    change.genuineDeviceEvent = genuineDeviceEvent ? 1u : 0u;
    change.injectedTopologyGeneration = newTopology.configGeneration;
    copy_event_text(change.source, sizeof(change.source), injectedEvent ? "injected-test" :
        (genuineDeviceEvent ? "virtio-gpu-device-event" : "explicit-refresh"));

    for (uint32_t i = 0u; i < oldTopology.outputCount && i < gxos::display::kVirtioGpuDisplayEventMaxScanouts; ++i) {
        const VirtioGpuDetectedOutput& oldOutput = oldTopology.outputs[i];
        const VirtioGpuDetectedOutput* newOutput = topology_output(newTopology, oldOutput.stableIdentity);
        if (newOutput == nullptr || (oldOutput.reported && !newOutput->reported)) {
            add_topology_identity(change.removedOutputIdentities,
                                  change.removedOutputCount,
                                  oldOutput.stableIdentity);
            if (oldOutput.operational) change.activeConfigurationAffected = 1u;
            change.persistedConfigurationAffected = 1u;
            continue;
        }
        const bool connectorChanged = oldOutput.connectorEnabled != newOutput->connectorEnabled;
        const bool geometryChanged = !detected_output_geometry_equal(oldOutput, *newOutput);
        if (connectorChanged) ++change.connectorEnabledChangeCount;
        if (geometryChanged) ++change.preferredGeometryChangeCount;
        if (connectorChanged || geometryChanged) ++change.changedOutputCount;
        if (oldOutput.operational && newOutput->operational && detected_output_runtime_equal(oldOutput, *newOutput)) {
            ++change.unchangedOperationalOutputCount;
        }
    }

    for (uint32_t i = 0u; i < newTopology.outputCount && i < gxos::display::kVirtioGpuDisplayEventMaxScanouts; ++i) {
        const VirtioGpuDetectedOutput& newOutput = newTopology.outputs[i];
        const VirtioGpuDetectedOutput* oldOutput = topology_output(oldTopology, newOutput.stableIdentity);
        if (oldOutput == nullptr || (!oldOutput->reported && newOutput.reported)) {
            add_topology_identity(change.addedOutputIdentities,
                                  change.addedOutputCount,
                                  newOutput.stableIdentity);
            change.persistedConfigurationAffected = 1u;
        }
    }

    const bool hasAddRemove = change.addedOutputCount != 0u || change.removedOutputCount != 0u;
    const bool hasGeometry = change.preferredGeometryChangeCount != 0u;
    const bool hasConnector = change.connectorEnabledChangeCount != 0u;
    change.requiresResourceRebuild = (hasAddRemove || hasGeometry) ? 1u : 0u;
    change.requiresLayoutReconciliation = hasAddRemove ? 1u : 0u;
    change.metadataOnly = (!hasAddRemove && !change.activeConfigurationAffected) ? 1u : 0u;
    change.supportedAutomatically = 0u;
    change.changeType = hasAddRemove
        ? (change.addedOutputCount != 0u && change.removedOutputCount != 0u
            ? static_cast<uint32_t>(gxos::display::VirtioGpuTopologyChangeType::Mixed)
            : change.addedOutputCount != 0u
                ? static_cast<uint32_t>(gxos::display::VirtioGpuTopologyChangeType::OutputAddition)
                : static_cast<uint32_t>(gxos::display::VirtioGpuTopologyChangeType::OutputRemoval))
        : (change.changedOutputCount != 0u
            ? static_cast<uint32_t>(gxos::display::VirtioGpuTopologyChangeType::MetadataOnly)
            : static_cast<uint32_t>(gxos::display::VirtioGpuTopologyChangeType::None));

    if (change.removedOutputCount != 0u) {
        copy_event_text(change.classification, sizeof(change.classification), "potential-topology-removal");
        copy_event_text(change.recommendedAction, sizeof(change.recommendedAction),
                        "Keep last-known-good output; user/service reconciliation required");
        copy_event_text(change.reason, sizeof(change.reason),
                        "An observed output became unavailable; active resources remain retained");
    } else if (change.addedOutputCount != 0u) {
        copy_event_text(change.classification, sizeof(change.classification), "potential-topology-addition");
        copy_event_text(change.recommendedAction, sizeof(change.recommendedAction),
                        "Review Display Options before adding the output");
        copy_event_text(change.reason, sizeof(change.reason),
                        "A previously absent scanout is reported; no resource was created automatically");
    } else if (hasGeometry) {
        copy_event_text(change.classification, sizeof(change.classification), "preferred-geometry-change");
        copy_event_text(change.recommendedAction, sizeof(change.recommendedAction),
                        "Review Display Options; active logical modes remain unchanged");
        copy_event_text(change.reason, sizeof(change.reason),
                        "Preferred geometry changed without resizing the active logical resource");
    } else if (hasConnector) {
        copy_event_text(change.classification, sizeof(change.classification), "metadata-only");
        copy_event_text(change.recommendedAction, sizeof(change.recommendedAction),
                        "Review detected display status");
        copy_event_text(change.reason, sizeof(change.reason),
                        "Connector state changed; active guest configuration remains unchanged");
    } else {
        copy_event_text(change.classification, sizeof(change.classification), "no-change");
        copy_event_text(change.recommendedAction, sizeof(change.recommendedAction), "No action required");
        copy_event_text(change.reason, sizeof(change.reason), "GET_DISPLAY_INFO topology matched the previous snapshot");
    }
    if (injectedEvent) {
        copy_event_text(change.injectedChangeType, sizeof(change.injectedChangeType),
            change.addedOutputCount != 0u ? "output-addition" :
            change.removedOutputCount != 0u ? "output-removal" :
            change.preferredGeometryChangeCount != 0u ? "preferred-geometry" : "connector-state");
    }
    return change;
}

static void publish_detected_topology_change(
    const VirtioGpuDetectedTopologySnapshot& oldTopology,
    const VirtioGpuDetectedTopologySnapshot& newTopology,
    bool injectedEvent,
    bool reasserted,
    bool genuineDeviceEvent = false)
{
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    observer.pendingChange = diff_detected_topologies(oldTopology, newTopology, injectedEvent, reasserted, genuineDeviceEvent);
    observer.detectedTopology = newTopology;
    observer.topologyGeneration = observer.topologyGeneration == 0u ? 1u : observer.topologyGeneration + 1u;
    observer.pendingChange.dismissed = 0u;
    observer.pendingChange.acknowledged = 0u;
    observer.pendingChange.applied = 0u;
    observer.pendingTopologyChange = observer.pendingChange.addedOutputCount != 0u ||
        observer.pendingChange.removedOutputCount != 0u ||
        observer.pendingChange.changedOutputCount != 0u;
    observer.lastReasserted = reasserted;
    kernel::serial::puts("Pending display topology: generation=");
    serial_put_u32_decimal(observer.topologyGeneration);
    kernel::serial::puts(" added=");
    serial_put_u32_decimal(observer.pendingChange.addedOutputCount);
    kernel::serial::puts(" removed=");
    serial_put_u32_decimal(observer.pendingChange.removedOutputCount);
    kernel::serial::puts(" changed=");
    serial_put_u32_decimal(observer.pendingChange.changedOutputCount);
    kernel::serial::puts(" activeAffected=");
    kernel::serial::puts(observer.pendingChange.activeConfigurationAffected ? "yes" : "no");
    kernel::serial::puts(" automaticApply=no injectedEvent=");
    kernel::serial::puts(injectedEvent ? "yes\n" : "no\n");
}

static bool clear_display_event_bit(DeviceState& state,
                                    VirtioGpuConfigSnapshot& afterClear)
{
    ModernTransport& transport = state.transport;
    // events_read is never written. Only the recognized bit that completed a
    // successful rescan is written to the device-owned write-to-clear field.
    mmio_write32(device_cfg_addr(transport, DEVICE_CONFIG_EVENTS_CLEAR),
                 cpu_to_le32(VIRTIO_GPU_EVENT_DISPLAY));
    ++s_displayEventObserver.eventClearWrites;
    s_displayEventObserver.lastEventsCleared = VIRTIO_GPU_EVENT_DISPLAY;
    const bool rereadOk = read_virtio_gpu_config_snapshot_internal(state, afterClear);
    const bool reasserted = rereadOk && (afterClear.eventsRead & VIRTIO_GPU_EVENT_DISPLAY) != 0u;
    s_displayEventObserver.lastReasserted = reasserted;
    if (reasserted) ++s_displayEventObserver.reassertions;
    kernel::serial::puts("VirtioGPU config event clear: written=0x");
    kernel::serial::put_hex32(VIRTIO_GPU_EVENT_DISPLAY);
    kernel::serial::puts(" eventsAfter=0x");
    kernel::serial::put_hex32(rereadOk ? afterClear.eventsRead : 0xFFFFFFFFu);
    kernel::serial::puts(" result=");
    kernel::serial::puts(rereadOk ? "success" : "reread-failed");
    kernel::serial::puts(" reasserted=");
    kernel::serial::puts(reasserted ? "yes\n" : "no\n");
    return rereadOk;
}

static bool process_pending_display_rescan(DeviceState& state, uint64_t now)
{
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    if (!observer.rescanPending || observer.rescanInProgress || now < observer.nextRetryTick) return false;
    observer.rescanInProgress = true;
    observer.rescanPending = false;
    ++observer.rescansSubmitted;

    VirtioGpuProtocolDisplaySnapshot protocol{};
    const bool getInfoOk = submit_display_info_snapshot_request(state, "display-event", protocol);
    if (!getInfoOk) {
        observer.rescanInProgress = false;
        ++observer.rescansFailed;
        ++observer.pendingRescanRetries;
        observer.nextRetryTick = now + kDisplayEventFailedRescanBackoffTicks;
        copy_event_text(observer.lastError, sizeof(observer.lastError), "GET_DISPLAY_INFO failed; display event retained");
        return false;
    }

    VirtioGpuConfigSnapshot afterInfo{};
    if (!read_virtio_gpu_config_snapshot_internal(state, afterInfo)) {
        observer.rescanInProgress = false;
        ++observer.rescansFailed;
        ++observer.pendingRescanRetries;
        observer.nextRetryTick = now + kDisplayEventFailedRescanBackoffTicks;
        copy_event_text(observer.lastError, sizeof(observer.lastError),
                        "post-GET_DISPLAY_INFO coherent config read failed; display event retained");
        return false;
    }

    VirtioGpuDetectedTopologySnapshot newTopology{};
    if (!build_detected_topology_snapshot(state, afterInfo, &protocol, newTopology)) {
        observer.rescanInProgress = false;
        ++observer.rescansFailed;
        observer.nextRetryTick = now + kDisplayEventFailedRescanBackoffTicks;
        return false;
    }

    const VirtioGpuDetectedTopologySnapshot oldTopology = observer.detectedTopology;
    publish_detected_topology_change(oldTopology, newTopology, false, false, true);
    VirtioGpuConfigSnapshot afterClear{};
    const bool clearRereadOk = clear_display_event_bit(state, afterClear);
    const bool reasserted = clearRereadOk && (afterClear.eventsRead & VIRTIO_GPU_EVENT_DISPLAY) != 0u;
    observer.pendingChange.reasserted = reasserted ? 1u : 0u;
    if (reasserted) {
        observer.rescanPending = true;
        observer.nextRetryTick = now + kDisplayEventFailedRescanBackoffTicks;
    } else {
        observer.pendingRescanRetries = 0u;
        observer.nextRetryTick = 0u;
    }
    observer.rescanInProgress = false;
    ++observer.rescansSuccessful;
    ++observer.displayEventsProcessed;
    copy_event_text(observer.lastError, sizeof(observer.lastError), clearRereadOk ? "none" : "event clear verification failed");
    kernel::serial::puts("VirtioGPU display rescan: result=success scanouts=");
    serial_put_u32_decimal(newTopology.numScanouts);
    kernel::serial::puts(" connectorEnabled=");
    serial_put_u32_decimal(protocol.enabledCount);
    kernel::serial::puts(" changes=");
    serial_put_u32_decimal(observer.pendingChange.changedOutputCount +
                            observer.pendingChange.addedOutputCount +
                            observer.pendingChange.removedOutputCount);
    kernel::serial::puts(" classification=");
    kernel::serial::puts(observer.pendingChange.classification);
    kernel::serial::puts(" activeMutation=no\n");
    return true;
}

static void display_event_observer_tick(DeviceState& state)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    (void)state;
    return;
#else
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    if (!observer.initialized || !observer.enabled || state.transport.mmioStopReason == nullptr ||
        state.transport.mmioStopReason[0] == '\0') return;
    if (s_livePresentation.stopped || s_displayConfigurationPresentationPaused) return;

    const uint64_t now = kernel::pit::ticks();
    if (now < observer.nextPollTick) return;
    observer.nextPollTick = now + observer.pollInterval;
    observer.lastPollTick = now;
    ++observer.polls;

    VirtioGpuConfigSnapshot config{};
    if (!read_virtio_gpu_config_snapshot_internal(state, config)) {
        ++observer.incoherentReads;
        copy_event_text(observer.lastError, sizeof(observer.lastError), config.failureReason);
        return;
    }
    ++observer.coherentReads;
    observer.lastEventsRead = config.eventsRead;
    observer.lastConfigGeneration = config.finalGeneration;
    if (config.eventsRead != 0u) ++observer.eventsObserved;
    const uint32_t unknownBits = config.eventsRead & ~kVirtioGpuKnownEventMask;
    if (unknownBits != 0u) ++observer.unknownEventBitsObserved;
    const bool displayEvent = (config.eventsRead & VIRTIO_GPU_EVENT_DISPLAY) != 0u;
    if (displayEvent) {
        ++observer.displayEventsObserved;
        const bool alreadyPending = observer.rescanPending || observer.rescanInProgress;
        if (alreadyPending) {
            ++observer.rescansCoalesced;
        } else {
            observer.rescanPending = true;
        }
        kernel::serial::puts("VirtioGPU config event: generation=");
        serial_put_u32_decimal(config.finalGeneration);
        kernel::serial::puts(" eventsRead=0x");
        kernel::serial::put_hex32(config.eventsRead);
        kernel::serial::puts(" display=yes processing=");
        kernel::serial::puts(s_displayConfigurationPresentationPaused ? "deferred\n" : "scheduled\n");
    }
    if (observer.rescanPending && !s_displayConfigurationPresentationPaused) {
        if (observer.pendingRescanRetries >= kDisplayEventRescanRetryLimit) {
            observer.nextRetryTick = now + kDisplayEventFailedRescanBackoffTicks;
            return;
        }
        (void)process_pending_display_rescan(state, now);
    }
#endif
}

static bool initialize_display_event_observer(DeviceState& state)
{
    s_displayEventObserver = VirtioGpuDisplayEventObserver{};
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    observer.initialized = true;
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    observer.enabled = false;
    copy_event_text(observer.disabledReason, sizeof(observer.disabledReason),
                    "display-event observer requires the QEMU smoke build gate");
    return false;
#else
    if (!state.device.initialized || !state.transport.modern || !state.transport.mmioMapped ||
        !state.transport.deviceCfg.present) {
        observer.enabled = false;
        copy_event_text(observer.disabledReason, sizeof(observer.disabledReason),
                        "modern mapped QEMU device configuration unavailable");
        return false;
    }
    VirtioGpuConfigSnapshot initialConfig{};
    if (!read_virtio_gpu_config_snapshot_internal(state, initialConfig)) {
        observer.enabled = false;
        copy_event_text(observer.disabledReason, sizeof(observer.disabledReason), initialConfig.failureReason);
        return false;
    }
    observer.enabled = true;
    observer.lastEventsRead = initialConfig.eventsRead;
    observer.lastConfigGeneration = initialConfig.finalGeneration;
    ++observer.coherentReads;
    if (initialConfig.eventsRead != 0u) ++observer.eventsObserved;
    if ((initialConfig.eventsRead & ~kVirtioGpuKnownEventMask) != 0u) ++observer.unknownEventBitsObserved;
    if (!build_detected_topology_snapshot(state, initialConfig, nullptr, observer.detectedTopology)) {
        observer.enabled = false;
        copy_event_text(observer.disabledReason, sizeof(observer.disabledReason), observer.lastError);
        return false;
    }
    observer.previousTopology = observer.detectedTopology;
    observer.topologyGeneration = 1u;
    kernel::serial::puts("VirtioGPU config snapshot: firstGeneration=");
    serial_put_u32_decimal(initialConfig.firstGeneration);
    kernel::serial::puts(" finalGeneration=");
    serial_put_u32_decimal(initialConfig.finalGeneration);
    kernel::serial::puts(" retryCount=");
    serial_put_u32_decimal(initialConfig.retryCount);
    kernel::serial::puts(" coherent=yes eventsRead=0x");
    kernel::serial::put_hex32(initialConfig.eventsRead);
    kernel::serial::puts(" numScanouts=");
    serial_put_u32_decimal(initialConfig.numScanouts);
    kernel::serial::puts(" numCapsets=");
    serial_put_u32_decimal(initialConfig.numCapsets);
    kernel::serial::putc('\n');
    kernel::serial::puts("VirtioGPU display-event observer: initialized=yes enabled=yes pollIntervalTicks=");
    serial_put_u32_decimal(observer.pollInterval);
    kernel::serial::puts(" qemuOnly=yes activeMutation=no\n");
    if ((initialConfig.eventsRead & VIRTIO_GPU_EVENT_DISPLAY) != 0u) observer.rescanPending = true;
    return true;
#endif
}

static bool refresh_detected_topology_without_event(DeviceState& state)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    (void)state;
    return false;
#else
    VirtioGpuConfigSnapshot before{};
    if (!read_virtio_gpu_config_snapshot_internal(state, before)) return false;
    VirtioGpuProtocolDisplaySnapshot protocol{};
    if (!submit_display_info_snapshot_request(state, "explicit-refresh", protocol)) return false;
    VirtioGpuConfigSnapshot after{};
    if (!read_virtio_gpu_config_snapshot_internal(state, after)) return false;
    VirtioGpuDetectedTopologySnapshot refreshed{};
    if (!build_detected_topology_snapshot(state, after, &protocol, refreshed)) return false;
    publish_detected_topology_change(s_displayEventObserver.detectedTopology, refreshed, false, false);
    s_displayEventObserver.rescanPending = false;
    kernel::serial::puts("VirtioGPU display refresh: result=success activeMutation=no eventClear=no\n");
    return true;
#endif
}

static bool inject_display_topology_change_internal(uint32_t kind)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    (void)kind;
    return false;
#else
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    if (!observer.enabled || observer.injectionInProgress || kind < static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::ConnectorState) ||
        kind > static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::OutputRemoval)) return false;
    observer.injectionInProgress = true;
    const VirtioGpuDetectedTopologySnapshot oldTopology = observer.detectedTopology;
    VirtioGpuDetectedTopologySnapshot nextTopology = oldTopology;
    nextTopology.configGeneration = oldTopology.configGeneration + 1u;
    const uint32_t primaryIndex = 0u;
    if (kind == static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::ConnectorState)) {
        if (nextTopology.outputCount == 0u) {
            observer.injectionInProgress = false;
            return false;
        }
        nextTopology.outputs[primaryIndex].connectorEnabled = nextTopology.outputs[primaryIndex].connectorEnabled ? 0u : 1u;
    } else if (kind == static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::PreferredGeometry)) {
        if (nextTopology.outputCount == 0u) {
            observer.injectionInProgress = false;
            return false;
        }
        nextTopology.outputs[primaryIndex].reportedWidth =
            nextTopology.outputs[primaryIndex].reportedWidth == 1280 ? 1024 : 1280;
        nextTopology.outputs[primaryIndex].reportedHeight =
            nextTopology.outputs[primaryIndex].reportedHeight == 800 ? 768 : 800;
    } else if (kind == static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::OutputAddition)) {
        if (nextTopology.outputCount >= gxos::display::kVirtioGpuDisplayEventMaxScanouts) {
            observer.injectionInProgress = false;
            return false;
        }
        const uint32_t index = nextTopology.outputCount++;
        ++nextTopology.numScanouts;
        VirtioGpuDetectedOutput& added = nextTopology.outputs[index];
        added = VirtioGpuDetectedOutput{};
        added.scanoutId = index;
        added.reported = 1u;
        added.connectorEnabled = 1u;
        added.reportedWidth = 800;
        added.reportedHeight = 600;
        copy_event_identity(added.stableIdentity, "display-", index + 1u);
    } else {
        if (nextTopology.outputCount <= 1u) {
            observer.injectionInProgress = false;
            return false;
        }
        --nextTopology.outputCount;
        --nextTopology.numScanouts;
    }
    publish_detected_topology_change(oldTopology, nextTopology, true, false);
    observer.injectionInProgress = false;
    kernel::serial::puts("VirtioGPU injected display event: injectedEvent=yes kind=");
    serial_put_u32_decimal(kind);
    kernel::serial::puts(" activeMutation=no eventRegisterWrite=no\n");
    return true;
#endif
}

static void mirror_diagnostic_resource_to_framebuffer(DeviceState& state,
                                                      const DiagnosticResourceState& resource)
{
    if (!resource.mirroredToFramebuffer) {
        return;
    }

    state.device.fbResourceId = resource.resourceId;
    state.device.fbWidth = resource.width;
    state.device.fbHeight = resource.height;
    state.device.fbFormat = resource.format;
}

static void clear_diagnostic_resource_from_framebuffer(DeviceState& state,
                                                       const DiagnosticResourceState& resource)
{
    if (!resource.mirroredToFramebuffer || state.device.fbResourceId != resource.resourceId) {
        return;
    }

    state.device.fbResourceId = 0u;
    state.device.fbBuffer = nullptr;
    state.device.fbBufferPhys = 0u;
    state.device.fbBufferSize = 0u;
    state.device.fbWidth = 0u;
    state.device.fbHeight = 0u;
    state.device.fbFormat = 0u;
}

static bool issue_resource_create_2d(DeviceState& state,
                                     DiagnosticResourceState& resource,
                                     uint32_t reservedResourceId,
                                     uint32_t resourceId,
                                     uint32_t width,
                                     uint32_t height,
                                     GpuFormat format,
                                     bool mirrorToFramebuffer,
                                     const char** failureReasonOut,
                                     bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    if (resourceId == 0u || resourceId == reservedResourceId || resource.resourceId != 0u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "diagnostic resource id is invalid";
        }
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    ResourceCreate2d* request = reinterpret_cast<ResourceCreate2d*>(&s_commandBuffer[0]);
    request->header.type = CMD_RESOURCE_CREATE_2D;
    request->header.flags = 0;
    request->header.fenceId = 0;
    request->header.ctxId = 0;
    request->header.padding = 0;
    request->resourceId = resourceId;
    request->format = static_cast<uint32_t>(format);
    request->width = width;
    request->height = height;

    log_init_step("RESOURCE_CREATE_2D begin");
    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     "RESOURCE_CREATE_2D",
                                     CMD_RESOURCE_CREATE_2D,
                                     request,
                                     sizeof(ResourceCreate2d),
                                     &s_responseBuffer[0],
                                     sizeof(CtrlHeader),
                                     RESP_OK_NODATA,
                                     &submitReason,
                                     &completionKnown)) {
        kernel::serial::puts("[VIRTIO-GPU] RESOURCE_CREATE_2D result=failed reason=");
        kernel::serial::puts(submitReason != nullptr ? submitReason : "n/a");
        kernel::serial::putc('\n');
        if (failureReasonOut != nullptr) {
            *failureReasonOut = submitReason;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    resource.resourceId = resourceId;
    resource.width = width;
    resource.height = height;
    resource.format = static_cast<uint32_t>(format);
    resource.mirroredToFramebuffer = mirrorToFramebuffer;
    resource.created = true;

    if (mirrorToFramebuffer) {
        mirror_diagnostic_resource_to_framebuffer(state, resource);
    }

    kernel::serial::puts("[VIRTIO-GPU] RESOURCE_CREATE_2D result=ready resourceId=0x");
    kernel::serial::put_hex32(resourceId);
    kernel::serial::puts(" format=");
    kernel::serial::puts(gpu_format_name(format));
    kernel::serial::puts(" width=");
    serial_put_u32_decimal(width);
    kernel::serial::puts(" height=");
    serial_put_u32_decimal(height);
    kernel::serial::putc('\n');
    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }
    return true;
}

static bool issue_resource_attach_backing(DeviceState& state,
                                          DiagnosticResourceState& resource,
                                          uint64_t backingVirtual,
                                          uint64_t backingPhysical,
                                          uint64_t backingBytes,
                                          uint64_t backingPageCount,
                                          const MemEntry* entries,
                                          uint32_t entryCount,
                                          const DiagnosticBackingLayoutAudit& audit,
                                          const char** failureReasonOut,
                                          bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    if (resource.resourceId == 0u || !resource.created || resource.backingAttached) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "attach backing resource id mismatch";
        }
        return false;
    }

    if (backingPhysical == 0u || backingVirtual == 0u || backingBytes == 0u || entryCount == 0u || entries == nullptr) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "attach backing parameters are invalid";
        }
        return false;
    }

    if (!audit.physicalCoverageValid || audit.totalMemEntries != entryCount || audit.coveredBytes != backingBytes) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "attach backing coverage validation failed";
        }
        return false;
    }

    if (entryCount > kDiagnosticBackingMaxMemEntries) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "attach backing entry count exceeds limit";
        }
        return false;
    }

    const size_t requestLength = sizeof(ResourceAttachBacking) + (static_cast<size_t>(entryCount) * sizeof(MemEntry));
    if (requestLength > sizeof(s_commandBuffer)) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "attach backing request is too large";
        }
        return false;
    }

    log_diagnostic_backing_layout(audit);

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    ResourceAttachBacking* request = reinterpret_cast<ResourceAttachBacking*>(&s_commandBuffer[0]);
    request->header.type = CMD_RESOURCE_ATTACH_BACKING;
    request->header.flags = 0;
    request->header.fenceId = 0;
    request->header.ctxId = 0;
    request->header.padding = 0;
    request->resourceId = resource.resourceId;
    request->numEntries = entryCount;
    MemEntry* requestEntries = reinterpret_cast<MemEntry*>(reinterpret_cast<uint8_t*>(request) + sizeof(ResourceAttachBacking));
    for (uint32_t i = 0u; i < entryCount; ++i) {
        requestEntries[i] = entries[i];
    }

    log_init_step("RESOURCE_ATTACH_BACKING begin");
    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     "RESOURCE_ATTACH_BACKING",
                                     CMD_RESOURCE_ATTACH_BACKING,
                                     request,
                                     requestLength,
                                     &s_responseBuffer[0],
                                     sizeof(CtrlHeader),
                                     RESP_OK_NODATA,
                                     &submitReason,
                                     &completionKnown)) {
        kernel::serial::puts("[VIRTIO-GPU] RESOURCE_ATTACH_BACKING result=failed reason=");
        kernel::serial::puts(submitReason != nullptr ? submitReason : "n/a");
        kernel::serial::putc('\n');
        if (failureReasonOut != nullptr) {
            *failureReasonOut = submitReason;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    resource.backingVirtual = backingVirtual;
    resource.backingPhysical = backingPhysical;
    resource.backingBytes = backingBytes;
    resource.backingPageCount = backingPageCount;
    resource.memEntryCount = entryCount;
    resource.contiguousRunCount = audit.contiguousRunCount;
    resource.coveredBytes = audit.coveredBytes;
    resource.physicalCoverageValid = audit.physicalCoverageValid;
    resource.backingAttached = true;

    if (resource.mirroredToFramebuffer) {
        state.device.fbBuffer = reinterpret_cast<uint8_t*>(backingVirtual);
        state.device.fbBufferPhys = backingPhysical;
        state.device.fbBufferSize = static_cast<size_t>(backingBytes);
    }

    kernel::serial::puts("[VIRTIO-GPU] RESOURCE_ATTACH_BACKING result=attached resourceId=0x");
    kernel::serial::put_hex32(resource.resourceId);
    kernel::serial::puts(" backingVirtualBase=0x");
    kernel::serial::put_hex64(backingVirtual);
    kernel::serial::puts(" totalBackingBytes=");
    serial_put_u64_decimal(backingBytes);
    kernel::serial::puts(" totalPages=");
    serial_put_u64_decimal(backingPageCount);
    kernel::serial::puts(" totalMemEntries=");
    serial_put_u32_decimal(entryCount);
    kernel::serial::puts(" contiguousRunCount=");
    serial_put_u32_decimal(audit.contiguousRunCount);
    kernel::serial::puts(" coveredBytes=");
    serial_put_u64_decimal(audit.coveredBytes);
    kernel::serial::puts(" physicalCoverageValid=");
    kernel::serial::puts(audit.physicalCoverageValid ? "yes" : "no");
    kernel::serial::puts(" firstPhysRange=0x");
    kernel::serial::put_hex64(audit.firstPhysicalStart);
    kernel::serial::puts("-0x");
    kernel::serial::put_hex64(audit.firstPhysicalEnd);
    kernel::serial::puts(" lastPhysRange=0x");
    kernel::serial::put_hex64(audit.lastPhysicalStart);
    kernel::serial::puts("-0x");
    kernel::serial::put_hex64(audit.lastPhysicalEnd);
    kernel::serial::putc('\n');
    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }
    return true;
}

static bool issue_set_scanout(DeviceState& state,
                              DiagnosticResourceState& resource,
                              uint32_t scanoutId,
                              uint32_t width,
                              uint32_t height,
                              const char** failureReasonOut,
                              bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    if (scanoutId > 1u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "scanout id is not permitted";
        }
        return false;
    }

    if (resource.resourceId == 0u || !resource.backingAttached || width == 0u || height == 0u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "set scanout resource state is invalid";
        }
        return false;
    }

    if (width > resource.width || height > resource.height) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "scanout rectangle exceeds resource bounds";
        }
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    SetScanout* request = reinterpret_cast<SetScanout*>(&s_commandBuffer[0]);
    request->header.type = CMD_SET_SCANOUT;
    request->header.flags = 0;
    request->header.fenceId = 0;
    request->header.ctxId = 0;
    request->header.padding = 0;
    request->rect.x = 0u;
    request->rect.y = 0u;
    request->rect.width = width;
    request->rect.height = height;
    request->scanoutId = scanoutId;
    request->resourceId = resource.resourceId;

    kernel::serial::puts("[VIRTIO-GPU] Init step: SET_SCANOUT scanout");
    serial_put_u32_decimal(scanoutId);
    kernel::serial::puts(" begin\n");

    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     "SET_SCANOUT",
                                     CMD_SET_SCANOUT,
                                     request,
                                     sizeof(SetScanout),
                                     &s_responseBuffer[0],
                                     sizeof(CtrlHeader),
                                     RESP_OK_NODATA,
                                     &submitReason,
                                     &completionKnown)) {
        kernel::serial::puts("[VIRTIO-GPU] SET_SCANOUT result=failed scanoutId=");
        serial_put_u32_decimal(scanoutId);
        kernel::serial::puts(" reason=");
        kernel::serial::puts(submitReason != nullptr ? submitReason : "n/a");
        kernel::serial::putc('\n');
        if (failureReasonOut != nullptr) {
            *failureReasonOut = submitReason;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    resource.scanoutSet = true;
    kernel::serial::puts("[VIRTIO-GPU] SET_SCANOUT result=set scanoutId=");
    serial_put_u32_decimal(scanoutId);
    kernel::serial::puts(" resourceId=0x");
    kernel::serial::put_hex32(resource.resourceId);
    kernel::serial::puts(" rect=0,0 ");
    serial_put_u32_decimal(width);
    kernel::serial::putc('x');
    serial_put_u32_decimal(height);
    kernel::serial::putc('\n');
    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }
    return true;
}

// VirtIO-GPU clears a scanout by issuing SET_SCANOUT with resourceId=0 and a
// zero rectangle. This is used only inside the explicit QEMU reconciliation
// transaction; event observation never calls it.
static bool issue_clear_scanout(DeviceState& state,
                                uint32_t scanoutId,
                                const char** failureReasonOut,
                                bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) *failureReasonOut = nullptr;
    if (completionKnownOut != nullptr) *completionKnownOut = false;
    if (scanoutId > 1u) {
        if (failureReasonOut != nullptr) *failureReasonOut = "scanout id is not permitted";
        return false;
    }
    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    SetScanout* request = reinterpret_cast<SetScanout*>(&s_commandBuffer[0]);
    request->header.type = CMD_SET_SCANOUT;
    request->header.flags = 0u;
    request->header.fenceId = 0u;
    request->header.ctxId = 0u;
    request->header.padding = 0u;
    request->rect.x = 0u;
    request->rect.y = 0u;
    request->rect.width = 0u;
    request->rect.height = 0u;
    request->scanoutId = scanoutId;
    request->resourceId = 0u;
    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport, "SET_SCANOUT clear", CMD_SET_SCANOUT,
                                     request, sizeof(SetScanout), &s_responseBuffer[0],
                                     sizeof(CtrlHeader), RESP_OK_NODATA, &submitReason,
                                     &completionKnown)) {
        if (failureReasonOut != nullptr) *failureReasonOut = submitReason;
        if (completionKnownOut != nullptr) *completionKnownOut = completionKnown;
        return false;
    }
    if (completionKnownOut != nullptr) *completionKnownOut = true;
    kernel::serial::puts("[VIRTIO-GPU] SET_SCANOUT clear result=ok scanoutId=");
    serial_put_u32_decimal(scanoutId);
    kernel::serial::puts(" resourceRetention=rollback-safe\n");
    return true;
}

static bool issue_transfer_to_host_2d(DeviceState& state,
                                      DiagnosticResourceState& resource,
                                      uint32_t scanoutId,
                                      const char** failureReasonOut,
                                      bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    // VirtIO 2D transfer is valid after backing attachment and before
    // SET_SCANOUT; this is required for prepare-before-replace validation.
    if (resource.resourceId == 0u || !resource.backingAttached || resource.width == 0u || resource.height == 0u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "transfer resource state is invalid";
        }
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    TransferToHost2d* request = reinterpret_cast<TransferToHost2d*>(&s_commandBuffer[0]);
    request->header.type = CMD_TRANSFER_TO_HOST_2D;
    request->header.flags = 0;
    request->header.fenceId = 0;
    request->header.ctxId = 0;
    request->header.padding = 0;
    request->rect.x = 0u;
    request->rect.y = 0u;
    request->rect.width = resource.width;
    request->rect.height = resource.height;
    request->offset = 0u;
    request->resourceId = resource.resourceId;
    request->padding = 0u;

    if (!s_liveCommandLoggingSuppressed) {
        kernel::serial::puts("[VIRTIO-GPU] Init step: TRANSFER_TO_HOST_2D scanout");
        serial_put_u32_decimal(scanoutId);
        kernel::serial::puts(" begin\n");
    }

    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     "TRANSFER_TO_HOST_2D",
                                     CMD_TRANSFER_TO_HOST_2D,
                                     request,
                                     sizeof(TransferToHost2d),
                                     &s_responseBuffer[0],
                                     sizeof(CtrlHeader),
                                     RESP_OK_NODATA,
                                     &submitReason,
                                     &completionKnown)) {
        if (!s_liveCommandLoggingSuppressed) {
            kernel::serial::puts("[VIRTIO-GPU] TRANSFER_TO_HOST_2D result=failed scanoutId=");
            serial_put_u32_decimal(scanoutId);
            kernel::serial::puts(" reason=");
            kernel::serial::puts(submitReason != nullptr ? submitReason : "n/a");
            kernel::serial::putc('\n');
        }
        if (failureReasonOut != nullptr) {
            *failureReasonOut = submitReason;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    resource.transferOk = true;
    if (!s_liveCommandLoggingSuppressed) {
        kernel::serial::puts("[VIRTIO-GPU] TRANSFER_TO_HOST_2D result=ok scanoutId=");
        serial_put_u32_decimal(scanoutId);
        kernel::serial::puts(" resourceId=0x");
        kernel::serial::put_hex32(resource.resourceId);
        kernel::serial::puts(" rect=0,0 ");
        serial_put_u32_decimal(resource.width);
        kernel::serial::putc('x');
        serial_put_u32_decimal(resource.height);
        kernel::serial::puts(" offset=0");
        kernel::serial::putc('\n');
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }
    return true;
}

static bool issue_resource_flush(DeviceState& state,
                                 DiagnosticResourceState& resource,
                                 uint32_t scanoutId,
                                 const char** failureReasonOut,
                                 bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    if (resource.resourceId == 0u || !resource.backingAttached || !resource.transferOk || resource.width == 0u || resource.height == 0u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "flush resource state is invalid";
        }
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    ResourceFlush* request = reinterpret_cast<ResourceFlush*>(&s_commandBuffer[0]);
    request->header.type = CMD_RESOURCE_FLUSH;
    request->header.flags = 0;
    request->header.fenceId = 0;
    request->header.ctxId = 0;
    request->header.padding = 0;
    request->rect.x = 0u;
    request->rect.y = 0u;
    request->rect.width = resource.width;
    request->rect.height = resource.height;
    request->resourceId = resource.resourceId;
    request->padding = 0u;

    if (!s_liveCommandLoggingSuppressed) {
        kernel::serial::puts("[VIRTIO-GPU] Init step: RESOURCE_FLUSH scanout");
        serial_put_u32_decimal(scanoutId);
        kernel::serial::puts(" begin\n");
    }

    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     "RESOURCE_FLUSH",
                                     CMD_RESOURCE_FLUSH,
                                     request,
                                     sizeof(ResourceFlush),
                                     &s_responseBuffer[0],
                                     sizeof(CtrlHeader),
                                     RESP_OK_NODATA,
                                     &submitReason,
                                     &completionKnown)) {
        if (!s_liveCommandLoggingSuppressed) {
            kernel::serial::puts("[VIRTIO-GPU] RESOURCE_FLUSH result=failed scanoutId=");
            serial_put_u32_decimal(scanoutId);
            kernel::serial::puts(" reason=");
            kernel::serial::puts(submitReason != nullptr ? submitReason : "n/a");
            kernel::serial::putc('\n');
        }
        if (failureReasonOut != nullptr) {
            *failureReasonOut = submitReason;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    resource.flushOk = true;
    if (!s_liveCommandLoggingSuppressed) {
        kernel::serial::puts("[VIRTIO-GPU] RESOURCE_FLUSH result=ok scanoutId=");
        serial_put_u32_decimal(scanoutId);
        kernel::serial::puts(" resourceId=0x");
        kernel::serial::put_hex32(resource.resourceId);
        kernel::serial::puts(" rect=0,0 ");
        serial_put_u32_decimal(resource.width);
        kernel::serial::putc('x');
        serial_put_u32_decimal(resource.height);
        kernel::serial::putc('\n');
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }
    return true;
}

static bool issue_resource_unref(DeviceState& state,
                                 DiagnosticResourceState& resource,
                                 const char** failureReasonOut,
                                 bool* completionKnownOut)
{
    ModernTransport& transport = state.transport;
    if (failureReasonOut != nullptr) {
        *failureReasonOut = nullptr;
    }
    if (completionKnownOut != nullptr) {
        *completionKnownOut = false;
    }

    if (resource.resourceId == 0u) {
        if (failureReasonOut != nullptr) {
            *failureReasonOut = "resource id is zero";
        }
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));
    ResourceUnref* request = reinterpret_cast<ResourceUnref*>(&s_commandBuffer[0]);
    request->header.type = CMD_RESOURCE_UNREF;
    request->header.flags = 0;
    request->header.fenceId = 0;
    request->header.ctxId = 0;
    request->header.padding = 0;
    request->resourceId = resource.resourceId;
    request->padding = 0u;

    log_init_step("RESOURCE_UNREF begin");
    const char* submitReason = nullptr;
    bool completionKnown = false;
    if (!submit_control_command_sync(transport,
                                     "RESOURCE_UNREF",
                                     CMD_RESOURCE_UNREF,
                                     request,
                                     sizeof(ResourceUnref),
                                     &s_responseBuffer[0],
                                     sizeof(CtrlHeader),
                                     RESP_OK_NODATA,
                                     &submitReason,
                                     &completionKnown)) {
        kernel::serial::puts("[VIRTIO-GPU] RESOURCE_UNREF result=failed reason=");
        kernel::serial::puts(submitReason != nullptr ? submitReason : "n/a");
        kernel::serial::putc('\n');
        if (failureReasonOut != nullptr) {
            *failureReasonOut = submitReason;
        }
        if (completionKnownOut != nullptr) {
            *completionKnownOut = completionKnown;
        }
        return false;
    }

    kernel::serial::puts("[VIRTIO-GPU] RESOURCE_UNREF result=ok resourceId=0x");
    kernel::serial::put_hex32(resource.resourceId);
    kernel::serial::putc('\n');
    if (resource.mirroredToFramebuffer) {
        clear_diagnostic_resource_from_framebuffer(state, resource);
    }
    resource = DiagnosticResourceState{};
    if (completionKnownOut != nullptr) {
        *completionKnownOut = true;
    }
    return true;
}

static void cleanup_diagnostic_resource_if_safe(DeviceState& state,
                                                DiagnosticResourceState& resource,
                                                bool commandCompleted,
                                                const char* cleanupReason)
{
    if (resource.resourceId == 0u) {
        kernel::serial::puts("[VIRTIO-GPU] Cleanup state resourceId=n/a skipped=already-cleaned reason=");
        kernel::serial::puts(cleanupReason != nullptr ? cleanupReason : "n/a");
        kernel::serial::putc('\n');
        return;
    }

    kernel::serial::puts("[VIRTIO-GPU] Cleanup state resourceId=0x");
    kernel::serial::put_hex32(resource.resourceId);
    kernel::serial::puts(" commandCompleted=");
    kernel::serial::puts(commandCompleted ? "yes" : "no");
    kernel::serial::puts(" reason=");
    kernel::serial::puts(cleanupReason != nullptr ? cleanupReason : "n/a");
    kernel::serial::putc('\n');

    if (!commandCompleted) {
        return;
    }

    const char* unrefReason = nullptr;
    bool unrefKnown = false;
    if (issue_resource_unref(state, resource, &unrefReason, &unrefKnown)) {
        kernel::serial::puts("[VIRTIO-GPU] Cleanup resource release complete\n");
    } else {
        kernel::serial::puts("[VIRTIO-GPU] Cleanup resource release failed reason=");
        kernel::serial::puts(unrefReason != nullptr ? unrefReason : "n/a");
        kernel::serial::putc('\n');
        (void)unrefKnown;
    }
}
#endif // GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE

static bool probe_device(DeviceState& state, uint8_t bus, uint8_t device, uint8_t function)
{
    ModernTransport& transport = state.transport;
    transport = ModernTransport{};
    transport.present = false;
    transport.modern = false;
    transport.probeComplete = false;
    transport.bus = bus;
    transport.device = device;
    transport.function = function;
    transport.vendorId = msi::pci_config_read16(bus, device, function, 0x00);
    transport.deviceId = msi::pci_config_read16(bus, device, function, 0x02);
    transport.revision = msi::pci_config_read8(bus, device, function, kPciRevisionOffset);
    const uint32_t classReg = msi::pci_config_read32(bus, device, function, kPciRevisionOffset);
    transport.progIf = static_cast<uint8_t>((classReg >> 8) & 0xFFu);
    transport.subclass = static_cast<uint8_t>((classReg >> 16) & 0xFFu);
    transport.classCode = static_cast<uint8_t>((classReg >> 24) & 0xFFu);
    transport.headerType = msi::pci_config_read8(bus, device, function, kPciHeaderTypeOffset);
    transport.command = msi::pci_config_read16(bus, device, function, kPciCommandOffset);
    transport.status = msi::pci_config_read16(bus, device, function, kPciStatusOffset);
    transport.subsystemVendorId = msi::pci_config_read16(bus, device, function, kPciSubsystemVendorOffset);
    transport.subsystemDeviceId = msi::pci_config_read16(bus, device, function, kPciSubsystemDeviceOffset);

    log_pci_candidate(bus, device, function,
                      transport.vendorId, transport.deviceId,
                      transport.revision, transport.classCode,
                      transport.subclass, transport.progIf,
                      transport.headerType, transport.command,
                      transport.status, transport.subsystemVendorId,
                      transport.subsystemDeviceId);

    if (transport.vendorId != kVirtioPciVendorId || transport.deviceId != kVirtioGpuPciDeviceId) {
        return false;
    }

    transport.present = true;

    if (!parse_virtio_regions(&transport)) {
        transport.modern = false;
        record_probe_outcome(state, false, DisplayInfoOutcome::NotQueried, 0u, 0u, 0u,
                             transport_blocker_reason(transport));
        return false;
    }

    transport.modern = true;

    return true;
}

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
static bool initialize_device(DeviceState& state)
{
    ModernTransport& transport = state.transport;
    GpuDevice& device = state.device;
    DisplayInfoOutcome displayInfoOutcome = DisplayInfoOutcome::NotQueried;
    uint32_t preRenderEnabledScanouts = 0u;
    uint32_t preRenderDisabledScanouts = 0u;
    uint32_t postRenderEnabledScanouts = 0u;
    DiagnosticResourceState resource1{};
    DiagnosticResourceState resource2{};
    DiagnosticBackingLayoutAudit backingAudit1{};
    DiagnosticBackingLayoutAudit backingAudit2{};
    MemEntry backingEntries1[kDiagnosticBackingMaxMemEntries]{};
    MemEntry backingEntries2[kDiagnosticBackingMaxMemEntries]{};
    uint64_t patternChecksum1 = 0u;
    uint64_t patternChecksum2 = 0u;
    uint32_t stageAEnabledScanouts = 0u;
    uint32_t stageBEnabledScanouts = 0u;
    bool resource2dReady = false;
    bool backingAttached = false;
    bool scanout0Set = false;
    bool transferOk = false;
    bool flushOk = false;
    bool resource2dReadySecondary = false;
    bool backingAttachedSecondary = false;
    bool scanout1Set = false;
    bool transfer1Ok = false;
    bool flush1Ok = false;
    bool distinctPatternsConfirmed = false;
    bool renderingTestPattern = false;
    s_probeOutcome.contentMode = "diagnostic-patterns";
    s_probeOutcome.frameMode = "single-shot";
    s_probeOutcome.continuousPresentationEnabled = false;
    auto fail_and_record = [&](const char* reason, DisplayInfoOutcome displayInfoOutcome) -> bool {
        const char* finalReason = (reason != nullptr && reason[0] != '\0')
            ? reason
            : "virtio-gpu initialization failed";
        mark_device_failed(transport, finalReason);
        s_probeOutcome.deviceConfigNumScanouts = transport.mmioDeviceScanouts;
        s_probeOutcome.qemuMaxOutputsIntent = kDiagnosticQemuMaxOutputsIntent;
        s_probeOutcome.enabledScanoutsAfter = stageBEnabledScanouts != 0u ? stageBEnabledScanouts : postRenderEnabledScanouts;
        s_probeOutcome.resource2dReady = resource2dReady;
        s_probeOutcome.backingAttached = backingAttached;
        s_probeOutcome.scanout0Set = scanout0Set;
        s_probeOutcome.transferOk = transferOk;
        s_probeOutcome.flushOk = flushOk;
        s_probeOutcome.resource2dReadySecondary = resource2dReadySecondary;
        s_probeOutcome.backingAttachedSecondary = backingAttachedSecondary;
        s_probeOutcome.scanout1Set = scanout1Set;
        s_probeOutcome.transfer1Ok = transfer1Ok;
        s_probeOutcome.flush1Ok = flush1Ok;
        s_probeOutcome.distinctPatternsConfirmed = distinctPatternsConfirmed;
        s_probeOutcome.renderingTestPattern = renderingTestPattern;
        s_probeOutcome.contentMode = "diagnostic-patterns";
        s_probeOutcome.frameMode = "single-shot";
        s_probeOutcome.continuousPresentationEnabled = false;
        record_probe_outcome(state,
                             device.initialized,
                             displayInfoOutcome,
                             preRenderEnabledScanouts,
                             preRenderDisabledScanouts,
                             transport.displayInfoSlots,
                             finalReason);
        return false;
    };

    memzero(&device, sizeof(device));
    device.initialized = false;
    device.isPci = true;
    device.pciBus = transport.bus;
    device.pciDevice = transport.device;
    device.pciFunction = transport.function;
    device.irqLine = 0xFF;
    device.nextResourceId = 1;

    transport.mmioMapped = false;
    transport.mmioSanityReadsOk = false;
    transport.featuresOk = false;
    transport.controlQueueReady = false;
    transport.probeComplete = false;
    transport.negotiatedFeatures = 0;
    transport.mmioMappedVirtual = 0;
    transport.mmioMappedPageCount = 0;
    transport.mmioNumQueues = 0;
    transport.mmioDeviceStatus = 0;
    transport.mmioConfigGeneration = 0;
    transport.mmioDeviceScanouts = 0;
    transport.mmioDeviceCapsets = 0;
    transport.displayInfoSlots = 0;
    transport.enabledScanouts = 0;
    transport.disabledScanouts = 0;
    transport.mmioCacheMode = "n/a";
    transport.mmioStopReason = "transport writes intentionally disabled";
    transport.queueSize = 0;
    transport.queueNotifyOff = 0;
    transport.deviceFeaturesLow = 0;
    transport.deviceFeaturesHigh = 0;

    kernel::serial::puts("[VIRTIO-GPU] Initializing diagnostic-only virtio-gpu device\n");
    kernel::serial::puts("[VIRTIO-GPU] Transport type detected: ");
    kernel::serial::puts(transport_kind_name(transport));
    kernel::serial::putc('\n');

    kernel::serial::puts("[VIRTIO-GPU] REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.\n");

    MmioTransportProbeState mmioProbe{};
    kernel::mmio::MappingReport mmioBlockerReport{};
    const char* mmioBlocker = transport_mmio_blocker_reason(transport, &mmioProbe, &mmioBlockerReport);
    if (mmioBlocker != nullptr) {
        kernel::serial::puts("[VIRTIO-GPU] MMIO mapping blocked: ");
        kernel::serial::puts(mmioBlocker);
        kernel::serial::putc('\n');
        kernel::serial::puts("[VIRTIO-GPU] Required next kernel memory feature: ");
        kernel::serial::puts(mmioBlockerReport.nextKernelFeature != nullptr ? mmioBlockerReport.nextKernelFeature : "n/a");
        kernel::serial::putc('\n');
        transport.mmioStopReason = mmioBlocker;
        record_probe_outcome(state, false, DisplayInfoOutcome::NotQueried, 0u, 0u, 0u, mmioBlocker);
        return false;
    }

    transport.mmioMapped = mmioProbe.mmioMapped;
    transport.mmioSanityReadsOk = mmioProbe.sanityReadsOk;
    transport.mmioMappedVirtual = mmioProbe.mappedVirtual;
    transport.mmioMappedPageCount = mmioProbe.pageCount;
    transport.mmioNumQueues = mmioProbe.numQueues;
    transport.mmioDeviceStatus = mmioProbe.deviceStatus;
    transport.mmioConfigGeneration = mmioProbe.configGeneration;
    transport.mmioDeviceScanouts = mmioProbe.deviceScanouts;
    transport.mmioDeviceCapsets = mmioProbe.deviceCapsets;
    transport.mmioCacheMode = mmioProbe.cacheMode;
    transport.mmioStopReason = mmioProbe.stopReason != nullptr ? mmioProbe.stopReason : "transport writes intentionally disabled";

    kernel::serial::puts("[VIRTIO-GPU] MMIO transport summary mmioMapped=");
    kernel::serial::puts(mmioProbe.mmioMapped ? "yes" : "no");
    kernel::serial::puts(" mappingVirtual=0x");
    kernel::serial::put_hex64(mmioProbe.mappedVirtual);
    kernel::serial::puts(" pageCount=");
    serial_put_u64_decimal(mmioProbe.pageCount);
    kernel::serial::puts(" cacheMode=");
    kernel::serial::puts(mmioProbe.cacheMode != nullptr ? mmioProbe.cacheMode : "n/a");
    kernel::serial::puts(" sanityReads=");
    kernel::serial::puts(mmioProbe.sanityReadsOk ? "ok" : "failed");
    kernel::serial::puts(" stopReason=");
    kernel::serial::puts(mmioProbe.stopReason != nullptr ? mmioProbe.stopReason : "n/a");
    kernel::serial::puts(" numQueues=");
    serial_put_u32_decimal(mmioProbe.numQueues);
    kernel::serial::puts(" deviceStatus=0x");
    kernel::serial::put_hex8(mmioProbe.deviceStatus);
    kernel::serial::puts(" configGeneration=0x");
    kernel::serial::put_hex8(mmioProbe.configGeneration);
    kernel::serial::putc('\n');

    kernel::serial::puts("[VIRTIO-GPU] MMIO transport mapped; read-only sanity reads complete; controlled transport initialization begins\n");
    kernel::serial::puts("[VIRTIO-GPU] Common config sanity num_queues=");
    serial_put_u32_decimal(mmioProbe.numQueues);
    kernel::serial::puts(" device_status=0x");
    kernel::serial::put_hex8(mmioProbe.deviceStatus);
    kernel::serial::puts(" config_generation=0x");
    kernel::serial::put_hex8(mmioProbe.configGeneration);
    kernel::serial::putc('\n');

    if (!reset_transport(transport)) {
        return fail_and_record(transport.mmioStopReason, DisplayInfoOutcome::NotQueried);
    }

    if (!set_status_and_verify(transport, STATUS_ACKNOWLEDGE, "ACKNOWLEDGE")) {
        return fail_and_record("device did not accept ACKNOWLEDGE status", DisplayInfoOutcome::NotQueried);
    }

    if (!set_status_and_verify(transport, static_cast<uint8_t>(read_status(transport) | STATUS_DRIVER), "DRIVER")) {
        return fail_and_record("device did not accept DRIVER status", DisplayInfoOutcome::NotQueried);
    }

    if (!negotiate_features(transport)) {
        record_probe_outcome(state, false, DisplayInfoOutcome::NotQueried, 0u, 0u, 0u,
                             transport.mmioStopReason);
        return false;
    }

    if (!setup_control_queue(transport)) {
        return fail_and_record(transport.mmioStopReason, DisplayInfoOutcome::NotQueried);
    }

    if (!set_status_and_verify(transport, static_cast<uint8_t>(read_status(transport) | STATUS_DRIVER_OK), "DRIVER_OK")) {
        return fail_and_record("device did not accept DRIVER_OK status", DisplayInfoOutcome::NotQueried);
    }

    const uint8_t finalStatus = read_status(transport);
    const uint8_t requiredStatus = static_cast<uint8_t>(STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK);
    kernel::serial::puts("[VIRTIO-GPU] Status verification ack=");
    kernel::serial::puts((finalStatus & STATUS_ACKNOWLEDGE) != 0u ? "yes" : "no");
    kernel::serial::puts(" driver=");
    kernel::serial::puts((finalStatus & STATUS_DRIVER) != 0u ? "yes" : "no");
    kernel::serial::puts(" featuresOk=");
    kernel::serial::puts((finalStatus & STATUS_FEATURES_OK) != 0u ? "yes" : "no");
    kernel::serial::puts(" driverOk=");
    kernel::serial::puts((finalStatus & STATUS_DRIVER_OK) != 0u ? "yes" : "no");
    kernel::serial::puts(" readback=0x");
    kernel::serial::put_hex8(finalStatus);
    kernel::serial::putc('\n');

    if ((finalStatus & requiredStatus) != requiredStatus) {
        transport.mmioStopReason = "device status verification failed after DRIVER_OK";
        return fail_and_record(transport.mmioStopReason, DisplayInfoOutcome::NotQueried);
    }

    transport.mmioDeviceStatus = finalStatus;

    if (!submit_display_info_request(state, "pre-render", true)) {
        displayInfoOutcome = DisplayInfoOutcome::Failed;
        return fail_and_record(transport.mmioStopReason, displayInfoOutcome);
    }

    displayInfoOutcome = DisplayInfoOutcome::Ok;
    preRenderEnabledScanouts = transport.enabledScanouts;
    preRenderDisabledScanouts = transport.disabledScanouts;
    device.initialized = true;

    const DisplayInfo& scanout0 = device.displays[0];
    const DisplayInfo& scanout1Initial = device.displays[1];
    if (!scanout0.enabled) {
        return fail_and_record("scanout 0 is not enabled", displayInfoOutcome);
    }
    if (transport.mmioDeviceScanouts < 2u) {
        return fail_and_record("deviceConfigNumScanouts reported fewer than two scanouts", displayInfoOutcome);
    }

    uint32_t selectedWidth = scanout0.width;
    uint32_t selectedHeight = scanout0.height;
    bool usedFallbackGeometry = false;
    const uint32_t kDiagnosticMaxDimension = 4096u;
    const uint32_t kFallbackWidth = 1024u;
    const uint32_t kFallbackHeight = 768u;
    if (selectedWidth == 0u || selectedHeight == 0u) {
        return fail_and_record("scanout 0 geometry is invalid", displayInfoOutcome);
    }
    if (selectedWidth > kDiagnosticMaxDimension || selectedHeight > kDiagnosticMaxDimension) {
        selectedWidth = kFallbackWidth;
        selectedHeight = kFallbackHeight;
        usedFallbackGeometry = true;
        kernel::serial::puts("[VIRTIO-GPU] Scanout 0 reported geometry exceeds the diagnostic limit; using conservative fallback size\n");
    }

    const DiagnosticPatternPalette& primaryPalette = diagnostic_pattern_palette(0u);
    const DiagnosticPatternPalette& secondaryPalette = diagnostic_pattern_palette(1u);
    resource1.patternName = primaryPalette.name;
    resource1.mirroredToFramebuffer = true;
    resource2.patternName = secondaryPalette.name;
    resource2.mirroredToFramebuffer = false;

    uint32_t bytesPerPixel = 0u;
    if (!gpu_format_bytes_per_pixel(FORMAT_B8G8R8X8_UNORM, &bytesPerPixel) || bytesPerPixel != kDiagnosticBytesPerPixel) {
        return fail_and_record("diagnostic pixel format is unsupported", displayInfoOutcome);
    }

    uint64_t strideBytes = 0u;
    if (mul_u64_overflow(static_cast<uint64_t>(selectedWidth), static_cast<uint64_t>(bytesPerPixel), &strideBytes) ||
        strideBytes == 0u ||
        strideBytes > static_cast<uint64_t>(~0u)) {
        return fail_and_record("diagnostic stride overflows", displayInfoOutcome);
    }

    uint64_t totalBackingBytes = 0u;
    if (mul_u64_overflow(strideBytes, static_cast<uint64_t>(selectedHeight), &totalBackingBytes) || totalBackingBytes == 0u) {
        return fail_and_record("diagnostic backing size overflows", displayInfoOutcome);
    }

    if (totalBackingBytes > static_cast<uint64_t>(kDiagnosticBackingBytes)) {
        return fail_and_record("diagnostic backing exceeds allocator limit", displayInfoOutcome);
    }

    const uint64_t backingPageCount = align_up(totalBackingBytes, kDiagnosticPageSizeBytes) / kDiagnosticPageSizeBytes;
    const uint32_t selectedFormat = FORMAT_B8G8R8X8_UNORM;
    const uint64_t primaryBackingVirtual = reinterpret_cast<uint64_t>(&s_diagnosticBackingStorage0[0]);
    const uint64_t primaryBackingPhysical = dma_address(&s_diagnosticBackingStorage0[0]);
    const uint64_t secondaryBackingVirtual = reinterpret_cast<uint64_t>(&s_diagnosticBackingStorage1[0]);
    const uint64_t secondaryBackingPhysical = dma_address(&s_diagnosticBackingStorage1[0]);
    auto updateOutputInventory = [&]() {
        const bool scanout0PresentationConfirmed = resource1.flushOk && resource1.patternChecksum != 0u;
        const bool scanout1PresentationConfirmed = resource2.flushOk && resource2.patternChecksum != 0u;
        s_probeOutcome.outputInventory = build_output_inventory(
            scanout0,
            scanout1Initial,
            resource1,
            resource2,
            transport.mmioDeviceScanouts,
            scanout0PresentationConfirmed,
            scanout1PresentationConfirmed,
            selectedWidth,
            selectedHeight);
    };

    auto run_compositor_frame = [&]() -> bool {
        CompositorFrameTargetResult target0Result{};
        CompositorFrameTargetResult target1Result{};
        const uint32_t virtualDesktopWidth = selectedWidth * 2u;
        const uint32_t virtualDesktopHeight = selectedHeight;

        auto finish = [&](const char* reason) -> bool {
            const char* finalReason = reason;
            if (finalReason == nullptr || finalReason[0] == '\0') {
                finalReason = target1Result.blocker != nullptr ? target1Result.blocker
                    : (target0Result.blocker != nullptr ? target0Result.blocker : "single-shot compositor frame milestone complete");
            }

            resource1.transferOk = target0Result.transferOk;
            resource1.flushOk = target0Result.flushOk;
            resource2.transferOk = target1Result.transferOk;
            resource2.flushOk = target1Result.flushOk;
            resource1.patternChecksum = target0Result.checksum;
            resource2.patternChecksum = target1Result.checksum;
            resource1.checksumValid = target0Result.checksum != 0u;
            resource2.checksumValid = target1Result.checksum != 0u;
            resource1.patternName = target0Result.renderOk ? "compositor-single-frame" : primaryPalette.name;
            resource2.patternName = target1Result.renderOk ? "compositor-single-frame" : secondaryPalette.name;

            postRenderEnabledScanouts = transport.enabledScanouts;
            stageBEnabledScanouts = postRenderEnabledScanouts;
            distinctPatternsConfirmed = (target0Result.checksum != 0u
                && target1Result.checksum != 0u
                && target0Result.checksum != target1Result.checksum);
            renderingTestPattern = false;

            transport.mmioStopReason = finalReason;
            transport.probeComplete = true;
            s_probeOutcome.deviceConfigNumScanouts = transport.mmioDeviceScanouts;
            s_probeOutcome.qemuMaxOutputsIntent = kDiagnosticQemuMaxOutputsIntent;
            s_probeOutcome.enabledScanoutsAfter = stageBEnabledScanouts != 0u ? stageBEnabledScanouts : stageAEnabledScanouts;
            s_probeOutcome.resource2dReady = resource2dReady;
            s_probeOutcome.backingAttached = backingAttached;
            s_probeOutcome.scanout0Set = scanout0Set;
            s_probeOutcome.transferOk = target0Result.transferOk;
            s_probeOutcome.flushOk = target0Result.flushOk;
            s_probeOutcome.resource2dReadySecondary = resource2dReadySecondary;
            s_probeOutcome.backingAttachedSecondary = backingAttachedSecondary;
            s_probeOutcome.scanout1Set = scanout1Set;
            s_probeOutcome.transfer1Ok = target1Result.transferOk;
            s_probeOutcome.flush1Ok = target1Result.flushOk;
            s_probeOutcome.distinctPatternsConfirmed = distinctPatternsConfirmed;
            s_probeOutcome.renderingTestPattern = renderingTestPattern;
            s_probeOutcome.contentMode = (target0Result.renderOk && target1Result.renderOk)
                ? "compositor-single-frame"
                : "fallback-patterns-after-failure";
            s_probeOutcome.frameMode = "single-shot";
            s_probeOutcome.continuousPresentationEnabled = false;

#if defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
            const bool liveResourcesReady = target0Result.renderOk
                && target0Result.transferOk
                && target0Result.flushOk
                && target1Result.renderOk
                && target1Result.transferOk
                && target1Result.flushOk
                && resource1.created
                && resource1.backingAttached
                && resource1.scanoutSet
                && resource2.created
                && resource2.backingAttached
                && resource2.scanoutSet;
            s_livePresentation.device = &state;
            s_livePresentation.enabled = liveResourcesReady;
            s_livePresentation.stopped = !liveResourcesReady;
            s_livePresentation.stoppedReason = liveResourcesReady ? "awaiting-scheduler" : "initial-frame-failed";
            s_livePresentation.resource0 = resource1;
            s_livePresentation.resource1 = resource2;
            s_livePresentation.activeOutputCount = 2u;
            s_livePresentation.backing0 = &s_diagnosticBackingStorage0[0];
            s_livePresentation.backing1 = &s_diagnosticBackingStorage1[0];
            s_livePresentation.backingPhysical0 = primaryBackingPhysical;
            s_livePresentation.backingPhysical1 = secondaryBackingPhysical;
            s_livePresentation.totalBackingBytes = totalBackingBytes;
            s_livePresentation.backingPageCount = backingPageCount;
            s_livePresentation.selectedWidth = selectedWidth;
            s_livePresentation.selectedHeight = selectedHeight;
            s_livePresentation.virtualDesktopWidth = selectedWidth * 2u;
            s_livePresentation.virtualDesktopHeight = selectedHeight;
            s_livePresentation.bytesPerPixel = bytesPerPixel;
            s_livePresentation.resourceFormat = static_cast<GpuFormat>(selectedFormat);
            s_livePresentation.initialTarget0Checksum = target0Result.checksum;
            s_livePresentation.initialTarget1Checksum = target1Result.checksum;
            s_livePresentation.finalTarget0Checksum = target0Result.checksum;
            s_livePresentation.finalTarget1Checksum = target1Result.checksum;
            if (liveResourcesReady) {
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                s_probeOutcome.contentMode = "compositor-live-manual";
                s_probeOutcome.frameMode = "manual";
#else
                s_probeOutcome.contentMode = "compositor-live-bounded";
                s_probeOutcome.frameMode = "bounded";
#endif
                s_probeOutcome.continuousPresentationEnabled = true;
            }
#endif

            updateOutputInventory();
#if defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
            if (s_livePresentation.enabled) {
                (void)initialize_display_event_observer(state);
            }
#endif
            record_probe_outcome(state,
                                 true,
                                 DisplayInfoOutcome::Ok,
                                 preRenderEnabledScanouts,
                                 preRenderDisabledScanouts,
                                 transport.displayInfoSlots,
                                 transport.mmioStopReason);
            log_compositor_proof_line(target0Result,
                                      target1Result,
                                      virtualDesktopWidth,
                                      virtualDesktopHeight,
                                      true,
                                      transport.mmioStopReason);
            return true;
        };

        DisplayRenderTarget compositorTarget0{};
        compositorTarget0.targetIndex = 1u;
        compositorTarget0.targetId = "virtio-gpu-target-1";
        compositorTarget0.source = "virtio-gpu";
        compositorTarget0.monitorId = 1u;
        compositorTarget0.monitorName = "Virtio GPU Output 0";
        compositorTarget0.scanoutId = 0u;
        compositorTarget0.resourceId = resource1.resourceId;
        compositorTarget0.viewportOriginX = 0;
        compositorTarget0.viewportOriginY = 0;
        compositorTarget0.width = static_cast<int>(selectedWidth);
        compositorTarget0.height = static_cast<int>(selectedHeight);
        compositorTarget0.framebufferRect = DisplayRect{ 0, 0, static_cast<int>(selectedWidth), static_cast<int>(selectedHeight) };
        compositorTarget0.preferredX = static_cast<int>(scanout0.x);
        compositorTarget0.preferredY = static_cast<int>(scanout0.y);
        compositorTarget0.preferredWidth = static_cast<int>(scanout0.width);
        compositorTarget0.preferredHeight = static_cast<int>(scanout0.height);
        compositorTarget0.assignedX = 0;
        compositorTarget0.assignedY = 0;
        compositorTarget0.assignedWidth = static_cast<int>(selectedWidth);
        compositorTarget0.assignedHeight = static_cast<int>(selectedHeight);
        compositorTarget0.primary = true;
        compositorTarget0.active = true;
        compositorTarget0.backedByHostedFramebuffer = false;
        compositorTarget0.backedByOutputResource = true;
        compositorTarget0.connectorEnabled = scanout0.enabled;
        compositorTarget0.resourceBound = resource1.scanoutSet;
        compositorTarget0.backingAttached = resource1.backingAttached;
        compositorTarget0.transferReady = resource1.transferOk;
        compositorTarget0.presentReady = resource1.flushOk;
        compositorTarget0.presentationConfirmed = resource1.flushOk && resource1.patternChecksum != 0u;
        compositorTarget0.syntheticHosted = false;
        compositorTarget0.backingVirtualAddress = primaryBackingVirtual;
        compositorTarget0.backingByteCount = totalBackingBytes;
        compositorTarget0.backingMemEntryCount = backingAudit1.totalMemEntries;
        compositorTarget0.patternChecksum = resource1.patternChecksum;
        compositorTarget0.lastCommandStatus = resource1.checksumValid ? "compositor target 0 ready" : "compositor target 0 pending";

#if defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
        store_live_target_descriptor(s_livePresentation.target0, compositorTarget0);
#endif

        target0Result = present_target_once(state,
                                            compositorTarget0,
                                            resource1,
                                            &s_diagnosticBackingStorage0[0],
                                            primaryBackingPhysical,
                                            totalBackingBytes,
                                            primaryPalette,
                                            static_cast<GpuFormat>(selectedFormat),
                                            selectedWidth,
                                            selectedHeight,
                                            bytesPerPixel,
                                            backingPageCount);

        if (!target0Result.renderOk) {
            postRenderEnabledScanouts = transport.enabledScanouts;
            stageBEnabledScanouts = postRenderEnabledScanouts;
            target1Result.blocker = target0Result.blocker;
            return finish(target0Result.blocker);
        }

        resource2.patternName = secondaryPalette.name;
        resource2.mirroredToFramebuffer = false;
        memzero(&s_diagnosticBackingStorage1[0], static_cast<size_t>(totalBackingBytes));
        fill_diagnostic_pattern(reinterpret_cast<uint32_t*>(&s_diagnosticBackingStorage1[0]),
                                selectedWidth,
                                selectedHeight,
                                selectedWidth,
                                secondaryPalette);
        patternChecksum2 = checksum_diagnostic_pattern(&s_diagnosticBackingStorage1[0], static_cast<size_t>(totalBackingBytes));
        resource2.patternChecksum = patternChecksum2;
        resource2.checksumValid = true;
        kernel::serial::puts("[VIRTIO-GPU] Diagnostic pattern name=");
        kernel::serial::puts(secondaryPalette.name);
        kernel::serial::puts(" width=");
        serial_put_u32_decimal(selectedWidth);
        kernel::serial::puts(" height=");
        serial_put_u32_decimal(selectedHeight);
        kernel::serial::puts(" stride=");
        serial_put_u64_decimal(strideBytes);
        kernel::serial::puts(" byteCount=");
        serial_put_u64_decimal(totalBackingBytes);
        kernel::serial::puts(" checksum=0x");
        kernel::serial::put_hex64(patternChecksum2);
        kernel::serial::putc('\n');

        if (!build_diagnostic_backing_layout(&s_diagnosticBackingStorage1[0],
                                             totalBackingBytes,
                                             backingEntries2,
                                             kDiagnosticBackingMaxMemEntries,
                                             &backingAudit2)) {
            target1Result.blocker = "secondary diagnostic backing physical coverage validation failed";
            return finish(target1Result.blocker);
        }

        const char* commandReason = nullptr;
        bool commandCompleted = false;
        if (!issue_resource_create_2d(state,
                                      resource2,
                                      resource1.resourceId,
                                      kDiagnosticResourceIdSecondary,
                                      selectedWidth,
                                      selectedHeight,
                                      static_cast<GpuFormat>(selectedFormat),
                                      false,
                                      &commandReason,
                                      &commandCompleted)) {
            target1Result.blocker = commandReason != nullptr ? commandReason : "secondary resource create failed";
            return finish(target1Result.blocker);
        }

        resource2dReadySecondary = true;

        if (!issue_resource_attach_backing(state,
                                           resource2,
                                           secondaryBackingVirtual,
                                           secondaryBackingPhysical,
                                           totalBackingBytes,
                                           backingPageCount,
                                           backingEntries2,
                                           backingAudit2.totalMemEntries,
                                           backingAudit2,
                                           &commandReason,
                                           &commandCompleted)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, commandCompleted, commandReason);
            target1Result.blocker = commandReason != nullptr ? commandReason : "secondary backing attach failed";
            return finish(target1Result.blocker);
        }

        backingAttachedSecondary = true;

        if (!issue_set_scanout(state,
                               resource2,
                               1u,
                               selectedWidth,
                               selectedHeight,
                               &commandReason,
                               &commandCompleted)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, commandCompleted, commandReason);
            target1Result.blocker = commandReason != nullptr ? commandReason : "secondary scanout assignment failed";
            return finish(target1Result.blocker);
        }

        scanout1Set = true;

        DisplayRenderTarget compositorTarget1{};
        compositorTarget1.targetIndex = 2u;
        compositorTarget1.targetId = "virtio-gpu-target-2";
        compositorTarget1.source = "virtio-gpu";
        compositorTarget1.monitorId = 2u;
        compositorTarget1.monitorName = "Virtio GPU Output 1";
        compositorTarget1.scanoutId = 1u;
        compositorTarget1.resourceId = resource2.resourceId;
        compositorTarget1.viewportOriginX = static_cast<int>(selectedWidth);
        compositorTarget1.viewportOriginY = 0;
        compositorTarget1.width = static_cast<int>(selectedWidth);
        compositorTarget1.height = static_cast<int>(selectedHeight);
        compositorTarget1.framebufferRect = DisplayRect{ 0, 0, static_cast<int>(selectedWidth), static_cast<int>(selectedHeight) };
        compositorTarget1.preferredX = static_cast<int>(scanout1Initial.x);
        compositorTarget1.preferredY = static_cast<int>(scanout1Initial.y);
        compositorTarget1.preferredWidth = static_cast<int>(scanout1Initial.width);
        compositorTarget1.preferredHeight = static_cast<int>(scanout1Initial.height);
        compositorTarget1.assignedX = static_cast<int>(selectedWidth);
        compositorTarget1.assignedY = 0;
        compositorTarget1.assignedWidth = static_cast<int>(selectedWidth);
        compositorTarget1.assignedHeight = static_cast<int>(selectedHeight);
        compositorTarget1.primary = false;
        compositorTarget1.active = true;
        compositorTarget1.backedByHostedFramebuffer = false;
        compositorTarget1.backedByOutputResource = true;
        compositorTarget1.connectorEnabled = scanout1Initial.enabled;
        compositorTarget1.resourceBound = resource2.scanoutSet;
        compositorTarget1.backingAttached = resource2.backingAttached;
        compositorTarget1.transferReady = resource2.transferOk;
        compositorTarget1.presentReady = resource2.flushOk;
        compositorTarget1.presentationConfirmed = resource2.flushOk && resource2.patternChecksum != 0u;
        compositorTarget1.syntheticHosted = false;
        compositorTarget1.backingVirtualAddress = secondaryBackingVirtual;
        compositorTarget1.backingByteCount = totalBackingBytes;
        compositorTarget1.backingMemEntryCount = backingAudit2.totalMemEntries;
        compositorTarget1.patternChecksum = resource2.patternChecksum;
        compositorTarget1.lastCommandStatus = resource2.checksumValid ? "compositor target 1 ready" : "compositor target 1 pending";

#if defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
        store_live_target_descriptor(s_livePresentation.target1, compositorTarget1);
#endif

        target1Result = present_target_once(state,
                                            compositorTarget1,
                                            resource2,
                                            &s_diagnosticBackingStorage1[0],
                                            secondaryBackingPhysical,
                                            totalBackingBytes,
                                            secondaryPalette,
                                            static_cast<GpuFormat>(selectedFormat),
                                            selectedWidth,
                                            selectedHeight,
                                            bytesPerPixel,
                                            backingPageCount);

        if (!submit_display_info_request(state, "post-render", false)) {
            target1Result.blocker = transport.mmioStopReason != nullptr ? transport.mmioStopReason : "post-render display-info request failed";
            return finish(target1Result.blocker);
        }

        return finish(nullptr);
    };

    kernel::serial::puts("[VIRTIO-GPU] Diagnostic scanout 1 initial enabled=");
    kernel::serial::puts(scanout1Initial.enabled ? "yes" : "no");
    kernel::serial::puts(" x=");
    serial_put_u32_decimal(scanout1Initial.x);
    kernel::serial::puts(" y=");
    serial_put_u32_decimal(scanout1Initial.y);
    kernel::serial::puts(" width=");
    serial_put_u32_decimal(scanout1Initial.width);
    kernel::serial::puts(" height=");
    serial_put_u32_decimal(scanout1Initial.height);
    kernel::serial::putc('\n');

    kernel::serial::puts("[VIRTIO-GPU] Diagnostic test pattern format=");
    kernel::serial::puts(gpu_format_name(static_cast<GpuFormat>(selectedFormat)));
    kernel::serial::puts(" selectedWidth=");
    serial_put_u32_decimal(selectedWidth);
    kernel::serial::puts(" selectedHeight=");
    serial_put_u32_decimal(selectedHeight);
    kernel::serial::puts(" bytesPerPixel=");
    serial_put_u32_decimal(bytesPerPixel);
    kernel::serial::puts(" stride=");
    serial_put_u64_decimal(strideBytes);
    kernel::serial::puts(" totalBackingBytes=");
    serial_put_u64_decimal(totalBackingBytes);
    kernel::serial::puts(" pageCount=");
    serial_put_u64_decimal(backingPageCount);
    kernel::serial::puts(" entryCount<= ");
    serial_put_u32_decimal(kDiagnosticBackingMaxMemEntries);
    kernel::serial::puts(" fallbackGeometry=");
    kernel::serial::puts(usedFallbackGeometry ? "yes" : "no");
    kernel::serial::putc('\n');

    memzero(&s_diagnosticBackingStorage0[0], static_cast<size_t>(totalBackingBytes));
    fill_diagnostic_pattern(reinterpret_cast<uint32_t*>(&s_diagnosticBackingStorage0[0]),
                            selectedWidth,
                            selectedHeight,
                            selectedWidth,
                            primaryPalette);
    patternChecksum1 = checksum_diagnostic_pattern(&s_diagnosticBackingStorage0[0], static_cast<size_t>(totalBackingBytes));
    resource1.patternChecksum = patternChecksum1;
    resource1.checksumValid = true;
    kernel::serial::puts("[VIRTIO-GPU] Diagnostic pattern name=");
    kernel::serial::puts(primaryPalette.name);
    kernel::serial::puts(" width=");
    serial_put_u32_decimal(selectedWidth);
    kernel::serial::puts(" height=");
    serial_put_u32_decimal(selectedHeight);
    kernel::serial::puts(" stride=");
    serial_put_u64_decimal(strideBytes);
    kernel::serial::puts(" byteCount=");
    serial_put_u64_decimal(totalBackingBytes);
    kernel::serial::puts(" checksum=0x");
    kernel::serial::put_hex64(patternChecksum1);
    kernel::serial::putc('\n');

    if (!build_diagnostic_backing_layout(&s_diagnosticBackingStorage0[0],
                                         totalBackingBytes,
                                         backingEntries1,
                                         kDiagnosticBackingMaxMemEntries,
                                         &backingAudit1)) {
        return fail_and_record("primary diagnostic backing physical coverage validation failed", displayInfoOutcome);
    }

    kernel::serial::puts("[VIRTIO-GPU] Primary backing lifetime=static until QEMU exit or cleanup\n");

    const char* commandReason = nullptr;
    bool commandCompleted = false;
    if (!issue_resource_create_2d(state,
                                  resource1,
                                  0u,
                                  kDiagnosticResourceId,
                                  selectedWidth,
                                  selectedHeight,
                                  static_cast<GpuFormat>(selectedFormat),
                                  true,
                                  &commandReason,
                                  &commandCompleted)) {
        return fail_and_record(commandReason, displayInfoOutcome);
    }

    resource2dReady = true;

    if (!issue_resource_attach_backing(state,
                                       resource1,
                                       primaryBackingVirtual,
                                       primaryBackingPhysical,
                                       totalBackingBytes,
                                       backingPageCount,
                                       backingEntries1,
                                       backingAudit1.totalMemEntries,
                                       backingAudit1,
                                       &commandReason,
                                       &commandCompleted)) {
        cleanup_diagnostic_resource_if_safe(state, resource1, commandCompleted, commandReason);
        return fail_and_record(commandReason, displayInfoOutcome);
    }

    backingAttached = true;

    if (!issue_set_scanout(state,
                           resource1,
                           0u,
                           selectedWidth,
                           selectedHeight,
                           &commandReason,
                           &commandCompleted)) {
        cleanup_diagnostic_resource_if_safe(state, resource1, commandCompleted, commandReason);
        return fail_and_record(commandReason, displayInfoOutcome);
    }

    scanout0Set = true;

#if !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_FRAME_ACTIVE) && !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    if (!issue_transfer_to_host_2d(state,
                                   resource1,
                                   0u,
                                   &commandReason,
                                   &commandCompleted)) {
        cleanup_diagnostic_resource_if_safe(state, resource1, commandCompleted, commandReason);
        return fail_and_record(commandReason, displayInfoOutcome);
    }

    transferOk = true;

    if (!issue_resource_flush(state,
                              resource1,
                              0u,
                              &commandReason,
                              &commandCompleted)) {
        cleanup_diagnostic_resource_if_safe(state, resource1, commandCompleted, commandReason);
        return fail_and_record(commandReason, displayInfoOutcome);
    }

    flushOk = true;

    if (!submit_display_info_request(state, "post-render", false)) {
        cleanup_diagnostic_resource_if_safe(state, resource1, true, transport.mmioStopReason);
        return fail_and_record(transport.mmioStopReason, displayInfoOutcome);
    }

    postRenderEnabledScanouts = transport.enabledScanouts;
    stageAEnabledScanouts = postRenderEnabledScanouts;
    renderingTestPattern = true;
    updateOutputInventory();

    kernel::serial::puts("[VIRTIO-GPU] Single-output proof: resource1=");
    kernel::serial::puts(resource1.created ? "ready" : "blocked");
    kernel::serial::puts(" backing1=");
    kernel::serial::puts(backingAudit1.physicalCoverageValid ? "valid" : "invalid");
    kernel::serial::puts(" scanout0=");
    kernel::serial::puts(resource1.scanoutSet ? "set" : "blocked");
    kernel::serial::puts(" transfer0=");
    kernel::serial::puts(resource1.transferOk ? "ok" : "blocked");
    kernel::serial::puts(" flush0=");
    kernel::serial::puts(resource1.flushOk ? "ok" : "blocked");
    kernel::serial::puts(" patternChecksum=0x");
    kernel::serial::put_hex64(resource1.patternChecksum);
    kernel::serial::putc('\n');
#endif

#if defined(GXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE)
    do {
        kernel::serial::puts("[VIRTIO-GPU] Stage B scanout capacity deviceConfigNumScanouts=");
        serial_put_u32_decimal(transport.mmioDeviceScanouts);
        kernel::serial::puts(" scanout1InitialEnabled=");
        kernel::serial::puts(scanout1Initial.enabled ? "yes" : "no");
        kernel::serial::puts(" qemuMaxOutputsIntent=");
        serial_put_u32_decimal(kDiagnosticQemuMaxOutputsIntent);
        kernel::serial::putc('\n');

#if defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_FRAME_ACTIVE) || defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
        if (run_compositor_frame()) {
            return true;
        }
#endif

        resource2.patternName = secondaryPalette.name;

        memzero(&s_diagnosticBackingStorage1[0], static_cast<size_t>(totalBackingBytes));
        fill_diagnostic_pattern(reinterpret_cast<uint32_t*>(&s_diagnosticBackingStorage1[0]),
                                selectedWidth,
                                selectedHeight,
                                selectedWidth,
                                secondaryPalette);
        patternChecksum2 = checksum_diagnostic_pattern(&s_diagnosticBackingStorage1[0], static_cast<size_t>(totalBackingBytes));
        resource2.patternChecksum = patternChecksum2;
        resource2.checksumValid = true;
        kernel::serial::puts("[VIRTIO-GPU] Diagnostic pattern name=");
        kernel::serial::puts(secondaryPalette.name);
        kernel::serial::puts(" width=");
        serial_put_u32_decimal(selectedWidth);
        kernel::serial::puts(" height=");
        serial_put_u32_decimal(selectedHeight);
        kernel::serial::puts(" stride=");
        serial_put_u64_decimal(strideBytes);
        kernel::serial::puts(" byteCount=");
        serial_put_u64_decimal(totalBackingBytes);
        kernel::serial::puts(" checksum=0x");
        kernel::serial::put_hex64(patternChecksum2);
        kernel::serial::putc('\n');

        if (!build_diagnostic_backing_layout(&s_diagnosticBackingStorage1[0],
                                             totalBackingBytes,
                                             backingEntries2,
                                             kDiagnosticBackingMaxMemEntries,
                                             &backingAudit2)) {
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=secondary diagnostic backing physical coverage validation failed\n");
            break;
        }

        commandReason = nullptr;
        commandCompleted = false;
        if (!issue_resource_create_2d(state,
                                      resource2,
                                      resource1.resourceId,
                                      kDiagnosticResourceIdSecondary,
                                      selectedWidth,
                                      selectedHeight,
                                      static_cast<GpuFormat>(selectedFormat),
                                      false,
                                      &commandReason,
                                      &commandCompleted)) {
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
            kernel::serial::puts(commandReason != nullptr ? commandReason : "n/a");
            kernel::serial::putc('\n');
            break;
        }

        resource2dReadySecondary = true;

        if (!issue_resource_attach_backing(state,
                                           resource2,
                                           secondaryBackingVirtual,
                                           secondaryBackingPhysical,
                                           totalBackingBytes,
                                           backingPageCount,
                                           backingEntries2,
                                           backingAudit2.totalMemEntries,
                                           backingAudit2,
                                           &commandReason,
                                           &commandCompleted)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, commandCompleted, commandReason);
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
            kernel::serial::puts(commandReason != nullptr ? commandReason : "n/a");
            kernel::serial::putc('\n');
            break;
        }

        backingAttachedSecondary = true;

        if (!issue_set_scanout(state,
                               resource2,
                               1u,
                               selectedWidth,
                               selectedHeight,
                               &commandReason,
                               &commandCompleted)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, commandCompleted, commandReason);
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
            kernel::serial::puts(commandReason != nullptr ? commandReason : "n/a");
            kernel::serial::putc('\n');
            break;
        }

        scanout1Set = true;

        if (!issue_transfer_to_host_2d(state,
                                       resource2,
                                       1u,
                                       &commandReason,
                                       &commandCompleted)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, commandCompleted, commandReason);
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
            kernel::serial::puts(commandReason != nullptr ? commandReason : "n/a");
            kernel::serial::putc('\n');
            break;
        }

        transfer1Ok = true;

        if (!issue_resource_flush(state,
                                  resource2,
                                  1u,
                                  &commandReason,
                                  &commandCompleted)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, commandCompleted, commandReason);
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
            kernel::serial::puts(commandReason != nullptr ? commandReason : "n/a");
            kernel::serial::putc('\n');
            break;
        }

        flush1Ok = true;

        if (!submit_display_info_request(state, "post-scanout1", false)) {
            cleanup_diagnostic_resource_if_safe(state, resource2, true, transport.mmioStopReason);
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
            kernel::serial::puts(transport.mmioStopReason != nullptr ? transport.mmioStopReason : "n/a");
            kernel::serial::putc('\n');
            break;
        }

        stageBEnabledScanouts = transport.enabledScanouts;
        distinctPatternsConfirmed = (resource1.patternChecksum != 0u && resource1.patternChecksum != resource2.patternChecksum);
        if (!distinctPatternsConfirmed) {
            cleanup_diagnostic_resource_if_safe(state, resource2, true, "distinct diagnostic patterns were not confirmed");
            stageBEnabledScanouts = postRenderEnabledScanouts;
            kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=distinct diagnostic patterns were not confirmed\n");
            break;
        }

        renderingTestPattern = true;
    } while (false);

    if (!distinctPatternsConfirmed && (scanout1Set || transfer1Ok || flush1Ok || resource2dReadySecondary || backingAttachedSecondary)) {
        // resource2 cleanup already handled above when a command completed.
    }

    if (!scanout1Set || !transfer1Ok || !flush1Ok || !distinctPatternsConfirmed) {
        s_probeOutcome.enabledScanoutsAfter = stageBEnabledScanouts != 0u ? stageBEnabledScanouts : stageAEnabledScanouts;
        s_probeOutcome.resource2dReady = resource2dReady;
        s_probeOutcome.backingAttached = backingAttached;
        s_probeOutcome.scanout0Set = scanout0Set;
        s_probeOutcome.transferOk = transferOk;
        s_probeOutcome.flushOk = flushOk;
        s_probeOutcome.resource2dReadySecondary = resource2dReadySecondary;
        s_probeOutcome.backingAttachedSecondary = backingAttachedSecondary;
        s_probeOutcome.scanout1Set = scanout1Set;
        s_probeOutcome.transfer1Ok = transfer1Ok;
        s_probeOutcome.flush1Ok = flush1Ok;
        s_probeOutcome.distinctPatternsConfirmed = distinctPatternsConfirmed;
        s_probeOutcome.renderingTestPattern = renderingTestPattern;
        transport.mmioStopReason = transport.mmioStopReason != nullptr && transport.mmioStopReason[0] != '\0'
            ? transport.mmioStopReason
            : "scanout 1 activation blocker";
        transport.probeComplete = true;
        record_probe_outcome(state,
                             true,
                             DisplayInfoOutcome::Ok,
                             preRenderEnabledScanouts,
                             preRenderDisabledScanouts,
                             transport.displayInfoSlots,
                             transport.mmioStopReason);
        kernel::serial::puts("[VIRTIO-GPU] Single-output proof: resource1=");
        kernel::serial::puts(resource1.created ? "ready" : "blocked");
        kernel::serial::puts(" backing1=");
        kernel::serial::puts(backingAudit1.physicalCoverageValid ? "valid" : "invalid");
        kernel::serial::puts(" scanout0=");
        kernel::serial::puts(resource1.scanoutSet ? "set" : "blocked");
        kernel::serial::puts(" transfer0=");
        kernel::serial::puts(resource1.transferOk ? "ok" : "blocked");
        kernel::serial::puts(" flush0=");
        kernel::serial::puts(resource1.flushOk ? "ok" : "blocked");
        kernel::serial::puts(" patternChecksum=0x");
        kernel::serial::put_hex64(resource1.patternChecksum);
        kernel::serial::putc('\n');
        kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: scanout0=working scanout1=failed reason=");
        kernel::serial::puts(transport.mmioStopReason);
        kernel::serial::putc('\n');
        s_probeOutcome.deviceConfigNumScanouts = transport.mmioDeviceScanouts;
        s_probeOutcome.qemuMaxOutputsIntent = kDiagnosticQemuMaxOutputsIntent;
        updateOutputInventory();
        return true;
    }
#endif

    kernel::serial::puts("[VIRTIO-GPU] Dual-output proof: resource1=");
    kernel::serial::puts(resource1.created ? "ready" : "blocked");
    kernel::serial::puts(" resource2=");
    kernel::serial::puts(resource2.created ? "ready" : "blocked");
    kernel::serial::puts(" scanout0=");
    kernel::serial::puts(resource1.scanoutSet ? "set" : "blocked");
    kernel::serial::puts(" scanout1=");
    kernel::serial::puts(resource2.scanoutSet ? "set" : "blocked");
    kernel::serial::puts(" transfer0=");
    kernel::serial::puts(resource1.transferOk ? "ok" : "blocked");
    kernel::serial::puts(" transfer1=");
    kernel::serial::puts(resource2.transferOk ? "ok" : "blocked");
    kernel::serial::puts(" flush0=");
    kernel::serial::puts(resource1.flushOk ? "ok" : "blocked");
    kernel::serial::puts(" flush1=");
    kernel::serial::puts(resource2.flushOk ? "ok" : "blocked");
    kernel::serial::puts(" enabledScanoutsAfter=");
    serial_put_u32_decimal(stageBEnabledScanouts != 0u ? stageBEnabledScanouts : stageAEnabledScanouts);
    kernel::serial::puts(" distinctPatterns=");
    kernel::serial::puts(distinctPatternsConfirmed ? "yes" : "no");
    kernel::serial::putc('\n');

    transport.mmioStopReason = "dual-output scanout 1 test pattern milestone complete";
    transport.probeComplete = true;
    s_probeOutcome.deviceConfigNumScanouts = transport.mmioDeviceScanouts;
    s_probeOutcome.qemuMaxOutputsIntent = kDiagnosticQemuMaxOutputsIntent;
    s_probeOutcome.enabledScanoutsAfter = stageBEnabledScanouts != 0u ? stageBEnabledScanouts : stageAEnabledScanouts;
    s_probeOutcome.resource2dReady = resource2dReady;
    s_probeOutcome.backingAttached = backingAttached;
    s_probeOutcome.scanout0Set = scanout0Set;
    s_probeOutcome.transferOk = transferOk;
    s_probeOutcome.flushOk = flushOk;
    s_probeOutcome.resource2dReadySecondary = resource2dReadySecondary;
    s_probeOutcome.backingAttachedSecondary = backingAttachedSecondary;
    s_probeOutcome.scanout1Set = scanout1Set;
    s_probeOutcome.transfer1Ok = transfer1Ok;
    s_probeOutcome.flush1Ok = flush1Ok;
    s_probeOutcome.distinctPatternsConfirmed = distinctPatternsConfirmed;
    s_probeOutcome.renderingTestPattern = renderingTestPattern;
    updateOutputInventory();
    record_probe_outcome(state,
                         true,
                         DisplayInfoOutcome::Ok,
                         preRenderEnabledScanouts,
                         preRenderDisabledScanouts,
                         transport.displayInfoSlots,
                         transport.mmioStopReason);
    return true;
}
#endif // GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE

static void print_device_summary(const DeviceState& state)
{
    const GpuDevice& device = state.device;
    const ModernTransport& transport = state.transport;

    kernel::serial::puts("[VIRTIO-GPU] Device summary:\n");
    kernel::serial::puts("  pci=");
    kernel::serial::put_hex8(device.pciBus);
    kernel::serial::putc(':');
    kernel::serial::put_hex8(device.pciDevice);
    kernel::serial::putc('.');
    kernel::serial::put_hex8(device.pciFunction);
    kernel::serial::puts(" vendor=0x");
    kernel::serial::put_hex16(transport.vendorId);
    kernel::serial::puts(" device=0x");
    kernel::serial::put_hex16(transport.deviceId);
    kernel::serial::puts(" modern=");
    kernel::serial::puts(transport.modern ? "yes" : "no");
    kernel::serial::putc('\n');

    kernel::serial::puts("  revision=0x");
    kernel::serial::put_hex8(transport.revision);
    kernel::serial::puts(" class=0x");
    kernel::serial::put_hex8(transport.classCode);
    kernel::serial::puts(" subclass=0x");
    kernel::serial::put_hex8(transport.subclass);
    kernel::serial::puts(" progIf=0x");
    kernel::serial::put_hex8(transport.progIf);
    kernel::serial::putc('\n');

    kernel::serial::puts("  subsystem=0x");
    kernel::serial::put_hex16(transport.subsystemVendorId);
    kernel::serial::putc(':');
    kernel::serial::put_hex16(transport.subsystemDeviceId);
    kernel::serial::putc('\n');

    kernel::serial::puts("  initialized=");
    kernel::serial::puts(device.initialized ? "yes" : "no");
    kernel::serial::puts(" mmioMapped=");
    kernel::serial::puts(transport.mmioMapped ? "yes" : "no");
    kernel::serial::puts(" sanityReads=");
    kernel::serial::puts(transport.mmioSanityReadsOk ? "ok" : "blocked");
    kernel::serial::puts(" mappingVirtual=");
    if (transport.mmioMapped) {
        kernel::serial::puts("0x");
        kernel::serial::put_hex64(transport.mmioMappedVirtual);
    } else {
        kernel::serial::puts("n/a");
    }
    kernel::serial::puts(" pageCount=");
    serial_put_u64_decimal(transport.mmioMappedPageCount);
    kernel::serial::puts(" cacheMode=");
    kernel::serial::puts(transport.mmioCacheMode != nullptr ? transport.mmioCacheMode : "n/a");
    kernel::serial::puts(" stopReason=");
    kernel::serial::puts(transport.mmioStopReason != nullptr ? transport.mmioStopReason : "n/a");
    kernel::serial::putc('\n');

    kernel::serial::puts("  scanouts=");
    serial_put_u32_decimal(device.numScanouts);
    kernel::serial::puts(" queueSize=");
    serial_put_u32_decimal(transport.queueSize);
    kernel::serial::putc('\n');

    for (uint32_t i = 0; i < device.numScanouts; ++i) {
        kernel::serial::puts("  scanout[");
        serial_put_u32_decimal(i);
        kernel::serial::puts("] enabled=");
        kernel::serial::puts(device.displays[i].enabled ? "1" : "0");
        kernel::serial::puts(" width=");
        serial_put_u32_decimal(device.displays[i].width);
        kernel::serial::puts(" height=");
        serial_put_u32_decimal(device.displays[i].height);
        kernel::serial::putc('\n');
    }
}

static DeviceState* active_state(GpuDevice* device)
{
    return find_state(device);
}

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
static void log_live_target_failure(uint64_t frameSequence,
                                    uint32_t targetId,
                                    const char* command,
                                    const char* response)
{
    kernel::serial::puts("[VIRTIO-GPU] live failure frame=");
    serial_put_u64_decimal(frameSequence);
    kernel::serial::puts(" target=");
    serial_put_u32_decimal(targetId);
    kernel::serial::puts(" command=");
    kernel::serial::puts(command != nullptr ? command : "unknown");
    kernel::serial::puts(" response=");
    kernel::serial::puts(response != nullptr && response[0] != '\0' ? response : "n/a");
    kernel::serial::puts(" timeout=");
    kernel::serial::puts(text_contains(response, "timed out") || text_contains(response, "timeout") ? "yes" : "no");
    kernel::serial::putc('\n');
}

static void print_live_presentation_summary()
{
    const LivePresentationBackendState& live = s_livePresentation;
    kernel::serial::puts("[VIRTIO-GPU] live presentation counters: presentationPolls=");
    serial_put_u32_decimal(live.presentationPolls);
    kernel::serial::puts(" eligibleAttempts=");
    serial_put_u32_decimal(live.eligibleAttempts);
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    kernel::serial::puts(" manualPresentationIterations=");
#else
    kernel::serial::puts(" boundedProofIterations=");
#endif
    serial_put_u32_decimal(live.boundedProofIterations);
    kernel::serial::puts(" renderedFrames=");
    serial_put_u32_decimal(live.framesRendered);
    kernel::serial::puts(" cleanSkips=");
    serial_put_u32_decimal(live.framesSkippedClean);
    kernel::serial::puts(" rateLimitSkips=");
    serial_put_u32_decimal(live.rateLimitSkips);
    kernel::serial::puts(" note=rateLimitSkips_counts_scheduler_polls_and_may_exceed_boundedProofIterations\n");
    kernel::serial::puts("[VIRTIO-GPU] VirtioGPU live presentation: enabled=");
    kernel::serial::puts(live.enabled ? "yes" : "no");
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    kernel::serial::puts(" outputs=2 contentMode=compositor-live-manual frameCap=unbounded frameLimit=disabled timeLimitTicks=disabled");
#else
    kernel::serial::puts(" outputs=2 contentMode=compositor-live-bounded frameCap=");
    serial_put_u32_decimal(live.configuredFrameCap);
    kernel::serial::puts(" frameLimit=");
    serial_put_u32_decimal(live.boundedRunLimit);
    kernel::serial::puts(" timeLimitTicks=");
    serial_put_u64_decimal(live.boundedTimeLimitTicks);
#endif
    kernel::serial::puts(" attempted=");
    serial_put_u32_decimal(live.framesAttempted);
    kernel::serial::puts(" rendered=");
    serial_put_u32_decimal(live.framesRendered);
    kernel::serial::puts(" dirtyFrames=");
    serial_put_u32_decimal(live.dirtyFrames);
    kernel::serial::puts(" cleanSkips=");
    serial_put_u32_decimal(live.framesSkippedClean);
    kernel::serial::puts(" rateSkips=");
    serial_put_u32_decimal(live.rateLimitSkips);
    kernel::serial::puts(" target0Frames=");
    serial_put_u32_decimal(live.target0FlushCount);
    kernel::serial::puts(" target1Frames=");
    serial_put_u32_decimal(live.target1FlushCount);
    kernel::serial::puts(" target0Transfer=");
    serial_put_u32_decimal(live.target0TransferCount);
    kernel::serial::puts(" target0Flush=");
    serial_put_u32_decimal(live.target0FlushCount);
    kernel::serial::puts(" target1Transfer=");
    serial_put_u32_decimal(live.target1TransferCount);
    kernel::serial::puts(" target1Flush=");
    serial_put_u32_decimal(live.target1FlushCount);
    kernel::serial::puts(" target0Failures=");
    serial_put_u32_decimal(live.target0Failures);
    kernel::serial::puts(" target1Failures=");
    serial_put_u32_decimal(live.target1Failures);
    kernel::serial::puts(" initialChecksum0=0x");
    kernel::serial::put_hex64(live.initialTarget0Checksum);
    kernel::serial::puts(" finalChecksum0=0x");
    kernel::serial::put_hex64(live.finalTarget0Checksum);
    kernel::serial::puts(" initialChecksum1=0x");
    kernel::serial::put_hex64(live.initialTarget1Checksum);
    kernel::serial::puts(" finalChecksum1=0x");
    kernel::serial::put_hex64(live.finalTarget1Checksum);
    kernel::serial::puts(" fallbackActivated=");
    kernel::serial::puts(live.fallbackActivated ? "yes" : "no");
    kernel::serial::puts(" fallbackTarget=");
    if (live.fallbackTargetValid) {
        serial_put_u32_decimal(live.fallbackTarget);
    } else {
        kernel::serial::puts("none");
    }
    kernel::serial::puts(" fallbackReason=");
    kernel::serial::puts(live.fallbackReason != nullptr ? live.fallbackReason : "none");
    kernel::serial::puts(" fallbackResult=");
    kernel::serial::puts(live.fallbackResult != nullptr ? live.fallbackResult : "not-used");
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    kernel::serial::puts(" continuousPresentation=manual stopReason=");
#else
    kernel::serial::puts(" continuousPresentation=bounded stopReason=");
#endif
    kernel::serial::puts(live.stoppedReason != nullptr ? live.stoppedReason : "unknown");
    kernel::serial::putc('\n');
}

static void stop_live_presentation(const char* reason)
{
    if (s_livePresentation.stopped) {
        return;
    }
    s_livePresentation.stopped = true;
    s_livePresentation.stoppedReason = reason != nullptr ? reason : "explicit-stop";
    print_live_presentation_summary();
}
#endif

} // namespace

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0) {
        s_kernelPhysicalBase = physicalBase;
    }
}

void init()
{
    if (s_initialized) {
        return;
    }

    s_initialized = true;
    memzero(&s_devices[0], sizeof(s_devices));
    s_deviceCount = 0;

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    kernel::serial::puts("[VIRTIO-GPU] Diagnostic probe enabled for QEMU virtio-gpu discovery\n");
    probe();
#endif
}

int probe()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    return 0;
#else
    if (!s_initialized) {
        init();
    }

    s_probeOutcome = ProbeOutcome{};
    s_probeOutcome.valid = false;
    s_probeOutcome.reason = "no compatible virtio-gpu PCI function found";
    s_displayConfigurationPresentationPaused = false;
    s_displayConfigurationMode = static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend);
    s_displayConfigurationPrimaryOutput = 0u;
    s_detectedConfigurationSnapshot = gxos::display::DisplayConfigurationSnapshot{};
    s_detectedConfigurationSnapshotReady = false;
    s_displayEventObserver = VirtioGpuDisplayEventObserver{};
#if defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    s_livePresentation = LivePresentationBackendState{};
    s_liveCommandLoggingSuppressed = false;
#endif

    kernel::serial::puts("[VIRTIO-GPU] Probing PCI bus for virtio-gpu devices\n");

    for (uint8_t bus = 0; bus < kProbeBusLimit; ++bus) {
        for (uint8_t device = 0; device < kProbeDeviceLimit; ++device) {
            uint32_t id0 = msi::pci_config_read32(bus, device, 0, 0x00);
            if (id0 == 0xFFFFFFFFu || id0 == 0) {
                continue;
            }

            uint8_t headerType = msi::pci_config_read8(bus, device, 0, kPciHeaderTypeOffset);
            uint8_t maxFunctions = (headerType & 0x80u) ? 8 : 1;

            for (uint8_t function = 0; function < maxFunctions; ++function) {
                uint32_t id = (function == 0) ? id0 : msi::pci_config_read32(bus, device, function, 0x00);
                if (id == 0xFFFFFFFFu || id == 0) {
                    continue;
                }

                uint16_t vendorId = static_cast<uint16_t>(id & 0xFFFFu);
                uint16_t deviceId = static_cast<uint16_t>(id >> 16);
                if (vendorId != kVirtioPciVendorId || deviceId != kVirtioGpuPciDeviceId) {
                    continue;
                }

                DeviceState* state = reserve_state();
                if (state == nullptr) {
                    kernel::serial::puts("[VIRTIO-GPU] Device capacity exhausted\n");
                    return s_deviceCount;
                }

                memzero(state, sizeof(DeviceState));
                state->transport.bus = bus;
                state->transport.device = device;
                state->transport.function = function;
                state->transport.vendorId = vendorId;
                state->transport.deviceId = deviceId;

                if (!probe_device(*state, bus, device, function)) {
                    kernel::serial::puts("[VIRTIO-GPU] Candidate matched but transport remained unresolved\n");
                    continue;
                }

                if (!initialize_device(*state)) {
                    kernel::serial::puts("[VIRTIO-GPU] Candidate initialization failed\n");
                    continue;
                }

                print_device_summary(*state);
                ++s_deviceCount;
            }
        }
    }

    kernel::serial::puts("[VIRTIO-GPU] Probe summary: detected devices=");
    serial_put_u32_decimal(static_cast<uint32_t>(s_deviceCount));
    kernel::serial::putc('\n');
    print_probe_outcome();

    return s_deviceCount;
#endif
}

GpuDevice* get_device(int index)
{
    if (index < 0 || index >= s_deviceCount) {
        return nullptr;
    }

    return &s_devices[index].device;
}

int device_count()
{
    return s_deviceCount;
}

bool read_virtio_gpu_config_snapshot(GpuDevice* dev, VirtioGpuConfigSnapshot* snapshot)
{
    if (snapshot == nullptr) return false;
    DeviceState* state = active_state(dev);
    if (state == nullptr) {
        *snapshot = VirtioGpuConfigSnapshot{};
        copy_event_text(snapshot->failureReason, sizeof(snapshot->failureReason), "VirtIO-GPU device is unavailable");
        return false;
    }
    return read_virtio_gpu_config_snapshot_internal(*state, *snapshot);
}

bool query_detected_topology_change(gxos::display::DisplayTopologyChangeQuery* query)
{
    if (query == nullptr) return false;
    *query = gxos::display::DisplayTopologyChangeQuery{};
    query->structureSize = sizeof(gxos::display::DisplayTopologyChangeQuery);
    const VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    query->pending = observer.pendingTopologyChange ? 1u : 0u;
    query->activeConfigurationAffected = observer.pendingChange.activeConfigurationAffected;
    query->automaticApplyPerformed = 0u;
    query->injectedEvent = observer.pendingChange.injectedEvent;
    query->genuineDeviceEvent = observer.pendingChange.genuineDeviceEvent;
    query->requiresUserAction = observer.pendingTopologyChange ? 1u : 0u;
    query->acknowledged = observer.pendingChange.acknowledged;
    query->dismissed = observer.pendingChange.dismissed;
    query->applied = observer.pendingChange.applied;
    query->metadataOnly = observer.pendingChange.metadataOnly;
    query->topologyGeneration = observer.topologyGeneration;
    query->injectedTopologyGeneration = observer.pendingChange.injectedTopologyGeneration;
    query->changeType = observer.pendingChange.changeType;
    query->addedOutputCount = observer.pendingChange.addedOutputCount;
    query->removedOutputCount = observer.pendingChange.removedOutputCount;
    query->changedOutputCount = observer.pendingChange.changedOutputCount;
    query->connectorEnabledChangeCount = observer.pendingChange.connectorEnabledChangeCount;
    query->preferredGeometryChangeCount = observer.pendingChange.preferredGeometryChangeCount;
    copy_event_text(query->classification, sizeof(query->classification), observer.pendingChange.classification);
    copy_event_text(query->recommendedAction, sizeof(query->recommendedAction), observer.pendingChange.recommendedAction);
    copy_event_text(query->reason, sizeof(query->reason), observer.pendingChange.reason);
    copy_event_text(query->source, sizeof(query->source), observer.pendingChange.source);
    copy_event_text(query->injectedChangeType, sizeof(query->injectedChangeType), observer.pendingChange.injectedChangeType);
    for (uint32_t i = 0u; i < gxos::display::kVirtioGpuDisplayEventMaxScanouts; ++i) {
        copy_event_text(query->addedOutputIdentities[i], sizeof(query->addedOutputIdentities[i]),
                        observer.pendingChange.addedOutputIdentities[i]);
        copy_event_text(query->removedOutputIdentities[i], sizeof(query->removedOutputIdentities[i]),
                        observer.pendingChange.removedOutputIdentities[i]);
    }
    return observer.initialized && observer.enabled;
}

bool refresh_detected_topology_for_service()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    return false;
#else
    DeviceState* state = s_livePresentation.device;
    if (state == nullptr && s_deviceCount > 0) state = &s_devices[0];
    if (state == nullptr || s_displayConfigurationPresentationPaused || s_livePresentation.stopped) return false;
    return refresh_detected_topology_without_event(*state);
#endif
}

bool inject_display_topology_change_for_test(uint32_t kind)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    (void)kind;
    return false;
#else
    return inject_display_topology_change_internal(kind);
#endif
}

bool dismiss_detected_topology_for_service(uint32_t topologyGeneration)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    (void)topologyGeneration;
    return false;
#else
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    if (!observer.initialized || !observer.enabled || !observer.pendingTopologyChange ||
        topologyGeneration == 0u || topologyGeneration != observer.topologyGeneration) return false;
    observer.pendingTopologyChange = false;
    observer.pendingChange.acknowledged = 1u;
    observer.pendingChange.dismissed = 1u;
    observer.pendingChange.applied = 0u;
    kernel::serial::puts("VirtioGPU pending topology dismissed: generation=");
    serial_put_u32_decimal(topologyGeneration);
    kernel::serial::puts(" activeMutation=no automaticApply=no injectedEvent=");
    kernel::serial::puts(observer.pendingChange.injectedEvent ? "yes\n" : "no\n");
    return true;
#endif
}

bool apply_detected_topology_for_service(uint32_t topologyGeneration)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE)
    (void)topologyGeneration;
    return false;
#else
    VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    if (!observer.initialized || !observer.enabled || !observer.pendingTopologyChange ||
        topologyGeneration == 0u || topologyGeneration != observer.topologyGeneration) return false;
    observer.pendingTopologyChange = false;
    observer.pendingChange.acknowledged = 1u;
    observer.pendingChange.applied = 1u;
    observer.pendingChange.dismissed = 0u;
    kernel::serial::puts("VirtioGPU pending topology applied: generation=");
    serial_put_u32_decimal(topologyGeneration);
    kernel::serial::puts(" activeMutation=authorized automaticApply=no injectedEvent=");
    kernel::serial::puts(observer.pendingChange.injectedEvent ? "yes\n" : "no\n");
    return true;
#endif
}

void get_display_event_observer_status(VirtioGpuDisplayEventObserverStatus* status)
{
    if (status == nullptr) return;
    *status = VirtioGpuDisplayEventObserverStatus{};
    const VirtioGpuDisplayEventObserver& observer = s_displayEventObserver;
    status->initialized = observer.initialized ? 1u : 0u;
    status->enabled = observer.enabled ? 1u : 0u;
    status->rescanInProgress = observer.rescanInProgress ? 1u : 0u;
    status->pendingTopologyChange = observer.pendingTopologyChange ? 1u : 0u;
    status->polls = observer.polls;
    status->coherentReads = observer.coherentReads;
    status->incoherentReads = observer.incoherentReads;
    status->eventsObserved = observer.eventsObserved;
    status->displayEventsObserved = observer.displayEventsObserved;
    status->unknownEventBitsObserved = observer.unknownEventBitsObserved;
    status->displayEventsProcessed = observer.displayEventsProcessed;
    status->eventClearWrites = observer.eventClearWrites;
    status->rescansSubmitted = observer.rescansSubmitted;
    status->rescansCoalesced = observer.rescansCoalesced;
    status->rescansSuccessful = observer.rescansSuccessful;
    status->rescansFailed = observer.rescansFailed;
    status->reassertions = observer.reassertions;
    status->lastEventsRead = observer.lastEventsRead;
    status->lastEventsCleared = observer.lastEventsCleared;
    status->lastConfigGeneration = observer.lastConfigGeneration;
    status->lastTopologyGeneration = observer.topologyGeneration;
    status->lastPollTick = observer.lastPollTick;
    status->pollInterval = observer.pollInterval;
    status->pendingRescanRetries = observer.pendingRescanRetries;
    status->lastReasserted = observer.lastReasserted ? 1u : 0u;
    copy_event_text(status->lastError, sizeof(status->lastError), observer.lastError);
    copy_event_text(status->disabledReason, sizeof(status->disabledReason), observer.disabledReason);
    (void)query_detected_topology_change(&status->pendingQuery);
}

GpuStatus init_device(GpuDevice* dev)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    (void)dev;
    return GPU_ERR_UNSUPPORTED;
#else
    DeviceState* state = active_state(dev);
    if (state == nullptr) {
        return GPU_ERR_INVALID;
    }

    if (state->device.initialized) {
        return GPU_OK;
    }

    kernel::serial::puts("[VIRTIO-GPU] init_device blocked: diagnostic-only probe stops before transport writes\n");
    return GPU_ERR_UNSUPPORTED;
#endif
}

GpuStatus reset_device(GpuDevice* dev)
{
    DeviceState* state = active_state(dev);
    if (state == nullptr) {
        return GPU_ERR_INVALID;
    }

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    (void)state;
    kernel::serial::puts("[VIRTIO-GPU] reset_device blocked: transport reset is disabled in diagnostic-only probe\n");
    return GPU_ERR_UNSUPPORTED;
#endif

    if (!state->device.isPci && state->device.baseAddr != 0) {
        mmio_write32(state->device.baseAddr + mmio::STATUS, 0);
        state->device.initialized = false;
        return GPU_OK;
    }

    return GPU_ERR_UNSUPPORTED;
}

GpuStatus get_display_info(GpuDevice* dev)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    (void)dev;
    return GPU_ERR_UNSUPPORTED;
#else
    DeviceState* state = active_state(dev);
    if (state == nullptr) {
        return GPU_ERR_INVALID;
    }

    kernel::serial::puts("[VIRTIO-GPU] GET_DISPLAY_INFO blocked: read-only transport probe stops before command submission\n");
    return GPU_ERR_UNSUPPORTED;
#endif
}

GpuStatus setup_framebuffer(GpuDevice* dev, uint32_t width, uint32_t height,
                            uint32_t scanoutId)
{
    (void)dev;
    (void)width;
    (void)height;
    (void)scanoutId;
    return GPU_ERR_UNSUPPORTED;
}

uint8_t* get_framebuffer(GpuDevice* dev)
{
    if (dev == nullptr || !dev->initialized) {
        return nullptr;
    }
    return dev->fbBuffer;
}

uint32_t get_framebuffer_width(GpuDevice* dev)
{
    return dev ? dev->fbWidth : 0;
}

uint32_t get_framebuffer_height(GpuDevice* dev)
{
    return dev ? dev->fbHeight : 0;
}

uint32_t get_framebuffer_pitch(GpuDevice* dev)
{
    return dev ? dev->fbWidth * 4 : 0;
}

GpuStatus flush_framebuffer(GpuDevice* dev, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height)
{
    (void)dev;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus flush_all(GpuDevice* dev)
{
    (void)dev;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus set_cursor(GpuDevice* dev, uint32_t resourceId,
                     uint32_t hotX, uint32_t hotY)
{
    (void)dev;
    (void)resourceId;
    (void)hotX;
    (void)hotY;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus move_cursor(GpuDevice* dev, uint32_t x, uint32_t y)
{
    (void)dev;
    (void)x;
    (void)y;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus hide_cursor(GpuDevice* dev)
{
    (void)dev;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus create_resource_2d(GpuDevice* dev, uint32_t* resourceIdOut,
                             uint32_t width, uint32_t height, GpuFormat format)
{
    (void)dev;
    (void)resourceIdOut;
    (void)width;
    (void)height;
    (void)format;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus attach_backing(GpuDevice* dev, uint32_t resourceId,
                         uint64_t physAddr, size_t size)
{
    (void)dev;
    (void)resourceId;
    (void)physAddr;
    (void)size;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus detach_backing(GpuDevice* dev, uint32_t resourceId)
{
    (void)dev;
    (void)resourceId;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus transfer_to_host(GpuDevice* dev, uint32_t resourceId,
                           uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    (void)dev;
    (void)resourceId;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return GPU_ERR_UNSUPPORTED;
}

GpuStatus destroy_resource(GpuDevice* dev, uint32_t resourceId)
{
    (void)dev;
    (void)resourceId;
    return GPU_ERR_UNSUPPORTED;
}

void irq_handler()
{
    kernel::serial::puts("[VIRTIO-GPU] IRQs are not used in diagnostic probe mode\n");
}

void poll(GpuDevice* dev)
{
    (void)dev;
}

void presentation_tick()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    return;
#else
    LivePresentationBackendState& live = s_livePresentation;
    if (!live.enabled || live.device == nullptr || live.stopped || s_displayConfigurationPresentationPaused) {
        return;
    }

    // The observer runs on this existing bounded service tick. It performs
    // only coherent config reads and a bounded GET_DISPLAY_INFO rescan; it
    // never changes live resources or scanout bindings.
    display_event_observer_tick(*live.device);

    ++live.presentationPolls;

    const uint64_t now = kernel::pit::ticks();
    if (!live.initialized) {
        live.initialized = true;
        live.presentationStartTicks = now;
        live.lastPresentationTicks = now;
        live.lastDirtyGeneration = kernel::desktop::redraw_generation();
        live.initialFrameReadyLogged = true;
        kernel::serial::puts("[VIRTIO-GPU] VirtioGPU live presentation configured: enabled=yes outputs=");
        serial_put_u32_decimal(live.activeOutputCount);
        kernel::serial::puts(" frameCap=");
        serial_put_u32_decimal(live.configuredFrameCap);
        kernel::serial::puts(" frameLimit=");
        serial_put_u32_decimal(live.boundedRunLimit);
        kernel::serial::puts(" timeLimitTicks=");
        serial_put_u64_decimal(live.boundedTimeLimitTicks);
        kernel::serial::puts(" rateIntervalTicks=");
        serial_put_u64_decimal(kLivePresentationIntervalTicks);
        kernel::serial::puts(" overlay=bounded-moving-marker\n");
        kernel::serial::puts("[VIRTIO-GPU] VirtioGPU live presentation: initial frame ready target0Checksum=0x");
        kernel::serial::put_hex64(live.initialTarget0Checksum);
        kernel::serial::puts(" target1Checksum=0x");
        kernel::serial::put_hex64(live.initialTarget1Checksum);
        kernel::serial::puts(" backingLifetime=static resourceIdsStable=yes scanoutsStable=yes\n");
        return;
    }

#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    if (now - live.presentationStartTicks >= live.boundedTimeLimitTicks) {
        stop_live_presentation("time-limit");
        return;
    }
    if (live.framesAttempted >= live.boundedRunLimit) {
        stop_live_presentation("frame-limit");
        return;
    }
#endif
    if (now - live.lastPresentationTicks < kLivePresentationIntervalTicks) {
        ++live.rateLimitSkips;
        return;
    }

    live.lastPresentationTicks = now;
    ++live.framesAttempted;
    ++live.eligibleAttempts;
    ++live.boundedProofIterations;
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    const bool overlayDue = false;
#else
    const bool overlayDue = (live.framesAttempted % kLivePresentationOverlayPeriodAttempts) == 1u;
#endif
    if (overlayDue) {
        // The overlay is a diagnostic-only visible state change, routed through
        // the normal desktop invalidation flag and rendered by this presenter.
        kernel::desktop::request_redraw();
    }

    uint64_t dirtyGeneration = kernel::desktop::redraw_generation();
    if (overlayDue) {
        // presentation_tick runs after the normal redraw consumer in main.cpp;
        // account for the pending invalidation without consuming it here.
        dirtyGeneration += 1u;
    }
    if (dirtyGeneration == live.lastDirtyGeneration) {
        ++live.framesSkippedClean;
#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
        if (live.framesAttempted >= live.boundedRunLimit) {
            stop_live_presentation("frame-limit");
        }
#endif
        return;
    }

    ++live.dirtyFrames;
    const uint64_t frameSequence = ++live.frameSequence;

    auto render_target = [&](uint32_t targetId,
                             const LivePresentationTargetDescriptor& descriptor,
                             DiagnosticResourceState& resource,
                             uint8_t* backing,
                             uint64_t backingPhysical,
                             const DiagnosticPatternPalette& palette,
                             uint32_t& transferCount,
                             uint32_t& flushCount,
                             uint32_t& failureCount,
                             uint32_t& failureStreak) -> CompositorFrameTargetResult {
        const DisplayRenderTarget target = make_live_target(descriptor);
        s_liveCommandLoggingSuppressed = true;
        CompositorFrameTargetResult result = present_target_once(*live.device,
                                                                  target,
                                                                  resource,
                                                                  backing,
                                                                  backingPhysical,
                                                                  resource.backingBytes,
                                                                  palette,
                                                                  live.resourceFormat,
                                                                  live.virtualDesktopWidth,
                                                                  live.virtualDesktopHeight,
                                                                  live.bytesPerPixel,
                                                                  live.backingPageCount,
                                                                  frameSequence,
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
                                                                   false,
#else
                                                                   true,
#endif
                                                                   false);
        if (result.transferOk) {
            ++transferCount;
        }
        if (result.flushOk) {
            ++flushCount;
        }

        const bool presented = result.renderOk && result.transferOk && result.flushOk;
        if (presented) {
            failureStreak = 0u;
        } else {
            ++failureCount;
            ++failureStreak;
            const char* command = !result.renderOk ? "RENDER"
                : (!result.transferOk ? "TRANSFER_TO_HOST_2D" : "RESOURCE_FLUSH");
            log_live_target_failure(frameSequence, targetId, command, result.blocker);

            if (failureStreak >= kLivePresentationFallbackFailureThreshold) {
                uint64_t fallbackChecksum = 0u;
                const bool fallbackOk = repaint_static_fallback_target(*live.device,
                                                                        resource,
                                                                        descriptor.scanoutId,
                                                                        backing,
                                                                        resource.width,
                                                                        resource.height,
                                                                        resource.backingBytes,
                                                                        palette,
                                                                        &fallbackChecksum);
                live.fallbackActivated = true;
                live.fallbackTargetValid = true;
                live.fallbackTarget = targetId;
                live.fallbackReason = result.blocker != nullptr ? result.blocker : command;
                live.fallbackResult = fallbackOk ? "static-pattern-repaint" : "static-pattern-repaint-failed";
                kernel::serial::puts("[VIRTIO-GPU] live fallback frame=");
                serial_put_u64_decimal(frameSequence);
                kernel::serial::puts(" target=");
                serial_put_u32_decimal(targetId);
                kernel::serial::puts(" activated=yes result=");
                kernel::serial::puts(fallbackOk ? "ok" : "failed");
                kernel::serial::puts(" checksum=0x");
                kernel::serial::put_hex64(fallbackChecksum);
                kernel::serial::putc('\n');
                if (fallbackOk) {
                    ++transferCount;
                    ++flushCount;
                }
                failureStreak = 0u;
            }
        }
        s_liveCommandLoggingSuppressed = false;
        return result;
    };

    CompositorFrameTargetResult target0Result = render_target(0u,
                                                               live.target0,
                                                               live.resource0,
                                                               live.backing0,
                                                               live.backingPhysical0,
                                                               diagnostic_pattern_palette(0u),
                                                               live.target0TransferCount,
                                                               live.target0FlushCount,
                                                               live.target0Failures,
                                                               live.target0FailureStreak);
    CompositorFrameTargetResult target1Result{};
    if (live.activeOutputCount > 1u && live.resource1.resourceId != 0u) {
        target1Result = render_target(1u,
                                      live.target1,
                                      live.resource1,
                                      live.backing1,
                                      live.backingPhysical1,
                                      diagnostic_pattern_palette(1u),
                                      live.target1TransferCount,
                                      live.target1FlushCount,
                                      live.target1Failures,
                                      live.target1FailureStreak);
    }

    const bool target0Presented = target0Result.renderOk && target0Result.transferOk && target0Result.flushOk;
    const bool target1Presented = live.activeOutputCount <= 1u ||
        (target1Result.renderOk && target1Result.transferOk && target1Result.flushOk);
    if (target0Presented || target1Presented) {
        ++live.framesRendered;
        live.lastPresentedFrame = frameSequence;
        live.lastDirtyGeneration = dirtyGeneration;
        live.finalTarget0Checksum = live.resource0.patternChecksum;
        live.finalTarget1Checksum = live.resource1.patternChecksum;
    }

#if !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    if (live.framesAttempted >= live.boundedRunLimit) {
        stop_live_presentation("frame-limit");
    }
#endif
#endif
}

bool presentation_finished()
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    return true;
#else
#if defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)
    return false;
#else
    return s_livePresentation.stopped;
#endif
#endif
}

bool get_display_input_layout(
    int32_t* left, int32_t* top, int32_t* right, int32_t* bottom,
    display_input::DisplayInputMonitor* monitors, uint8_t capacity,
    uint8_t* monitorCount)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    (void)monitors;
    (void)capacity;
    (void)monitorCount;
    return false;
#else
    if (left == nullptr || top == nullptr || right == nullptr || bottom == nullptr ||
        monitors == nullptr || monitorCount == nullptr || capacity == 0u ||
        !s_probeOutcome.valid) {
        return false;
    }
    const VirtioGpuOutputInventory& inventory = s_probeOutcome.outputInventory;
    if (inventory.monitors.empty() || inventory.monitors.size() > capacity) {
        return false;
    }
    *left = inventory.virtualDesktop.left;
    *top = inventory.virtualDesktop.top;
    *right = inventory.virtualDesktop.right;
    *bottom = inventory.virtualDesktop.bottom;
    *monitorCount = 0u;
    for (uint32_t i = 0; i < inventory.monitors.size(); ++i) {
        const DisplayMonitorDescriptor& source = inventory.monitors[i];
        display_input::DisplayInputMonitor& destination = monitors[i];
        destination.id = static_cast<int32_t>(source.id);
        destination.virtualX = source.virtualX;
        destination.virtualY = source.virtualY;
        destination.width = source.width;
        destination.height = source.height;
        destination.assignedX = source.assignedX;
        destination.assignedY = source.assignedY;
        destination.assignedWidth = source.assignedWidth;
        destination.assignedHeight = source.assignedHeight;
        destination.primary = source.primary;
        destination.enabled = source.enabled && source.width > 0 && source.height > 0;
        *monitorCount = static_cast<uint8_t>(i + 1u);
    }
    return *monitorCount > 0u;
#endif
}

namespace {

static void copy_display_contract_text(char* destination, uint32_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0u) return;
    uint32_t index = 0u;
    if (source != nullptr) {
        while (source[index] != '\0' && index + 1u < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
    while (++index < capacity) destination[index] = '\0';
}

static bool display_contract_text_equals(const char* left, const char* right)
{
    if (left == nullptr || right == nullptr) return left == right;
    uint32_t index = 0u;
    while (left[index] != '\0' || right[index] != '\0') {
        if (left[index] != right[index]) return false;
        ++index;
    }
    return true;
}

static void copy_display_contract_text(char* destination, uint32_t capacity, const char* prefix, uint32_t ordinal)
{
    if (destination == nullptr || capacity == 0u) return;
    uint32_t index = 0u;
    if (prefix != nullptr) {
        while (prefix[index] != '\0' && index + 1u < capacity) {
            destination[index] = prefix[index];
            ++index;
        }
    }
    char digits[11];
    uint32_t digitCount = 0u;
    do {
        digits[digitCount++] = static_cast<char>('0' + (ordinal % 10u));
        ordinal /= 10u;
    } while (ordinal != 0u && digitCount < sizeof(digits));
    while (digitCount > 0u && index + 1u < capacity) {
        destination[index++] = digits[--digitCount];
    }
    destination[index] = '\0';
    while (++index < capacity) destination[index] = '\0';
}

static void set_backend_diagnostic(DisplayConfigurationBackendResult& result, const char* text)
{
    copy_display_contract_text(result.diagnostic, sizeof(result.diagnostic), text);
}

static uint32_t backend_output_count()
{
    return s_probeOutcome.outputInventory.monitors.size();
}

static void fill_backend_snapshot(gxos::display::DisplayConfigurationSnapshot& snapshot,
                                  uint32_t mode,
                                  uint32_t primaryOutput,
                                  bool presenterActive)
{
    snapshot = gxos::display::DisplayConfigurationSnapshot{};
    snapshot.version = gxos::display::kDisplayConfigurationContractVersion;
    snapshot.structureSize = sizeof(snapshot);
    copy_display_contract_text(snapshot.backend, sizeof(snapshot.backend), "virtio-gpu");
    snapshot.mode = mode;
    snapshot.outputCount = backend_output_count() > gxos::display::kDisplayConfigurationMaxOutputs
        ? gxos::display::kDisplayConfigurationMaxOutputs : backend_output_count();
    snapshot.qemuOnly = 1u;
    snapshot.presenterActive = presenterActive ? 1u : 0u;
    for (uint32_t i = 0u; i < snapshot.outputCount; ++i) {
        const DisplayMonitorDescriptor& monitor = s_probeOutcome.outputInventory.monitors[i];
        gxos::display::DisplayConfigurationOutput& output = snapshot.outputs[i];
        output = gxos::display::DisplayConfigurationOutput{};
        copy_display_contract_text(output.stableId, sizeof(output.stableId), "display-", i + 1u);
        copy_display_contract_text(output.backendType, sizeof(output.backendType), "virtio-gpu");
        copy_display_contract_text(output.backendDeviceId, sizeof(output.backendDeviceId), "gpu0");
        copy_display_contract_text(output.modeId, sizeof(output.modeId),
            qemu_logical_mode_id(static_cast<uint32_t>(monitor.width), static_cast<uint32_t>(monitor.height)));
        output.scanoutId = monitor.scanoutId;
        output.logicalOrdinal = i + 1u;
        copy_display_contract_text(output.stableName, sizeof(output.stableName), "Display ", i + 1u);
        output.virtualX = monitor.virtualX;
        output.virtualY = monitor.virtualY;
        output.width = monitor.width;
        output.height = monitor.height;
        output.enabled = monitor.enabled ? 1u : 0u;
        output.primary = (primaryOutput == monitor.scanoutId) ? 1u : 0u;
        if (output.primary) {
            copy_display_contract_text(snapshot.primaryOutputId, sizeof(snapshot.primaryOutputId), output.stableId);
        }
    }
    if (snapshot.primaryOutputId[0] == '\0' && snapshot.outputCount > 0u) {
        copy_display_contract_text(snapshot.primaryOutputId, sizeof(snapshot.primaryOutputId), "display-", primaryOutput + 1u);
    }
    copy_display_contract_text(snapshot.taskbarMonitorId, sizeof(snapshot.taskbarMonitorId), snapshot.primaryOutputId);
    if (snapshot.outputCount > 0u) {
        snapshot.virtualDesktopX = 0;
        snapshot.virtualDesktopY = 0;
        snapshot.virtualDesktopWidth = s_probeOutcome.outputInventory.virtualDesktop.width();
        snapshot.virtualDesktopHeight = s_probeOutcome.outputInventory.virtualDesktop.height();
    }
}

static bool requested_output_is(const gxos::display::DisplayConfigurationOutput& output, uint32_t ordinal)
{
    char expected[gxos::display::kDisplayConfigurationOutputIdBytes]{};
    copy_display_contract_text(expected, sizeof(expected), "display-", ordinal);
    for (uint32_t i = 0u; i < sizeof(expected); ++i) {
        if (output.stableId[i] != expected[i]) return false;
        if (expected[i] == '\0') break;
    }
    return true;
}

static uint32_t primary_output_from_request(const gxos::display::DisplayConfigurationRequest& request)
{
    for (uint32_t i = 0u; i < request.outputCount && i < gxos::display::kDisplayConfigurationMaxOutputs; ++i) {
        if (request.outputs[i].primary != 0u) return i;
    }
    const char* primary = request.primaryOutputId;
    if (primary != nullptr && primary[0] == 'd' && primary[7] == '-') {
        const uint32_t ordinal = static_cast<uint32_t>(primary[8] - '0');
        if (ordinal >= 1u && ordinal <= request.outputCount) return ordinal - 1u;
    }
    return 0u;
}

static void refresh_backend_inventory_from_live(uint32_t mode)
{
    FixedList<VirtioGpuScanoutState, kVirtioGpuMaxOutputs> scanouts;
    const VirtioGpuOutputInventory oldInventory = s_probeOutcome.outputInventory;
    for (uint32_t i = 0u; i < s_livePresentation.activeOutputCount; ++i) {
        const DiagnosticResourceState& resource = i == 0u ? s_livePresentation.resource0 : s_livePresentation.resource1;
        const LivePresentationTargetDescriptor& target = i == 0u ? s_livePresentation.target0 : s_livePresentation.target1;
        if (resource.resourceId == 0u) continue;
        DisplayInfo preferred{};
        if (i < oldInventory.monitors.size()) {
            preferred.x = static_cast<uint32_t>(oldInventory.monitors[i].preferredX);
            preferred.y = static_cast<uint32_t>(oldInventory.monitors[i].preferredY);
            preferred.width = static_cast<uint32_t>(oldInventory.monitors[i].preferredWidth);
            preferred.height = static_cast<uint32_t>(oldInventory.monitors[i].preferredHeight);
            preferred.enabled = oldInventory.monitors[i].connectorEnabled;
        } else {
            preferred.enabled = true;
            preferred.width = static_cast<uint32_t>(target.width);
            preferred.height = static_cast<uint32_t>(target.height);
        }
        scanouts.push_back(make_scanout_state(
            i,
            preferred,
            resource,
            preferred.enabled,
            target.viewportOriginX,
            target.viewportOriginY,
            target.width,
            target.height,
            resource.flushOk && resource.patternChecksum != 0u,
            resource.flushOk ? "RESOURCE_FLUSH result=ok" : "RESOURCE_FLUSH blocked",
            target.primary));
    }
    s_probeOutcome.outputInventory = VirtioGpuDisplayBackend::getVirtioGpuOutputInventory(
        scanouts, s_probeOutcome.deviceConfigNumScanouts);
    if (mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror)) {
        // The generic inventory builder defaults to horizontal Extend. Mirror
        // is a separate logical layout: both viewports occupy the same origin
        // while retaining their independent resources and scanouts.
        for (uint32_t i = 0u; i < s_probeOutcome.outputInventory.monitors.size(); ++i) {
            auto& monitor = s_probeOutcome.outputInventory.monitors[i];
            monitor.virtualX = 0;
            monitor.virtualY = 0;
            monitor.assignedX = 0;
            monitor.assignedY = 0;
        }
        for (uint32_t i = 0u; i < s_probeOutcome.outputInventory.viewports.size(); ++i) {
            auto& viewport = s_probeOutcome.outputInventory.viewports[i];
            viewport.originX = 0;
            viewport.originY = 0;
            viewport.assignedX = 0;
            viewport.assignedY = 0;
        }
        for (uint32_t i = 0u; i < s_probeOutcome.outputInventory.renderTargets.size(); ++i) {
            auto& target = s_probeOutcome.outputInventory.renderTargets[i];
            target.viewportOriginX = 0;
            target.viewportOriginY = 0;
            target.assignedX = 0;
            target.assignedY = 0;
        }
        if (!s_probeOutcome.outputInventory.monitors.empty()) {
            s_probeOutcome.outputInventory.virtualDesktop.left = 0;
            s_probeOutcome.outputInventory.virtualDesktop.top = 0;
            s_probeOutcome.outputInventory.virtualDesktop.right =
                s_probeOutcome.outputInventory.monitors[0].width;
            s_probeOutcome.outputInventory.virtualDesktop.bottom =
                s_probeOutcome.outputInventory.monitors[0].height;
        }
    }
    s_probeOutcome.outputInventory.virtualDesktop.mode = mode;
}

static void update_backend_layout(uint32_t mode, uint32_t primaryOrdinal,
                                  uint32_t outputCount,
                                  uint32_t width0, uint32_t height0,
                                  uint32_t width1, uint32_t height1)
{
    const int origin1 = mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror)
        ? 0 : static_cast<int>(width0);
    s_livePresentation.target0.viewportOriginX = 0;
    s_livePresentation.target0.viewportOriginY = 0;
    s_livePresentation.target0.width = static_cast<int>(width0);
    s_livePresentation.target0.height = static_cast<int>(height0);
    s_livePresentation.target1.viewportOriginX = outputCount > 1u ? origin1 : 0;
    s_livePresentation.target1.viewportOriginY = 0;
    s_livePresentation.target1.width = outputCount > 1u ? static_cast<int>(width1) : 0;
    s_livePresentation.target1.height = outputCount > 1u ? static_cast<int>(height1) : 0;
    s_livePresentation.target0.primary = primaryOrdinal == 0u;
    s_livePresentation.target1.primary = outputCount > 1u && primaryOrdinal == 1u;
    s_livePresentation.activeOutputCount = outputCount;
    s_livePresentation.virtualDesktopWidth = mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror)
        ? width0 : (outputCount > 1u ? width0 + width1 : width0);
    s_livePresentation.virtualDesktopHeight = outputCount > 1u && height1 > height0 ? height1 : height0;
    refresh_backend_inventory_from_live(mode);
}

} // namespace

bool get_display_configuration_backend_snapshots(
    gxos::display::DisplayConfigurationSnapshot* detected,
    gxos::display::DisplayConfigurationSnapshot* active)
{
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    (void)detected;
    (void)active;
    return false;
#else
    if (detected == nullptr || active == nullptr || !s_probeOutcome.valid || !s_livePresentation.enabled || backend_output_count() < 1u) {
        return false;
    }
    if (!s_detectedConfigurationSnapshotReady) {
        fill_backend_snapshot(s_detectedConfigurationSnapshot,
                              static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend),
                              s_probeOutcome.outputInventory.primaryOutput,
                              true);
        s_detectedConfigurationSnapshotReady = true;
    }
    *detected = s_detectedConfigurationSnapshot;
    fill_backend_snapshot(*active,
                          s_displayConfigurationMode,
                          s_displayConfigurationPrimaryOutput,
                          s_livePresentation.enabled && !s_livePresentation.stopped && !s_displayConfigurationPresentationPaused);
    return true;
#endif
}

void set_display_configuration_backend_presentation_paused(bool paused)
{
    s_displayConfigurationPresentationPaused = paused;
}

bool display_configuration_backend_presentation_paused()
{
    return s_displayConfigurationPresentationPaused;
}

bool apply_display_configuration_backend_layout(
    const gxos::display::DisplayConfigurationRequest& requested,
    uint32_t failureInjectionFlags,
    DisplayConfigurationBackendResult* result)
{
    if (result == nullptr) return false;
    *result = DisplayConfigurationBackendResult{};
#if !defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) || !defined(GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE)
    set_backend_diagnostic(*result, "QEMU-only display backend is not enabled");
    return false;
#else
    // REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
    // Physical backends: resolution changes are not supported. This bounded
    // resource rebuild is enabled only by the QEMU virtio-gpu probe gate.
    if (!s_probeOutcome.valid || !s_livePresentation.enabled || backend_output_count() < 1u) {
        set_backend_diagnostic(*result, "virtio-gpu presentation backend unavailable");
        return false;
    }
    if (!s_displayConfigurationPresentationPaused) {
        set_backend_diagnostic(*result, "presentation was not paused at the safe point");
        return false;
    }
    if (requested.outputCount == 0u || requested.outputCount > 2u ||
        requested.outputCount > gxos::display::kDisplayConfigurationMaxOutputs) {
        set_backend_diagnostic(*result, "one or two operational outputs are required");
        return false;
    }
    if (requested.mode != static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror) &&
        requested.mode != static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend)) {
        set_backend_diagnostic(*result, "unsupported display mode");
        return false;
    }

    DiagnosticResourceState oldResources[2] = { s_livePresentation.resource0, s_livePresentation.resource1 };
    DiagnosticResourceState candidateResources[2] = { oldResources[0], oldResources[1] };
    uint8_t* oldBacking[2] = { s_livePresentation.backing0, s_livePresentation.backing1 };
    uint8_t* candidateBacking[2] = { oldBacking[0], oldBacking[1] };
    uint64_t oldPhysical[2] = { s_livePresentation.backingPhysical0, s_livePresentation.backingPhysical1 };
    uint64_t candidatePhysical[2] = { oldPhysical[0], oldPhysical[1] };
    LivePresentationTargetDescriptor oldTargets[2] = { s_livePresentation.target0, s_livePresentation.target1 };
    LivePresentationTargetDescriptor candidateTargets[2] = { oldTargets[0], oldTargets[1] };
    uint8_t* replacementBacking[2] = { &s_rebuildBackingStorage0[0], &s_rebuildBackingStorage1[0] };
    MemEntry replacementEntries[2][kDiagnosticBackingMaxMemEntries]{};
    DiagnosticBackingLayoutAudit replacementAudits[2]{};
    VirtioGpuOutputRebuildPlan plans[2]{};
    uint32_t widths[2]{};
    uint32_t heights[2]{};
    uint64_t backingBytes[2]{};
    uint64_t totalBackingBytes = 0u;

    for (uint32_t i = 0u; i < requested.outputCount; ++i) {
        if (!requested_output_is(requested.outputs[i], i + 1u)) {
            set_backend_diagnostic(*result, "stable output id is unavailable");
            return false;
        }
        if (requested.outputs[i].backendType[0] != '\0' &&
            !display_contract_text_equals(requested.outputs[i].backendType, "virtio-gpu")) {
            set_backend_diagnostic(*result, "stable output backend identity is unavailable");
            return false;
        }
        if (requested.outputs[i].backendDeviceId[0] != '\0' &&
            !display_contract_text_equals(requested.outputs[i].backendDeviceId, "gpu0")) {
            set_backend_diagnostic(*result, "stable output device identity is unavailable");
            return false;
        }
        if (requested.outputs[i].logicalOrdinal != 0u && requested.outputs[i].logicalOrdinal != i + 1u) {
            set_backend_diagnostic(*result, "stable output ordinal is unavailable");
            return false;
        }
        widths[i] = requested.outputs[i].width > 0 ? static_cast<uint32_t>(requested.outputs[i].width) : oldResources[i].width;
        heights[i] = requested.outputs[i].height > 0 ? static_cast<uint32_t>(requested.outputs[i].height) : oldResources[i].height;
        const char* modeId = qemu_logical_mode_id(widths[i], heights[i]);
        if (modeId == nullptr || (requested.outputs[i].modeId[0] != '\0' &&
            !display_contract_text_equals(requested.outputs[i].modeId, modeId))) {
            set_backend_diagnostic(*result, "unsupported QEMU logical resolution");
            return false;
        }
        if (!checked_qemu_logical_backing_bytes(widths[i], heights[i], backingBytes[i])) {
            set_backend_diagnostic(*result, "bounded logical resolution backing validation failed");
            return false;
        }
        if (totalBackingBytes > kQemuLogicalModeTotalBackingLimit - backingBytes[i]) {
            set_backend_diagnostic(*result, "total logical resolution backing limit exceeded");
            return false;
        }
        totalBackingBytes += backingBytes[i];
        plans[i].outputIdentity = i + 1u;
        plans[i].scanoutId = i;
        plans[i].oldResourceId = oldResources[i].resourceId;
        plans[i].oldWidth = oldResources[i].width;
        plans[i].oldHeight = oldResources[i].height;
        plans[i].newWidth = widths[i];
        plans[i].newHeight = heights[i];
    }
    if (requested.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror) &&
        requested.outputCount >= 2u &&
        (widths[0] != widths[1] || heights[0] != heights[1])) {
        set_backend_diagnostic(*result, "Mirror dimensions incompatible");
        return false;
    }
    if (widths[0] > 0x7FFFFFFFu - (requested.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror) || requested.outputCount < 2u ? 0u : widths[1])) {
        set_backend_diagnostic(*result, "virtual desktop geometry overflow");
        return false;
    }
    const uint32_t primaryOrdinal = primary_output_from_request(requested);
    if (primaryOrdinal >= requested.outputCount) {
        set_backend_diagnostic(*result, "primary output is unavailable");
        return false;
    }
    const uint32_t virtualDesktopWidth = requested.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror) || requested.outputCount < 2u
        ? widths[0] : widths[0] + widths[1];
    const uint32_t virtualDesktopHeight = requested.outputCount < 2u || heights[0] > heights[1] ? heights[0] : heights[1];
    const int originX[2] = { 0, requested.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror)
        ? 0 : static_cast<int>(widths[0]) };
    const int originY[2] = { 0, 0 };
    for (uint32_t i = 0u; i < requested.outputCount; ++i) {
        plans[i].targetOriginX = originX[i];
        plans[i].targetOriginY = originY[i];
        candidateTargets[i] = oldTargets[i];
        candidateTargets[i].targetIndex = i + 1u;
        candidateTargets[i].scanoutId = i;
        candidateTargets[i].viewportOriginX = originX[i];
        candidateTargets[i].viewportOriginY = originY[i];
        candidateTargets[i].width = widths[i];
        candidateTargets[i].height = heights[i];
        candidateTargets[i].primary = i == primaryOrdinal;
        plans[i].prepared = oldResources[i].width == widths[i] && oldResources[i].height == heights[i];
        if (plans[i].prepared) {
            plans[i].newResourceId = oldResources[i].resourceId;
            plans[i].newBackingBytes = oldResources[i].backingBytes;
            plans[i].newBackingPages = oldResources[i].backingPageCount;
            plans[i].newBackingMemEntries = oldResources[i].memEntryCount;
        }
    }
    for (uint32_t i = requested.outputCount; i < 2u; ++i) {
        candidateResources[i] = DiagnosticResourceState{};
        candidateBacking[i] = nullptr;
        candidatePhysical[i] = 0u;
        candidateTargets[i] = LivePresentationTargetDescriptor{};
        plans[i].outputIdentity = i + 1u;
        plans[i].scanoutId = i;
        plans[i].oldResourceId = oldResources[i].resourceId;
        plans[i].oldWidth = oldResources[i].width;
        plans[i].oldHeight = oldResources[i].height;
        plans[i].newResourceId = 0u;
    }
    if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectValidationFailure) != 0u) {
        set_backend_diagnostic(*result, "injected-validation-failure");
        return false;
    }

    auto releaseProvisional = [&]() {
        bool released = true;
        for (uint32_t i = 0u; i < 2u; ++i) {
            if (plans[i].newResourceId != 0u && plans[i].newResourceId != oldResources[i].resourceId) {
                const char* reason = nullptr;
                bool completed = false;
                if (candidateResources[i].resourceId == 0u) continue;
                if (!issue_resource_unref(*s_livePresentation.device, candidateResources[i], &reason, &completed)) {
                    released = false;
                    ++s_cleanupFailures;
                } else {
                    ++s_resourcesUnreferenced;
                    if (s_activeBackingAllocations > 0u) --s_activeBackingAllocations;
                }
            }
        }
        return released;
    };

    bool prepareOk = true;
    const char* failureReason = nullptr;
    bool completionKnown = false;
    for (uint32_t i = 0u; i < 2u && prepareOk; ++i) {
        if (i >= requested.outputCount) continue;
        if (plans[i].newResourceId != 0u && plans[i].newResourceId == oldResources[i].resourceId) {
            continue;
        }
        candidateBacking[i] = replacementBacking[i];
        candidatePhysical[i] = dma_address(candidateBacking[i]);
        if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectBackingAllocationFailure) != 0u) {
            failureReason = "injected replacement backing allocation failure";
            prepareOk = false;
            break;
        }
        if (candidatePhysical[i] == 0u || !build_diagnostic_backing_layout(
                candidateBacking[i], backingBytes[i], replacementEntries[i],
                kDiagnosticBackingMaxMemEntries, &replacementAudits[i])) {
            failureReason = "replacement backing physical coverage validation failed";
            prepareOk = false;
            break;
        }
        const uint32_t id = allocate_replacement_resource_id(oldResources[0], oldResources[1],
            plans[0].newResourceId, plans[1].newResourceId);
        if (id == 0u) {
            failureReason = "replacement resource id namespace exhausted";
            prepareOk = false;
            break;
        }
        plans[i].newResourceId = id;
        candidateResources[i] = DiagnosticResourceState{};
        if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectResourceCreateFailure) != 0u) {
            failureReason = "injected RESOURCE_CREATE_2D response failure";
            prepareOk = false;
            break;
        }
        if (!issue_resource_create_2d(*s_livePresentation.device, candidateResources[i],
                                      oldResources[0].resourceId, id, widths[i], heights[i],
                                      s_livePresentation.resourceFormat, false,
                                      &failureReason, &completionKnown)) {
            prepareOk = false;
            break;
        }
        ++s_resourcesCreated;
        ++s_activeBackingAllocations;
        if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectAttachBackingFailure) != 0u) {
            failureReason = "injected RESOURCE_ATTACH_BACKING response failure";
            prepareOk = false;
            break;
        }
        if (!issue_resource_attach_backing(*s_livePresentation.device, candidateResources[i],
                                            reinterpret_cast<uint64_t>(candidateBacking[i]),
                                            candidatePhysical[i], backingBytes[i],
                                            align_up(backingBytes[i], kDiagnosticPageSizeBytes) / kDiagnosticPageSizeBytes,
                                            replacementEntries[i], replacementAudits[i].totalMemEntries,
                                            replacementAudits[i], &failureReason, &completionKnown)) {
            prepareOk = false;
            break;
        }
        plans[i].newBackingMemEntries = candidateResources[i].memEntryCount;
        plans[i].newBackingBytes = backingBytes[i];
        plans[i].newBackingPages = candidateResources[i].backingPageCount;
        plans[i].attached = true;
        plans[i].prepared = true;
        kernel::serial::puts("Display mode rebuild: output=");
        serial_put_u32_decimal(plans[i].outputIdentity);
        kernel::serial::puts(" scanout=");
        serial_put_u32_decimal(plans[i].scanoutId);
        kernel::serial::puts(" old=");
        serial_put_u32_decimal(plans[i].oldWidth);
        kernel::serial::putc('x');
        serial_put_u32_decimal(plans[i].oldHeight);
        kernel::serial::puts(" new=");
        serial_put_u32_decimal(plans[i].newWidth);
        kernel::serial::putc('x');
        serial_put_u32_decimal(plans[i].newHeight);
        kernel::serial::puts(" oldResource=");
        serial_put_u32_decimal(plans[i].oldResourceId);
        kernel::serial::puts(" newResource=");
        serial_put_u32_decimal(plans[i].newResourceId);
        kernel::serial::puts(" backingBytes=");
        serial_put_u64_decimal(plans[i].newBackingBytes);
        kernel::serial::puts(" pages=");
        serial_put_u64_decimal(plans[i].newBackingPages);
        kernel::serial::puts(" memEntries=");
        serial_put_u32_decimal(replacementAudits[i].totalMemEntries);
        kernel::serial::puts(" physicalCoverage=valid prepared=yes\n");
    }
    if (!prepareOk) {
        const bool released = releaseProvisional();
        set_backend_diagnostic(*result, failureReason != nullptr ? failureReason : "replacement preparation failed");
        kernel::serial::puts("Display mode rebuild: result=failed stage=prepare rollback=no provisionalReleased=");
        kernel::serial::puts(released ? "yes\n" : "no\n");
        return false;
    }

    for (uint32_t i = 0u; i < 2u; ++i) {
        candidateTargets[i].resourceId = candidateResources[i].resourceId;
    }

    // Render and transfer into every candidate before any replacement scanout
    // is bound. No provisional target is published into the global inventory.
    s_liveCommandLoggingSuppressed = true;
    const uint64_t validationSequence = ++s_livePresentation.frameSequence;
    bool validationOk = true;
    for (uint32_t i = 0u; i < requested.outputCount && validationOk; ++i) {
        if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectValidationFrameFailure) != 0u && i == 1u) {
            failureReason = "injected validation-frame failure";
            validationOk = false;
            break;
        }
        const CompositorFrameTargetResult validation = present_target_once(
            *s_livePresentation.device, make_live_target(candidateTargets[i]),
            candidateResources[i], candidateBacking[i], candidatePhysical[i], backingBytes[i],
            diagnostic_pattern_palette(i), s_livePresentation.resourceFormat,
            virtualDesktopWidth, virtualDesktopHeight, s_livePresentation.bytesPerPixel,
            candidateResources[i].backingPageCount,
            validationSequence, false, false);
        validationOk = validation.renderOk && validation.transferOk && validation.flushOk;
        plans[i].validationPresented = validationOk;
        if (!validationOk) failureReason = validation.blocker != nullptr ? validation.blocker : "validation frame failed";
    }
    s_liveCommandLoggingSuppressed = false;
    if (!validationOk) {
        const bool released = releaseProvisional();
        set_backend_diagnostic(*result, failureReason != nullptr ? failureReason : "validation frame failed");
        kernel::serial::puts("Display mode rebuild: result=failed stage=validation-frame rollback=no provisionalReleased=");
        kernel::serial::puts(released ? "yes\n" : "no\n");
        return false;
    }

    // Bind replacements only after all independent backing stores have been
    // verified and presented. The old resources remain alive for rollback.
    bool bindOk = true;
    for (uint32_t i = requested.outputCount; i < 2u && bindOk; ++i) {
        if (oldResources[i].resourceId == 0u) continue;
        if (!issue_clear_scanout(*s_livePresentation.device, i, &failureReason, &completionKnown)) {
            bindOk = false;
            break;
        }
        plans[i].scanoutUnbound = true;
    }
    for (uint32_t i = 0u; i < requested.outputCount && bindOk; ++i) {
        if (plans[i].newResourceId == oldResources[i].resourceId) continue;
        if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectSetScanoutFailure) != 0u && i == 1u) {
            failureReason = "injected SET_SCANOUT response failure";
            bindOk = false;
            break;
        }
        if (!issue_set_scanout(*s_livePresentation.device, candidateResources[i], i,
                               widths[i], heights[i], &failureReason, &completionKnown)) {
            bindOk = false;
            break;
        }
        plans[i].scanoutBound = true;
    }
    if (!bindOk) {
        bool restored = true;
        for (uint32_t i = 0u; i < 2u; ++i) {
            if (plans[i].scanoutBound || plans[i].scanoutUnbound) {
                if (oldResources[i].resourceId != 0u) {
                    if (!issue_set_scanout(*s_livePresentation.device, oldResources[i], i,
                                           oldResources[i].width, oldResources[i].height,
                                           &failureReason, &completionKnown)) restored = false;
                } else if (!issue_clear_scanout(*s_livePresentation.device, i, &failureReason, &completionKnown)) {
                    restored = false;
                }
            }
        }
        s_liveCommandLoggingSuppressed = true;
        for (uint32_t i = 0u; i < 2u && restored; ++i) {
            const CompositorFrameTargetResult rollbackFrame = present_target_once(
                *s_livePresentation.device, make_live_target(oldTargets[i]), oldResources[i],
                oldBacking[i], oldPhysical[i], oldResources[i].backingBytes,
                diagnostic_pattern_palette(i), s_livePresentation.resourceFormat,
                s_livePresentation.virtualDesktopWidth, s_livePresentation.virtualDesktopHeight,
                s_livePresentation.bytesPerPixel, oldResources[i].backingPageCount,
                ++s_livePresentation.frameSequence, false, false);
            if (!rollbackFrame.renderOk || !rollbackFrame.transferOk || !rollbackFrame.flushOk) restored = false;
        }
        s_liveCommandLoggingSuppressed = false;
        const bool released = releaseProvisional();
        ++s_resourcesRolledBack;
        set_backend_diagnostic(*result, failureReason != nullptr ? failureReason : "SET_SCANOUT failed");
        kernel::serial::puts("Display mode rebuild: result=failed stage=set-scanout rollback=");
        kernel::serial::puts(restored ? "yes" : "no");
        kernel::serial::puts(" oldResourceRestored=");
        kernel::serial::puts(restored ? "yes" : "no");
        kernel::serial::puts(" provisionalReleased=");
        kernel::serial::puts(released ? "yes\n" : "no\n");
        return false;
    }

    // Verify the actual post-bind scanouts before publishing the candidate
    // target inventory. This closes the old/new target mixing window.
    s_liveCommandLoggingSuppressed = true;
    bool postBindOk = true;
    for (uint32_t i = 0u; i < requested.outputCount && postBindOk; ++i) {
        const CompositorFrameTargetResult boundFrame = present_target_once(
            *s_livePresentation.device, make_live_target(candidateTargets[i]), candidateResources[i],
            candidateBacking[i], candidatePhysical[i], backingBytes[i], diagnostic_pattern_palette(i),
            s_livePresentation.resourceFormat, virtualDesktopWidth, virtualDesktopHeight,
            s_livePresentation.bytesPerPixel, candidateResources[i].backingPageCount,
            ++s_livePresentation.frameSequence, false, false);
        postBindOk = boundFrame.renderOk && boundFrame.transferOk && boundFrame.flushOk;
        if (!postBindOk) failureReason = boundFrame.blocker != nullptr ? boundFrame.blocker : "post-bind validation failed";
    }
    s_liveCommandLoggingSuppressed = false;
    if ((failureInjectionFlags & gxos::display::DisplayConfigurationFlagTestInjectSecondOutputCommitFailure) != 0u) {
        failureReason = "injected second-output commit failure";
        postBindOk = false;
    }
    if (!postBindOk) {
        bool restored = true;
        for (uint32_t i = 0u; i < 2u; ++i) {
            if (plans[i].scanoutBound || plans[i].scanoutUnbound) {
                if (oldResources[i].resourceId != 0u) {
                    if (!issue_set_scanout(*s_livePresentation.device, oldResources[i], i,
                            oldResources[i].width, oldResources[i].height, &failureReason, &completionKnown)) restored = false;
                } else if (!issue_clear_scanout(*s_livePresentation.device, i, &failureReason, &completionKnown)) {
                    restored = false;
                }
            }
        }
        s_liveCommandLoggingSuppressed = true;
        for (uint32_t i = 0u; i < 2u && restored; ++i) {
            const CompositorFrameTargetResult rollbackFrame = present_target_once(
                *s_livePresentation.device, make_live_target(oldTargets[i]), oldResources[i],
                oldBacking[i], oldPhysical[i], oldResources[i].backingBytes,
                diagnostic_pattern_palette(i), s_livePresentation.resourceFormat,
                s_livePresentation.virtualDesktopWidth, s_livePresentation.virtualDesktopHeight,
                s_livePresentation.bytesPerPixel, oldResources[i].backingPageCount,
                ++s_livePresentation.frameSequence, false, false);
            if (!rollbackFrame.renderOk || !rollbackFrame.transferOk || !rollbackFrame.flushOk) restored = false;
        }
        s_liveCommandLoggingSuppressed = false;
        const bool released = releaseProvisional();
        ++s_resourcesRolledBack;
        set_backend_diagnostic(*result, failureReason != nullptr ? failureReason : "post-bind validation failed");
        kernel::serial::puts("Display mode rebuild: result=failed stage=post-bind-validation rollback=");
        kernel::serial::puts(restored ? "yes" : "no");
        kernel::serial::puts(" provisionalReleased=");
        kernel::serial::puts(released ? "yes\n" : "no\n");
        return false;
    }

    for (uint32_t i = 0u; i < 2u; ++i) {
        plans[i].committed = true;
    }
    s_livePresentation.resource0 = candidateResources[0];
    s_livePresentation.resource1 = candidateResources[1];
    s_livePresentation.backing0 = candidateBacking[0];
    s_livePresentation.backing1 = candidateBacking[1];
    s_livePresentation.backingPhysical0 = candidatePhysical[0];
    s_livePresentation.backingPhysical1 = candidatePhysical[1];
    s_livePresentation.target0 = candidateTargets[0];
    s_livePresentation.target1 = candidateTargets[1];
    s_livePresentation.selectedWidth = widths[0];
    s_livePresentation.selectedHeight = heights[0];
    s_livePresentation.totalBackingBytes = backingBytes[0] > backingBytes[1] ? backingBytes[0] : backingBytes[1];
    s_livePresentation.backingPageCount = candidateResources[0].backingPageCount > candidateResources[1].backingPageCount
        ? candidateResources[0].backingPageCount : candidateResources[1].backingPageCount;
    s_displayConfigurationMode = requested.mode;
    s_displayConfigurationPrimaryOutput = s_probeOutcome.outputInventory.monitors[primaryOrdinal].scanoutId;
    update_backend_layout(requested.mode, primaryOrdinal, requested.outputCount,
                          widths[0], heights[0], widths[1], heights[1]);
    result->targetRebuilt = 1u;
    result->validationFrame = 1u;

    bool cleanupOk = true;
    for (uint32_t i = 0u; i < 2u; ++i) {
        if (plans[i].newResourceId == oldResources[i].resourceId) continue;
        if (oldResources[i].resourceId == 0u) continue;
        const char* cleanupReason = nullptr;
        bool cleanupKnown = false;
        if (!issue_resource_unref(*s_livePresentation.device, oldResources[i], &cleanupReason, &cleanupKnown)) {
            cleanupOk = false;
            ++s_cleanupFailures;
        } else {
            ++s_resourcesUnreferenced;
            if (s_activeBackingAllocations > 0u) --s_activeBackingAllocations;
        }
    }
    s_resourcesCommitted += (plans[0].newResourceId != oldResources[0].resourceId ? 1u : 0u);
    s_resourcesCommitted += (plans[1].newResourceId != oldResources[1].resourceId ? 1u : 0u);
    kernel::serial::puts("Display resource lifecycle: created=");
    serial_put_u32_decimal(s_resourcesCreated);
    kernel::serial::puts(" committed=");
    serial_put_u32_decimal(s_resourcesCommitted);
    kernel::serial::puts(" rolledBack=");
    serial_put_u32_decimal(s_resourcesRolledBack);
    kernel::serial::puts(" unreferenced=");
    serial_put_u32_decimal(s_resourcesUnreferenced);
    kernel::serial::puts(" activeBacking=");
    serial_put_u32_decimal(s_activeBackingAllocations);
    kernel::serial::puts(" cleanupFailures=");
    serial_put_u32_decimal(s_cleanupFailures);
    kernel::serial::putc('\n');
    kernel::desktop::request_redraw();
    kernel::serial::puts("Display configuration apply: mode=");
    kernel::serial::puts(requested.mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Extend) ? "Extend" : "Mirror");
    kernel::serial::puts(" output1=");
    serial_put_u32_decimal(widths[0]);
    kernel::serial::putc('x');
    serial_put_u32_decimal(heights[0]);
    kernel::serial::puts(" output2=");
    serial_put_u32_decimal(widths[1]);
    kernel::serial::putc('x');
    serial_put_u32_decimal(heights[1]);
    kernel::serial::puts(" virtualDesktop=");
    serial_put_u32_decimal(virtualDesktopWidth);
    kernel::serial::putc('x');
    serial_put_u32_decimal(virtualDesktopHeight);
    kernel::serial::puts(" targets=");
    serial_put_u32_decimal(requested.outputCount);
    kernel::serial::puts(" validation=ok cleanup=");
    kernel::serial::puts(cleanupOk ? "ok\n" : "failed\n");
    set_backend_diagnostic(*result, cleanupOk ? "display layout applied and validation frame flushed; cleanup=ok"
                                             : "display layout applied; cleanup failure recorded");
    result->success = cleanupOk ? 1u : 0u;
    return cleanupOk;
#endif
}

GpuStatus register_as_framebuffer(GpuDevice* dev)
{
    (void)dev;
    return GPU_ERR_UNSUPPORTED;
}

void print_status(GpuDevice* dev)
{
    if (dev == nullptr) {
        kernel::serial::puts("[VIRTIO-GPU] No device\n");
        return;
    }

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    DeviceState* state = active_state(dev);
    if (state != nullptr) {
        print_device_summary(*state);
        return;
    }
#endif

    kernel::serial::puts("[VIRTIO-GPU] Diagnostic probe disabled or device unavailable\n");
}

void print_all_devices()
{
    kernel::serial::puts("[VIRTIO-GPU] Device summary:\n");
    kernel::serial::puts("  Total devices: ");
    serial_put_u32_decimal(static_cast<uint32_t>(s_deviceCount));
    kernel::serial::putc('\n');

    for (int i = 0; i < s_deviceCount; ++i) {
        kernel::serial::puts("  Device ");
        serial_put_u32_decimal(static_cast<uint32_t>(i));
        kernel::serial::puts(":\n");
        print_status(&s_devices[i].device);
    }
}

const char* status_string(GpuStatus status)
{
    switch (status) {
        case GPU_OK:              return "OK";
        case GPU_ERR_NOT_FOUND:   return "Not found";
        case GPU_ERR_NO_DEVICE:   return "No device";
        case GPU_ERR_INIT_FAIL:   return "Initialization failed";
        case GPU_ERR_NO_MEMORY:   return "Out of memory";
        case GPU_ERR_INVALID:     return "Invalid parameter";
        case GPU_ERR_IO:          return "I/O error";
        case GPU_ERR_TIMEOUT:     return "Timeout";
        case GPU_ERR_UNSUPPORTED: return "Unsupported";
        default:                  return "Unknown error";
    }
}

} // namespace gpu
} // namespace virtio
} // namespace kernel
