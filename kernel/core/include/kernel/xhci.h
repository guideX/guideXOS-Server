//
// xHCI Controller Bring-up — Types, Constants, and Pure Logic
//
// Architecture-independent xHCI (eXtensible Host Controller Interface) types
// and helpers for bounded controller bring-up.  This header contains only
// pure logic (no port I/O and no MMIO) so every layer can be exercised by
// deterministic hosted tests.
//
// Standards reference:
//   - eXtensible Host Controller Interface for USB (xHCI) specification,
//     revision 1.1 (May 2011), sections:
//       5.3  Capability Registers
//       5.4  Operational Registers
//       5.5  Runtime Registers
//       5.6  Doorbell Registers
//       6.1  Device Context Base Address Array
//       6.2.5 Command Ring
//       6.2.6 Event Ring
//       7.2  USB Legacy Support (extended capability)
//       7.4  USB Supported Protocol (extended capability)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"
#include "kernel/usb_controller.h"

namespace kernel {
namespace usb {
namespace xhci {

// ================================================================
// TRB (Transfer Request Block) types
// ================================================================

enum TrbType : uint8_t {
    TRB_TYPE_NORMAL          = 1,
    TRB_TYPE_SETUP_STAGE     = 2,
    TRB_TYPE_DATA_STAGE      = 3,
    TRB_TYPE_STATUS_STAGE    = 4,
    TRB_TYPE_ISOCH           = 5,
    TRB_TYPE_LINK            = 6,
    TRB_TYPE_EVENT_DATA      = 7,
    TRB_TYPE_NO_OP           = 8,
    TRB_TYPE_ENABLE_SLOT     = 9,
    TRB_TYPE_DISABLE_SLOT    = 10,
    TRB_TYPE_ADDRESS_DEVICE  = 11,
    TRB_TYPE_CONFIGURE_EP    = 12,
    TRB_TYPE_EVAL_CONTEXT    = 13,
    TRB_TYPE_RESET_ENDPOINT  = 14,
    TRB_TYPE_STOP_ENDPOINT   = 15,
    TRB_TYPE_SET_TR_DEQUEUE  = 16,
    TRB_TYPE_RESET_DEVICE    = 17,
    TRB_TYPE_FORCE_EVENT     = 18,
    TRB_TYPE_NEGOTIATE_BW    = 19,
    TRB_TYPE_SET_LATENCY     = 20,
    TRB_TYPE_GET_PORT_BW     = 21,
    TRB_TYPE_FORCE_HEADER    = 22,
    TRB_TYPE_NO_OP_CMD      = 23,
    TRB_TYPE_GET_EXT_PROPERTY = 24,
    TRB_TYPE_SET_EXT_PROPERTY = 25,
    // Event TRBs (32..63)
    TRB_TYPE_TRANSFER_EVENT  = 32,
    TRB_TYPE_COMMAND_COMP    = 33,
    TRB_TYPE_PORT_STATUS_CHANGE = 34,
    TRB_TYPE_HOST_CONTROLLER  = 37,
    TRB_TYPE_DEVICE_NOTIFICATION = 38,
    TRB_TYPE_MFINDEX_WRAP    = 39,
};

// ================================================================
// TRB completion codes
// ================================================================

enum TrbCompletionCode : uint8_t {
    CC_SUCCESS              = 1,
    CC_DATA_BUFFER_ERROR    = 2,
    CC_BABBLE_DETECTED      = 3,
    CC_USB_TRANSACTION_ERROR = 4,
    CC_TRB_ERROR            = 5,
    CC_STALL_ERROR          = 6,
    CC_RESOURCE_ERROR       = 7,
    CC_BANDWIDTH_ERROR      = 8,
    CC_NO_SLOTS_ERROR       = 9,
    CC_INVALID_STREAM_TYPE  = 10,
    CC_SLOT_NOT_ENABLED     = 11,
    CC_ENDPOINT_NOT_ENABLED = 12,
    CC_SHORT_PACKET         = 13,
    CC_RING_UNDERRUN        = 14,
    CC_RING_OVERRUN         = 15,
    CC_VF_EVENT_RING_FULL   = 16,
    CC_PARAMETER_ERROR      = 17,
    CC_BANDWIDTH_OVERRUN    = 18,
    CC_CONTEXT_STATE_ERROR  = 19,
    CC_NO_PING_RESPONSE     = 20,
    CC_EVENT_RING_FULL      = 21,
    CC_INCOMPATIBLE_DEVICE  = 22,
    CC_MISSED_SERVICE_ERROR = 23,
    CC_COMMAND_RING_STOPPED = 24,
    CC_COMMAND_ABORTED      = 25,
    CC_STOPPED              = 26,
    CC_STOPPED_LENGTH_INVALID = 27,
    CC_STOPPED_SHORT_PACKET = 28,
    CC_MAX_EXIT_LATENCY_ERROR = 29,
    CC_ISOCH_BUFFER_OVERRUN = 31,
    CC_EVENT_LOST_ERROR     = 32,
    CC_UNDEFINED_ERROR      = 33,
    CC_INVALID_STREAM_ID    = 34,
    CC_SECONDARY_BANDWIDTH  = 35,
    CC_SPLIT_TRANSACTION_ERROR = 36,
};

// ================================================================
// TRB structures (16 bytes each, packed)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define XHCI_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define XHCI_PACKED
#endif

struct XhciTrb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} XHCI_PACKED;

struct XhciEventTrb {
    uint64_t eventData;
    uint32_t status;
    uint32_t control;
} XHCI_PACKED;

struct XhciLinkTrb {
    uint64_t ringSegmentPtr;
    uint32_t status;
    uint32_t control;
} XHCI_PACKED;

struct XhciCommandCompletionEvent {
    uint64_t commandTrbPtr;
    uint32_t completionCode : 8;
    uint32_t reserved : 24;
    uint32_t control;
} XHCI_PACKED;

struct XhciPortStatusChangeEvent {
    uint32_t portId : 8;
    uint32_t reserved : 24;
    uint32_t reserved2;
    uint32_t completionCode : 8;
    uint32_t reserved3 : 24;
    uint32_t control;
} XHCI_PACKED;

struct XhciTransferEvent {
    uint64_t trbPtr;
    uint32_t transferLength : 24;
    uint32_t completionCode : 8;
    uint32_t control;
} XHCI_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef XHCI_PACKED

// ================================================================
// Operational register offsets (from opBase)
// ================================================================

static const uint32_t XHCI_OP_USBCMD      = 0x00;
static const uint32_t XHCI_OP_USBSTS      = 0x04;
static const uint32_t XHCI_OP_PAGESIZE    = 0x08;
static const uint32_t XHCI_OP_DNCTRL      = 0x14;
static const uint32_t XHCI_OP_CRCR        = 0x18;
static const uint32_t XHCI_OP_DCBAAP     = 0x30;
static const uint32_t XHCI_OP_CONFIG      = 0x38;

// USBCMD bits
static const uint32_t XHCI_USBCMD_RUN_STOP = 0x00000001;
static const uint32_t XHCI_USBCMD_HCRST    = 0x00000002;
static const uint32_t XHCI_USBCMD_INTE     = 0x00000004;
static const uint32_t XHCI_USBCMD_HSEE     = 0x00000008;

// USBSTS bits
static const uint32_t XHCI_USBSTS_HCHALTED = 0x00000001;
static const uint32_t XHCI_USBSTS_HSE      = 0x00000004;
static const uint32_t XHCI_USBSTS_EINT     = 0x00000008;
static const uint32_t XHCI_USBSTS_PCD      = 0x00000010;
static const uint32_t XHCI_USBSTS_CNR      = 0x00000800;

// CONFIG bits
static const uint32_t XHCI_CONFIG_MAXSLOTS = 0x000000FF;

// CRCR bits
static const uint64_t XHCI_CRCR_RCS       = 0x00000001;
static const uint64_t XHCI_CRCR_CS        = 0x00000002;
static const uint64_t XHCI_CRCR_CA        = 0x00000004;
static const uint64_t XHCI_CRCR_CRR       = 0x00000008;

// DCBAAP bits
static const uint64_t XHCI_DCBAAP_PTR_MASK = 0xFFFFFFFFFFFFFFC0ULL;

// ================================================================
// Runtime register offsets (from rtBase)
// ================================================================

static const uint32_t XHCI_RT_MFINDEX     = 0x00;
static const uint32_t XHCI_RT_IMAN        = 0x00; // IMAN is at rtBase + 0x20
static const uint32_t XHCI_RT_IMOD        = 0x04; // IMOD is at rtBase + 0x24
static const uint32_t XHCI_RT_ERSTSZ      = 0x08; // ERSTSZ is at rtBase + 0x28
static const uint32_t XHCI_RT_ERSTBA      = 0x10; // ERSTBA is at rtBase + 0x30
static const uint32_t XHCI_RT_ERDP        = 0x18; // ERDP is at rtBase + 0x38

// IMAN bits
static const uint32_t XHCI_IMAN_IP       = 0x00000001;
static const uint32_t XHCI_IMAN_IE       = 0x00000002;

// ERSTBA bits
static const uint64_t XHCI_ERSTBA_PTR_MASK = 0xFFFFFFFFFFFFFFC0ULL;

// ERDP bits
static const uint64_t XHCI_ERDP_PTR_MASK   = 0xFFFFFFFFFFFFFFF0ULL;
static const uint64_t XHCI_ERDP_EHB        = 0x00000008;

// ================================================================
// Port register offsets (from opBase + 0x400 + (portIndex-1)*0x10)
// ================================================================

static const uint32_t XHCI_PORT_SC       = 0x00; // Port Status and Control
static const uint32_t XHCI_PORT_PM_SC     = 0x04; // Port Power Management Status and Control
static const uint32_t XHCI_PORT_LI       = 0x08; // Port Link Info
static const uint32_t XHCI_PORT_HLPMC    = 0x0C; // Port Hardware LPM Control

// PORTSC bits
static const uint32_t XHCI_PORTSC_CCS    = 0x00000001; // Current Connect Status
static const uint32_t XHCI_PORTSC_PED    = 0x00000002; // Port Enabled/Disabled
static const uint32_t XHCI_PORTSC_OCA    = 0x00000008; // Over-current Active
static const uint32_t XHCI_PORTSC_PR     = 0x00000010; // Port Reset
static const uint32_t XHCI_PORTSC_PLS    = 0x000001E0; // Port Link State (bits 5:8)
static const uint32_t XHCI_PORTSC_PP     = 0x00000200; // Port Power
static const uint32_t XHCI_PORTSC_SPEED  = 0x00003C00; // Port Speed (bits 10:13)
static const uint32_t XHCI_PORTSC_PIC    = 0x0000C000; // Port Indicator Control (bits 14:15)
static const uint32_t XHCI_PORTSC_LWS    = 0x00010000; // Port Link State Write Strobe
static const uint32_t XHCI_PORTSC_CSC    = 0x00020000; // Connect Status Change
static const uint32_t XHCI_PORTSC_PEC    = 0x00040000; // Port Enabled/Disabled Change
static const uint32_t XHCI_PORTSC_WRC    = 0x00080000; // Warm Port Reset Change
static const uint32_t XHCI_PORTSC_OCC    = 0x00100000; // Over-current Change
static const uint32_t XHCI_PORTSC_PRC    = 0x00200000; // Port Reset Change
static const uint32_t XHCI_PORTSC_PLC    = 0x00400000; // Port Link State Change
static const uint32_t XHCI_PORTSC_CEC    = 0x00800000; // Port Config Error Change
static const uint32_t XHCI_PORTSC_CAS    = 0x01000000; // Cold Attach Status
static const uint32_t XHCI_PORTSC_WCE    = 0x02000000; // Wake on Connect Enable
static const uint32_t XHCI_PORTSC_WDE    = 0x04000000; // Wake on Disconnect Enable
static const uint32_t XHCI_PORTSC_WOE    = 0x08000000; // Wake on Over-current Enable
static const uint32_t XHCI_PORTSC_DR     = 0x40000000; // Device Removable
static const uint32_t XHCI_PORTSC_WPR    = 0x80000000; // Warm Port Reset

// Port Link State values
static const uint32_t XHCI_PLS_U0        = 0x0;
static const uint32_t XHCI_PLS_U1        = 0x1;
static const uint32_t XHCI_PLS_U2        = 0x2;
static const uint32_t XHCI_PLS_U3        = 0x3;
static const uint32_t XHCI_PLS_DISABLED  = 0x4;
static const uint32_t XHCI_PLS_RX_DETECT = 0x5;
static const uint32_t XHCI_PLS_INACTIVE  = 0x6;
static const uint32_t XHCI_PLS_POLLING   = 0x7;
static const uint32_t XHCI_PLS_RECOVERY  = 0x8;
static const uint32_t XHCI_PLS_HOT_RESET = 0x9;
static const uint32_t XHCI_PLS_COMPLIANCE = 0xA;
static const uint32_t XHCI_PLS_TEST_MODE = 0xB;
static const uint32_t XHCI_PLS_RESUME    = 0xF;

// Port Speed values (PORTSC bits 10:13)
static const uint32_t XHCI_SPEED_FULL    = 1;  // 12 Mbps
static const uint32_t XHCI_SPEED_LOW     = 2;  // 1.5 Mbps
static const uint32_t XHCI_SPEED_HIGH    = 3;  // 480 Mbps
static const uint32_t XHCI_SPEED_SUPER  = 4;  // 5 Gbps
static const uint32_t XHCI_SPEED_SUPER_PLUS = 5; // 10 Gbps

// ================================================================
// Event Ring Segment Table entry (16 bytes)
// ================================================================

struct XhciErstEntry {
    uint64_t ringSegmentBaseAddr;
    uint32_t ringSegmentSize;
    uint32_t reserved;
} XHCI_PACKED;

// ================================================================
// USB Legacy Support extended capability
// ================================================================

static const uint32_t XHCI_USBLEGSUP_CAPID = 0x01;
static const uint32_t XHCI_USBLEGSUP_BIOS_OWNED = 0x00000100;
static const uint32_t XHCI_USBLEGSUP_OS_OWNED   = 0x00000001;
static const uint32_t XHCI_USBLEGSUP_HC_OS_OWNED_SEM = 0x00010000;
static const uint32_t XHCI_USBLEGSUP_HC_BIOS_OWNED_SEM = 0x00020000;

// ================================================================
// Controller state machine
// ================================================================

enum class XhciControllerState : uint8_t {
    Idle = 0,
    Halted,
    Reset,
    Ready,
    Running,
    Error,
};

// ================================================================
// Ownership state
// ================================================================

enum class XhciOwnershipState : uint8_t {
    NoCapability = 0,
    BiosOwned,
    OsOwned,
    HandoffTimeout,
};

// ================================================================
// Port status snapshot
// ================================================================

struct XhciPortStatus {
    uint8_t  portIndex;       // 1-based
    bool     connected;       // CCS
    bool     enabled;         // PED
    bool     powered;         // PP
    uint8_t  speed;           // PORTSC speed field
    uint8_t  linkState;       // PLS
    bool     connectChange;   // CSC
    bool     resetChange;     // PRC
    bool     linkStateChange; // PLC
    bool     configError;     // CEC
    bool     overCurrent;     // OCA
    bool     warmReset;       // WPR
    bool     deviceRemovable; // DR
};

// ================================================================
// Pure logic helpers
// ================================================================

inline uint32_t trb_type(const XhciTrb& trb)
{
    return (trb.control >> 10) & 0x3F;
}

inline uint32_t trb_cycle_bit(const XhciTrb& trb)
{
    return trb.control & 0x01;
}

inline uint32_t trb_completion_code(const XhciEventTrb& trb)
{
    return (trb.status >> 24) & 0xFF;
}

inline uint32_t trb_event_data(const XhciEventTrb& trb)
{
    return (uint32_t)(trb.eventData & 0xFFFFFFFF);
}

inline bool trb_is_port_status_change(const XhciEventTrb& trb)
{
    return trb_type(reinterpret_cast<const XhciTrb&>(trb)) == TRB_TYPE_PORT_STATUS_CHANGE;
}

inline uint8_t trb_port_id(const XhciPortStatusChangeEvent& evt)
{
    return (uint8_t)(evt.portId & 0xFF);
}

inline uint32_t portsc_get_pls(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_PLS) >> 5;
}

inline uint32_t portsc_get_speed(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_SPEED) >> 10;
}

