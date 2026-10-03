// Conservative AHCI/SATA block transport.
//
// The production profile supports direct SATA disks on x86/AMD64, one
// non-NCQ command slot at a time, polling, a bounded PRDT-backed DMA bounce
// buffer, and IDENTIFY-advertised cache flush. NCQ, port multipliers, ATAPI,
// interrupts, and hotplug events are intentionally not claimed.

#include "include/kernel/ahci.h"
#include "include/kernel/ahci_logic.h"
#include "include/kernel/arch.h"
#include "include/kernel/ata.h"
#include "include/kernel/block_device.h"
#include "include/kernel/mmio.h"
#include "include/kernel/pit.h"

#if defined(__GNUC__) || defined(__clang__)
#include "include/kernel/serial_debug.h"
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#define AHCI_ALIGNED(n) __declspec(align(n))
#else
#define AHCI_ALIGNED(n) __attribute__((aligned(n)))
#endif

#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(__i386__) || defined(__x86_64__)
#define AHCI_HAS_PCI_PORT_IO 1
#else
#define AHCI_HAS_PCI_PORT_IO 0
#endif

namespace kernel {
namespace ahci {
namespace {

static const uint8_t kMaxControllers = 4;
static const uint8_t kMaxDevices = 8;
static const uint32_t kAbarMappedBytes = 0x1100u;
static const uint32_t kMaxTransferBytes = 128u * 1024u;
static const uint32_t kCommandTimeoutTicks = 3000u; // 30 seconds at 100 Hz.
static const uint32_t kFlushTimeoutTicks = 6000u;   // 60 seconds at 100 Hz.
static const uint32_t kPortTimeoutTicks = 500u;      // 5 seconds.
static const uint32_t kControllerTimeoutTicks = 1000u; // 10 seconds.
static const uint32_t kFallbackPollLimit = 250000000u;
static const uint32_t kLockPollLimit = 25000000u;
static const uint64_t kDmaPhysicalLimit = 0x000FFFFFFFFFFFFFULL;
static const uint32_t kHbaPortBase = 0x100u;
static const uint32_t kHbaPortStride = 0x80u;
static const uint32_t kAhciGhC = 0x04u;
static const uint32_t kAhciGlobalIs = 0x08u;
static const uint32_t kAhciPi = 0x0Cu;
static const uint32_t kAhciVersion = 0x10u;
static const uint32_t kAhciCap2 = 0x24u;
static const uint32_t kAhciBohc = 0x28u;
static const uint32_t kGhCReset = 1u;
static const uint32_t kGhCAhciEnable = 1u << 31;
static const uint32_t kGhCInterruptEnable = 1u << 1;
static const uint32_t kCapS64A = 1u << 31;
static const uint32_t kCapBoh = 1u;
static const uint32_t kBohBiosOwned = 1u;
static const uint32_t kBohOsOwned = 1u << 1;
static const uint32_t kBohBiosBusy = 1u << 4;
static const uint32_t kPxClb = 0x00u;
static const uint32_t kPxClbu = 0x04u;
static const uint32_t kPxFb = 0x08u;
static const uint32_t kPxFbu = 0x0Cu;
static const uint32_t kPxIs = 0x10u;
static const uint32_t kPxIe = 0x14u;
static const uint32_t kPxCmd = 0x18u;
static const uint32_t kPxTfd = 0x20u;
static const uint32_t kPxSig = 0x24u;
static const uint32_t kPxSsts = 0x28u;
static const uint32_t kPxSctl = 0x2Cu;
static const uint32_t kPxSerr = 0x30u;
static const uint32_t kPxSact = 0x34u;
static const uint32_t kPxCi = 0x38u;
static const uint32_t kPxCmdSt = 1u;
static const uint32_t kPxCmdFre = 1u << 4;
static const uint32_t kPxCmdFr = 1u << 14;
static const uint32_t kPxCmdCr = 1u << 15;
static const uint32_t kPxIsFatal = (1u << 30) | (1u << 29) | (1u << 28) |
    (1u << 27) | (1u << 26) | (1u << 24);
static const uint32_t kMmioMapFlags = mmio::MAP_FLAG_NON_USER |
    mmio::MAP_FLAG_NO_EXEC | mmio::MAP_FLAG_UNCACHED;

struct Controller {
    bool active;
    bool dma64;
    uint8_t pciBus;
    uint8_t pciDevice;
    uint8_t pciFunction;
    uint16_t vendorId;
    uint16_t deviceId;
    uint64_t abarPhysical;
    uint64_t abarVirtual;
    uint32_t capabilities;
    uint32_t capabilities2;
    uint32_t implementedPorts;
    uint32_t version;
    uint8_t slotCount;
};

struct CommandHeader {
    uint16_t flags;
    uint16_t prdtLength;
    uint32_t transferredBytes;
    uint32_t tableAddressLow;
    uint32_t tableAddressHigh;
    uint32_t reserved[4];
};

struct CommandTablePrefix {
    uint8_t commandFis[64];
    uint8_t atapiCommand[16];
    uint8_t reserved[48];
    logic::Prdt prdt;
};

struct IoDiagnostic {
    bool valid;
    block::Status status;
    uint8_t stage;
    uint8_t command;
    uint8_t slot;
    uint64_t lba;
    uint32_t count;
    uint32_t taskFileData;
    uint32_t portInterruptStatus;
    uint32_t sataError;
    uint32_t completedSectors;
    uint32_t transferredBytes;
};

struct PortDevice {
    DeviceInfo info;
    uint8_t controllerIndex;
    uint8_t ahciPort;
    uint8_t globalIndex;
    uint64_t registrationId;
    bool online;
    IoDiagnostic diagnostic;
};

static Controller s_controllers[kMaxControllers];
static PortDevice s_devices[kMaxDevices];
static uint8_t s_controllerCount;
static uint8_t s_deviceCount;
static uint64_t s_kernelPhysicalBase = 0x100000ULL;
static volatile uint32_t s_ioLock;

// Per-port allocations avoid sharing CLB/RFIS storage between simultaneously
// running port engines.  The global I/O lock still serializes all commands.
AHCI_ALIGNED(1024) static uint8_t s_commandList[kMaxDevices][1024];
AHCI_ALIGNED(256) static uint8_t s_receivedFis[kMaxDevices][256];
AHCI_ALIGNED(128) static uint8_t s_commandTables[kMaxDevices][256];
AHCI_ALIGNED(4096) static uint8_t s_dmaBuffers[kMaxDevices][kMaxTransferBytes];
AHCI_ALIGNED(4096) static uint8_t s_identifyCopies[kMaxDevices][512];

static void memzero(void* destination, size_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}

static void memcopy(void* destination, const void* source, size_t size)
{
    uint8_t* dst = static_cast<uint8_t*>(destination);
    const uint8_t* src = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < size; ++i) dst[i] = src[i];
}

static void memory_barrier()
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
    _mm_mfence();
#endif
}

static bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void fix_ata_string(char* text, uint32_t length)
{
    for (uint32_t i = 0; i + 1u < length; i += 2u) {
        const char value = text[i];
        text[i] = text[i + 1u];
        text[i + 1u] = value;
    }
    int32_t end = static_cast<int32_t>(length) - 1;
    while (end >= 0 && (text[end] == ' ' || text[end] == '\0'))
        text[end--] = '\0';
    text[length] = '\0';
}

#if defined(__GNUC__) || defined(__clang__)
static const char* port_state_name(logic::PortState state)
{
    switch (state) {
        case logic::PORT_NOT_IMPLEMENTED: return "not-implemented";
        case logic::PORT_NO_DEVICE: return "no-device";
        case logic::PORT_LINK_ERROR: return "link-error";
        case logic::PORT_POWERED_DOWN: return "powered-down";
        case logic::PORT_LINK_INACTIVE: return "link-inactive";
        case logic::PORT_SATA_DISK: return "sata-disk";
        case logic::PORT_SATAPI: return "satapi";
        case logic::PORT_SEMB: return "semb";
        case logic::PORT_MULTIPLIER: return "port-multiplier";
        case logic::PORT_UNSUPPORTED_SIGNATURE: return "unsupported-signature";
        default: return "unknown";
    }
}
#endif

static bool acquire_io_lock()
{
    for (uint32_t i = 0; i < kLockPollLimit; ++i) {
#if defined(__GNUC__) || defined(__clang__)
        if (__sync_lock_test_and_set(&s_ioLock, 1u) == 0u) {
            __sync_synchronize();
            return true;
        }
#elif defined(_MSC_VER)
        if (_InterlockedExchange(reinterpret_cast<volatile long*>(&s_ioLock), 1) == 0) {
            _ReadWriteBarrier();
            return true;
        }
#endif
    }
    return false;
}

static void release_io_lock()
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
    __sync_lock_release(&s_ioLock);
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
    _InterlockedExchange(reinterpret_cast<volatile long*>(&s_ioLock), 0);
#endif
}

