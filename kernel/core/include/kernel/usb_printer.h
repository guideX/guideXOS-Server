// USB Printer Class Driver
//
// Supports:
//   - Unidirectional and bidirectional printing
//   - IEEE 1284 Device ID retrieval
//   - Port status (paper empty, selected, error)
//   - Soft reset
//
// Reference: USB Printer Class Definition 1.1
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_PRINTER_H
#define KERNEL_USB_PRINTER_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_printer {

// ================================================================
// Printer class-specific requests
// ================================================================

enum PrinterRequest : uint8_t {
    PRINTER_REQ_GET_DEVICE_ID  = 0x00,
    PRINTER_REQ_GET_PORT_STATUS = 0x01,
    PRINTER_REQ_SOFT_RESET     = 0x02,
};

// ================================================================
// Port status bits (from GET_PORT_STATUS)
// ================================================================

enum PortStatusBit : uint8_t {
    PORT_STATUS_NOT_ERROR   = 0x08,  // 1 = no error
    PORT_STATUS_SELECTED    = 0x10,  // 1 = selected (online)
    PORT_STATUS_PAPER_EMPTY = 0x20,  // 1 = paper empty
};

// ================================================================
// Printer protocol types
// ================================================================

enum PrinterProtocol : uint8_t {
    PRINTER_UNIDIRECTIONAL  = 0x01,
    PRINTER_BIDIRECTIONAL   = 0x02,
    PRINTER_1284_4_BIDIR    = 0x03,  // IEEE 1284.4 compatible bidirectional
};

// ================================================================
// Printer device instance
// ================================================================

static const uint16_t MAX_DEVICE_ID_LEN = 256;

struct PrinterDevice {
    bool     active;
    uint8_t  usbAddress;
    uint8_t  interfaceNum;
    uint8_t  protocol;        // PrinterProtocol
    uint8_t  bulkOutEP;       // bulk OUT endpoint (host ? printer)
    uint8_t  bulkInEP;        // bulk IN endpoint (printer ? host, bidirectional only)
    uint16_t bulkOutMaxPkt;
    uint16_t bulkInMaxPkt;
    uint8_t  portStatus;
    char     deviceId[MAX_DEVICE_ID_LEN]; // IEEE 1284 Device ID string
    uint16_t deviceIdLen;
};

static const uint8_t MAX_PRINTER_DEVICES = 2;

// ================================================================
// Public API
// ================================================================

// Initialise the printer class driver.
void init();

// Probe a USB device for printer interfaces. Returns true if claimed.
bool probe(uint8_t usbAddress);

// Release printer interfaces on a device (on detach).
void release(uint8_t usbAddress);

// Return number of active printer devices.
uint8_t device_count();

// Get device info by index.
const PrinterDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Printer operations
// ----------------------------------------------------------------

// Get the IEEE 1284 Device ID string.
// The string is stored in the PrinterDevice and also returned via buffer.
usb::TransferStatus get_device_id(uint8_t devIndex,
                                   char* buffer,
                                   uint16_t maxLen,
                                   uint16_t* idLen);

// Get current port status.
usb::TransferStatus get_port_status(uint8_t devIndex, uint8_t* status);

// Perform a soft reset on the printer.
usb::TransferStatus soft_reset(uint8_t devIndex);

// Send print data to the printer.
usb::TransferStatus write(uint8_t devIndex,
                           const void* data, uint16_t len,
                           uint16_t* written);

// Read data from the printer (bidirectional mode only).
usb::TransferStatus read(uint8_t devIndex,
                          void* data, uint16_t maxLen,
                          uint16_t* bytesRead);

// Check convenience status methods
bool is_online(uint8_t devIndex);
bool is_paper_empty(uint8_t devIndex);
bool has_error(uint8_t devIndex);

} // namespace usb_printer
} // namespace kernel

#endif // KERNEL_USB_PRINTER_H
