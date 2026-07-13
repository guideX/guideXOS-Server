// VirtIO GPU Driver
//
// Diagnostic-only probe path for QEMU virtio-gpu discovery and
// single-scanout 2D test-pattern rendering.
//
// Safety boundaries:
// - PCI discovery and VirtIO config/queue access only behind the QEMU gate
// - QEMU-only 2D resource create, attach, scanout, transfer, and flush
// - No cursor queue setup
// - No 3D, virgl, Venus, blob, or compositor integration
// - No continuous frame rendering or animation
// - No real hardware GPU/MMIO enablement
// REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/virtio_gpu.h"

#include "include/kernel/mmio.h"
#include "include/kernel/msi.h"
#include "include/kernel/serial_debug.h"

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
static const uint16_t kInvalidDescriptorIndex = 0xFFFFu;
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
    const char* reason;
    const DeviceState* state;
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

static bool s_initialized = false;
static DeviceState s_devices[4];
static int s_deviceCount = 0;
static ProbeOutcome s_probeOutcome{};
static uint64_t s_kernelPhysicalBase = 0x100000ULL;

#if defined(_MSC_VER)
__declspec(align(4096)) static uint8_t s_queueStorage[16384];
__declspec(align(4096)) static uint8_t s_commandBuffer[4096];
__declspec(align(4096)) static uint8_t s_responseBuffer[sizeof(RespDisplayInfo)];
__declspec(align(4096)) static uint8_t s_diagnosticBackingStorage0[kDiagnosticBackingBytes];
__declspec(align(4096)) static uint8_t s_diagnosticBackingStorage1[kDiagnosticBackingBytes];
#else
static uint8_t s_queueStorage[16384] __attribute__((aligned(4096)));
static uint8_t s_commandBuffer[4096] __attribute__((aligned(4096)));
static uint8_t s_responseBuffer[sizeof(RespDisplayInfo)] __attribute__((aligned(4096)));
static uint8_t s_diagnosticBackingStorage0[kDiagnosticBackingBytes] __attribute__((aligned(4096)));
static uint8_t s_diagnosticBackingStorage1[kDiagnosticBackingBytes] __attribute__((aligned(4096)));
#endif

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
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
        kernel::serial::puts("[VIRTIO-GPU] ");
        kernel::serial::puts(commandName != nullptr ? commandName : "control command");
        kernel::serial::puts(" timed out usedIdx=");
        serial_put_u32_decimal(expectedUsedIdx);
        kernel::serial::puts(" spins=");
        serial_put_u32_decimal(spin);
        kernel::serial::putc('\n');
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

    kernel::serial::puts("[VIRTIO-GPU] ");
    kernel::serial::puts(commandName != nullptr ? commandName : "control command");
    kernel::serial::puts(" completion usedIdx=");
    serial_put_u32_decimal(completedUsedIdx);
    kernel::serial::puts(" usedLen=");
    serial_put_u32_decimal(usedElem.len);
    kernel::serial::puts(" headDescriptor=");
    serial_put_u32_decimal(usedElem.id);
    kernel::serial::putc('\n');

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

    if (resource.resourceId == 0u || !resource.scanoutSet || resource.width == 0u || resource.height == 0u) {
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

    kernel::serial::puts("[VIRTIO-GPU] Init step: TRANSFER_TO_HOST_2D scanout");
    serial_put_u32_decimal(scanoutId);
    kernel::serial::puts(" begin\n");

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
        kernel::serial::puts("[VIRTIO-GPU] TRANSFER_TO_HOST_2D result=failed scanoutId=");
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

    resource.transferOk = true;
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

    if (resource.resourceId == 0u || !resource.transferOk || resource.width == 0u || resource.height == 0u) {
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

    kernel::serial::puts("[VIRTIO-GPU] Init step: RESOURCE_FLUSH scanout");
    serial_put_u32_decimal(scanoutId);
    kernel::serial::puts(" begin\n");

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
        kernel::serial::puts("[VIRTIO-GPU] RESOURCE_FLUSH result=failed scanoutId=");
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

    resource.flushOk = true;
    kernel::serial::puts("[VIRTIO-GPU] RESOURCE_FLUSH result=ok scanoutId=");
    serial_put_u32_decimal(scanoutId);
    kernel::serial::puts(" resourceId=0x");
    kernel::serial::put_hex32(resource.resourceId);
    kernel::serial::puts(" rect=0,0 ");
    serial_put_u32_decimal(resource.width);
    kernel::serial::putc('x');
    serial_put_u32_decimal(resource.height);
    kernel::serial::putc('\n');
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

#if defined(GXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE)
    do {
        kernel::serial::puts("[VIRTIO-GPU] Stage B scanout capacity deviceConfigNumScanouts=");
        serial_put_u32_decimal(transport.mmioDeviceScanouts);
        kernel::serial::puts(" scanout1InitialEnabled=");
        kernel::serial::puts(scanout1Initial.enabled ? "yes" : "no");
        kernel::serial::puts(" qemuMaxOutputsIntent=");
        serial_put_u32_decimal(kDiagnosticQemuMaxOutputsIntent);
        kernel::serial::putc('\n');

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
