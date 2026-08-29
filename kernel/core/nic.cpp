// NIC Driver - Implementation
//
// Scans PCI for network controllers (class 02/00), initialises the
// first supported Intel E1000 found, sets up RX/TX descriptor rings,
// and provides raw Ethernet frame send/receive.
//
// Uses MMIO-mapped BAR0 registers.  On x86/amd64 the PCI config
// space is accessed via port-I/O (0xCF8/0xCFC).  On architectures
// without PCI the driver is a no-op stub.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/nic.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/interrupts.h"
#endif

namespace kernel {
namespace nic {

// ================================================================
// Internal helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

// ================================================================
// Internal state
// ================================================================

static NICDevice   s_device;
static bool        s_initialised = false;
static uint64_t    s_kernelPhysicalBase = 0x100000;

// Descriptor rings (statically allocated, 16-byte aligned)
#if defined(__GNUC__) || defined(__clang__)
static RxDescriptor s_rxDescs[NUM_RX_DESC] __attribute__((aligned(16)));
static TxDescriptor s_txDescs[NUM_TX_DESC] __attribute__((aligned(16)));
#else
__declspec(align(16)) static RxDescriptor s_rxDescs[NUM_RX_DESC];
__declspec(align(16)) static TxDescriptor s_txDescs[NUM_TX_DESC];
#endif

// Packet buffers for RX ring (each 2048 bytes)
static uint8_t s_rxBuffers[NUM_RX_DESC][RX_BUFFER_SIZE];

// TX packet buffer (single frame staging area)
static uint8_t s_txBuffer[ETH_FRAME_MAX];

// Current descriptor indices
static uint16_t s_rxCur = 0;
static uint16_t s_txCur = 0;

static bool is_i219_device(uint16_t deviceId)
{
    return deviceId == PCI_DEVICE_I219_LM;
}

static void set_init_failure(InitStage stage, const char* reason)
{
    s_device.initStage = stage;
    s_device.active = false;
    s_device.nicRegistered = false;
    s_device.pollingEnabled = false;
    s_device.irqRegistered = false;

    uint32_t i = 0;
    if (reason != nullptr) {
        while (reason[i] != '\0' && i + 1 < sizeof(s_device.lastInitFailure)) {
            s_device.lastInitFailure[i] = reason[i];
            ++i;
        }
    }
    s_device.lastInitFailure[i] = '\0';

    serial::puts(is_i219_device(s_device.deviceId) ? "[NIC] I219 init failed: "
                                                     : "[NIC] E1000 init failed: ");
    serial::puts(s_device.lastInitFailure);
    serial::putc('\n');
}

static bool dma_address(const void* ptr, uint64_t* physicalOut)
{
    if (ptr == nullptr || physicalOut == nullptr) return false;

    const uint64_t virt = reinterpret_cast<uint64_t>(ptr);
    // The linker places kernel static storage at virtual 0x100000.  The
    // bootloader supplies the physical base for that image; do not silently
    // treat an unrelated virtual address as DMA-capable physical memory.
    if (virt < 0x100000) return false;

    const uint64_t offset = virt - 0x100000;
    if (s_kernelPhysicalBase > (~0ULL - offset)) return false;

    const uint64_t physical = s_kernelPhysicalBase + offset;
    if (physical == 0) return false;

    *physicalOut = physical;
    return true;
}

static inline void dma_memory_barrier()
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" ::: "memory");
#endif
}

// ================================================================
// MMIO register access
//
// In a real kernel these would use volatile MMIO pointers.
// For the MSVC build path (host-side simulation) we provide
// stub implementations.
// ================================================================

static inline void mmio_write32(uint64_t base, uint32_t reg, uint32_t val)
{
    volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + reg));
    *addr = val;
}

static inline uint32_t mmio_read32(uint64_t base, uint32_t reg)
{
    volatile uint32_t* addr = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + reg));
    return *addr;
}

static bool is_supported_nic(uint16_t vendor, uint16_t device)
{
    if (vendor != PCI_VENDOR_INTEL) return false;
    return (device == PCI_DEVICE_E1000 ||
            device == PCI_DEVICE_E1000E ||
            device == PCI_DEVICE_I217 ||
            device == PCI_DEVICE_I219_LM);
}

// ================================================================
// PCI configuration space (port-I/O method - x86/amd64)
// ================================================================

#if ARCH_HAS_PORT_IO

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    return arch::inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    arch::outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t dword = pci_read32(bus, dev, func, offset & 0xFC);
    return static_cast<uint16_t>(dword >> ((offset & 2) * 8));
}

// ================================================================
// Read station address from the device
// ================================================================

static uint16_t eeprom_read(uint64_t mmioBase, uint8_t addr)
{
    uint32_t val = (static_cast<uint32_t>(addr) << E1000_EERD_ADDR_SHIFT) |
                   E1000_EERD_START;
    mmio_write32(mmioBase, E1000_EERD, val);

    // Poll for DONE
    for (uint32_t i = 0; i < 100000; ++i) {
        uint32_t rd = mmio_read32(mmioBase, E1000_EERD);
        if (rd & E1000_EERD_DONE) {
            return static_cast<uint16_t>(rd >> E1000_EERD_DATA_SHIFT);
        }
    }
    return 0;
}