inline bool portsc_is_connected(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_CCS) != 0;
}

inline bool portsc_is_enabled(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_PED) != 0;
}

inline bool portsc_is_powered(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_PP) != 0;
}

inline bool portsc_is_connect_change(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_CSC) != 0;
}

inline bool portsc_is_reset_change(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_PRC) != 0;
}

inline bool portsc_is_link_change(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_PLC) != 0;
}

inline bool portsc_is_config_error(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_CEC) != 0;
}

inline bool portsc_is_over_current(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_OCA) != 0;
}

inline bool portsc_is_warm_reset(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_WPR) != 0;
}

inline bool portsc_is_removable(uint32_t portsc)
{
    return (portsc & XHCI_PORTSC_DR) != 0;
}

inline XhciPortStatus decode_port_status(uint8_t portIndex, uint32_t portsc)
{
    XhciPortStatus ps;
    ps.portIndex       = portIndex;
    ps.connected       = portsc_is_connected(portsc);
    ps.enabled         = portsc_is_enabled(portsc);
    ps.powered         = portsc_is_powered(portsc);
    ps.speed           = (uint8_t)portsc_get_speed(portsc);
    ps.linkState       = (uint8_t)portsc_get_pls(portsc);
    ps.connectChange   = portsc_is_connect_change(portsc);
    ps.resetChange     = portsc_is_reset_change(portsc);
    ps.linkStateChange = portsc_is_link_change(portsc);
    ps.configError     = portsc_is_config_error(portsc);
    ps.overCurrent     = portsc_is_over_current(portsc);
    ps.warmReset       = portsc_is_warm_reset(portsc);
    ps.deviceRemovable = portsc_is_removable(portsc);
    return ps;
}

