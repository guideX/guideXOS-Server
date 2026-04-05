// NIC Driver — Implementation
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

// ================================================================
// PCI configuration space (port-I/O method — x86/amd64)
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
// Read MAC address from EEPROM (E1000)
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

static void read_mac_address(uint64_t mmioBase, uint8_t* mac)
{
    // Try EEPROM first
    uint16_t w0 = eeprom_read(mmioBase, 0);
    uint16_t w1 = eeprom_read(mmioBase, 1);
    uint16_t w2 = eeprom_read(mmioBase, 2);

    if (w0 != 0 || w1 != 0 || w2 != 0) {
        mac[0] = static_cast<uint8_t>(w0 & 0xFF);
        mac[1] = static_cast<uint8_t>(w0 >> 8);
        mac[2] = static_cast<uint8_t>(w1 & 0xFF);
        mac[3] = static_cast<uint8_t>(w1 >> 8);
        mac[4] = static_cast<uint8_t>(w2 & 0xFF);
        mac[5] = static_cast<uint8_t>(w2 >> 8);
    } else {
        // Fall back to RAL0/RAH0 registers
        uint32_t ral = mmio_read32(mmioBase, E1000_RAL0);
        uint32_t rah = mmio_read32(mmioBase, E1000_RAH0);
        mac[0] = static_cast<uint8_t>(ral);
        mac[1] = static_cast<uint8_t>(ral >> 8);
        mac[2] = static_cast<uint8_t>(ral >> 16);
        mac[3] = static_cast<uint8_t>(ral >> 24);
        mac[4] = static_cast<uint8_t>(rah);
        mac[5] = static_cast<uint8_t>(rah >> 8);
    }
}

// ================================================================
// Initialise RX descriptor ring
// ================================================================

static void init_rx(uint64_t mmioBase)
{
    // Initialise each RX descriptor to point at its buffer
    for (uint16_t i = 0; i < NUM_RX_DESC; ++i) {
        memzero(&s_rxDescs[i], sizeof(RxDescriptor));
        s_rxDescs[i].bufferAddr = reinterpret_cast<uint64_t>(&s_rxBuffers[i][0]);
        s_rxDescs[i].status     = 0;
    }

    // Program the RX descriptor ring base address
    uint64_t rxDescPhys = reinterpret_cast<uint64_t>(&s_rxDescs[0]);
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
    mmio_write32(mmioBase, E1000_RCTL, rctl);
}

// ================================================================
// Initialise TX descriptor ring
// ================================================================

static void init_tx(uint64_t mmioBase)
{
    for (uint16_t i = 0; i < NUM_TX_DESC; ++i) {
        memzero(&s_txDescs[i], sizeof(TxDescriptor));
        s_txDescs[i].status = E1000_TXD_STAT_DD; // mark as done (available)
    }

    // Program the TX descriptor ring base address
    uint64_t txDescPhys = reinterpret_cast<uint64_t>(&s_txDescs[0]);
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

// ================================================================
// Check if a PCI device is a supported E1000 NIC
// ================================================================

static bool is_supported_nic(uint16_t vendor, uint16_t device)
{
    if (vendor != PCI_VENDOR_INTEL) return false;
    return (device == PCI_DEVICE_E1000 ||
            device == PCI_DEVICE_E1000E ||
            device == PCI_DEVICE_I217);
}

// ================================================================
// Reset and initialise the E1000 hardware
// ================================================================

static bool init_e1000(uint64_t mmioBase)
{
    // Global reset
    uint32_t ctrl = mmio_read32(mmioBase, E1000_CTRL);
    mmio_write32(mmioBase, E1000_CTRL, ctrl | E1000_CTRL_RST);

    // Wait for reset to complete (busy-wait)
    for (volatile uint32_t i = 0; i < 100000; ++i) { }

    // Disable all interrupts during setup
    mmio_write32(mmioBase, E1000_IMC, 0xFFFFFFFF);

    // Clear any pending interrupt causes
    mmio_read32(mmioBase, E1000_ICR);

    // Set link up, auto-speed detect, full duplex
    ctrl = mmio_read32(mmioBase, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE | E1000_CTRL_FD;
    mmio_write32(mmioBase, E1000_CTRL, ctrl);

    // Clear the Multicast Table Array (128 dwords)
    for (uint32_t i = 0; i < 128; ++i) {
        mmio_write32(mmioBase, E1000_MTA + (i * 4), 0);
    }

    // Initialise RX and TX descriptor rings
    init_rx(mmioBase);
    init_tx(mmioBase);

    // Enable NIC interrupts
    enable_nic_interrupts(mmioBase);

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

                if (baseClass != PCI_CLASS_NETWORK || subClass != PCI_SUBCLASS_ETH)
                    continue;

                uint16_t vendor = static_cast<uint16_t>(id & 0xFFFF);
                uint16_t device = static_cast<uint16_t>(id >> 16);

                serial::puts("[NIC] Found NIC: vendor=");
                serial::put_hex32(vendor);
                serial::puts(" device=");
                serial::put_hex32(device);
                serial::putc('\n');

                if (!is_supported_nic(vendor, device)) {
                    serial::puts("[NIC] NIC not supported (not Intel E1000)\n");
                    continue;
                }

                // Found a supported NIC — read BAR0 (MMIO base)
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
                s_device.mmioBase = mmioBase;
                s_device.irqLine  = irqLine;

                // Set device name
                s_device.name[0] = 'e'; s_device.name[1] = 't';
                s_device.name[2] = 'h'; s_device.name[3] = '0';
                s_device.name[4] = '\0';

                // For now, skip hardware initialization since MMIO is not mapped
                // The device is detected but not active until MMIO mapping is available
                serial::puts("[NIC] Device detected but MMIO not mapped - skipping hw init\n");
                
                // Set a placeholder MAC (will be read when MMIO is available)
                s_device.macAddress[0] = 0x52;
                s_device.macAddress[1] = 0x54;
                s_device.macAddress[2] = 0x00;
                s_device.macAddress[3] = 0x12;
                s_device.macAddress[4] = 0x34;
                s_device.macAddress[5] = 0x56;
                
                s_device.link = NIC_LINK_DOWN;
                s_device.active = false;  // Not active until MMIO is mapped
                
                return true;  // Found a device, even if not initialized
            }
        }
    }
    return false;
}