static bool read_rar0_address(uint64_t mmioBase, uint8_t* mac, bool requireValidBit)
{
    if (!mac) return false;

    const uint32_t ral = mmio_read32(mmioBase, E1000_RAL0);
    const uint32_t rah = mmio_read32(mmioBase, E1000_RAH0);
    if (ral == 0xFFFFFFFFu || rah == 0xFFFFFFFFu) return false;
    if (requireValidBit && (rah & E1000_RAH_AV) == 0u) return false;

    mac[0] = static_cast<uint8_t>(ral);
    mac[1] = static_cast<uint8_t>(ral >> 8);
    mac[2] = static_cast<uint8_t>(ral >> 16);
    mac[3] = static_cast<uint8_t>(ral >> 24);
    mac[4] = static_cast<uint8_t>(rah);
    mac[5] = static_cast<uint8_t>(rah >> 8);
    return is_valid_station_mac(mac);
}

static bool read_mac_address(uint64_t mmioBase, uint8_t* mac)
{
    if (!mac) return false;

    // I219 is the PCH LAN controller.  Its station address is loaded from
    // NVM into RAL0/RAH0; use that hardware state directly and do not issue
    // the older discrete-controller EEPROM command sequence.
    if (is_i219_device(s_device.deviceId)) {
        return read_rar0_address(mmioBase, mac, true);
    }

    // Preserve the existing EEPROM-first behavior for the older supported
    // E1000-family devices, but accept only a valid station address.
    const uint16_t w0 = eeprom_read(mmioBase, 0);
    const uint16_t w1 = eeprom_read(mmioBase, 1);
    const uint16_t w2 = eeprom_read(mmioBase, 2);
    mac[0] = static_cast<uint8_t>(w0);
    mac[1] = static_cast<uint8_t>(w0 >> 8);
    mac[2] = static_cast<uint8_t>(w1);
    mac[3] = static_cast<uint8_t>(w1 >> 8);
    mac[4] = static_cast<uint8_t>(w2);
    mac[5] = static_cast<uint8_t>(w2 >> 8);
    if (is_valid_station_mac(mac)) return true;

    return read_rar0_address(mmioBase, mac, false);
}

static bool mdic_read(uint64_t mmioBase, uint8_t phyAddress,
                      uint8_t registerAddress, uint16_t* valueOut)
{
    if (!valueOut) return false;

    const uint32_t command = E1000_MDIC_OP_READ |
                             (static_cast<uint32_t>(phyAddress) << E1000_MDIC_PHY_SHIFT) |
                             (static_cast<uint32_t>(registerAddress) << E1000_MDIC_REG_SHIFT);
    mmio_write32(mmioBase, E1000_MDIC, command);

    for (uint32_t i = 0; i < 100000u; ++i) {
        const uint32_t mdic = mmio_read32(mmioBase, E1000_MDIC);
        s_device.mdicValue = mdic;
        if ((mdic & E1000_MDIC_READY) == 0u) continue;
        if ((mdic & E1000_MDIC_ERROR) != 0u) return false;
        const uint16_t value = static_cast<uint16_t>(mdic & E1000_MDIC_DATA_MASK);
        if (value == 0xFFFFu) return false;
        *valueOut = value;
        return true;
    }

    return false;
}

static bool read_i219_phy_status(uint64_t mmioBase)
{
    uint16_t phyId1 = 0;
    uint16_t phyId2 = 0;
    uint16_t phyStatus = 0;
    if (!mdic_read(mmioBase, I219_PHY_ADDRESS, 2, &phyId1) ||
        !mdic_read(mmioBase, I219_PHY_ADDRESS, 3, &phyId2) ||
        !mdic_read(mmioBase, I219_PHY_ADDRESS, I219_PHY_STATUS_REG, &phyStatus)) {
        s_device.phyAccess = NIC_PHY_FAILED;
        return false;
    }

    if ((phyId1 == 0u && phyId2 == 0u) ||
        (phyId1 == 0xFFFFu && phyId2 == 0xFFFFu)) {
        s_device.phyAccess = NIC_PHY_FAILED;
        return false;
    }

    s_device.phyId1 = phyId1;
    s_device.phyId2 = phyId2;
    s_device.phyStatusValue = phyStatus;
    s_device.phyAddress = I219_PHY_ADDRESS;
    s_device.phyAccess = NIC_PHY_OK;
    s_device.link = (phyStatus & I219_PHY_STATUS_LINK) ? NIC_LINK_UP : NIC_LINK_DOWN;
    if (s_device.link == NIC_LINK_DOWN) {
        s_device.negotiatedSpeed = 0;
        s_device.negotiatedFullDuplex = false;
        return true;
    }
    switch (phyStatus & I219_PHY_STATUS_SPEED_MASK) {
        case (0u << 8): s_device.negotiatedSpeed = 10; break;
        case (1u << 8): s_device.negotiatedSpeed = 100; break;
        case (2u << 8): s_device.negotiatedSpeed = 1000; break;
        default: s_device.negotiatedSpeed = 0; break;
    }
    s_device.negotiatedFullDuplex = (phyStatus & I219_PHY_STATUS_DUPLEX) != 0u;
    return true;
}

// ================================================================
// Initialise RX descriptor ring
// ================================================================