static uint64_t virtual_to_physical(const void* pointer)
{
    const uint64_t virtualAddress = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(pointer));
    if (virtualAddress < 0x100000ULL ||
        virtualAddress - 0x100000ULL > (~0ULL - s_kernelPhysicalBase))
        return 0;
    return s_kernelPhysicalBase + (virtualAddress - 0x100000ULL);
}

static bool dma_range_valid(uint64_t physicalAddress, uint64_t length,
                            bool dma64)
{
    if (physicalAddress == 0 || length == 0 ||
        physicalAddress > kDmaPhysicalLimit ||
        length - 1u > kDmaPhysicalLimit - physicalAddress) return false;
    const uint64_t last = physicalAddress + length - 1u;
    return dma64 || last <= 0xFFFFFFFFULL;
}

static volatile uint32_t* hba_registers(const Controller& controller)
{
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(controller.abarVirtual));
}

static uint32_t hba_read(const Controller& controller, uint32_t offset)
{
    volatile uint32_t* registers = hba_registers(controller);
    const uint32_t value = registers[offset / 4u];
    memory_barrier();
    return value;
}

static void hba_write(const Controller& controller, uint32_t offset,
                      uint32_t value)
{
    memory_barrier();
    volatile uint32_t* registers = hba_registers(controller);
    registers[offset / 4u] = value;
    memory_barrier();
}

static uint32_t port_offset(uint8_t port, uint32_t registerOffset)
{
    return kHbaPortBase + static_cast<uint32_t>(port) * kHbaPortStride +
        registerOffset;
}

static uint32_t port_read(const Controller& controller, uint8_t port,
                          uint32_t registerOffset)
{
    return hba_read(controller, port_offset(port, registerOffset));
}

static void port_write(const Controller& controller, uint8_t port,
                       uint32_t registerOffset, uint32_t value)
{
    hba_write(controller, port_offset(port, registerOffset), value);
}

static void pause_cpu()
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(_MSC_VER)
    _mm_pause();
#endif
}

static bool wait_register(const Controller& controller, uint32_t offset,
                          uint32_t mask, uint32_t expected,
                          uint32_t timeoutTicks)
{
    const uint64_t startTick = pit::ticks();
    bool clockAdvanced = false;
    uint64_t previousTick = startTick;
    for (uint32_t i = 0; i < kFallbackPollLimit; ++i) {
        if ((hba_read(controller, offset) & mask) == expected) return true;
        const uint64_t now = pit::ticks();
        if (now != previousTick) clockAdvanced = true;
        previousTick = now;
        if (clockAdvanced && now - startTick >= timeoutTicks) return false;
        pause_cpu();
    }
    return false;
}

static bool wait_port_register(const Controller& controller, uint8_t port,
                               uint32_t registerOffset, uint32_t mask,
                               uint32_t expected, uint32_t timeoutTicks)
{
    const uint64_t startTick = pit::ticks();
    bool clockAdvanced = false;
    uint64_t previousTick = startTick;
    for (uint32_t i = 0; i < kFallbackPollLimit; ++i) {
        if ((port_read(controller, port, registerOffset) & mask) == expected)
            return true;
        const uint64_t now = pit::ticks();
        if (now != previousTick) clockAdvanced = true;
        previousTick = now;
        if (clockAdvanced && now - startTick >= timeoutTicks) return false;
        pause_cpu();
    }
    return false;
}

static bool stop_port(const Controller& controller, uint8_t port)
{
    uint32_t command = port_read(controller, port, kPxCmd);
    if ((command & kPxCmdSt) != 0u) {
        port_write(controller, port, kPxCmd, command & ~kPxCmdSt);
        if (!wait_port_register(controller, port, kPxCmd, kPxCmdCr, 0u,
                                kPortTimeoutTicks)) return false;
    }
    command = port_read(controller, port, kPxCmd);
    if ((command & kPxCmdFre) != 0u) {
        port_write(controller, port, kPxCmd, command & ~kPxCmdFre);
        if (!wait_port_register(controller, port, kPxCmd, kPxCmdFr, 0u,
                                kPortTimeoutTicks)) return false;
    }
    return true;
}

