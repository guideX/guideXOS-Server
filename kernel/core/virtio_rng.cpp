// VirtIO RNG Driver Implementation
//
// Supports the legacy/transitional PCI transport used by QEMU's
// virtio-rng-pci device when modern mode is disabled for smoke tests.
//

#include "include/kernel/virtio_rng.h"

#include "include/kernel/arch.h"
#include "include/kernel/feature_report.h"
#include "include/kernel/msi.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/virtio.h"

namespace kernel {
namespace virtio {
namespace rng {

namespace {

static const uint16_t kLegacyPciDeviceId = PCI_DEVICE_ENTROPY_LEGACY;
static const uint16_t kModernPciDeviceId = static_cast<uint16_t>(PCI_DEVICE_BASE_MODERN + DEVICE_ENTROPY);

static const uint8_t kPciCommandOffset = 0x04;
static const uint16_t kPciCommandIoSpace = 0x0001;
static const uint16_t kPciCommandBusMaster = 0x0004;
static const uint8_t kPciHeaderTypeOffset = 0x0E;
static const uint8_t kPciBar0Offset = 0x10;

static const uint16_t kLegacyOffsetDriverFeatures = 0x04;
static const uint16_t kLegacyOffsetQueueAddress = 0x08;
static const uint16_t kLegacyOffsetQueueSize = 0x0C;
static const uint16_t kLegacyOffsetQueueSelect = 0x0E;
static const uint16_t kLegacyOffsetQueueNotify = 0x10;
static const uint16_t kLegacyOffsetDeviceStatus = 0x12;

static const uint32_t kQueueAlign = 4096;
static const uint16_t kMaxQueueSize = 128;
static const size_t kQueueStorageBytes = 16384;
static const size_t kMaxRequestBytes = 256;
static const uint64_t kRequestTimeoutTicks = 3;
static const uint32_t kBusyWaitLimit = 5000000;

struct DeviceState {
    bool initAttempted;
    bool deviceDetected;
    bool driverReady;
    bool legacyTransport;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t ioBase;
    uint16_t queueSize;
    uint16_t lastUsedIdx;
    uint64_t kernelPhysicalBase;
    Status lastStatus;
};

static DeviceState s_state = {
    false, false, false, false,
    0, 0, 0, 0, 0, 0,
    0x100000,
    STATUS_NOT_INITIALIZED
};

#if defined(__GNUC__) || defined(__clang__)
static uint8_t s_queueStorage[kQueueStorageBytes] __attribute__((aligned(kQueueAlign)));
#else
__declspec(align(4096)) static uint8_t s_queueStorage[kQueueStorageBytes];
#endif

static Virtqueue s_queue;
static uint8_t s_requestBuffer[kMaxRequestBytes];

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) p[i] = 0;
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static uint64_t dma_address(const void* ptr)
{
    const uint64_t virt = reinterpret_cast<uint64_t>(ptr);
    if (virt >= 0x100000) {
        return s_state.kernelPhysicalBase + (virt - 0x100000);
    }
    return virt;
}

static void barrier()
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" ::: "memory");
#else
    _ReadWriteBarrier();
#endif
}

static uint8_t io_read8(uint16_t base, uint16_t offset)
{
    return arch::inb(static_cast<uint16_t>(base + offset));
}

static uint16_t io_read16(uint16_t base, uint16_t offset)
{
    return arch::inw(static_cast<uint16_t>(base + offset));
}

static uint32_t io_read32(uint16_t base, uint16_t offset)
{
    return arch::inl(static_cast<uint16_t>(base + offset));
}

static void io_write8(uint16_t base, uint16_t offset, uint8_t value)
{
    arch::outb(static_cast<uint16_t>(base + offset), value);
}

static void io_write16(uint16_t base, uint16_t offset, uint16_t value)
{
    arch::outw(static_cast<uint16_t>(base + offset), value);
}

static void io_write32(uint16_t base, uint16_t offset, uint32_t value)
{
    arch::outl(static_cast<uint16_t>(base + offset), value);
}

static void set_last_status(Status status)
{
    s_state.lastStatus = status;
}

static void update_feature_report_status()
{
    using namespace kernel::feature_report;
    switch (s_state.lastStatus) {
    case STATUS_SUCCESS:
        complete_init_details(VIRTIO_RNG, "legacy PCI transitional");
        break;
    case STATUS_DEVICE_NOT_FOUND:
        update_status_details(VIRTIO_RNG, STATUS_NOT_PRESENT, "not detected");
        break;
    case STATUS_UNSUPPORTED_ARCH:
        update_status_details(VIRTIO_RNG, STATUS_DISABLED, "port I/O PCI required");
        break;
    case STATUS_UNSUPPORTED_VIRTIO_MODE:
        update_status_details(VIRTIO_RNG, STATUS_ERROR, "modern/non-transitional unsupported");
        break;
    case STATUS_QUEUE_SETUP_FAILED:
        update_status_details(VIRTIO_RNG, STATUS_ERROR, "queue setup failed");
        break;
    case STATUS_REQUEST_TIMEOUT:
        update_status_details(VIRTIO_RNG, STATUS_ERROR, "request timeout");
        break;
    case STATUS_SHORT_READ:
        update_status_details(VIRTIO_RNG, STATUS_ERROR, "short read");
        break;
    case STATUS_DEVICE_ERROR:
        update_status_details(VIRTIO_RNG, STATUS_ERROR, "device error");
        break;
    default:
        update_status_details(VIRTIO_RNG, STATUS_PRESENT, "initializing");
        break;
    }
}

static bool queue_layout(uint16_t queueSize, Virtqueue* out)
{
    if (!out || queueSize == 0 || queueSize > kMaxQueueSize) return false;

    memzero(s_queueStorage, sizeof(s_queueStorage));
    memzero(out, sizeof(*out));

    const uint64_t base = reinterpret_cast<uint64_t>(&s_queueStorage[0]);
    const uint64_t desc = base;
    const uint64_t avail = desc + queueSize * sizeof(VringDesc);
    const uint64_t used = align_up(avail + sizeof(uint16_t) * (3 + queueSize), kQueueAlign);
    const uint64_t end = used + sizeof(uint16_t) * 3 + queueSize * sizeof(VringUsedElem);
    if (end > base + sizeof(s_queueStorage)) return false;

    out->size = queueSize;
    out->index = 0;
    out->desc = reinterpret_cast<VringDesc*>(desc);
    out->avail = reinterpret_cast<VringAvail*>(avail);
    out->used = reinterpret_cast<VringUsed*>(used);
    out->lastUsedIdx = 0;
    out->freeHead = 0;
    out->numFree = queueSize;
    out->descPhys = dma_address(reinterpret_cast<void*>(desc));
    out->availPhys = dma_address(reinterpret_cast<void*>(avail));
    out->usedPhys = dma_address(reinterpret_cast<void*>(used));
    return true;
}

static void reset_device()
{
    if (s_state.ioBase == 0) return;
    io_write8(s_state.ioBase, kLegacyOffsetDeviceStatus, 0);
    io_write32(s_state.ioBase, kLegacyOffsetQueueAddress, 0);
    barrier();
}

static bool setup_queue()
{
    io_write16(s_state.ioBase, kLegacyOffsetQueueSelect, 0);
    const uint16_t queueSize = io_read16(s_state.ioBase, kLegacyOffsetQueueSize);
    if (queueSize == 0 || queueSize > kMaxQueueSize) return false;
    if (!queue_layout(queueSize, &s_queue)) return false;

    io_write32(s_state.ioBase, kLegacyOffsetQueueAddress,
        static_cast<uint32_t>(s_queue.descPhys / kQueueAlign));
    if (io_read32(s_state.ioBase, kLegacyOffsetQueueAddress) == 0) return false;

    s_state.queueSize = queueSize;
    s_state.lastUsedIdx = 0;
    return true;
}

static bool scan_pci_for_device()
{
    bool sawModernOnlyDevice = false;

    for (uint8_t bus = 0; bus < 8; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            const uint32_t id0 = msi::pci_config_read32(bus, dev, 0, 0x00);
            if (id0 == 0xFFFFFFFF || id0 == 0) continue;

            const uint8_t headerType = msi::pci_config_read8(bus, dev, 0, kPciHeaderTypeOffset);
            const uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < maxFunc; ++func) {
                const uint32_t id = (func == 0) ? id0 : msi::pci_config_read32(bus, dev, func, 0x00);
                if (id == 0xFFFFFFFF || id == 0) continue;

                const uint16_t vendor = static_cast<uint16_t>(id & 0xFFFFu);
                const uint16_t deviceId = static_cast<uint16_t>(id >> 16);
                if (vendor != PCI_VENDOR_ID) continue;
                if (deviceId != kLegacyPciDeviceId && deviceId != kModernPciDeviceId) continue;

                s_state.deviceDetected = true;
                s_state.bus = bus;
                s_state.device = dev;
                s_state.function = func;

                if (deviceId == kModernPciDeviceId) {
                    sawModernOnlyDevice = true;
                    continue;
                }

                const uint32_t bar0 = msi::pci_config_read32(bus, dev, func, kPciBar0Offset);
                if ((bar0 & 0x1u) == 0) {
                    set_last_status(STATUS_UNSUPPORTED_VIRTIO_MODE);
                    return false;
                }

                s_state.ioBase = static_cast<uint16_t>(bar0 & ~0x3u);
                s_state.legacyTransport = true;

                uint16_t command = msi::pci_config_read16(bus, dev, func, kPciCommandOffset);
                command |= static_cast<uint16_t>(kPciCommandIoSpace | kPciCommandBusMaster);
                msi::pci_config_write16(bus, dev, func, kPciCommandOffset, command);
                return true;
            }
        }
    }