static bool init_rx(uint64_t mmioBase)
{
    // Initialise each RX descriptor to point at its buffer
    for (uint16_t i = 0; i < NUM_RX_DESC; ++i) {
        memzero(&s_rxDescs[i], sizeof(RxDescriptor));
        uint64_t bufferPhys = 0;
        if (!dma_address(&s_rxBuffers[i][0], &bufferPhys)) return false;
        s_rxDescs[i].bufferAddr = bufferPhys;
        s_rxDescs[i].status     = 0;
    }

    // Program the RX descriptor ring base address
    uint64_t rxDescPhys = 0;
    if (!dma_address(&s_rxDescs[0], &rxDescPhys) || (rxDescPhys & 0x0Fu) != 0u) {
        return false;
    }
    mmio_write32(mmioBase, E1000_RDBAL, static_cast<uint32_t>(rxDescPhys & 0xFFFFFFFF));
    mmio_write32(mmioBase, E1000_RDBAH, static_cast<uint32_t>(rxDescPhys >> 32));

    // Descriptor ring length (in bytes)
    mmio_write32(mmioBase, E1000_RDLEN, NUM_RX_DESC * sizeof(RxDescriptor));

    // Head = 0, Tail = NUM_RX_DESC - 1 (all descriptors available)
    mmio_write32(mmioBase, E1000_RDH, 0);
    mmio_write32(mmioBase, E1000_RDT, NUM_RX_DESC - 1);
    s_rxCur = 0;

    // Enable receiver
    uint32_t rctl = E1000_RCTL_EN |
                    E1000_RCTL_BAM |          // accept broadcast
                    E1000_RCTL_BSIZE_2048 |   // 2048-byte buffers
                    E1000_RCTL_SECRC;          // strip CRC
    dma_memory_barrier();
    mmio_write32(mmioBase, E1000_RCTL, rctl);
    s_device.rxRingInitialized = true;
    return true;
}

// ================================================================
// Initialise TX descriptor ring
// ================================================================

static bool init_tx(uint64_t mmioBase)
{
    for (uint16_t i = 0; i < NUM_TX_DESC; ++i) {
        memzero(&s_txDescs[i], sizeof(TxDescriptor));
        s_txDescs[i].status = E1000_TXD_STAT_DD; // mark as done (available)
    }

    // Program the TX descriptor ring base address
    uint64_t txDescPhys = 0;
    if (!dma_address(&s_txDescs[0], &txDescPhys) || (txDescPhys & 0x0Fu) != 0u) {
        return false;
    }
    mmio_write32(mmioBase, E1000_TDBAL, static_cast<uint32_t>(txDescPhys & 0xFFFFFFFF));
    mmio_write32(mmioBase, E1000_TDBAH, static_cast<uint32_t>(txDescPhys >> 32));

    // Descriptor ring length (in bytes)
    mmio_write32(mmioBase, E1000_TDLEN, NUM_TX_DESC * sizeof(TxDescriptor));

    // Head = Tail = 0 (empty ring)
    mmio_write32(mmioBase, E1000_TDH, 0);
    mmio_write32(mmioBase, E1000_TDT, 0);
    s_txCur = 0;

    // Enable transmitter
    // CT  = 0x0F (collision threshold)
    // COLD = 0x040 (collision distance for full duplex)
    uint32_t tctl = E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (0x0F << E1000_TCTL_CT_SHIFT) |
                    (0x040 << E1000_TCTL_COLD_SHIFT);
    mmio_write32(mmioBase, E1000_TCTL, tctl);

    // Set inter-packet gap (TIPG)
    mmio_write32(mmioBase, E1000_TIPG, E1000_TIPG_DEFAULT);
    s_device.txRingInitialized = true;
    return true;
}

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0) {
        s_kernelPhysicalBase = physicalBase;
    }
}

// ================================================================
// Enable interrupts on the NIC
// ================================================================

static void enable_nic_interrupts(uint64_t mmioBase)
{
    // Clear any pending interrupts
    mmio_read32(mmioBase, E1000_ICR);

    // Enable RX timer, RX descriptor min threshold, link status change
    mmio_write32(mmioBase, E1000_IMS,
                 E1000_ICR_RXT0 |
                 E1000_ICR_RXDMT0 |
                 E1000_ICR_LSC);
}

uint8_t enumerate_network_controllers(NetworkControllerInfo* out,
                                      uint8_t capacity)
{
    if (!out || capacity == 0) return 0;

#if ARCH_HAS_PORT_IO
    uint8_t found = 0;
    for (uint16_t bus = 0; bus < 8; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            uint8_t bus8 = static_cast<uint8_t>(bus);
            uint32_t id = pci_read32(bus8, dev, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0) continue;

            uint32_t headerReg = pci_read32(bus8, dev, 0, 0x0C);
            uint8_t headerType = static_cast<uint8_t>(headerReg >> 16);
            uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < maxFunc; ++func) {
                if (func > 0) {
                    id = pci_read32(bus8, dev, func, 0);
                    if (id == 0xFFFFFFFFu || id == 0) continue;
                }

                uint32_t classReg = pci_read32(bus8, dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                if (baseClass != PCI_CLASS_NETWORK) continue;

                // The diagnostic surface has a fixed bound. Continue the
                // read-only scan so no write-only probing is introduced.
                if (found >= capacity) continue;

                NetworkControllerInfo& info = out[found++];
                info.pciBus = bus8;
                info.pciSlot = dev;
                info.pciFunc = func;
                info.vendorId = static_cast<uint16_t>(id & 0xFFFFu);
                info.deviceId = static_cast<uint16_t>(id >> 16);
                info.subsystemVendorId = pci_read16(bus8, dev, func, 0x2C);
                info.subsystemDeviceId = pci_read16(bus8, dev, func, 0x2E);
                info.revisionId = static_cast<uint8_t>(classReg & 0xFFu);
                info.classCode = baseClass;
                info.subclass = static_cast<uint8_t>(classReg >> 16);
                info.progIf = static_cast<uint8_t>((classReg >> 8) & 0xFFu);
                info.supportedEthernet =
                    info.subclass == PCI_SUBCLASS_ETH &&
                    is_supported_nic(info.vendorId, info.deviceId);
            }
        }
    }
    return found;
