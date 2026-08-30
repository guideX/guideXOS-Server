// Network Interface Card (NIC) Driver
//
// Supports:
//   - Intel E1000 / E1000E (PCI MMIO)  - QEMU default, VirtualBox, Bochs
//   - Intel I219-LM/PCH (8086:156F) via the bounded E1000e-compatible path
//   - PCI bus scan for class 0x02 (Network Controller)
//   - Raw Ethernet frame send / receive via descriptor rings
//   - IRQ-driven receive with ring buffer
//
// Functional on architectures with PCI MMIO access:
//   x86, amd64, ia64, sparc64, riscv64
// Stub-only on architectures without PCI:
//   sparc (SBus only), arm (no PCI in current board model)
//
// Reference: Intel 8254x Software Developer's Manual (SDM)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_NIC_H
#define KERNEL_NIC_H

#include "kernel/types.h"
#include "kernel/arch.h"

// Phase 5 physical isolation selector.  The UEFI loader is built with the
// same value.  Only the exact I219-LM device uses it; existing NIC paths keep
// their normal behavior.
#ifndef GXOS_AIDA_I219_PHASE5_STAGE
#define GXOS_AIDA_I219_PHASE5_STAGE 8
#endif

#if GXOS_AIDA_I219_PHASE5_STAGE < 0 || GXOS_AIDA_I219_PHASE5_STAGE > 8
#error GXOS_AIDA_I219_PHASE5_STAGE must be in the range 0..8
#endif

