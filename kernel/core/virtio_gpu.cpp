// VirtIO GPU Driver
//
// Diagnostic-only probe path for QEMU virtio-gpu discovery and
// GET_DISPLAY_INFO-style scanout inspection.
//
// Safety boundaries:
// - PCI discovery and VirtIO config/queue access only
// - No resource creation
// - No backing attachment
// - No scanout updates
// - No transfers or flushes
// - No framebuffer rendering
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
static const uint32_t kResponseSpinLimit = 1000000;
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
    DisplayInfoOutcome displayInfoOutcome;
    uint32_t scanoutCount;
    const char* reason;
    const DeviceState* state;
};

static bool s_initialized = false;
static DeviceState s_devices[4];
static int s_deviceCount = 0;
static ProbeOutcome s_probeOutcome{};

#if defined(_MSC_VER)
__declspec(align(4096)) static uint8_t s_queueStorage[16384];
__declspec(align(4096)) static uint8_t s_commandBuffer[512];
__declspec(align(4096)) static uint8_t s_responseBuffer[sizeof(RespDisplayInfo)];
#else
static uint8_t s_queueStorage[16384] __attribute__((aligned(4096)));
static uint8_t s_commandBuffer[512] __attribute__((aligned(4096)));
static uint8_t s_responseBuffer[sizeof(RespDisplayInfo)] __attribute__((aligned(4096)));
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