#else
    return 0;
#endif
}

// ================================================================
// Reset and initialise the E1000 hardware
// ================================================================

static bool init_e1000(uint64_t mmioBase)
{
    if (mmioBase == 0 || s_device.mmioSize < E1000_MMIO_MIN_SIZE) {
        set_init_failure(NIC_INIT_MMIO, "MMIO BAR is unavailable or too small");
        return false;
    }

    // Disable all interrupts and engines before reset.  The reset wait is
    // finite and observes the device's self-clearing CTRL.RST bit.
    s_device.initStage = NIC_INIT_RESET;
    mmio_write32(mmioBase, E1000_RCTL, 0);
    mmio_write32(mmioBase, E1000_TCTL, 0);
    mmio_write32(mmioBase, E1000_IMC, 0xFFFFFFFF);
    mmio_read32(mmioBase, E1000_ICR);

    uint32_t ctrl = mmio_read32(mmioBase, E1000_CTRL);
    s_device.ctrlValue = ctrl;
    if (ctrl == 0xFFFFFFFFu) {
        set_init_failure(NIC_INIT_MMIO, "MMIO CTRL read returned all-ones");
        return false;
    }

    mmio_write32(mmioBase, E1000_CTRL, ctrl | E1000_CTRL_RST);
    bool resetComplete = false;
    for (uint32_t i = 0; i < 100000u; ++i) {
        ctrl = mmio_read32(mmioBase, E1000_CTRL);
        s_device.ctrlValue = ctrl;
        if (ctrl == 0xFFFFFFFFu) {
            set_init_failure(NIC_INIT_RESET, "reset read returned all-ones");
            return false;
        }
        if ((ctrl & E1000_CTRL_RST) == 0u) {
            resetComplete = true;
            break;
        }
    }
    if (!resetComplete) {
        set_init_failure(NIC_INIT_RESET, "reset timeout");
        return false;
    }
    serial::puts("[NIC] MAC reset: complete (bounded)\n");

    // Keep interrupts masked until all state and rings are ready.
    mmio_write32(mmioBase, E1000_IMC, 0xFFFFFFFF);
    mmio_read32(mmioBase, E1000_ICR);

    // Let the PHY negotiate.  I219 does not use the older forced-full-
    // duplex assumption; preserve the legacy setup for the older devices.
    ctrl = mmio_read32(mmioBase, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    if (!is_i219_device(s_device.deviceId)) ctrl |= E1000_CTRL_FD;
    mmio_write32(mmioBase, E1000_CTRL, ctrl);
    s_device.ctrlValue = mmio_read32(mmioBase, E1000_CTRL);
    s_device.statusValue = mmio_read32(mmioBase, E1000_STATUS);

    s_device.initStage = NIC_INIT_MAC;
    if (!read_mac_address(mmioBase, s_device.macAddress)) {
        set_init_failure(NIC_INIT_MAC, "invalid MAC in RAL0/RAH0");
        return false;
    }
    serial::puts(is_i219_device(s_device.deviceId)
                 ? "[NIC] MAC acquisition: RAL0/RAH0 valid\n"
                 : "[NIC] MAC acquisition: EERD/RAR valid\n");

    if (is_i219_device(s_device.deviceId)) {
        s_device.initStage = NIC_INIT_PHY;
        if (!read_i219_phy_status(mmioBase)) {
            set_init_failure(NIC_INIT_PHY, "PHY MDIC timeout or invalid response");
            return false;
        }
        serial::puts("[NIC] I219 PHY discovery: MDIC address=1 status=26 valid\n");
    } else {
        s_device.link = (s_device.statusValue & E1000_STATUS_LU)
                        ? NIC_LINK_UP : NIC_LINK_DOWN;
    }

    // Clear the Multicast Table Array (128 dwords)
    for (uint32_t i = 0; i < 128; ++i) {
        mmio_write32(mmioBase, E1000_MTA + (i * 4), 0);
    }

    // Initialise RX and TX descriptor rings.  The static storage is part of
    // the kernel image and translated through the supplied physical base;
    // no stack memory is ever handed to the device.
    s_device.initStage = NIC_INIT_RX_RING;
    if (!init_rx(mmioBase)) {
        set_init_failure(NIC_INIT_RX_RING, "RX ring DMA address unavailable");
        return false;
    }
    serial::puts("[NIC] RX ring setup: 32 descriptors ready\n");

    s_device.initStage = NIC_INIT_TX_RING;
    if (!init_tx(mmioBase)) {
        set_init_failure(NIC_INIT_TX_RING, "TX ring DMA address unavailable");
        return false;
    }
    serial::puts("[NIC] TX ring setup: 8 descriptors ready\n");

    // Keep NIC interrupt causes masked until the kernel has installed the
    // device IRQ handler.  Enabling them here leaves a real device a window
    // in which it can assert an IRQ before the handler is registered.

    return true;
}

// ================================================================
// PCI bus scan for network controllers
// ================================================================

static bool scan_pci_nic()
{
    // Scan only common bus/device ranges to avoid excessive PCI reads
    for (uint16_t bus = 0; bus < 8; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            // Only check function 0 first, then others if multi-function
            uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, 0, 0);
            if (id == 0xFFFFFFFF || id == 0) continue;

            // Check header type to see if multi-function
            uint32_t headerReg = pci_read32(static_cast<uint8_t>(bus), dev, 0, 0x0C);
            uint8_t headerType = static_cast<uint8_t>(headerReg >> 16);
            uint8_t maxFunc = (headerType & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < maxFunc; ++func) {
                if (func > 0) {
                    id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                    if (id == 0xFFFFFFFF || id == 0) continue;
                }

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                if (baseClass != PCI_CLASS_NETWORK)
                    continue;

                uint16_t vendor = static_cast<uint16_t>(id & 0xFFFF);
                uint16_t device = static_cast<uint16_t>(id >> 16);
                uint8_t revision = static_cast<uint8_t>(classReg & 0xFF);
                uint16_t subsystemVendor = pci_read16(static_cast<uint8_t>(bus), dev, func, 0x2C);
                uint16_t subsystemDevice = pci_read16(static_cast<uint8_t>(bus), dev, func, 0x2E);

                serial::puts("[NIC] PCI network controller ");
                serial::put_hex8(static_cast<uint8_t>(bus));
                serial::putc(':');
                serial::put_hex8(dev);
                serial::putc('.');
                serial::put_hex8(func);
                serial::puts(" vendor=");
                serial::put_hex32(vendor);
                serial::puts(" device=");
                serial::put_hex32(device);
                serial::puts(" subsystem=");
                serial::put_hex32(subsystemVendor);
                serial::putc(':');
                serial::put_hex32(subsystemDevice);
                serial::puts(" class=");
                serial::put_hex8(baseClass);
                serial::putc('/');
                serial::put_hex8(subClass);
                serial::puts(" progif=");
                serial::put_hex8(static_cast<uint8_t>((classReg >> 8) & 0xFF));
                serial::puts(" rev=");
                serial::put_hex8(revision);
                serial::putc('\n');

                if (subClass != PCI_SUBCLASS_ETH || !is_supported_nic(vendor, device)) {
                    serial::puts("[NIC] Driver: unsupported (identity only; no binding)\n");
                    continue;
                }

                // Found a supported NIC - read the selected register BAR
                uint32_t bar0 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x10);
                if (bar0 & 0x01) {
                    serial::puts("[NIC] BAR0 is I/O space, skipping\n");
                    continue;
                }

                uint64_t mmioBase = bar0 & 0xFFFFFFF0u;

                // 64-bit BAR: read upper 32 bits
                if ((bar0 & 0x06) == 0x04) {
                    uint32_t bar1 = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x14);
                    mmioBase |= (static_cast<uint64_t>(bar1) << 32);
                }

                serial::puts("[NIC] MMIO base: ");
                serial::put_hex32(static_cast<uint32_t>(mmioBase >> 32));
                serial::put_hex32(static_cast<uint32_t>(mmioBase));
                serial::putc('\n');

                // SAFETY CHECK: MMIO base must be mapped
                // The bootloader doesn't map arbitrary MMIO regions.
                // For now, we'll record the device info but skip hardware init
                // unless the MMIO region is in a known-mapped range.
                // 
                // In QEMU, the E1000 is typically at 0xFEBC0000 or similar,
                // which is NOT mapped by the bootloader's page tables.
                //
                // We need the bootloader to map this region, or we need
                // to implement dynamic page table updates.
                
                // Read interrupt line
                uint32_t intReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x3C);
                uint8_t irqLine = static_cast<uint8_t>(intReg & 0xFF);

                serial::puts("[NIC] IRQ line: ");
                serial::put_hex8(irqLine);
                serial::putc('\n');

                // Populate device info (without MMIO access)
                s_device.pciBus   = static_cast<uint8_t>(bus);
                s_device.pciSlot  = dev;
                s_device.pciFunc  = func;
                s_device.vendorId = vendor;
                s_device.deviceId = device;
                s_device.subsystemVendorId = subsystemVendor;
                s_device.subsystemDeviceId = subsystemDevice;
                s_device.revisionId = revision;
                s_device.classCode = baseClass;
                s_device.subclass = subClass;
                s_device.progIf = static_cast<uint8_t>((classReg >> 8) & 0xFF);
                s_device.mmioBase = mmioBase;
                s_device.mmioPhys = mmioBase;
                s_device.mmioSize = 0;
                s_device.irqLine  = irqLine;
                s_device.pciCommand = pci_read16(static_cast<uint8_t>(bus), dev, func, 0x04);
                s_device.driverBound = true;
                s_device.initStage = NIC_INIT_BOUND;
                s_device.phyAccess = NIC_PHY_NOT_ATTEMPTED;
                s_device.link = NIC_LINK_UNKNOWN;

                // Set device name
                s_device.name[0] = 'e'; s_device.name[1] = 't';
                s_device.name[2] = 'h'; s_device.name[3] = '0';
                s_device.name[4] = '\0';

                // The legacy scan has no safe kernel MMIO mapping handoff.
                // Keep the bound identity visible, but do not invent a MAC
                // or claim that the device is ready.
                set_init_failure(NIC_INIT_MMIO, "MMIO not mapped by bootloader");
                s_device.active = false;  // Not active until MMIO is mapped
                
                return true;  // Found a device, even if not initialized
            }
        }
    }
    return false;
}