namespace kernel {
namespace nic {

// ================================================================
// Ethernet constants
// ================================================================

static const uint16_t ETH_ALEN      = 6;       // MAC address length
static const uint16_t ETH_HLEN      = 14;      // Ethernet header size
static const uint16_t ETH_MTU       = 1500;    // standard MTU
static const uint16_t ETH_FRAME_MAX = 1518;    // header + MTU + FCS
static const uint16_t ETH_FRAME_MIN = 60;      // minimum frame (no FCS)

// ================================================================
// PCI identification
// ================================================================

static const uint16_t PCI_VENDOR_INTEL   = 0x8086;
static const uint16_t PCI_DEVICE_E1000   = 0x100E;  // 82540EM (QEMU default)
static const uint16_t PCI_DEVICE_E1000E  = 0x10D3;  // 82574L
static const uint16_t PCI_DEVICE_I217    = 0x153A;   // I217-LM
static const uint16_t PCI_DEVICE_I219_LM = 0x156F;   // I219-LM/PCH

static const uint8_t  PCI_CLASS_NETWORK  = 0x02;
static const uint8_t  PCI_SUBCLASS_ETH   = 0x00;

// ================================================================
// E1000 register offsets (from the selected register MMIO base)
// ================================================================

static const uint32_t E1000_CTRL     = 0x0000;  // Device Control
static const uint32_t E1000_STATUS   = 0x0008;  // Device Status
static const uint32_t E1000_EECD     = 0x0010;  // EEPROM/Flash Control
static const uint32_t E1000_EERD     = 0x0014;  // EEPROM Read
static const uint32_t E1000_CTRL_EXT  = 0x0018;  // Extended Device Control
static const uint32_t E1000_MDIC     = 0x0020;  // MDI/PHY management
static const uint32_t E1000_ICR      = 0x00C0;  // Interrupt Cause Read
static const uint32_t E1000_ICS      = 0x00C8;  // Interrupt Cause Set
static const uint32_t E1000_IMS      = 0x00D0;  // Interrupt Mask Set
static const uint32_t E1000_IMC      = 0x00D8;  // Interrupt Mask Clear
static const uint32_t E1000_RCTL     = 0x0100;  // Receive Control
static const uint32_t E1000_TCTL     = 0x0400;  // Transmit Control
static const uint32_t E1000_TIPG     = 0x0410;  // Transmit IPG
static const uint32_t E1000_RDBAL    = 0x2800;  // RX Descriptor Base Low
static const uint32_t E1000_RDBAH    = 0x2804;  // RX Descriptor Base High
static const uint32_t E1000_RDLEN    = 0x2808;  // RX Descriptor Length
static const uint32_t E1000_RDH      = 0x2810;  // RX Descriptor Head
static const uint32_t E1000_RDT      = 0x2818;  // RX Descriptor Tail
static const uint32_t E1000_TDBAL    = 0x3800;  // TX Descriptor Base Low
static const uint32_t E1000_TDBAH    = 0x3804;  // TX Descriptor Base High
static const uint32_t E1000_TDLEN    = 0x3808;  // TX Descriptor Length
static const uint32_t E1000_TDH      = 0x3810;  // TX Descriptor Head
static const uint32_t E1000_TDT      = 0x3818;  // TX Descriptor Tail
static const uint32_t E1000_MTA      = 0x5200;  // Multicast Table Array (128 dwords)
static const uint32_t E1000_RAL0     = 0x5400;  // Receive Address Low  (MAC [0])
static const uint32_t E1000_RAH0     = 0x5404;  // Receive Address High (MAC [0])
static const uint32_t E1000_MMIO_MIN_SIZE = E1000_RAH0 + sizeof(uint32_t);
static const uint32_t E1000_RAH_AV   = (1u << 31); // Receive address valid

// CTRL register bits
static const uint32_t E1000_CTRL_SLU   = (1u << 6);   // Set Link Up
static const uint32_t E1000_CTRL_RST   = (1u << 26);  // Device Reset
static const uint32_t E1000_CTRL_ASDE  = (1u << 5);   // Auto-Speed Detection Enable
static const uint32_t E1000_CTRL_FD    = (1u << 0);   // Full Duplex

// STATUS register bits
static const uint32_t E1000_STATUS_LU  = (1u << 1);   // Link Up
static const uint32_t E1000_STATUS_SPEED_MASK = (3u << 6);
static const uint32_t E1000_STATUS_SPEED_10   = (0u << 6);
static const uint32_t E1000_STATUS_SPEED_100  = (1u << 6);
static const uint32_t E1000_STATUS_SPEED_1000 = (2u << 6);

// RCTL (Receive Control) bits
static const uint32_t E1000_RCTL_EN    = (1u << 1);   // Receiver Enable
static const uint32_t E1000_RCTL_SBP   = (1u << 2);   // Store Bad Packets
static const uint32_t E1000_RCTL_UPE   = (1u << 3);   // Unicast Promiscuous
static const uint32_t E1000_RCTL_MPE   = (1u << 4);   // Multicast Promiscuous
static const uint32_t E1000_RCTL_BAM   = (1u << 15);  // Broadcast Accept Mode
static const uint32_t E1000_RCTL_BSIZE_2048 = (0u << 16); // Buffer Size 2048
static const uint32_t E1000_RCTL_BSIZE_4096 = (3u << 16) | (1u << 25); // BSEX + size
static const uint32_t E1000_RCTL_SECRC = (1u << 26);  // Strip Ethernet CRC

// TCTL (Transmit Control) bits
static const uint32_t E1000_TCTL_EN    = (1u << 1);   // Transmitter Enable
static const uint32_t E1000_TCTL_PSP   = (1u << 3);   // Pad Short Packets
static const uint32_t E1000_TCTL_CT_SHIFT  = 4;       // Collision Threshold
static const uint32_t E1000_TCTL_COLD_SHIFT = 12;     // Collision Distance

// ICR / IMS interrupt bits
static const uint32_t E1000_ICR_TXDW   = (1u << 0);   // TX Descriptor Written Back
static const uint32_t E1000_ICR_TXQE   = (1u << 1);   // TX Queue Empty
static const uint32_t E1000_ICR_LSC    = (1u << 2);   // Link Status Change
static const uint32_t E1000_ICR_RXDMT0 = (1u << 4);   // RX Descriptor Min Threshold
static const uint32_t E1000_ICR_RXT0   = (1u << 7);   // RX Timer Interrupt

// TIPG recommended values (IEEE 802.3)
static const uint32_t E1000_TIPG_DEFAULT = (10) | (10 << 10) | (10 << 20);

// EEPROM Read register bits
static const uint32_t E1000_EERD_START = (1u << 0);
static const uint32_t E1000_EERD_DONE  = (1u << 4);
static const uint32_t E1000_EERD_ADDR_SHIFT = 8;
static const uint32_t E1000_EERD_DATA_SHIFT = 16;

// MDIC fields.  The I219 integrated PHY is MDI address 1.
static const uint32_t E1000_MDIC_DATA_MASK  = 0x0000FFFFu;
static const uint32_t E1000_MDIC_REG_SHIFT  = 16;
static const uint32_t E1000_MDIC_PHY_SHIFT  = 21;
static const uint32_t E1000_MDIC_OP_SHIFT   = 26;
static const uint32_t E1000_MDIC_OP_WRITE   = (1u << E1000_MDIC_OP_SHIFT);
static const uint32_t E1000_MDIC_OP_READ    = (2u << E1000_MDIC_OP_SHIFT);
static const uint32_t E1000_MDIC_READY      = (1u << 28);
static const uint32_t E1000_MDIC_ERROR      = (1u << 30);
static const uint8_t  I219_PHY_ADDRESS      = 1;
static const uint8_t  I219_PHY_STATUS_REG   = 26;
static const uint16_t I219_PHY_STATUS_LINK  = (1u << 6);
static const uint16_t I219_PHY_STATUS_DUPLEX = (1u << 7);
static const uint16_t I219_PHY_STATUS_SPEED_MASK = (3u << 8);

// ================================================================
// Descriptor ring sizes
// ================================================================

static const uint16_t NUM_RX_DESC = 32;
static const uint16_t NUM_TX_DESC = 8;
static const uint16_t RX_BUFFER_SIZE = 2048;

// ================================================================
// Receive Descriptor (E1000 legacy format, 16 bytes)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define NIC_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define NIC_PACKED
#endif

struct RxDescriptor {
    uint64_t bufferAddr;    // physical address of packet buffer
    uint16_t length;        // length of received data
    uint16_t checksum;      // packet checksum
    volatile uint8_t status; // descriptor status written by the NIC
    volatile uint8_t errors; // descriptor errors written by the NIC
    uint16_t special;       // VLAN tag (if applicable)
} NIC_PACKED;

// RX descriptor status bits
static const uint8_t E1000_RXD_STAT_DD   = (1u << 0); // Descriptor Done
static const uint8_t E1000_RXD_STAT_EOP  = (1u << 1); // End of Packet

// ================================================================
// Transmit Descriptor (E1000 legacy format, 16 bytes)
// ================================================================

struct TxDescriptor {
    uint64_t bufferAddr;    // physical address of packet data
    uint16_t length;        // data length
    uint8_t  cso;           // checksum offset
    uint8_t  cmd;           // command field
    volatile uint8_t status; // descriptor status written by the NIC
    uint8_t  css;           // checksum start
    uint16_t special;       // VLAN / special
} NIC_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef NIC_PACKED

// TX descriptor command bits
static const uint8_t E1000_TXD_CMD_EOP  = (1u << 0); // End of Packet
static const uint8_t E1000_TXD_CMD_IFCS = (1u << 1); // Insert FCS / CRC
static const uint8_t E1000_TXD_CMD_RS   = (1u << 3); // Report Status

// TX descriptor status bits
static const uint8_t E1000_TXD_STAT_DD  = (1u << 0); // Descriptor Done

// ================================================================
// Link state
// ================================================================

enum LinkState : uint8_t {
    NIC_LINK_DOWN = 0,
    NIC_LINK_UP   = 1,
    NIC_LINK_UNKNOWN = 2,
};

enum PhyAccessState : uint8_t {
    NIC_PHY_NOT_ATTEMPTED = 0,
    NIC_PHY_OK             = 1,
    NIC_PHY_FAILED         = 2,
};

enum InitStage : uint8_t {
    NIC_INIT_NONE = 0,
    NIC_INIT_BOUND,
    NIC_INIT_MMIO,
    NIC_INIT_PCI,
    NIC_INIT_RESET,
    NIC_INIT_MAC,
    NIC_INIT_PHY,
    NIC_INIT_RX_RING,
    NIC_INIT_TX_RING,
    NIC_INIT_READY,
};

inline const char* phy_access_state_name(PhyAccessState state)
{
    switch (state) {
        case NIC_PHY_OK:             return "ok";
        case NIC_PHY_FAILED:         return "failed";
        default:                     return "not attempted";
    }
}

inline const char* init_stage_name(InitStage stage)
{
    switch (stage) {
        case NIC_INIT_BOUND:         return "PCI binding";
        case NIC_INIT_MMIO:          return "MMIO";
        case NIC_INIT_PCI:           return "PCI activation";
        case NIC_INIT_RESET:         return "reset";
        case NIC_INIT_MAC:           return "MAC";
        case NIC_INIT_PHY:           return "PHY";
        case NIC_INIT_RX_RING:       return "RX ring";
        case NIC_INIT_TX_RING:       return "TX ring";
        case NIC_INIT_READY:         return "ready";
        default:                     return "none";
    }
}

// ================================================================
// NIC status codes
// ================================================================

enum Status : uint8_t {
    NIC_OK              = 0,
    NIC_ERR_NO_DEVICE   = 1,    // no NIC found on PCI bus
    NIC_ERR_INIT_FAIL   = 2,    // hardware init failed
    NIC_ERR_NO_LINK     = 3,    // link is down
    NIC_ERR_TX_FULL     = 4,    // transmit ring full
    NIC_ERR_RX_EMPTY    = 5,    // no received frames pending
    NIC_ERR_BUFFER_TOO_SMALL = 6,
    NIC_ERR_FRAME_TOO_LARGE = 7,
};

// ================================================================
// Network statistics
// ================================================================

struct NetStats {
    uint32_t txAttempted; // Valid frames submitted to the TX path
    uint32_t txFrames;
    uint32_t rxFrames;
    uint32_t rxObserved;  // Completed RX descriptors observed by the driver
    uint32_t txBytes;
    uint32_t rxBytes;
    uint32_t txErrors;
    uint32_t rxErrors;
    uint32_t txDropped;
    uint32_t rxDropped;
    uint32_t rxMalformed; // Bad/multi-descriptor/oversize RX frames
    uint32_t interrupts;
};

// ================================================================
// NIC device descriptor
// ================================================================

struct NICDevice {
    bool        active;
    uint8_t     pciBus;
    uint8_t     pciSlot;
    uint8_t     pciFunc;
    uint16_t    vendorId;
    uint16_t    deviceId;
    uint16_t    subsystemVendorId;
    uint16_t    subsystemDeviceId;
    uint8_t     revisionId;
    uint8_t     classCode;
    uint8_t     subclass;
    uint8_t     progIf;
    uint64_t    mmioBase;       // Selected register BAR (virtual after mapping)
    uint64_t    mmioPhys;       // Selected register BAR physical address
    uint64_t    mmioSize;       // Selected register BAR size when reported
    uint8_t     irqLine;        // PCI interrupt line
    uint8_t     macAddress[ETH_ALEN];
    LinkState   link;
    NetStats    stats;
    char        name[32];       // e.g. "eth0"
    bool        mmioMapped;     // true if MMIO is mapped by bootloader
    bool        irqRegistered;  // kernel IRQ handler registered
    bool        pollingEnabled; // receive path is drained by main-loop polling
    bool        driverBound;    // exact PCI match claimed by this driver
    bool        nicRegistered;  // ready NIC exposed to the network stack
    bool        rxRingInitialized;
    bool        txRingInitialized;
    uint16_t    pciCommand;
    uint32_t    ctrlValue;
    uint32_t    statusValue;
    uint32_t    mdicValue;
    uint16_t    phyStatusValue;
    uint16_t    phyId1;
    uint16_t    phyId2;
    uint8_t     phyAddress;
    uint16_t    negotiatedSpeed; // 10, 100, 1000, or 0 when unknown
    bool        negotiatedFullDuplex;
    PhyAccessState phyAccess;
    InitStage   initStage;
    uint8_t     phase5Stage;       // 0..8 for I219; 0xFF for other NICs
    bool        phase5Stopped;     // intentionally stopped or failed safely
    bool        interruptsEnabled; // hardware NIC interrupt mask is enabled
    char        lastInitFailure[96];
};

// Read-only identity record for a PCI network controller. This deliberately
// contains no BAR or command-register state: diagnostic enumeration must not
// probe, resize, or bind unsupported devices.
struct NetworkControllerInfo {
    uint8_t  pciBus;
    uint8_t  pciSlot;
    uint8_t  pciFunc;
    uint16_t vendorId;
    uint16_t deviceId;
    uint16_t subsystemVendorId;
    uint16_t subsystemDeviceId;
    uint8_t  revisionId;
    uint8_t  classCode;
    uint8_t  subclass;
    uint8_t  progIf;
    bool     supportedEthernet;
};

// ================================================================
// NIC BootInfo (passed from bootloader)
// Must match guideXOS::NicInfo in guidexOSBootInfo.h
// ================================================================

struct NicBootInfo {
    uint64_t mmioPhys;      // Physical selected register BAR address
    uint64_t mmioVirt;      // Virtual address (mapped by bootloader)
    uint64_t mmioSize;      // Size of MMIO region
    uint16_t vendorId;
    uint16_t deviceId;
    uint16_t subsystemVendorId;
    uint16_t subsystemDeviceId;
    uint8_t  revisionId;
    uint8_t  classCode;
    uint8_t  subclass;
    uint8_t  progIf;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint8_t  irqLine;
    uint8_t  macAddress[6];
    uint8_t  reserved0[2];
    uint32_t flags;
    uint32_t reserved1;
};

// NicBootInfo flags
static const uint32_t NIC_BOOT_FLAG_FOUND  = (1u << 0);
static const uint32_t NIC_BOOT_FLAG_MAPPED = (1u << 1);
static const uint32_t NIC_BOOT_FLAG_ACTIVE = (1u << 2);

// ================================================================
// Public API
// ================================================================

// Initialize NIC using BootInfo from bootloader (preferred method)
// Returns true if NIC was initialized successfully
bool init_from_bootinfo(const NicBootInfo* nicInfo);

// Validate an IEEE station address without consulting hardware.
// This is also used by hosted proof tests.
inline bool is_valid_station_mac(const uint8_t* mac)
{
    if (!mac) return false;

    bool allZero = true;
    bool allFF = true;
    for (uint8_t i = 0; i < ETH_ALEN; ++i) {
        if (mac[i] != 0x00) allZero = false;
        if (mac[i] != 0xFF) allFF = false;
    }

    return !allZero && !allFF && ((mac[0] & 0x01u) == 0u);
}

// Set the physical address where the kernel image was loaded.
void set_kernel_physical_base(uint64_t physicalBase);

// Scan PCI bus for network controllers and initialise the first
// supported NIC found.  Sets up RX/TX descriptor rings and
// enables the hardware.
void init();

// Return true if a NIC was found and initialised.
bool is_active();

// Return the NIC device info.
const NICDevice* get_device();

// Perform a bounded, read-only PCI scan and return network-controller
// identities, including unsupported PCI wireless controllers. No BAR is
// sized and no device is modified or claimed by this call.
uint8_t enumerate_network_controllers(NetworkControllerInfo* out,
                                      uint8_t capacity);

// Get the MAC address (6 bytes).
const uint8_t* get_mac_address();

// Get the cached link state.  This function is side-effect-free and must not
// perform MMIO/MDIC transactions because it is used by desktop redraw paths.
LinkState get_link_state();

// Record whether the kernel registered the device IRQ.  NIC causes remain
// masked until this is set true; exact I219 Stage 8 still defers hardware
// enablement until the main-loop readiness checkpoint.
void set_irq_registered(bool registered);

// Stage 8 only: enable the I219 hardware interrupt mask after the main-loop
// readiness checkpoint.  This is a no-op for earlier diagnostic stages and
// for existing non-I219 devices whose legacy ordering is preserved.
void enable_deferred_interrupts();

// ----------------------------------------------------------------
// Frame-level I/O
// ----------------------------------------------------------------

// Send a raw Ethernet frame (caller provides full frame including
// the 14-byte Ethernet header; FCS is appended by hardware).
// Returns NIC_OK on success.
Status send_frame(const uint8_t* data, uint16_t len);

// Receive a raw Ethernet frame into 'buffer'.
// On success, writes the frame (including 14-byte header, excluding
// FCS) and sets *received to the number of bytes written.
// Returns NIC_OK if a frame was available, NIC_ERR_RX_EMPTY if
// no frames are pending.
Status receive_frame(uint8_t* buffer, uint16_t max_len, uint16_t* received);

// ----------------------------------------------------------------
// Interrupt handler
// ----------------------------------------------------------------

// Called from the IRQ dispatch table when the NIC's interrupt fires.
void irq_handler();

// ----------------------------------------------------------------
// Statistics
// ----------------------------------------------------------------

// Return accumulated statistics.
const NetStats* get_stats();

} // namespace nic
} // namespace kernel

#endif // KERNEL_NIC_H