// ================================================================
// Ring helpers
// ================================================================

static const uint32_t XHCI_RING_MAX_SIZE = 256;
static const uint32_t XHCI_RING_ALIGNMENT = 64;
static const uint32_t XHCI_TRB_SIZE = 16;

inline bool ring_size_valid(uint32_t size)
{
    return size >= 2 && size <= XHCI_RING_MAX_SIZE;
}

inline uint32_t ring_index(uint32_t index, uint32_t size)
{
    return index & (size - 1);
}

inline bool ring_is_power_of_two(uint32_t size)
{
    return size != 0 && (size & (size - 1)) == 0;
}

// ================================================================
// ERST helpers
// ================================================================

static const uint32_t XHCI_ERST_MAX_ENTRIES = 256;
static const uint32_t XHCI_ERST_ALIGNMENT = 64;

inline bool erst_size_valid(uint32_t size)
{
    return size >= 1 && size <= XHCI_ERST_MAX_ENTRIES;
}

inline uint64_t erst_encode_base(uint64_t base)
{
    return base & XHCI_ERSTBA_PTR_MASK;
}

// ================================================================
// PCI BAR validation
// ================================================================

struct XhciBarInfo {
    bool     valid;
    bool     is64;
    uint64_t base;
    uint32_t minSize; // minimum MMIO span required
};

