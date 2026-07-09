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
    bool present;
    uint8_t bar;
    uint64_t base;
    uint32_t offset;
    uint32_t length;
};

struct ModernTransport {
    bool present;
    bool modern;
    bool probeComplete;
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
    PciRegion commonCfg;
    PciRegion notifyCfg;
    PciRegion isrCfg;
    PciRegion deviceCfg;
    PciRegion pciCfg;
    Virtqueue controlQueue;
};

struct DeviceState {
    GpuDevice device;
    ModernTransport transport;
};

static bool s_initialized = false;
static DeviceState s_devices[4];
static int s_deviceCount = 0;

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

static bool read_bar_base(uint8_t bus, uint8_t device, uint8_t function, uint8_t barIndex, uint64_t* baseOut)
{
    if (baseOut == nullptr || barIndex > 5) {
        return false;
    }

    const uint8_t offset = static_cast<uint8_t>(kPciBar0Offset + (barIndex * 4));
    const uint32_t barLow = msi::pci_config_read32(bus, device, function, offset);
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
    return true;
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

static void log_region(const char* label, const PciRegion& region)
{
    kernel::serial::puts("[VIRTIO-GPU] ");
    kernel::serial::puts(label);
    kernel::serial::puts(" bar=");
    kernel::serial::put_hex8(region.bar);
    kernel::serial::puts(" base=0x");
    kernel::serial::put_hex64(region.base);
    kernel::serial::puts(" offset=0x");
    kernel::serial::put_hex32(region.offset);
    kernel::serial::puts(" length=0x");
    kernel::serial::put_hex32(region.length);
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
        return false;
    }

    uint8_t capPtr = static_cast<uint8_t>(msi::pci_config_read8(bus, device, function, 0x34) & 0xFCu);
    int guard = 64;

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

        if (cap.capId == kPciCapabilityVendorSpecific) {
            PciRegion* region = nullptr;
            const char* label = nullptr;

            switch (cap.cfgType) {
            case pci::CAP_COMMON_CFG:
                region = &transport->commonCfg;
                label = "common";
                break;
            case pci::CAP_NOTIFY_CFG:
                region = &transport->notifyCfg;
                label = "notify";
                break;
            case pci::CAP_ISR_CFG:
                region = &transport->isrCfg;
                label = "isr";
                break;
            case pci::CAP_DEVICE_CFG:
                region = &transport->deviceCfg;
                label = "device";
                break;
            case pci::CAP_PCI_CFG:
                region = &transport->pciCfg;
                label = "pci-cfg";
                break;
            default:
                break;
            }

            if (region != nullptr) {
                uint64_t base = 0;
                if (!read_bar_base(bus, device, function, cap.bar, &base)) {
                    const uint32_t rawBar = msi::pci_config_read32(bus, device, function,
                                                                    static_cast<uint8_t>(kPciBar0Offset + (cap.bar * 4u)));
                    kernel::serial::puts("[VIRTIO-GPU] Failed to resolve BAR for virtio capability capPtr=0x");
                    kernel::serial::put_hex8(capPtr);
                    kernel::serial::puts(" cfgType=0x");
                    kernel::serial::put_hex8(cap.cfgType);
                    kernel::serial::puts(" bar=");
                    kernel::serial::put_hex8(cap.bar);
                    kernel::serial::puts(" rawBar=0x");
                    kernel::serial::put_hex32(rawBar);
                    kernel::serial::putc('\n');
                    return false;
                }

                region->present = true;
                region->bar = cap.bar;
                region->base = base;
                region->offset = cap.offset;
                region->length = cap.length;
                log_region(label, *region);

                if (cap.cfgType == pci::CAP_NOTIFY_CFG) {
                    transport->notifyOffMultiplier = msi::pci_config_read32(bus, device, function, static_cast<uint8_t>(capPtr + 16));
                    kernel::serial::puts("[VIRTIO-GPU] notify multiplier=0x");
                    kernel::serial::put_hex32(transport->notifyOffMultiplier);
                    kernel::serial::putc('\n');
                }
            }
        }

        capPtr = cap.nextPtr;
    }

    return transport->commonCfg.present &&
           transport->notifyCfg.present &&
           transport->isrCfg.present &&
           transport->deviceCfg.present;
}