#endif // ARCH_HAS_PORT_IO

#if !ARCH_HAS_PORT_IO
uint8_t enumerate_network_controllers(NetworkControllerInfo* out,
                                      uint8_t capacity)
{
    (void)out;
    (void)capacity;
    return 0;
}
#endif

// ================================================================
// Initialize from BootInfo (bootloader provides mapped MMIO)
// ================================================================

bool init_from_bootinfo(const NicBootInfo* nicInfo)
{
    if (!nicInfo) {
        serial::puts("[NIC] init_from_bootinfo: null pointer\n");
        return false;
    }
    
    // Check if NIC was found by bootloader
    if (!(nicInfo->flags & NIC_BOOT_FLAG_FOUND)) {
        serial::puts("[NIC] init_from_bootinfo: NIC not found by bootloader\n");
        return false;
    }

    // Preserve the bound identity even if a later hardware stage fails.  The
    // shell can then report the exact frontier instead of collapsing back to
    // "no NIC device structure".
    memzero(&s_device, sizeof(s_device));
    memzero(s_rxDescs, sizeof(s_rxDescs));
    memzero(s_txDescs, sizeof(s_txDescs));
    s_rxCur = 0;
    s_txCur = 0;
    s_initialised = true;

    serial::puts("[NIC] Initializing from BootInfo:\n");
    serial::puts("[NIC]   Vendor: ");
    serial::put_hex32(nicInfo->vendorId);
    serial::puts(" Device: ");
    serial::put_hex32(nicInfo->deviceId);
    serial::putc('\n');
    serial::puts("[NIC]   MMIO Phys: ");
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioPhys >> 32));
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioPhys));
    serial::putc('\n');
    serial::puts("[NIC]   MMIO Virt: ");
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioVirt >> 32));
    serial::put_hex32(static_cast<uint32_t>(nicInfo->mmioVirt));
    serial::putc('\n');
    
    // Initialize device structure
    memzero(&s_device, sizeof(s_device));
    memzero(s_rxDescs, sizeof(s_rxDescs));
    memzero(s_txDescs, sizeof(s_txDescs));
    s_rxCur = 0;
    s_txCur = 0;
    
    // Populate device info from BootInfo
    s_device.pciBus = nicInfo->bus;
    s_device.pciSlot = nicInfo->device;
    s_device.pciFunc = nicInfo->function;
    s_device.vendorId = nicInfo->vendorId;
    s_device.deviceId = nicInfo->deviceId;
    s_device.subsystemVendorId = nicInfo->subsystemVendorId;
    s_device.subsystemDeviceId = nicInfo->subsystemDeviceId;
    s_device.revisionId = nicInfo->revisionId;
    s_device.classCode = nicInfo->classCode;
    s_device.subclass = nicInfo->subclass;
    s_device.progIf = nicInfo->progIf;
    s_device.mmioBase = nicInfo->mmioVirt;  // Use virtual address for MMIO access
    s_device.mmioPhys = nicInfo->mmioPhys;
    s_device.mmioSize = nicInfo->mmioSize;
    s_device.irqLine = nicInfo->irqLine;
    s_device.mmioMapped = (nicInfo->flags & NIC_BOOT_FLAG_MAPPED) != 0u;
    const bool exactDriverMatch = is_supported_nic(s_device.vendorId, s_device.deviceId) &&
                                  s_device.subclass == PCI_SUBCLASS_ETH;
    s_device.driverBound = exactDriverMatch;
    s_device.initStage = NIC_INIT_BOUND;
    s_device.phyAccess = NIC_PHY_NOT_ATTEMPTED;
    s_device.link = NIC_LINK_UNKNOWN;

    serial::puts(exactDriverMatch
                 ? (is_i219_device(s_device.deviceId)
                    ? "[NIC] PCI match: accepted; driver=intel-i219-lm (PCH)\n"
                    : "[NIC] PCI match: accepted; driver=intel-e1000 family\n")
                 : "[NIC] PCI match: rejected; no exact Ethernet driver\n");

    if (!exactDriverMatch) {
        set_init_failure(NIC_INIT_BOUND, "PCI identity is outside the exact driver match");
        return false;
    }

    // Set device name
    s_device.name[0] = 'e'; s_device.name[1] = 't';
    s_device.name[2] = 'h'; s_device.name[3] = '0';
    s_device.name[4] = '\0';
    