static bool set_port_memory(const Controller& controller, uint8_t port,
                            uint8_t driverIndex)
{
    const uint64_t commandList = virtual_to_physical(s_commandList[driverIndex]);
    const uint64_t receivedFis = virtual_to_physical(s_receivedFis[driverIndex]);
    if ((commandList & 0x3FFu) != 0u || (receivedFis & 0xFFu) != 0u ||
        !dma_range_valid(commandList, sizeof(s_commandList[driverIndex]), controller.dma64) ||
        !dma_range_valid(receivedFis, sizeof(s_receivedFis[driverIndex]), controller.dma64))
        return false;

    memzero(s_commandList[driverIndex], sizeof(s_commandList[driverIndex]));
    memzero(s_receivedFis[driverIndex], sizeof(s_receivedFis[driverIndex]));
    port_write(controller, port, kPxClb, static_cast<uint32_t>(commandList));
    port_write(controller, port, kPxClbu, static_cast<uint32_t>(commandList >> 32));
    port_write(controller, port, kPxFb, static_cast<uint32_t>(receivedFis));
    port_write(controller, port, kPxFbu, static_cast<uint32_t>(receivedFis >> 32));

#if defined(GXOS_AHCI_DIAGNOSTICS)
    serial::puts("[AHCI-DMA] command-list va=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(s_commandList[driverIndex]));
    serial::puts(" pa="); serial::put_hex64(commandList);
    serial::puts(" CLB="); serial::put_hex32(port_read(controller, port, kPxClb));
    serial::puts(" CLBU="); serial::put_hex32(port_read(controller, port, kPxClbu));
    serial::puts(" received-fis va=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(s_receivedFis[driverIndex]));
    serial::puts(" pa="); serial::put_hex64(receivedFis);
    serial::puts(" FB="); serial::put_hex32(port_read(controller, port, kPxFb));
    serial::puts(" FBU="); serial::put_hex32(port_read(controller, port, kPxFbu));
    serial::putc('\n');
#endif
    return true;
}

static bool start_port(const Controller& controller, uint8_t port)
{
    uint32_t command = port_read(controller, port, kPxCmd);
    command |= kPxCmdFre;
    port_write(controller, port, kPxCmd, command);
    if (!wait_port_register(controller, port, kPxCmd, kPxCmdFr, kPxCmdFr,
                            kPortTimeoutTicks)) return false;
    command = port_read(controller, port, kPxCmd);
    port_write(controller, port, kPxCmd, command | kPxCmdSt);
    return wait_port_register(controller, port, kPxCmd, kPxCmdCr, kPxCmdCr,
                              kPortTimeoutTicks);
}

static bool initialize_port_engine(const Controller& controller,
                                   uint8_t port, uint8_t driverIndex)
{
    if (!stop_port(controller, port)) return false;
    port_write(controller, port, kPxIe, 0u); // polling profile, no port IRQs
    port_write(controller, port, kPxIs, 0xFFFFFFFFu);
    port_write(controller, port, kPxSerr, 0xFFFFFFFFu);
    if (!set_port_memory(controller, port, driverIndex)) return false;
    return start_port(controller, port);
}

#if AHCI_HAS_PCI_PORT_IO
static const uint16_t kPciAddressPort = 0x0CF8u;
static const uint16_t kPciDataPort = 0x0CFCu;

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset)
{
    const uint32_t address = 0x80000000u |
        (static_cast<uint32_t>(bus) << 16) |
        (static_cast<uint32_t>(device) << 11) |
        (static_cast<uint32_t>(function) << 8) | (offset & 0xFCu);
    arch::outl(kPciAddressPort, address);
    return arch::inl(kPciDataPort);
}

static void pci_write_command(uint8_t bus, uint8_t device, uint8_t function,
                              uint16_t command)
{
    const uint32_t address = 0x80000000u |
        (static_cast<uint32_t>(bus) << 16) |
        (static_cast<uint32_t>(device) << 11) |
        (static_cast<uint32_t>(function) << 8) | 0x04u;
    arch::outl(kPciAddressPort, address);
    // The status half is W1C: write zeros so unrelated PCI status is not
    // accidentally acknowledged while enabling Memory Space and Bus Master.
    arch::outl(kPciDataPort, command);
}
#endif

static bool wait_bios_handoff(Controller& controller)
{
    controller.capabilities2 = hba_read(controller, kAhciCap2);
    if ((controller.capabilities2 & kCapBoh) == 0u) return true;
    hba_write(controller, kAhciBohc,
        hba_read(controller, kAhciBohc) | kBohOsOwned);
    const uint64_t startTick = pit::ticks();
    bool clockAdvanced = false;
    uint64_t previousTick = startTick;
    for (uint32_t i = 0; i < kFallbackPollLimit; ++i) {
        const uint32_t ownership = hba_read(controller, kAhciBohc);
        if ((ownership & (kBohBiosOwned | kBohBiosBusy)) == 0u) return true;
        const uint64_t now = pit::ticks();
        if (now != previousTick) clockAdvanced = true;
        previousTick = now;
        if (clockAdvanced && now - startTick >= kControllerTimeoutTicks)
            return false;
        pause_cpu();
    }
    return false;
}

static bool initialize_controller(Controller& controller)
{
    if (!wait_bios_handoff(controller)) return false;
    uint32_t ghc = hba_read(controller, kAhciGhC);
    hba_write(controller, kAhciGhC, (ghc | kGhCAhciEnable) & ~kGhCInterruptEnable);
    hba_write(controller, kAhciGhC,
        hba_read(controller, kAhciGhC) | kGhCReset | kGhCAhciEnable);
    if (!wait_register(controller, kAhciGhC, kGhCReset, 0u,
                       kControllerTimeoutTicks)) return false;
    ghc = hba_read(controller, kAhciGhC);
    hba_write(controller, kAhciGhC,
              (ghc | kGhCAhciEnable) & ~kGhCInterruptEnable);
    hba_write(controller, kAhciGlobalIs, 0xFFFFFFFFu);
    controller.capabilities = hba_read(controller, 0x00u);
    controller.capabilities2 = hba_read(controller, kAhciCap2);
    controller.implementedPorts = hba_read(controller, kAhciPi);
    controller.version = hba_read(controller, kAhciVersion);
    controller.slotCount = static_cast<uint8_t>(
        ((controller.capabilities >> 8) & 0x1Fu) + 1u);
    controller.dma64 = (controller.capabilities & kCapS64A) != 0u;
    return (hba_read(controller, kAhciGhC) & kGhCAhciEnable) != 0u &&
           controller.slotCount != 0u;
}

static bool encode_pci_and_map(uint8_t bus, uint8_t device, uint8_t function,
                               uint16_t vendorId, uint16_t deviceId,
                               Controller& controller)
{
#if AHCI_HAS_PCI_PORT_IO
    const uint32_t bar5 = pci_read32(bus, device, function, 0x24u);
    uint64_t abarPhysical = 0;
    if (!logic::parse_abar_bar5(bar5, abarPhysical)) return false;
    const uint32_t commandStatus = pci_read32(bus, device, function, 0x04u);
    const uint16_t command = static_cast<uint16_t>(commandStatus) | 0x0006u;
    pci_write_command(bus, device, function, command);

    uint64_t abarVirtual = 0;
    mmio::MappingReport mapReport = {};
    if (!mmio::mapForDevice(abarPhysical, kAbarMappedBytes, &abarVirtual,
                            &mapReport, kMmioMapFlags)) {
#if defined(GXOS_AHCI_DIAGNOSTICS)
        serial::puts("[AHCI] ABAR map rejected: ");
        serial::puts(mapReport.reason ? mapReport.reason : "unknown");
        serial::putc('\n');
#endif
        return false;
    }
    memzero(&controller, sizeof(controller));
    controller.active = true;
    controller.pciBus = bus;
    controller.pciDevice = device;
    controller.pciFunction = function;
    controller.vendorId = vendorId;
    controller.deviceId = deviceId;
    controller.abarPhysical = abarPhysical;
    controller.abarVirtual = abarVirtual;
    return true;
#else
    (void)bus; (void)device; (void)function; (void)vendorId; (void)deviceId;
    (void)controller;
    return false;
#endif
}

static bool parse_identify(uint8_t driverIndex, const ata::IdentifyData& identify,
                           DeviceInfo& info)
{
    const bool commandSetsValid =
        (identify.commandSets83 & 0xC000u) == 0x4000u;
    const bool lba48 = commandSetsValid &&
        (identify.commandSets83 & (1u << 10)) != 0u;
    uint64_t totalSectors = lba48 ? identify.lba48Sectors
                                  : identify.lba28Sectors;
    if (totalSectors == 0u || (lba48 && totalSectors > (1ULL << 48)) ||
        (!lba48 && totalSectors > (1ULL << 28))) return false;

    uint32_t sectorSize = 512u;
    const bool sectorInfoValid =
        (identify.logicalSectorInfo & 0xC000u) == 0x4000u;
    if (sectorInfoValid &&
        (identify.logicalSectorInfo & 0x1000u) != 0u) {
        const uint64_t logicalBytes =
            static_cast<uint64_t>(identify.logicalSectorWords) * 2u;
        if (logicalBytes < 512u || logicalBytes > 4096u ||
            (logicalBytes & (logicalBytes - 1u)) != 0u) return false;
        sectorSize = static_cast<uint32_t>(logicalBytes);
    }
    uint32_t physicalSectorSize = 0;
    if (sectorInfoValid) {
        physicalSectorSize = sectorSize;
        if ((identify.logicalSectorInfo & 0x2000u) != 0u) {
            const uint8_t exponent = static_cast<uint8_t>(
                identify.logicalSectorInfo & 0x000Fu);
            if (exponent >= 32u ||
                sectorSize > (UINT32_MAX >> exponent)) return false;
            physicalSectorSize = sectorSize << exponent;
        }
    }

    memzero(&info, sizeof(info));
    info.active = true;
    info.driverIndex = driverIndex;
    info.lba48 = lba48;
    info.totalSectors = totalSectors;
    info.sectorSize = sectorSize;
    info.physicalSectorSize = physicalSectorSize;
    info.flushCache = commandSetsValid &&
        (identify.commandSets83 & (1u << 12)) != 0u;
    info.flushCacheExt = commandSetsValid &&
        (identify.commandSets83 & (1u << 13)) != 0u;
    memcopy(info.model, identify.model, sizeof(identify.model));
    fix_ata_string(info.model, 40u);
    memcopy(info.serial, identify.serial, sizeof(identify.serial));
    fix_ata_string(info.serial, 20u);
    memcopy(info.firmware, identify.firmware, sizeof(identify.firmware));
    fix_ata_string(info.firmware, 8u);
    return true;
}

static bool identify_device(uint8_t driverIndex, bool allowLba28,
                            const Controller& controller, uint8_t port,
                            DeviceInfo& info, bool maySetDiagnostic);

static void set_io_diagnostic(uint8_t driverIndex, block::Status status,
                              uint8_t stage, uint8_t command, uint8_t slot,
                              uint64_t lba, uint32_t count,
                              uint32_t taskFileData, uint32_t portIs,
                              uint32_t sataError, uint32_t completedSectors,
                              uint32_t transferredBytes)
{
    if (driverIndex >= kMaxDevices) return;
    IoDiagnostic& diagnostic = s_devices[driverIndex].diagnostic;
    diagnostic.valid = true;
    diagnostic.status = status;
    diagnostic.stage = stage;
    diagnostic.command = command;
    diagnostic.slot = slot;
    diagnostic.lba = lba;
    diagnostic.count = count;
    diagnostic.taskFileData = taskFileData;
    diagnostic.portInterruptStatus = portIs;
    diagnostic.sataError = sataError;
    diagnostic.completedSectors = completedSectors;
    diagnostic.transferredBytes = transferredBytes;
}

static bool get_io_diagnostic(uint8_t driverIndex,
                              block::TransportIoDiagnostic& out)
{
    memzero(&out, sizeof(out));
    if (driverIndex >= s_deviceCount || !s_devices[driverIndex].info.active)
        return false;
    const IoDiagnostic& source = s_devices[driverIndex].diagnostic;
    if (!source.valid || source.status == block::BLOCK_OK) return false;
    out.valid = true;
    out.stage = source.stage;
    out.commandOpcode = source.command;
    out.commandSlot = source.slot;
    out.statusRegister = static_cast<uint8_t>(source.taskFileData);
    out.errorRegister = static_cast<uint8_t>(source.taskFileData >> 8);
    out.errorRegisterValid = true;
    out.transportStatus = source.portInterruptStatus;
    out.transportError = source.sataError;
    out.failingLba = source.lba;
    out.completedSectors = source.completedSectors;
    out.dataSectorsTransferred = source.transferredBytes /
        (s_devices[driverIndex].info.sectorSize
            ? s_devices[driverIndex].info.sectorSize : 512u);
    return true;
}

static bool command_data_addresses(uint8_t driverIndex,
                                   const Controller& controller,
                                   uint64_t& commandTablePhysical,
                                   uint64_t& dmaBufferPhysical)
{
    commandTablePhysical = virtual_to_physical(s_commandTables[driverIndex]);
    dmaBufferPhysical = virtual_to_physical(s_dmaBuffers[driverIndex]);
    if ((commandTablePhysical & 0x7Fu) != 0u ||
        (dmaBufferPhysical & 0xFFFu) != 0u ||
        !dma_range_valid(commandTablePhysical,
                         sizeof(s_commandTables[driverIndex]), controller.dma64) ||
        !dma_range_valid(dmaBufferPhysical,
                         sizeof(s_dmaBuffers[driverIndex]), controller.dma64))
        return false;
    return true;
}

static block::Status issue_command(uint8_t driverIndex, const logic::Fis& fis,
                                   uint32_t expectedBytes, bool isWrite,
                                   bool isFlush, uint64_t lba, uint32_t count,
                                   uint32_t timeoutTicks,
                                   uint32_t& transferredBytes)
{
    transferredBytes = 0;
    if (driverIndex >= s_deviceCount || !s_devices[driverIndex].online)
        return block::BLOCK_ERR_NO_MEDIA;
    PortDevice& device = s_devices[driverIndex];
    Controller& controller = s_controllers[device.controllerIndex];
    const uint8_t slot = 0u;
    uint32_t commandBit = 0;
    if (!logic::slot_bit(slot, controller.slotCount, commandBit) ||
        !logic::slot_is_free(port_read(controller, device.ahciPort, kPxCi),
            port_read(controller, device.ahciPort, kPxSact),
            controller.slotCount, slot)) return block::BLOCK_ERR_NOT_READY;
    if ((hba_read(controller, kAhciGhC) & kGhCAhciEnable) == 0u ||
        (port_read(controller, device.ahciPort, kPxCmd) & kPxCmdSt) == 0u)
        return block::BLOCK_ERR_NOT_READY;

    uint64_t commandTablePhysical = 0, dmaBufferPhysical = 0;
    if (!command_data_addresses(driverIndex, controller,
                                commandTablePhysical, dmaBufferPhysical))
        return block::BLOCK_ERR_INVALID;
    memzero(s_commandTables[driverIndex], sizeof(s_commandTables[driverIndex]));
    CommandHeader* headers = reinterpret_cast<CommandHeader*>(
        s_commandList[driverIndex]);
    CommandHeader& header = headers[slot];
    memzero(&header, sizeof(header));
    CommandTablePrefix* table = reinterpret_cast<CommandTablePrefix*>(
        s_commandTables[driverIndex]);
    memcopy(table->commandFis, fis.bytes, sizeof(fis.bytes));

    uint16_t prdtLength = 0;
    if (expectedBytes != 0u) {
        if (expectedBytes > kMaxTransferBytes ||
            !logic::build_prdt(dmaBufferPhysical, expectedBytes,
                               controller.dma64, table->prdt))
            return block::BLOCK_ERR_INVALID;
        prdtLength = 1u;
    }
    header.flags = logic::command_header_flags(isWrite);
    header.prdtLength = prdtLength;
    header.tableAddressLow = static_cast<uint32_t>(commandTablePhysical);
    header.tableAddressHigh = static_cast<uint32_t>(commandTablePhysical >> 32);

    // Descriptor/bounce-buffer writes must be visible before PxCI is raised.
    memory_barrier();
    port_write(controller, device.ahciPort, kPxIs, 0xFFFFFFFFu);
    port_write(controller, device.ahciPort, kPxSerr, 0xFFFFFFFFu);
    port_write(controller, device.ahciPort, kPxCi, commandBit);

#if defined(GXOS_AHCI_DIAGNOSTICS)
    serial::puts("[AHCI-DMA] command-table va=");
    serial::put_hex64(reinterpret_cast<uintptr_t>(s_commandTables[driverIndex]));
    serial::puts(" pa="); serial::put_hex64(commandTablePhysical);
    serial::puts(" CTBA="); serial::put_hex32(header.tableAddressLow);
    serial::puts(" CTBAU="); serial::put_hex32(header.tableAddressHigh);
    if (expectedBytes != 0u) {
        serial::puts(" prdt-buffer va=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(s_dmaBuffers[driverIndex]));
        serial::puts(" pa="); serial::put_hex64(dmaBufferPhysical);
        serial::puts(" DBA="); serial::put_hex32(table->prdt.addressLow);
        serial::puts(" DBAU="); serial::put_hex32(table->prdt.addressHigh);
        serial::puts(" DBC/IOC="); serial::put_hex32(table->prdt.byteCountAndInterrupt);
    }
    serial::puts(" command="); serial::put_hex8(fis.bytes[2]);
    serial::puts(" CI="); serial::put_hex32(port_read(controller, device.ahciPort, kPxCi));
    serial::putc('\n');
#endif

    bool issued = true;
    bool completed = false;
    uint32_t taskFileData = 0;
    uint32_t portIs = 0;
    uint32_t sataError = 0;
    const uint64_t startTick = pit::ticks();
    bool clockAdvanced = false;
    uint64_t previousTick = startTick;
    uint32_t iterations = 0;
    for (; iterations < kFallbackPollLimit; ++iterations) {
        taskFileData = port_read(controller, device.ahciPort, kPxTfd);
        portIs = port_read(controller, device.ahciPort, kPxIs);
        sataError = port_read(controller, device.ahciPort, kPxSerr);
        const bool slotBusy = (port_read(controller, device.ahciPort, kPxCi) &
                               commandBit) != 0u;
        if ((portIs & kPxIsFatal) != 0u ||
            logic::has_fatal_port_error(portIs, sataError, taskFileData))
            break;
        if (!slotBusy && (static_cast<uint8_t>(taskFileData) &
                          (0x80u | 0x08u)) == 0u) {
            completed = true;
            break;
        }
        const uint64_t now = pit::ticks();
        if (now != previousTick) clockAdvanced = true;
        previousTick = now;
        if (clockAdvanced && now - startTick >= timeoutTicks) break;
        pause_cpu();
    }
    if (iterations >= kFallbackPollLimit) completed = false;
    memory_barrier(); // acquire device DMA results before reading PRDBC.
    transferredBytes = header.transferredBytes;
    taskFileData = port_read(controller, device.ahciPort, kPxTfd);
    portIs |= port_read(controller, device.ahciPort, kPxIs);
    sataError |= port_read(controller, device.ahciPort, kPxSerr);
    const bool slotStillBusy =
        (port_read(controller, device.ahciPort, kPxCi) & commandBit) != 0u;
    const block::Status result = logic::command_result(issued, !completed || slotStillBusy,
        portIs, sataError, taskFileData, transferredBytes, expectedBytes,
        isWrite, isFlush);
    const uint32_t sectorsCompleted = result == block::BLOCK_OK ? count : 0u;
    set_io_diagnostic(driverIndex, result, result == block::BLOCK_OK ? 0u : 1u,
        fis.bytes[2], slot, lba, count, taskFileData, portIs, sataError,
        sectorsCompleted, transferredBytes);
    port_write(controller, device.ahciPort, kPxIs, portIs);
    if (sataError != 0u) port_write(controller, device.ahciPort, kPxSerr, sataError);
    return result;
}

static bool identify_device(uint8_t driverIndex, bool allowLba28,
                            const Controller& controller, uint8_t port,
                            DeviceInfo& info, bool maySetDiagnostic)
{
    if (driverIndex >= kMaxDevices || !controller.active) return false;
    const uint64_t commandTablePhysical = virtual_to_physical(
        s_commandTables[driverIndex]);
    const uint64_t bufferPhysical = virtual_to_physical(s_dmaBuffers[driverIndex]);
    if ((commandTablePhysical & 0x7Fu) != 0u || (bufferPhysical & 0xFFFu) != 0u ||
        !dma_range_valid(commandTablePhysical, sizeof(s_commandTables[driverIndex]),
                         controller.dma64) ||
        !dma_range_valid(bufferPhysical, 512u, controller.dma64)) return false;

    memzero(s_commandList[driverIndex], sizeof(s_commandList[driverIndex]));
    memzero(s_commandTables[driverIndex], sizeof(s_commandTables[driverIndex]));
    memzero(s_dmaBuffers[driverIndex], 512u);
    CommandHeader& header = reinterpret_cast<CommandHeader*>(
        s_commandList[driverIndex])[0];
    CommandTablePrefix* table = reinterpret_cast<CommandTablePrefix*>(
        s_commandTables[driverIndex]);
    logic::Fis fis = {};
    (void)logic::build_identify_fis(fis);
    memcopy(table->commandFis, fis.bytes, sizeof(fis.bytes));
    if (!logic::build_prdt(bufferPhysical, 512u, controller.dma64, table->prdt))
        return false;
    header.flags = 5u;
    header.prdtLength = 1u;
    header.tableAddressLow = static_cast<uint32_t>(commandTablePhysical);
    header.tableAddressHigh = static_cast<uint32_t>(commandTablePhysical >> 32);
    memory_barrier();
    port_write(controller, port, kPxIs, 0xFFFFFFFFu);
    port_write(controller, port, kPxSerr, 0xFFFFFFFFu);
    if (!logic::slot_is_free(port_read(controller, port, kPxCi),
            port_read(controller, port, kPxSact), controller.slotCount, 0u))
        return false;
    port_write(controller, port, kPxCi, 1u);

    bool completed = false;
    uint32_t taskFileData = 0, portIs = 0, sataError = 0;
    const uint64_t startTick = pit::ticks();
    bool clockAdvanced = false;
    uint64_t previousTick = startTick;
    uint32_t iterations = 0;
    for (; iterations < kFallbackPollLimit; ++iterations) {
        taskFileData = port_read(controller, port, kPxTfd);
        portIs = port_read(controller, port, kPxIs);
        sataError = port_read(controller, port, kPxSerr);
        const bool slotBusy = (port_read(controller, port, kPxCi) & 1u) != 0u;
        if ((portIs & kPxIsFatal) != 0u ||
            logic::has_fatal_port_error(portIs, sataError, taskFileData)) break;
        if (!slotBusy && (static_cast<uint8_t>(taskFileData) &
                          (0x80u | 0x08u)) == 0u) {
            completed = true;
            break;
        }
        const uint64_t now = pit::ticks();
        if (now != previousTick) clockAdvanced = true;
        previousTick = now;
        if (clockAdvanced && now - startTick >= kCommandTimeoutTicks) break;
        pause_cpu();
    }
    memory_barrier();
    const uint32_t transferred = header.transferredBytes;
    taskFileData = port_read(controller, port, kPxTfd);
    portIs |= port_read(controller, port, kPxIs);
    sataError |= port_read(controller, port, kPxSerr);
    const bool good = completed &&
        (port_read(controller, port, kPxCi) & 1u) == 0u &&
        !logic::has_fatal_port_error(portIs, sataError, taskFileData) &&
        transferred == 512u;
    port_write(controller, port, kPxIs, portIs);
    if (sataError != 0u) port_write(controller, port, kPxSerr, sataError);
    if (!good) {
        if (maySetDiagnostic && driverIndex < s_deviceCount)
            set_io_diagnostic(driverIndex, block::BLOCK_ERR_IO, 2u, 0xECu, 0u,
                0u, 1u, taskFileData, portIs, sataError, 0u, transferred);
        return false;
    }
    memcopy(s_identifyCopies[driverIndex], s_dmaBuffers[driverIndex], 512u);
    const ata::IdentifyData& identify = *reinterpret_cast<const ata::IdentifyData*>(
        s_identifyCopies[driverIndex]);
    if (!allowLba28 && (identify.commandSets83 & 0xC000u) != 0x4000u)
        return false;
    return parse_identify(driverIndex, identify, info);
}

static bool identity_matches(uint8_t driverIndex, const DeviceInfo& expected)
{
    Controller& controller = s_controllers[s_devices[driverIndex].controllerIndex];
    DeviceInfo current = {};
    if (!identify_device(driverIndex, true, controller,
            s_devices[driverIndex].ahciPort, current, false)) return false;
    return current.totalSectors == expected.totalSectors &&
        current.sectorSize == expected.sectorSize &&
        current.lba48 == expected.lba48 &&
        current.flushCache == expected.flushCache &&
        current.flushCacheExt == expected.flushCacheExt &&
        text_equal(current.model, expected.model) &&
        text_equal(current.serial, expected.serial);
}

static void recover_port(uint8_t driverIndex)
{
    if (driverIndex >= s_deviceCount) return;
    PortDevice& device = s_devices[driverIndex];
    Controller& controller = s_controllers[device.controllerIndex];
    bool recovered = stop_port(controller, device.ahciPort);
    port_write(controller, device.ahciPort, kPxIs, 0xFFFFFFFFu);
    port_write(controller, device.ahciPort, kPxSerr, 0xFFFFFFFFu);
    recovered = recovered && set_port_memory(controller, device.ahciPort, driverIndex);
    recovered = recovered && start_port(controller, device.ahciPort);
    recovered = recovered && identity_matches(driverIndex, device.info);
    if (!recovered) {
        device.online = false;
        if (device.globalIndex < block::MAX_BLOCK_DEVICES &&
            device.registrationId != 0u)
            (void)block::mark_device_offline(device.globalIndex,
                                             device.registrationId);
    }
}

static bool link_is_online(const PortDevice& device)
{
    const Controller& controller = s_controllers[device.controllerIndex];
    const uint32_t status = port_read(controller, device.ahciPort, kPxSsts);
    return (status & 0x0Fu) == 3u && ((status >> 8) & 0x0Fu) == 1u &&
        port_read(controller, device.ahciPort, kPxSig) == 0x00000101u;
}

static block::Status transfer(uint8_t driverIndex, uint64_t lba,
                              uint32_t count, void* readBuffer,
                              const void* writeBuffer, bool write)
{
    if (driverIndex >= s_deviceCount || !s_devices[driverIndex].online)
        return block::BLOCK_ERR_NO_MEDIA;
    PortDevice& device = s_devices[driverIndex];
    if (count == 0u || lba >= device.info.totalSectors ||
        static_cast<uint64_t>(count) > device.info.totalSectors - lba ||
        (!write && readBuffer == nullptr) || (write && writeBuffer == nullptr))
        return block::BLOCK_ERR_INVALID;
    const uint64_t byteCount64 = static_cast<uint64_t>(count) *
        device.info.sectorSize;
    if (byteCount64 == 0u || byteCount64 > kMaxTransferBytes ||
        byteCount64 > 0xFFFFFFFFu) return block::BLOCK_ERR_INVALID;
    if (!link_is_online(device)) return block::BLOCK_ERR_NO_MEDIA;
    if (!acquire_io_lock()) return block::BLOCK_ERR_NOT_READY;

    // Recheck link/slot state under the transport lock so concurrent callers
    // cannot build descriptors for a command that is already in flight.
    if (!link_is_online(device)) {
        release_io_lock();
        return block::BLOCK_ERR_NO_MEDIA;
    }
    const uint32_t byteCount = static_cast<uint32_t>(byteCount64);
    if (write) memcopy(s_dmaBuffers[driverIndex], writeBuffer, byteCount);
    logic::Fis fis = {};
    if (!logic::build_data_fis(fis, lba, count, device.info.totalSectors,
                               device.info.lba48, write)) {
        release_io_lock();
        return block::BLOCK_ERR_INVALID;
    }
    uint32_t transferredBytes = 0;
    const block::Status status = issue_command(driverIndex, fis, byteCount,
        write, false, lba, count, kCommandTimeoutTicks, transferredBytes);
    if (status == block::BLOCK_OK) {
        if (write) {
            set_io_diagnostic(driverIndex, status, 0u, fis.bytes[2], 0u,
                lba, count, 0u, 0u, 0u, count, transferredBytes);
        } else {
            memcopy(readBuffer, s_dmaBuffers[driverIndex], byteCount);
        }
    } else {
        recover_port(driverIndex); // Never replay the submitted operation.
    }
    release_io_lock();
    return status;
}

static block::Status read_sectors(uint8_t driverIndex, uint64_t lba,
                                  uint32_t count, void* buffer)
{
    return transfer(driverIndex, lba, count, buffer, nullptr, false);
}

static block::Status write_sectors(uint8_t driverIndex, uint64_t lba,
                                   uint32_t count, const void* buffer)
{
    return transfer(driverIndex, lba, count, nullptr, buffer, true);
}

static block::Status flush_device(uint8_t driverIndex)
{
    if (driverIndex >= s_deviceCount || !s_devices[driverIndex].online)
        return block::BLOCK_ERR_NO_MEDIA;
    PortDevice& device = s_devices[driverIndex];
    if (!device.info.flushCache && !device.info.flushCacheExt)
        return block::BLOCK_ERR_UNSUPPORTED;
    if (!link_is_online(device)) return block::BLOCK_ERR_NO_MEDIA;
    if (!acquire_io_lock()) return block::BLOCK_ERR_DURABILITY_UNVERIFIED;
    if (!link_is_online(device)) {
        release_io_lock();
        return block::BLOCK_ERR_NO_MEDIA;
    }
    logic::Fis fis = {};
    if (!logic::build_flush_fis(fis, device.info.flushCache,
                                device.info.flushCacheExt)) {
        release_io_lock();
        return block::BLOCK_ERR_UNSUPPORTED;
    }
    uint32_t transferredBytes = 0;
    const block::Status status = issue_command(driverIndex, fis, 0u, false,
        true, 0u, 0u, kFlushTimeoutTicks, transferredBytes);
    if (status != block::BLOCK_OK) recover_port(driverIndex);
    release_io_lock();
    return status;
}

static void log_port_state(const Controller& controller, uint8_t port,
                           logic::PortState state, uint32_t ssts,
                           uint32_t signature)
{
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[AHCI] port="); serial::put_hex8(port);
    serial::puts(" state="); serial::puts(port_state_name(state));
    serial::puts(" SSTS="); serial::put_hex32(ssts);
    serial::puts(" SIG="); serial::put_hex32(signature);
    if (state == logic::PORT_LINK_ERROR) {
        serial::puts(" SERR="); serial::put_hex32(port_read(controller, port, kPxSerr));
    }
    serial::putc('\n');
#else
    (void)controller; (void)port; (void)state; (void)ssts; (void)signature;
#endif
}

static void register_port_device(uint8_t controllerIndex, uint8_t port)
{
    if (s_deviceCount >= kMaxDevices) return;
    Controller& controller = s_controllers[controllerIndex];
    const uint8_t driverIndex = s_deviceCount;
    uint32_t ssts = port_read(controller, port, kPxSsts);
    uint32_t signature = port_read(controller, port, kPxSig);
    logic::PortState state = logic::classify_port(
        (controller.implementedPorts & (1u << port)) != 0u, ssts, signature);
    bool engineStarted = false;

    // Some firmware/controller combinations publish PxSIG only after the
    // FIS receive engine has been configured and started. Start a bounded
    // probe engine for an active link with no signature, then classify the
    // signature again before treating the port as a disk.
    if (state == logic::PORT_UNSUPPORTED_SIGNATURE &&
        (ssts & 0x0Fu) == 3u && ((ssts >> 8) & 0x0Fu) == 1u) {
        if (!initialize_port_engine(controller, port, driverIndex)) {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[AHCI] active-link port probe init failed port=");
            serial::put_hex8(port); serial::putc('\n');
#endif
            return;
        }
        engineStarted = true;
        ssts = port_read(controller, port, kPxSsts);
        signature = port_read(controller, port, kPxSig);
        state = logic::classify_port(true, ssts, signature);
    }
    log_port_state(controller, port, state, ssts, signature);
    if (state != logic::PORT_SATA_DISK) {
        if (engineStarted) (void)stop_port(controller, port);
        return;
    }
    if (!engineStarted && !initialize_port_engine(controller, port, driverIndex)) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[AHCI] port engine init failed port=");
        serial::put_hex8(port); serial::putc('\n');
#endif
        return;
    }

    DeviceInfo info = {};
    if (!identify_device(driverIndex, true, controller, port, info, false)) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[AHCI] IDENTIFY failed port=");
        serial::put_hex8(port); serial::putc('\n');
#endif
        (void)stop_port(controller, port);
        return;
    }
    info.port = port;
    info.pciBus = controller.pciBus;
    info.pciDevice = controller.pciDevice;
    info.pciFunction = controller.pciFunction;
    info.vendorId = controller.vendorId;
    info.deviceId = controller.deviceId;
    info.abarPhysical = controller.abarPhysical;
    info.capabilities = controller.capabilities;
    info.version = controller.version;
    PortDevice& device = s_devices[driverIndex];
    memzero(&device, sizeof(device));
    device.info = info;
    device.controllerIndex = controllerIndex;
    device.ahciPort = port;
    device.globalIndex = 0xFFu;
    device.online = true;

    block::BlockDevice blockDevice = {};
    blockDevice.active = true;
    blockDevice.type = block::BDEV_AHCI;
    blockDevice.bootProvenance = block::BOOT_PROVENANCE_UNKNOWN;
    blockDevice.driverIndex = driverIndex;
    blockDevice.totalSectors = info.totalSectors;
    blockDevice.sectorSize = info.sectorSize;
    blockDevice.physicalSectorSize = info.physicalSectorSize;
    blockDevice.readFn = read_sectors;
#if defined(GXOS_DM15_QEMU_AHCI_PROOF) && \
    defined(GXOS_DM15_AHCI_PRIVATE_PROOF)
    // First-stage QEMU proof reaches this same write implementation through
    // the private wrapper while the shared registry advertises read-only.
    blockDevice.writeFn = nullptr;
#else
    blockDevice.writeFn = write_sectors;
#endif
    blockDevice.flushFn = (info.flushCache || info.flushCacheExt)
        ? flush_device : nullptr;
    blockDevice.flushSemanticsKnown = blockDevice.flushFn != nullptr;
    blockDevice.removableKnown = true;
    blockDevice.removable = false;
    blockDevice.pciLocationValid = true;
    blockDevice.pciSegment = 0; // PCI mechanism 1 exposes segment zero only.
    blockDevice.pciBus = info.pciBus;
    blockDevice.pciDevice = info.pciDevice;
    blockDevice.pciFunction = info.pciFunction;
    blockDevice.ahciPortValid = true;
    blockDevice.ahciPort = port;
    blockDevice.maxTransferBytes = kMaxTransferBytes;
    blockDevice.getIoDiagnosticFn = get_io_diagnostic;
    const size_t modelLength = 39u;
    const size_t serialLength = 23u;
    memcopy(blockDevice.model, info.model,
        modelLength < sizeof(info.model) ? modelLength : sizeof(info.model));
    memcopy(blockDevice.serial, info.serial,
        serialLength < sizeof(info.serial) ? serialLength : sizeof(info.serial));
    blockDevice.name[0] = 'a'; blockDevice.name[1] = 'h';
    blockDevice.name[2] = 'c'; blockDevice.name[3] = 'i';
    blockDevice.name[4] = static_cast<char>('0' + controllerIndex);
    blockDevice.name[5] = 'p';
    blockDevice.name[6] = static_cast<char>('0' + (port / 10u));
    blockDevice.name[7] = static_cast<char>('0' + (port % 10u));
    blockDevice.name[8] = '\0';

    const uint8_t globalIndex = block::register_device(blockDevice);
    if (globalIndex == 0xFFu) {
        memzero(&device, sizeof(device));
        (void)stop_port(controller, port);
        return;
    }
    device.globalIndex = globalIndex;
    block::BlockDevice registered = {};
    if (block::copy_device(globalIndex, registered))
        device.registrationId = registered.registrationId;
    ++s_deviceCount;

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[AHCI] registered "); serial::puts(blockDevice.name);
    serial::puts(" BDF="); serial::put_hex8(info.pciBus); serial::putc(':');
    serial::put_hex8(info.pciDevice); serial::putc('.');
    serial::put_hex8(info.pciFunction);
    serial::puts(" port="); serial::put_hex8(port);
    serial::puts(" model="); serial::puts(info.model);
    serial::puts(" serial="); serial::puts(info.serial);
    serial::puts(" firmware="); serial::puts(info.firmware);
    serial::puts(" sectors="); serial::put_hex64(info.totalSectors);
    serial::puts(" sectorSize="); serial::put_hex32(info.sectorSize);
    serial::puts(" physicalSectorSize=");
    if (info.physicalSectorSize != 0)
        serial::put_hex32(info.physicalSectorSize);
    else
        serial::puts("unknown");
    serial::puts(" LBA48="); serial::puts(info.lba48 ? "yes" : "no");
    serial::puts(" FLUSH="); serial::puts(info.flushCache ? "yes" : "no");
    serial::puts(" FLUSH_EXT="); serial::puts(info.flushCacheExt ? "yes" : "no");
    serial::putc('\n');
#endif
}

