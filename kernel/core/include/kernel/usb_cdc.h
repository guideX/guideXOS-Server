// USB CDC (Communications Device Class) Driver
//
// Supports:
//   - CDC ACM (Abstract Control Model) — virtual serial ports
//   - CDC ECM (Ethernet Control Model) — USB-to-Ethernet adapters
//   - Line coding (baud rate, data bits, parity, stop bits)
//   - Notification endpoint for serial state changes
//
// Reference: USB CDC 1.2, CDC PSTN Subclass 1.2
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_CDC_H
#define KERNEL_USB_CDC_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_cdc {

// ================================================================
// CDC class-specific requests
// ================================================================

enum CDCRequest : uint8_t {
    CDC_REQ_SET_LINE_CODING        = 0x20,
    CDC_REQ_GET_LINE_CODING        = 0x21,
    CDC_REQ_SET_CONTROL_LINE_STATE = 0x22,
    CDC_REQ_SEND_BREAK             = 0x23,
    CDC_REQ_SET_ETHERNET_MULTICAST = 0x40,
    CDC_REQ_SET_ETHERNET_PM_FILTER = 0x41,
    CDC_REQ_GET_ETHERNET_PM_FILTER = 0x42,
    CDC_REQ_SET_ETHERNET_PKT_FILT  = 0x43,
    CDC_REQ_GET_ETHERNET_STATISTIC = 0x44,
};

// ================================================================
// Line coding structure (7 bytes)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define CDC_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define CDC_PACKED
#endif

struct LineCoding {
    uint32_t dwDTERate;       // baud rate
    uint8_t  bCharFormat;     // 0=1 stop, 1=1.5 stop, 2=2 stop
    uint8_t  bParityType;     // 0=none, 1=odd, 2=even, 3=mark, 4=space
    uint8_t  bDataBits;       // 5, 6, 7, 8, or 16
} CDC_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef CDC_PACKED

// ================================================================
// Parity type constants
// ================================================================

enum Parity : uint8_t {
    PARITY_NONE  = 0,
    PARITY_ODD   = 1,
    PARITY_EVEN  = 2,
    PARITY_MARK  = 3,
    PARITY_SPACE = 4,
};

// ================================================================
// Control line state bits (for SET_CONTROL_LINE_STATE)
// ================================================================

enum ControlLineState : uint16_t {
    LINE_STATE_DTR = 0x0001,
    LINE_STATE_RTS = 0x0002,
};

// ================================================================
// Notification types (from interrupt IN)
// ================================================================

enum CDCNotification : uint8_t {
    CDC_NOTIFY_NETWORK_CONNECTION = 0x00,
    CDC_NOTIFY_SERIAL_STATE       = 0x20,
    CDC_NOTIFY_SPEED_CHANGE       = 0x2A,
};

// ================================================================
// Serial state bits (from SERIAL_STATE notification)
// ================================================================

enum SerialState : uint16_t {
    SERIAL_STATE_DCD       = 0x0001,  // carrier detect
    SERIAL_STATE_DSR       = 0x0002,  // data set ready
    SERIAL_STATE_BREAK     = 0x0004,
    SERIAL_STATE_RI        = 0x0008,  // ring indicator
    SERIAL_STATE_FRAMING   = 0x0010,
    SERIAL_STATE_PARITY    = 0x0020,
    SERIAL_STATE_OVERRUN   = 0x0040,
};

// ================================================================
// CDC ACM device instance
// ================================================================

struct CDCACMDevice {
    bool     active;
    uint8_t  usbAddress;
    uint8_t  commInterface;    // CDC control interface number
    uint8_t  dataInterface;    // CDC data interface number
    uint8_t  bulkInEP;
    uint8_t  bulkOutEP;
    uint8_t  notifyEP;         // interrupt IN for notifications
    uint16_t bulkInMaxPkt;
    uint16_t bulkOutMaxPkt;
    uint16_t notifyMaxPkt;
    LineCoding lineCoding;
    uint16_t controlLineState;
    uint16_t serialState;
};

// ================================================================
// CDC ECM device instance
// ================================================================

struct CDCECMDevice {
    bool     active;
    uint8_t  usbAddress;
    uint8_t  commInterface;
    uint8_t  dataInterface;
    uint8_t  bulkInEP;
    uint8_t  bulkOutEP;
    uint8_t  notifyEP;
    uint16_t bulkInMaxPkt;
    uint16_t bulkOutMaxPkt;
    uint16_t maxSegmentSize;
    uint8_t  macAddress[6];
    bool     connected;
};

static const uint8_t MAX_CDC_ACM_DEVICES = 4;
static const uint8_t MAX_CDC_ECM_DEVICES = 2;

// ================================================================
// Public API — ACM (serial)
// ================================================================

void init();

// Probe a USB device for CDC interfaces. Returns true if claimed.
bool probe(uint8_t usbAddress);

// Release CDC interfaces on a device (on detach).
void release(uint8_t usbAddress);

// ----------------------------------------------------------------
// ACM serial I/O
// ----------------------------------------------------------------

// Return number of active ACM (serial) devices.
uint8_t acm_count();

// Get/set line coding (baud rate, etc.).
usb::TransferStatus acm_get_line_coding(uint8_t devIndex, LineCoding* lc);
usb::TransferStatus acm_set_line_coding(uint8_t devIndex, const LineCoding* lc);

// Set DTR/RTS control line state.
usb::TransferStatus acm_set_control_lines(uint8_t devIndex, uint16_t state);

// Send a break signal (durationMs = 0 to clear).
usb::TransferStatus acm_send_break(uint8_t devIndex, uint16_t durationMs);

// Write data to the serial port.
usb::TransferStatus acm_write(uint8_t devIndex,
                               const void* data, uint16_t len,
                               uint16_t* written);

// Read data from the serial port.
usb::TransferStatus acm_read(uint8_t devIndex,
                              void* data, uint16_t len,
                              uint16_t* bytesRead);

// Poll notification endpoint for serial state updates.
void acm_poll_notifications(uint8_t devIndex);

// Get the latest serial state.
uint16_t acm_get_serial_state(uint8_t devIndex);

// ----------------------------------------------------------------
// ECM Ethernet I/O
// ----------------------------------------------------------------

// Return number of active ECM (Ethernet) devices.
uint8_t ecm_count();

// Send an Ethernet frame.
usb::TransferStatus ecm_send(uint8_t devIndex,
                              const void* frame, uint16_t len);

// Receive an Ethernet frame.
usb::TransferStatus ecm_receive(uint8_t devIndex,
                                 void* frame, uint16_t maxLen,
                                 uint16_t* received);

// Get the MAC address of an ECM device.
const uint8_t* ecm_get_mac(uint8_t devIndex);

// Check if the ECM link is connected.
bool ecm_is_connected(uint8_t devIndex);

} // namespace usb_cdc
} // namespace kernel

#endif // KERNEL_USB_CDC_H