    if (sawModernOnlyDevice) {
        set_last_status(STATUS_UNSUPPORTED_VIRTIO_MODE);
        return false;
    }

    set_last_status(STATUS_DEVICE_NOT_FOUND);
    return false;
}

static bool initialize_legacy_device()
{
    reset_device();
    io_write8(s_state.ioBase, kLegacyOffsetDeviceStatus, STATUS_ACKNOWLEDGE);
    io_write8(s_state.ioBase, kLegacyOffsetDeviceStatus, static_cast<uint8_t>(STATUS_ACKNOWLEDGE | STATUS_DRIVER));
    io_write32(s_state.ioBase, kLegacyOffsetDriverFeatures, 0);

    if (!setup_queue()) {
        set_last_status(STATUS_QUEUE_SETUP_FAILED);
        update_feature_report_status();
        return false;
    }

    io_write8(s_state.ioBase, kLegacyOffsetDeviceStatus,
        static_cast<uint8_t>(STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_DRIVER_OK));
    s_state.driverReady = true;
    set_last_status(STATUS_SUCCESS);
    update_feature_report_status();
    serial::puts("[VIRTIO-RNG] Initialized via legacy PCI transport\n");
    return true;
}

static bool ensure_request_queue_ready()
{
    if (!s_state.driverReady) {
        return init();
    }

    if (s_queue.desc == nullptr || s_queue.avail == nullptr || s_queue.used == nullptr || s_queue.size == 0) {
        return initialize_legacy_device();
    }

    // The bare-metal TLS path can arrive after queue storage was cleared while
    // the device transport remained present. Re-arm the legacy queue when the
    // remembered last-used slot has moved ahead of the current ring state.
    if (s_queue.lastUsedIdx > s_queue.used->idx) {
        return initialize_legacy_device();
    }

    return true;
}