static void scan_pci_ahci()
{
#if AHCI_HAS_PCI_PORT_IO
    for (uint16_t bus = 0; bus < 256u; ++bus) {
        for (uint8_t pciDevice = 0; pciDevice < 32u; ++pciDevice) {
            for (uint8_t function = 0; function < 8u; ++function) {
                if (s_controllerCount >= kMaxControllers ||
                    s_deviceCount >= kMaxDevices) return;
                const uint32_t id = pci_read32(static_cast<uint8_t>(bus),
                    pciDevice, function, 0x00u);
                if (id == 0xFFFFFFFFu) continue;
                const uint32_t classReg = pci_read32(static_cast<uint8_t>(bus),
                    pciDevice, function, 0x08u);
                const uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                const uint8_t subClass = static_cast<uint8_t>(classReg >> 16);
                const uint8_t progIf = static_cast<uint8_t>(classReg >> 8);
                if (!logic::is_ahci_pci(baseClass, subClass, progIf)) continue;

                Controller& controller = s_controllers[s_controllerCount];
                const uint16_t vendorId = static_cast<uint16_t>(id);
                const uint16_t deviceId = static_cast<uint16_t>(id >> 16);
                if (!encode_pci_and_map(static_cast<uint8_t>(bus), pciDevice,
                        function, vendorId, deviceId, controller)) {
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[AHCI] invalid BAR5 or uncached mapping at ");
                    serial::put_hex8(static_cast<uint8_t>(bus)); serial::putc(':');
                    serial::put_hex8(pciDevice); serial::putc('.');
                    serial::put_hex8(function); serial::putc('\n');
#endif
                    continue;
                }
                if (!initialize_controller(controller)) {
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[AHCI] controller init failed BDF=");
                    serial::put_hex8(static_cast<uint8_t>(bus)); serial::putc(':');
                    serial::put_hex8(pciDevice); serial::putc('.');
                    serial::put_hex8(function); serial::putc('\n');
#endif
                    controller.active = false;
                    continue;
                }
                const uint8_t controllerIndex = s_controllerCount++;
#if defined(__GNUC__) || defined(__clang__)
                serial::puts("[AHCI] controller BDF=");
                serial::put_hex8(controller.pciBus); serial::putc(':');
                serial::put_hex8(controller.pciDevice); serial::putc('.');
                serial::put_hex8(controller.pciFunction);
                serial::puts(" vendor/device="); serial::put_hex16(vendorId);
                serial::putc(':'); serial::put_hex16(deviceId);
                serial::puts(" ABAR="); serial::put_hex64(controller.abarPhysical);
                serial::puts(" CAP="); serial::put_hex32(controller.capabilities);
                serial::puts(" CAP2="); serial::put_hex32(controller.capabilities2);
                serial::puts(" PI="); serial::put_hex32(controller.implementedPorts);
                serial::puts(" VS="); serial::put_hex32(controller.version);
                serial::puts(" slots="); serial::put_hex8(controller.slotCount);
                serial::puts(" DMA64="); serial::puts(controller.dma64 ? "yes" : "no");
                serial::puts(" interrupts=polling NCQ=unsupported\n");
#endif
                for (uint8_t port = 0; port < 32u; ++port) {
                    if ((controller.implementedPorts & (1u << port)) == 0u) continue;
                    register_port_device(controllerIndex, port);
                }
            }
        }
    }
#endif
}

} // namespace

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0u) s_kernelPhysicalBase = physicalBase;
}