static uint64_t common_cfg_addr(const ModernTransport& transport, uint32_t fieldOffset)
{
    return transport.commonCfg.base + transport.commonCfg.offset + fieldOffset;
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
    const uint64_t notifyAddr = transport.notifyCfg.base +
                                transport.notifyCfg.offset +
                                static_cast<uint64_t>(transport.queueNotifyOff) *
                                static_cast<uint64_t>(transport.notifyOffMultiplier);

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
    transport.modern = true;

    if (!parse_virtio_regions(&transport)) {
        kernel::serial::puts("[VIRTIO-GPU] Modern virtio-gpu capability discovery failed; candidate appears legacy or lacks modern VirtIO PCI caps\n");
        transport.modern = false;
        return false;
    }

    return true;
}

static bool initialize_device(DeviceState& state)
{
    ModernTransport& transport = state.transport;
    GpuDevice& device = state.device;

    memzero(&device, sizeof(device));
    device.isPci = true;
    device.pciBus = transport.bus;
    device.pciDevice = transport.device;
    device.pciFunction = transport.function;
    device.irqLine = 0xFF;

    kernel::serial::puts("[VIRTIO-GPU] Initializing diagnostic-only virtio-gpu device\n");

    reset_device(transport);
    write_status(transport, STATUS_ACKNOWLEDGE);
    write_status(transport, static_cast<uint8_t>(read_status(transport) | STATUS_DRIVER));

    transport.deviceFeaturesLow = read_device_features(transport) & 0xFFFFFFFFu;
    transport.deviceFeaturesHigh = static_cast<uint32_t>(read_device_features(transport) >> 32);
    transport.negotiatedFeatures = 0;

    kernel::serial::puts("[VIRTIO-GPU] Device features low=0x");
    kernel::serial::put_hex32(transport.deviceFeaturesLow);
    kernel::serial::puts(" high=0x");
    kernel::serial::put_hex32(transport.deviceFeaturesHigh);
    kernel::serial::putc('\n');

    uint64_t negotiated = 0;
    if ((read_device_features(transport) & kCommonCfgRequiredFeatureBits) != 0) {
        negotiated |= FEATURE_VERSION_1;
    }

    transport.negotiatedFeatures = negotiated;
    write_driver_features(transport, negotiated);
    write_status(transport, static_cast<uint8_t>(read_status(transport) | STATUS_FEATURES_OK));
    if ((read_status(transport) & STATUS_FEATURES_OK) == 0) {
        kernel::serial::puts("[VIRTIO-GPU] Device rejected negotiated features\n");
        return false;
    }

    if (!setup_control_queue(transport)) {
        kernel::serial::puts("[VIRTIO-GPU] Control queue setup failed\n");
        return false;
    }

    transport.queueNotifyOff = mmio_read16(common_cfg_addr(transport, pci::COMMON_Q_NOTIFY_OFF));
    kernel::serial::puts("[VIRTIO-GPU] queue notify offset=0x");
    kernel::serial::put_hex16(transport.queueNotifyOff);
    kernel::serial::puts(" multiplier=0x");
    kernel::serial::put_hex32(transport.notifyOffMultiplier);
    kernel::serial::putc('\n');

    write_status(transport, static_cast<uint8_t>(read_status(transport) | STATUS_DRIVER_OK));

    if (!submit_display_info_request(state)) {
        kernel::serial::puts("[VIRTIO-GPU] GET_DISPLAY_INFO query failed\n");
        return false;
    }

    device.nextResourceId = 1;
    device.fbResourceId = 0;
    device.fbWidth = 0;
    device.fbHeight = 0;
    device.fbFormat = 0;
    device.fbBuffer = nullptr;
    device.fbBufferPhys = 0;
    device.fbBufferSize = 0;
    device.framesDisplayed = 0;
    device.flushCount = 0;
    device.has3D = false;
    device.initialized = true;

    transport.modern = true;
    transport.probeComplete = true;

    kernel::serial::puts("[VIRTIO-GPU] Diagnostic probe complete\n");
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
                    kernel::serial::puts("[VIRTIO-GPU] Candidate matched but could not be safely queried\n");
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

    kernel::serial::puts("[VIRTIO-GPU] Probe complete, devices=");
    serial_put_u32_decimal(static_cast<uint32_t>(s_deviceCount));
    kernel::serial::putc('\n');

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

    return initialize_device(*state) ? GPU_OK : GPU_ERR_INIT_FAIL;
#endif
}

GpuStatus reset_device(GpuDevice* dev)
{
    DeviceState* state = active_state(dev);
    if (state == nullptr) {
        return GPU_ERR_INVALID;
    }

#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE)
    if (state->transport.present && state->transport.commonCfg.present) {
        reset_device(state->transport);
        state->device.initialized = false;
        return GPU_OK;
    }
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

    return submit_display_info_request(*state) ? GPU_OK : GPU_ERR_IO;
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