static bool wait_for_request_completion(uint16_t expectedUsedIdx)
{
    const uint64_t startTick = pit::ticks();
    uint32_t spins = 0;
    while (true) {
        barrier();
        const uint16_t usedIdx = s_queue.used->idx;
        if (usedIdx != expectedUsedIdx) return true;
        if ((pit::ticks() - startTick) >= kRequestTimeoutTicks) return false;
        if (++spins >= kBusyWaitLimit) return false;
    }
}

} // namespace

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0) s_state.kernelPhysicalBase = physicalBase;
}

bool init()
{
#if !ARCH_HAS_PORT_IO
    if (!s_state.initAttempted) {
        s_state.initAttempted = true;
        set_last_status(STATUS_UNSUPPORTED_ARCH);
        update_feature_report_status();
    }
    return false;
#else
    if (s_state.driverReady) return true;
    if (s_state.initAttempted && !s_state.driverReady) return false;

    s_state.initAttempted = true;
    s_state.deviceDetected = false;
    s_state.driverReady = false;
    s_state.legacyTransport = false;
    s_state.ioBase = 0;
    s_state.queueSize = 0;
    s_state.lastUsedIdx = 0;
    set_last_status(STATUS_NOT_INITIALIZED);
    update_feature_report_status();

    if (!scan_pci_for_device()) {
        update_feature_report_status();
        if (s_state.lastStatus == STATUS_UNSUPPORTED_VIRTIO_MODE) {
            serial::puts("[VIRTIO-RNG] Entropy device found but transport mode is unsupported\n");
        } else {
            serial::puts("[VIRTIO-RNG] No virtio-rng PCI device detected\n");
        }
        return false;
    }

    return initialize_legacy_device();
#endif
}