#endif // ARCH_HAS_PORT_IO

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
    // Architectures without PCI port-I/O: stub — MMIO PCI ECAM
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

#if ARCH_HAS_PORT_IO
    uint32_t status = mmio_read32(s_device.mmioBase, E1000_STATUS);
    s_device.link = (status & E1000_STATUS_LU) ? NIC_LINK_UP : NIC_LINK_DOWN;
#endif

    return s_device.link;
}

// ================================================================
// send_frame — transmit a raw Ethernet frame
// ================================================================

Status send_frame(const uint8_t* data, uint16_t len)
{
    if (!s_initialised || !s_device.active) return NIC_ERR_NO_DEVICE;
    if (len < ETH_HLEN) return NIC_ERR_FRAME_TOO_LARGE; // too small
    if (len > ETH_FRAME_MAX) return NIC_ERR_FRAME_TOO_LARGE;

#if ARCH_HAS_PORT_IO
    // Check that the current TX descriptor is available
    if (!(s_txDescs[s_txCur].status & E1000_TXD_STAT_DD)) {
        s_device.stats.txDropped++;
        return NIC_ERR_TX_FULL;
    }

    // Copy frame data to TX buffer
    memcopy(s_txBuffer, data, static_cast<uint32_t>(len));

    // Set up the descriptor
    s_txDescs[s_txCur].bufferAddr = reinterpret_cast<uint64_t>(s_txBuffer);
    s_txDescs[s_txCur].length     = static_cast<uint16_t>(len);
    s_txDescs[s_txCur].cmd        = E1000_TXD_CMD_EOP |
                                    E1000_TXD_CMD_IFCS |
                                    E1000_TXD_CMD_RS;
    s_txDescs[s_txCur].status     = 0;

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

    s_device.stats.txErrors++;
    return NIC_ERR_INIT_FAIL;
#else
    (void)data;
    (void)len;
    return NIC_ERR_NO_DEVICE;
#endif
}

// ================================================================
// receive_frame — read the next pending Ethernet frame
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

    // Verify end-of-packet flag
    if (!(s_rxDescs[s_rxCur].status & E1000_RXD_STAT_EOP)) {
        // Multi-descriptor frames not supported; drop and advance
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxDropped++;
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
        return NIC_ERR_BUFFER_TOO_SMALL;
    }

    // Check for receive errors
    if (s_rxDescs[s_rxCur].errors) {
        s_rxDescs[s_rxCur].status = 0;
        uint16_t oldRx = s_rxCur;
        s_rxCur = (s_rxCur + 1) % NUM_RX_DESC;
        mmio_write32(s_device.mmioBase, E1000_RDT, oldRx);
        s_device.stats.rxErrors++;
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
// IRQ handler — called from interrupt dispatch
// ================================================================

void irq_handler()
{
#if ARCH_HAS_PORT_IO
    if (!s_initialised) return;

    // Read interrupt cause (auto-clears on read)
    uint32_t icr = mmio_read32(s_device.mmioBase, E1000_ICR);

    s_device.stats.interrupts++;

    if (icr & E1000_ICR_LSC) {
        // Link status changed — update cached state
        uint32_t status = mmio_read32(s_device.mmioBase, E1000_STATUS);
        s_device.link = (status & E1000_STATUS_LU) ? NIC_LINK_UP : NIC_LINK_DOWN;
        serial::puts("[NIC] Link status changed: ");
        serial::puts(s_device.link == NIC_LINK_UP ? "UP" : "DOWN");
        serial::putc('\n');
    }

    // RX interrupt: frames are available — the main loop will call
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