inline XhciBarInfo validate_mmio_bar(uint32_t bar0, uint32_t bar1,
                                      uint32_t minSize)
{
    XhciBarInfo info;
    info.valid = false;
    info.is64 = false;
    info.base = 0;
    info.minSize = minSize;

    if (bar0 == 0 || bar0 == 0xFFFFFFFF) {
        return info;
    }

    if (bar0 & 0x01) {
        return info; // I/O BAR
    }

    const uint32_t barType = (bar0 >> 1) & 0x03;
    if (barType == 0x02) {
        info.is64 = true;
        if (bar1 == 0 || bar1 == 0xFFFFFFFF) {
            return info;
        }
        info.base = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0);
    } else if (barType == 0x00) {
        info.is64 = false;
        info.base = bar0 & 0xFFFFFFF0;
    } else {
        return info; // reserved type
    }

    if (info.base == 0 || info.base == 0xFFFFFFFF) {
        return info;
    }

    info.valid = true;
    return info;
}

// ================================================================
// Ownership detection
// ================================================================

inline XhciOwnershipState detect_ownership(uint32_t legsup)
{
    if ((legsup & XHCI_USBLEGSUP_HC_BIOS_OWNED_SEM) != 0) {
        return XhciOwnershipState::BiosOwned;
    }
    if ((legsup & XHCI_USBLEGSUP_HC_OS_OWNED_SEM) != 0) {
        return XhciOwnershipState::OsOwned;
    }
    return XhciOwnershipState::NoCapability;
}