#if ARCH_HAS_PORT_IO
    // The UEFI handoff is the normal bare-metal path.  It must provide a
    // nonzero BAR, a mapped virtual address, and enough bytes for the
    // registers used by this driver before any MMIO access is attempted.
    if (!s_device.mmioMapped || s_device.mmioBase == 0) {
        set_init_failure(NIC_INIT_MMIO, "MMIO BAR unavailable or not mapped");
        return false;
    }
    if (s_device.mmioSize < E1000_MMIO_MIN_SIZE) {
        set_init_failure(NIC_INIT_MMIO, "MMIO BAR is smaller than the register window");
        return false;
    }
    serial::puts("[NIC] MMIO mapping: accepted size=");
    serial::put_hex32(s_device.mmioSize);
    serial::puts(" bytes\n");

    s_device.initStage = NIC_INIT_PCI;
    uint16_t command = pci_read16(s_device.pciBus, s_device.pciSlot,
                                  s_device.pciFunc, 0x04);
    command = static_cast<uint16_t>(command | (1u << 1) | (1u << 2));
    pci_write32(s_device.pciBus, s_device.pciSlot, s_device.pciFunc, 0x04,
                (pci_read32(s_device.pciBus, s_device.pciSlot, s_device.pciFunc, 0x04) &
                 0xFFFF0000u) | command);
    s_device.pciCommand = pci_read16(s_device.pciBus, s_device.pciSlot,
                                     s_device.pciFunc, 0x04);
    serial::puts("[NIC] PCI command: ");
    serial::put_hex32(s_device.pciCommand);
    serial::puts(" (memory+bus-master enabled)\n");
    if ((s_device.pciCommand & ((1u << 1) | (1u << 2))) != ((1u << 1) | (1u << 2))) {
        set_init_failure(NIC_INIT_PCI, "PCI memory-space/bus-master enable failed");
        return false;
    }

    serial::puts("[NIC] Initializing E1000-family hardware...\n");

    if (!init_e1000(s_device.mmioBase)) {
        return false;
    }

    serial::puts("[NIC] MAC: ");
    for (uint8_t i = 0; i < 6; ++i) {
        if (i > 0) serial::putc(':');
        serial::put_hex8(s_device.macAddress[i]);
    }
    serial::putc('\n');
    
    serial::puts("[NIC] Link: ");
    serial::puts(s_device.link == NIC_LINK_UP ? "UP" :
                 (s_device.link == NIC_LINK_DOWN ? "DOWN" : "UNKNOWN"));
    serial::putc('\n');
    
    s_device.active = true;
    s_device.pollingEnabled = true;
    s_device.nicRegistered = true;
    s_device.initStage = NIC_INIT_READY;
    serial::puts("[NIC] NIC registration: complete; interrupt causes remain masked\n");
    
    serial::puts(is_i219_device(s_device.deviceId)
                 ? "[NIC] I219-LM initialization complete\n"
                 : "[NIC] E1000 initialization complete\n");
    return true;