void init()
{
    memzero(s_controllers, sizeof(s_controllers));
    memzero(s_devices, sizeof(s_devices));
    memzero(s_commandList, sizeof(s_commandList));
    memzero(s_receivedFis, sizeof(s_receivedFis));
    memzero(s_commandTables, sizeof(s_commandTables));
    memzero(s_dmaBuffers, sizeof(s_dmaBuffers));
    memzero(s_identifyCopies, sizeof(s_identifyCopies));
    s_controllerCount = 0;
    s_deviceCount = 0;
    s_ioLock = 0;
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[AHCI] PCI scan class=01 subclass=06 progIF=01\n");
#endif
    scan_pci_ahci();
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[AHCI] init complete controllers=");
    serial::put_hex8(s_controllerCount);
    serial::puts(" SATA disks="); serial::put_hex8(s_deviceCount);
    serial::putc('\n');
#endif
}

uint8_t device_count() { return s_deviceCount; }

const DeviceInfo* get_device(uint8_t index)
{
    return index < s_deviceCount && s_devices[index].info.active
        ? &s_devices[index].info : nullptr;
}

#if defined(GXOS_DM15_QEMU_AHCI_PROOF)
block::Status proof_write(uint8_t driverIndex, uint64_t lba,
                          uint32_t count, const void* buffer)
{
    return write_sectors(driverIndex, lba, count, buffer);
}

block::Status proof_flush(uint8_t driverIndex)
{
    return flush_device(driverIndex);
}
#endif

} // namespace ahci
} // namespace kernel