// ================================================================
// Link state name
// ================================================================

inline const char* link_state_name(uint8_t pls)
{
    switch (pls) {
        case XHCI_PLS_U0:         return "U0";
        case XHCI_PLS_U1:         return "U1";
        case XHCI_PLS_U2:         return "U2";
        case XHCI_PLS_U3:         return "U3";
        case XHCI_PLS_DISABLED:   return "Disabled";
        case XHCI_PLS_RX_DETECT:  return "RxDetect";
        case XHCI_PLS_INACTIVE:   return "Inactive";
        case XHCI_PLS_POLLING:    return "Polling";
        case XHCI_PLS_RECOVERY:   return "Recovery";
        case XHCI_PLS_HOT_RESET:  return "HotReset";
        case XHCI_PLS_COMPLIANCE: return "Compliance";
        case XHCI_PLS_TEST_MODE:  return "TestMode";
        case XHCI_PLS_RESUME:     return "Resume";
        default:                  return "Unknown";
    }
}

// ================================================================
// Speed name
// ================================================================

inline const char* speed_name(uint8_t speed)
{
    switch (speed) {
        case XHCI_SPEED_FULL:       return "Full";
        case XHCI_SPEED_LOW:        return "Low";
        case XHCI_SPEED_HIGH:       return "High";
        case XHCI_SPEED_SUPER:     return "Super";
        case XHCI_SPEED_SUPER_PLUS: return "SuperPlus";
        default:                    return "Unknown";
    }
}