#else
    set_init_failure(NIC_INIT_PCI, "PCI port-I/O support is unavailable");
    return false;
#endif
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(&s_device, sizeof(s_device));
    memzero(s_rxDescs, sizeof(s_rxDescs));
    memzero(s_txDescs, sizeof(s_txDescs));
    s_initialised = false;
    s_rxCur = 0;
    s_txCur = 0;

#if ARCH_HAS_PORT_IO
    serial::puts("[NIC] Scanning PCI bus for network controllers...\n");

    if (scan_pci_nic()) {
        s_initialised = true;
        serial::puts("[NIC] Found ");
        serial::puts(s_device.name);
        serial::puts("  vendor=");
        serial::put_hex32(s_device.vendorId);
        serial::puts(" device=");
        serial::put_hex32(s_device.deviceId);
        serial::putc('\n');
        
        if (s_device.active) {
            serial::puts("[NIC] MAC=");
            for (int i = 0; i < 6; ++i) {
                if (i > 0) serial::putc(':');
                serial::put_hex8(s_device.macAddress[i]);
            }
            serial::puts("  Link=");
            serial::puts(s_device.link == NIC_LINK_UP ? "UP" : "DOWN");
            serial::putc('\n');
        } else {
            serial::puts("[NIC] Device found but not active (MMIO not mapped)\n");
        }
    } else {
        serial::puts("[NIC] No supported NIC found\n");
    }
#else
    // Architectures without PCI port-I/O: stub - MMIO PCI ECAM
    // enumeration would go here for ia64/sparc64/riscv64.
#endif
}

bool is_active()
{
    return s_initialised && s_device.active;
}

const NICDevice* get_device()
{
    return s_initialised ? &s_device : nullptr;
}

const uint8_t* get_mac_address()
{
    return s_initialised ? s_device.macAddress : nullptr;
}

LinkState get_link_state()
{
    if (!s_initialised) return NIC_LINK_DOWN;

    // This is a status query, not a hardware transaction.  The desktop
    // network widget and shell diagnostics may call it from redraw paths;
    // those paths must never perform MMIO or a potentially long MDIC poll.
    // The cached value is populated during bounded initialisation and updated
    // by the NIC IRQ handler when link-change events are acknowledged.
    return s_device.link;
}

void set_irq_registered(bool registered)
{
    if (!s_initialised) return;

    s_device.irqRegistered = registered;

#if ARCH_HAS_PORT_IO
    if (s_device.active && s_device.mmioMapped) {
        if (registered) {
            enable_nic_interrupts(s_device.mmioBase);
        } else {
            mmio_write32(s_device.mmioBase, E1000_IMC, 0xFFFFFFFFu);
            mmio_read32(s_device.mmioBase, E1000_ICR);
        }
    }
#endif
}

// ================================================================
// send_frame - transmit a raw Ethernet frame
// ================================================================