static uint64_t virt_to_phys(void* ptr)
{
    return reinterpret_cast<uint64_t>(ptr);
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
        if (transport.commonCfg.mapped) {
            numQueues = mmio_read16(region_mmio_addr(transport.commonCfg, pci::COMMON_NUM_QUEUES));
            deviceStatus = mmio_read8(region_mmio_addr(transport.commonCfg, pci::COMMON_STATUS));
            configGeneration = mmio_read8(region_mmio_addr(transport.commonCfg, pci::COMMON_CFG_GEN));
        }
        if (transport.deviceCfg.mapped) {
            deviceScanouts = mmio_read32(region_mmio_addr(transport.deviceCfg, 0x08));
            deviceCapsets = mmio_read32(region_mmio_addr(transport.deviceCfg, 0x0C));
        }
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

    if (summarySuccess && mappingOut != nullptr) {
        kernel::serial::puts("[VIRTIO-GPU] MMIO transport summary mmioMapped=yes mappingVirtual=0x");
        kernel::serial::put_hex64(commonVirtual);
        kernel::serial::puts(" pageCount=");
        serial_put_u64_decimal(totalUniquePages);
        kernel::serial::puts(" cacheMode=");
        kernel::serial::puts(transport.mmioCacheMode != nullptr ? transport.mmioCacheMode : "n/a");
        kernel::serial::puts(" sanityReads=");
        kernel::serial::puts(allSanityReadsOk ? "ok" : "failed");
        kernel::serial::puts(" stopReason=");
        kernel::serial::puts(transport.mmioStopReason != nullptr ? transport.mmioStopReason : "n/a");
        kernel::serial::putc('\n');

        kernel::serial::puts("[VIRTIO-GPU] MMIO sanity reads numQueues=");
        serial_put_u32_decimal(numQueues);
        kernel::serial::puts(" deviceStatus=0x");
        kernel::serial::put_hex8(deviceStatus);
        kernel::serial::puts(" configGeneration=0x");
        kernel::serial::put_hex8(configGeneration);
        kernel::serial::puts(" deviceScanouts=");
        serial_put_u32_decimal(deviceScanouts);
        kernel::serial::puts(" deviceCapsets=");
        serial_put_u32_decimal(deviceCapsets);
        kernel::serial::putc('\n');
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
                                 uint32_t scanoutCount, const char* reason)
{
    s_probeOutcome.valid = true;
    s_probeOutcome.candidateCount = 1;
    s_probeOutcome.initialized = initialized;
    s_probeOutcome.displayInfoOutcome = displayInfoOutcome;
    s_probeOutcome.scanoutCount = scanoutCount;
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
        kernel::serial::puts(" scanouts=");
        serial_put_u32_decimal(s_probeOutcome.scanoutCount);
        break;
    case DisplayInfoOutcome::Failed:
        kernel::serial::puts("failed");
        break;
    case DisplayInfoOutcome::NotQueried:
    default:
        kernel::serial::puts("not-queried");
        break;
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
    queue->descPhys = virt_to_phys(reinterpret_cast<void*>(desc));
    queue->availPhys = virt_to_phys(reinterpret_cast<void*>(avail));
    queue->usedPhys = virt_to_phys(reinterpret_cast<void*>(used));

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

static bool device_wait_for_reset(ModernTransport& transport)
{
    const uint64_t statusAddr = common_cfg_addr(transport, pci::COMMON_STATUS);
    for (uint32_t i = 0; i < 100000; ++i) {
        if (mmio_read8(statusAddr) == 0) {
            return true;
        }
    }
    return mmio_read8(statusAddr) == 0;
}

static void reset_device(ModernTransport& transport)
{
    const uint64_t statusAddr = common_cfg_addr(transport, pci::COMMON_STATUS);
    mmio_write8(statusAddr, 0);
    (void)device_wait_for_reset(transport);
}

static uint64_t read_device_features(ModernTransport& transport)
{
    const uint64_t featureSelectAddr = common_cfg_addr(transport, pci::COMMON_DFSELECT);
    const uint64_t featureAddr = common_cfg_addr(transport, pci::COMMON_DF);

    mmio_write32(featureSelectAddr, 0);
    const uint64_t low = mmio_read32(featureAddr);

    mmio_write32(featureSelectAddr, 1);
    const uint64_t high = mmio_read32(featureAddr);

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

static bool setup_control_queue(ModernTransport& transport)
{
    const uint64_t queueSelectAddr = common_cfg_addr(transport, pci::COMMON_Q_SELECT);
    const uint64_t queueSizeAddr = common_cfg_addr(transport, pci::COMMON_Q_SIZE);
    const uint64_t queueDescAddr = common_cfg_addr(transport, pci::COMMON_Q_DESC);
    const uint64_t queueAvailAddr = common_cfg_addr(transport, pci::COMMON_Q_AVAIL);
    const uint64_t queueUsedAddr = common_cfg_addr(transport, pci::COMMON_Q_USED);
    const uint64_t queueEnableAddr = common_cfg_addr(transport, pci::COMMON_Q_ENABLE);

    mmio_write16(queueSelectAddr, 0);

    const uint16_t queueMax = mmio_read16(queueSizeAddr);
    if (queueMax < kMinControlQueueSize) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue unavailable\n");
        return false;
    }

    const uint16_t queueSize = choose_queue_size(queueMax);
    if (queueSize < kMinControlQueueSize) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue size too small\n");
        return false;
    }

    if (!layout_control_queue(&transport.controlQueue, queueSize)) {
        kernel::serial::puts("[VIRTIO-GPU] Failed to lay out control queue\n");
        return false;
    }

    transport.queueSize = queueSize;
    transport.controlQueue.index = 0;

    mmio_write16(queueSizeAddr, queueSize);
    mmio_write64(queueDescAddr, transport.controlQueue.descPhys);
    mmio_write64(queueAvailAddr, transport.controlQueue.availPhys);
    mmio_write64(queueUsedAddr, transport.controlQueue.usedPhys);
    mmio_write16(queueEnableAddr, 1);

    kernel::serial::puts("[VIRTIO-GPU] Control queue ready size=");
    serial_put_u32_decimal(queueSize);
    kernel::serial::puts(" desc=0x");
    kernel::serial::put_hex64(transport.controlQueue.descPhys);
    kernel::serial::puts(" avail=0x");
    kernel::serial::put_hex64(transport.controlQueue.availPhys);
    kernel::serial::puts(" used=0x");
    kernel::serial::put_hex64(transport.controlQueue.usedPhys);
    kernel::serial::putc('\n');

    return true;
}

static void queue_notify(ModernTransport& transport, uint16_t queueIndex)
{
    const uint64_t notifyAddr = notify_cfg_addr(transport,
                                static_cast<uint32_t>(static_cast<uint64_t>(transport.queueNotifyOff) *
                                                      static_cast<uint64_t>(transport.notifyOffMultiplier)));

    mmio_write16(notifyAddr, queueIndex);
}

static bool submit_display_info_request(DeviceState& state)
{
    ModernTransport& transport = state.transport;
    Virtqueue& queue = transport.controlQueue;

    if (queue.desc == nullptr || queue.avail == nullptr || queue.used == nullptr || queue.size < kMinControlQueueSize) {
        return false;
    }

    memzero(&s_commandBuffer[0], sizeof(s_commandBuffer));
    memzero(&s_responseBuffer[0], sizeof(s_responseBuffer));

    CtrlHeader* request = reinterpret_cast<CtrlHeader*>(&s_commandBuffer[0]);
    request->type = CMD_GET_DISPLAY_INFO;
    request->flags = 0;
    request->fenceId = 0;
    request->ctxId = 0;
    request->padding = 0;

    queue.desc[0].addr = virt_to_phys(request);
    queue.desc[0].len = sizeof(CtrlHeader);
    queue.desc[0].flags = 0;
    queue.desc[0].next = 1;

    queue.desc[1].addr = virt_to_phys(&s_responseBuffer[0]);
    queue.desc[1].len = sizeof(RespDisplayInfo);
    queue.desc[1].flags = VRING_DESC_F_WRITE;
    queue.desc[1].next = 0;

    const uint16_t slot = static_cast<uint16_t>(queue.avail->idx % queue.size);
    const uint16_t usedBefore = queue.used->idx;
    queue.avail->ring[slot] = 0;
    MEMORY_BARRIER();
    queue.avail->idx = static_cast<uint16_t>(queue.avail->idx + 1);
    MEMORY_BARRIER();
    queue_notify(transport, 0);

    uint32_t spin = 0;
    while (queue.used->idx == usedBefore && spin < kResponseSpinLimit) {
        MEMORY_BARRIER();
        ++spin;
    }

    if (queue.used->idx == usedBefore) {
        kernel::serial::puts("[VIRTIO-GPU] GET_DISPLAY_INFO timed out\n");
        return false;
    }

    const VringUsedElem& usedElem = queue.used->ring[queue.lastUsedIdx % queue.size];
    queue.lastUsedIdx = queue.used->idx;

    if (usedElem.id != 0) {
        kernel::serial::puts("[VIRTIO-GPU] GET_DISPLAY_INFO completed on unexpected descriptor\n");
        return false;
    }

    const RespDisplayInfo* response = reinterpret_cast<const RespDisplayInfo*>(&s_responseBuffer[0]);
    if (response->header.type != RESP_OK_DISPLAY_INFO) {
        kernel::serial::puts("[VIRTIO-GPU] GET_DISPLAY_INFO returned type=0x");
        kernel::serial::put_hex32(response->header.type);
        kernel::serial::putc('\n');
        return false;
    }

    state.device.numScanouts = 0;
    if (transport.deviceCfg.present) {
        const GpuConfig* config = reinterpret_cast<const GpuConfig*>(transport.deviceCfg.base + transport.deviceCfg.offset);
        state.device.numScanouts = config->numScanouts;
        state.device.features = 0;

        kernel::serial::puts("[VIRTIO-GPU] device config scanouts=");
        serial_put_u32_decimal(config->numScanouts);
        kernel::serial::puts(" capsets=");
        serial_put_u32_decimal(config->numCapsets);
        kernel::serial::putc('\n');
    }

    if (state.device.numScanouts > MAX_SCANOUTS) {
        state.device.numScanouts = MAX_SCANOUTS;
    }

    const uint32_t reportedCount = state.device.numScanouts;
    kernel::serial::puts("[VIRTIO-GPU] Display info scanouts=");
    serial_put_u32_decimal(reportedCount);
    kernel::serial::putc('\n');

    uint32_t discovered = 0;
    for (uint32_t i = 0; i < MAX_SCANOUTS; ++i) {
        const DisplayOne& mode = response->pmodes[i];
        const bool withinReportedCount = (i < reportedCount);
        const bool hasData = withinReportedCount ||
                             mode.enabled != 0 ||
                             mode.flags != 0 ||
                             mode.rect.x != 0 ||
                             mode.rect.y != 0 ||
                             mode.rect.width != 0 ||
                             mode.rect.height != 0;
        if (!hasData) {
            continue;
        }

        if (withinReportedCount) {
            ++discovered;
        }

        state.device.displays[i].width = mode.rect.width;
        state.device.displays[i].height = mode.rect.height;
        state.device.displays[i].enabled = mode.enabled != 0;

        kernel::serial::puts("[VIRTIO-GPU]   scanout[");
        serial_put_u32_decimal(i);
        kernel::serial::puts("] enabled=");
        kernel::serial::puts(mode.enabled != 0 ? "1" : "0");
        kernel::serial::puts(" x=");
        serial_put_u32_decimal(mode.rect.x);
        kernel::serial::puts(" y=");
        serial_put_u32_decimal(mode.rect.y);
        kernel::serial::puts(" width=");
        serial_put_u32_decimal(mode.rect.width);
        kernel::serial::puts(" height=");
        serial_put_u32_decimal(mode.rect.height);
        kernel::serial::puts(" flags=0x");
        kernel::serial::put_hex32(mode.flags);
        kernel::serial::putc('\n');
    }

    state.device.numScanouts = discovered;
    state.device.initialized = true;
    state.transport.probeComplete = true;
    return true;
}

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
        record_probe_outcome(state, false, DisplayInfoOutcome::NotQueried, 0,
                             transport_blocker_reason(transport));
        return false;
    }

    transport.modern = true;

    return true;
}