// ================================================================
// Completion code name
// ================================================================

inline const char* completion_code_name(uint8_t cc)
{
    switch (cc) {
        case CC_SUCCESS:              return "Success";
        case CC_DATA_BUFFER_ERROR:    return "DataBufferError";
        case CC_BABBLE_DETECTED:      return "BabbleDetected";
        case CC_USB_TRANSACTION_ERROR: return "UsbTransactionError";
        case CC_TRB_ERROR:            return "TrbError";
        case CC_STALL_ERROR:          return "StallError";
        case CC_RESOURCE_ERROR:       return "ResourceError";
        case CC_BANDWIDTH_ERROR:      return "BandwidthError";
        case CC_NO_SLOTS_ERROR:       return "NoSlotsError";
        case CC_INVALID_STREAM_TYPE:  return "InvalidStreamType";
        case CC_SLOT_NOT_ENABLED:     return "SlotNotEnabled";
        case CC_ENDPOINT_NOT_ENABLED: return "EndpointNotEnabled";
        case CC_SHORT_PACKET:         return "ShortPacket";
        case CC_RING_UNDERRUN:        return "RingUnderrun";
        case CC_RING_OVERRUN:         return "RingOverrun";
        case CC_VF_EVENT_RING_FULL:   return "VfEventRingFull";
        case CC_PARAMETER_ERROR:      return "ParameterError";
        case CC_BANDWIDTH_OVERRUN:    return "BandwidthOverrun";
        case CC_CONTEXT_STATE_ERROR:  return "ContextStateError";
        case CC_NO_PING_RESPONSE:     return "NoPingResponse";
        case CC_EVENT_RING_FULL:      return "EventRingFull";
        case CC_INCOMPATIBLE_DEVICE:  return "IncompatibleDevice";
        case CC_MISSED_SERVICE_ERROR: return "MissedServiceError";
        case CC_COMMAND_RING_STOPPED: return "CommandRingStopped";
        case CC_COMMAND_ABORTED:      return "CommandAborted";
        case CC_STOPPED:              return "Stopped";
        case CC_STOPPED_LENGTH_INVALID: return "StoppedLengthInvalid";
        case CC_STOPPED_SHORT_PACKET: return "StoppedShortPacket";
        case CC_MAX_EXIT_LATENCY_ERROR: return "MaxExitLatencyError";
        case CC_ISOCH_BUFFER_OVERRUN: return "IsochBufferOverrun";
        case CC_EVENT_LOST_ERROR:     return "EventLostError";
        case CC_UNDEFINED_ERROR:      return "UndefinedError";
        case CC_INVALID_STREAM_ID:    return "InvalidStreamId";
        case CC_SECONDARY_BANDWIDTH:  return "SecondaryBandwidth";
        case CC_SPLIT_TRANSACTION_ERROR: return "SplitTransactionError";
        default:                      return "Unknown";
    }
}