bool fill(void* buffer, size_t len)
{
    if (!buffer || len == 0 || len > kMaxRequestBytes) {
        if (len == 0) return true;
        set_last_status(STATUS_DEVICE_ERROR);
        update_feature_report_status();
        return false;
    }
    if (!ensure_request_queue_ready()) return false;

    memzero(buffer, len);
    memzero(s_requestBuffer, sizeof(s_requestBuffer));
    s_queue.desc[0].addr = dma_address(&s_requestBuffer[0]);
    s_queue.desc[0].len = static_cast<uint32_t>(len);
    s_queue.desc[0].flags = VRING_DESC_F_WRITE;
    s_queue.desc[0].next = 0;

    const uint16_t availSlot = static_cast<uint16_t>(s_queue.avail->idx % s_queue.size);
    const uint16_t expectedUsedIdx = s_queue.used->idx;
    s_queue.avail->ring[availSlot] = 0;
    barrier();
    s_queue.avail->idx = static_cast<uint16_t>(s_queue.avail->idx + 1);
    barrier();
    io_write16(s_state.ioBase, kLegacyOffsetQueueNotify, 0);

    if (!wait_for_request_completion(expectedUsedIdx)) {
        set_last_status(STATUS_REQUEST_TIMEOUT);
        update_feature_report_status();
        return false;
    }

    const VringUsedElem& usedElem = s_queue.used->ring[s_queue.lastUsedIdx % s_queue.size];
    s_queue.lastUsedIdx = s_queue.used->idx;

    if (usedElem.id != 0) {
        set_last_status(STATUS_DEVICE_ERROR);
        update_feature_report_status();
        return false;
    }
    if (usedElem.len < len) {
        set_last_status(STATUS_SHORT_READ);
        update_feature_report_status();
        return false;
    }

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < len; ++i) dst[i] = s_requestBuffer[i];

    set_last_status(STATUS_SUCCESS);
    update_feature_report_status();
    return true;
}

bool detected()
{
    if (!s_state.initAttempted) init();
    return s_state.deviceDetected;
}

bool ready()
{
    if (!s_state.initAttempted) init();
    return s_state.driverReady;
}

Status last_status()
{
    if (!s_state.initAttempted) init();
    return s_state.lastStatus;
}

const char* last_status_name()
{
    switch (last_status()) {
    case STATUS_DEVICE_NOT_FOUND: return "device-not-found";
    case STATUS_UNSUPPORTED_ARCH: return "unsupported-arch";
    case STATUS_UNSUPPORTED_VIRTIO_MODE: return "unsupported-virtio-mode";
    case STATUS_QUEUE_SETUP_FAILED: return "queue-setup-failed";
    case STATUS_REQUEST_TIMEOUT: return "request-timeout";
    case STATUS_SHORT_READ: return "short-read";
    case STATUS_DEVICE_ERROR: return "device-error";
    case STATUS_SUCCESS: return "success";
    default: return "not-initialized";
    }
}

const char* backend_name()
{
    return "virtio-rng legacy PCI transitional";
}

} // namespace rng
} // namespace virtio
} // namespace kernel