static bool initialize_device(DeviceState& state)
{
    ModernTransport& transport = state.transport;
    GpuDevice& device = state.device;

    memzero(&device, sizeof(device));
    device.initialized = false;
    device.isPci = true;
    device.pciBus = transport.bus;
    device.pciDevice = transport.device;
    device.pciFunction = transport.function;
    device.irqLine = 0xFF;
    transport.mmioMapped = false;
    transport.mmioSanityReadsOk = false;
    transport.mmioMappedVirtual = 0;
    transport.mmioMappedPageCount = 0;
    transport.mmioNumQueues = 0;
    transport.mmioDeviceStatus = 0;
    transport.mmioConfigGeneration = 0;
    transport.mmioDeviceScanouts = 0;
    transport.mmioDeviceCapsets = 0;
    transport.mmioCacheMode = "n/a";
    transport.mmioStopReason = "transport writes intentionally disabled";

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
        record_probe_outcome(state, false, DisplayInfoOutcome::NotQueried, 0, mmioBlocker);
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

    kernel::serial::puts("[VIRTIO-GPU] MMIO transport mapped; read-only sanity reads complete; GET_DISPLAY_INFO remains disabled in this diagnostic pass\n");
    record_probe_outcome(state, false, DisplayInfoOutcome::NotQueried, 0,
                         transport.mmioStopReason);
    return true;
}

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