// ================================================================
// TRB type name
// ================================================================

inline const char* trb_type_name(uint8_t type)
{
    switch (type) {
        case TRB_TYPE_NORMAL:          return "Normal";
        case TRB_TYPE_SETUP_STAGE:     return "SetupStage";
        case TRB_TYPE_DATA_STAGE:      return "DataStage";
        case TRB_TYPE_STATUS_STAGE:    return "StatusStage";
        case TRB_TYPE_ISOCH:           return "Isoch";
        case TRB_TYPE_LINK:            return "Link";
        case TRB_TYPE_EVENT_DATA:      return "EventData";
        case TRB_TYPE_NO_OP:           return "NoOp";
        case TRB_TYPE_ENABLE_SLOT:     return "EnableSlot";
        case TRB_TYPE_DISABLE_SLOT:    return "DisableSlot";
        case TRB_TYPE_ADDRESS_DEVICE:  return "AddressDevice";
        case TRB_TYPE_CONFIGURE_EP:    return "ConfigureEp";
        case TRB_TYPE_EVAL_CONTEXT:    return "EvalContext";
        case TRB_TYPE_RESET_ENDPOINT:  return "ResetEndpoint";
        case TRB_TYPE_STOP_ENDPOINT:   return "StopEndpoint";
        case TRB_TYPE_SET_TR_DEQUEUE:  return "SetTrDequeue";
        case TRB_TYPE_RESET_DEVICE:    return "ResetDevice";
        case TRB_TYPE_FORCE_EVENT:     return "ForceEvent";
        case TRB_TYPE_NEGOTIATE_BW:    return "NegotiateBw";
        case TRB_TYPE_SET_LATENCY:     return "SetLatency";
        case TRB_TYPE_GET_PORT_BW:     return "GetPortBw";
        case TRB_TYPE_FORCE_HEADER:    return "ForceHeader";
        case TRB_TYPE_NO_OP_CMD:      return "NoOpCmd";
        case TRB_TYPE_GET_EXT_PROPERTY: return "GetExtProperty";
        case TRB_TYPE_SET_EXT_PROPERTY: return "SetExtProperty";
        case TRB_TYPE_TRANSFER_EVENT:  return "TransferEvent";
        case TRB_TYPE_COMMAND_COMP:    return "CommandCompletion";
        case TRB_TYPE_PORT_STATUS_CHANGE: return "PortStatusChange";
        case TRB_TYPE_HOST_CONTROLLER:  return "HostController";
        case TRB_TYPE_DEVICE_NOTIFICATION: return "DeviceNotification";
        case TRB_TYPE_MFINDEX_WRAP:    return "MfindexWrap";
        default:                       return "Unknown";
    }
}

// ================================================================
// Controller bring-up interface (implemented in xhci.cpp)
// ================================================================

namespace controller {

// Initialize the xHCI controller: validate BAR, acquire ownership, reset,
// configure rings, start the controller.  Returns true on success.
bool init();

// Return true if the controller is running.
bool is_running();

// Poll the event ring for new events.  Returns the number of events processed.
uint32_t poll_events();

// Get the current controller state.
XhciControllerState get_state();

// Get the ownership state.
XhciOwnershipState get_ownership_state();

// Get the number of root ports.
uint8_t port_count();

// Read the status of a specific root port (1-based).
bool port_status(uint8_t portIndex, XhciPortStatus* out);

// Get the MMIO base address (for diagnostics).
uint64_t mmio_base();

// Get the validated capability registers.
const XhciCapabilities* capabilities();

} // namespace controller

} // namespace xhci
} // namespace usb
} // namespace kernel