Status send_frame(const uint8_t* data, uint16_t len)
{
    if (!s_initialised || !s_device.active) {
        serial::puts("[NIC] send_frame: not initialized or not active\n");
        return NIC_ERR_NO_DEVICE;
    }
    if (!data || len < ETH_HLEN) {
        serial::puts("[NIC] send_frame: frame too small\n");
        return NIC_ERR_FRAME_TOO_LARGE; // too small
    }
    if (len > ETH_FRAME_MAX) {
        serial::puts("[NIC] send_frame: frame too large\n");
        return NIC_ERR_FRAME_TOO_LARGE;
    }

#if ARCH_HAS_PORT_IO
    s_device.stats.txAttempted++;

    // Check that the current TX descriptor is available
    if (!(s_txDescs[s_txCur].status & E1000_TXD_STAT_DD)) {
        serial::puts("[NIC] send_frame: TX descriptor not available (TX ring full)\n");
        s_device.stats.txDropped++;
        return NIC_ERR_TX_FULL;
    }

    // Copy frame data to TX buffer
    memcopy(s_txBuffer, data, static_cast<uint32_t>(len));

    // Set up the descriptor
    uint64_t bufAddr = 0;
    if (!dma_address(s_txBuffer, &bufAddr)) {
        serial::puts("[NIC] send_frame: TX buffer DMA address unavailable\n");
        s_device.stats.txErrors++;
        return NIC_ERR_INIT_FAIL;
    }
    dma_memory_barrier();
    s_txDescs[s_txCur].bufferAddr = bufAddr;
    s_txDescs[s_txCur].length     = static_cast<uint16_t>(len);
    s_txDescs[s_txCur].cmd        = E1000_TXD_CMD_EOP |
                                    E1000_TXD_CMD_IFCS |
                                    E1000_TXD_CMD_RS;
    s_txDescs[s_txCur].status     = 0;
    dma_memory_barrier();

    // Advance tail pointer to submit the descriptor
    uint16_t oldTx = s_txCur;
    s_txCur = (s_txCur + 1) % NUM_TX_DESC;
    mmio_write32(s_device.mmioBase, E1000_TDT, s_txCur);

    // Wait for transmission to complete (busy-poll descriptor status)
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (s_txDescs[oldTx].status & E1000_TXD_STAT_DD) {
            s_device.stats.txFrames++;
            s_device.stats.txBytes += static_cast<uint32_t>(len);
            return NIC_OK;
        }
    }

    serial::puts("[NIC] TX TIMEOUT! Descriptor status=0x");
    serial::put_hex8(s_txDescs[oldTx].status);
    serial::putc('\n');
    s_device.stats.txErrors++;
    return NIC_ERR_INIT_FAIL;
#else
    (void)data;
    (void)len;
    return NIC_ERR_NO_DEVICE;
#endif
}

// ================================================================
// receive_frame - read the next pending Ethernet frame
// ================================================================

Status receive_frame(uint8_t* buffer, uint16_t max_len, uint16_t* received)
{
    if (!s_initialised || !s_device.active) return NIC_ERR_NO_DEVICE;
    if (received) *received = 0;

#if ARCH_HAS_PORT_IO
    // Check if current RX descriptor has a completed frame
    if (!(s_rxDescs[s_rxCur].status & E1000_RXD_STAT_DD)) {
        return NIC_ERR_RX_EMPTY;
    }

    s_device.stats.rxObserved++;
    dma_memory_barrier();

    // Verify end-of-packet flag
    if (!(s_rxDescs[s_rxCur].status & E1000_RXD_STAT_EOP)) {
        // Multi-descriptor frames not supported; drop and advance
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxDropped++;
        s_device.stats.rxMalformed++;
        return NIC_ERR_RX_EMPTY;
    }

    uint16_t frameLen = s_rxDescs[s_rxCur].length;

    if (frameLen > max_len) {
        // Caller's buffer too small; drop frame
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxDropped++;
        s_device.stats.rxMalformed++;
        return NIC_ERR_BUFFER_TOO_SMALL;
    }

    // Check for receive errors
    if (s_rxDescs[s_rxCur].errors) {
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxErrors++;
        s_device.stats.rxMalformed++;
        return NIC_ERR_RX_EMPTY;
    }

    // Copy frame to caller's buffer
    memcopy(buffer, s_rxBuffers[s_rxCur], frameLen);
    if (received) *received = frameLen;

    // Recycle the descriptor
    s_rxDescs[s_rxCur].status = 0;
    uint16_t oldRx = s_rxCur;
    s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
    mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);

    s_device.stats.rxFrames++;
    s_device.stats.rxBytes += frameLen;
    return NIC_OK;
#else
    (void)buffer;
    (void)max_len;
    (void)received;
    return NIC_ERR_NO_DEVICE;
#endif
}

// ================================================================
// IRQ handler - called from interrupt dispatch
// ================================================================

void irq_handler()
{
#if ARCH_HAS_PORT_IO
    if (!s_initialised) return;

    // Read interrupt cause (auto-clears on read)
    uint32_t icr = mmio_read32(s_device.mmioBase, E1000_ICR);

    s_device.stats.interrupts++;

    if (icr & E1000_ICR_LSC) {
        // Link status changed - update cached state
        if (is_i219_device(s_device.deviceId)) {
            if (!read_i219_phy_status(s_device.mmioBase)) {
                s_device.link = NIC_LINK_UNKNOWN;
            }
        } else {
            uint32_t status = mmio_read32(s_device.mmioBase, E1000_STATUS);
            s_device.statusValue = status;
            s_device.link = (status & E1000_STATUS_LU) ? NIC_LINK_UP : NIC_LINK_DOWN;
        }
        serial::puts("[NIC] Link status changed: ");
        serial::puts(s_device.link == NIC_LINK_UP ? "UP" :
                     (s_device.link == NIC_LINK_DOWN ? "DOWN" : "UNKNOWN"));
        serial::putc('\n');
    }

    // RX interrupt: frames are available - the main loop will call
    // receive_frame() to drain them.  No action needed here beyond
    // acknowledging the interrupt.

    // Send EOI to PIC
#if ARCH_HAS_PIC_8259
    interrupts::eoi(s_device.irqLine);
#endif

#endif // ARCH_HAS_PORT_IO
}

// ================================================================
// Statistics
// ================================================================

const NetStats* get_stats()
{
    return s_initialised ? &s_device.stats : nullptr;
}

} // namespace nic
} // namespace kernel
