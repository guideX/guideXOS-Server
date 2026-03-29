// USB Printer Class Driver — Implementation
//
// Unidirectional / bidirectional printing, IEEE 1284 Device ID,
// port status, and soft reset.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_printer.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_printer {

// ================================================================
// Internal state
// ================================================================

static PrinterDevice s_devices[MAX_PRINTER_DEVICES];
static uint8_t s_deviceCount = 0;

// ================================================================
// Helpers
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
// Printer class-specific control requests
// ================================================================

static usb::TransferStatus printer_get_device_id(uint8_t addr,
                                                  uint8_t iface,
                                                  void* buf,
                                                  uint16_t maxLen)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = PRINTER_REQ_GET_DEVICE_ID;
    setup.wValue        = 0;    // config index
    setup.wIndex        = static_cast<uint16_t>((iface << 8) | 0); // interface + alt setting
    setup.wLength       = maxLen;
    return usb::control_transfer(addr, &setup, buf, maxLen);
}

static usb::TransferStatus printer_get_port_status(uint8_t addr,
                                                    uint8_t iface,
                                                    uint8_t* status)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1;
    setup.bRequest      = PRINTER_REQ_GET_PORT_STATUS;
    setup.wValue        = 0;
    setup.wIndex        = iface;
    setup.wLength       = 1;
    return usb::control_transfer(addr, &setup, status, 1);
}

static usb::TransferStatus printer_soft_reset(uint8_t addr, uint8_t iface)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = PRINTER_REQ_SOFT_RESET;
    setup.wValue        = 0;
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

// ================================================================
// Find bulk endpoints for the printer interface
// ================================================================

static bool find_printer_endpoints(const usb::Device* usbDev,
                                   uint8_t protocol,
                                   uint8_t* bulkOut, uint16_t* boPkt,
                                   uint8_t* bulkIn, uint16_t* biPkt)
{
    bool foundOut = false;
    bool foundIn  = false;

    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (!ep.active || ep.type != usb::TRANSFER_BULK) continue;

        if (ep.dir == usb::DIR_HOST_TO_DEVICE && !foundOut) {
            *bulkOut = ep.address;
            *boPkt   = ep.maxPacketSize;
            foundOut = true;
        }
        if (ep.dir == usb::DIR_DEVICE_TO_HOST && !foundIn) {
            *bulkIn = ep.address;
            *biPkt  = ep.maxPacketSize;
            foundIn = true;
        }
    }

    // Unidirectional only needs OUT; bidirectional needs both
    if (protocol == PRINTER_UNIDIRECTIONAL) {
        return foundOut;
    }
    return foundOut && foundIn;
}

// ================================================================
// Parse IEEE 1284 Device ID
//
// The first two bytes are big-endian length (including the length
// bytes themselves).  The rest is an ASCII key=value string.
// ================================================================

static void parse_device_id(PrinterDevice* dev,
                            const uint8_t* raw,
                            uint16_t rawLen)
{
    if (rawLen < 2) {
        dev->deviceIdLen = 0;
        dev->deviceId[0] = '\0';
        return;
    }

    uint16_t idLen = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
    if (idLen < 2) idLen = 2;
    uint16_t strLen = static_cast<uint16_t>(idLen - 2);
    if (strLen > rawLen - 2) strLen = static_cast<uint16_t>(rawLen - 2);
    if (strLen >= MAX_DEVICE_ID_LEN) strLen = MAX_DEVICE_ID_LEN - 1;

    memcopy(dev->deviceId, &raw[2], strLen);
    dev->deviceId[strLen] = '\0';
    dev->deviceIdLen = strLen;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
}

bool probe(uint8_t usbAddress)
{
    const usb::Device* usbDev = usb::get_device(usbAddress);
    if (!usbDev) return false;

    bool claimed = false;

    for (uint8_t iface = 0; iface < usbDev->numInterfaces; ++iface) {
        if (usbDev->interfaceClass[iface] != usb::CLASS_PRINTER) continue;
        if (s_deviceCount >= MAX_PRINTER_DEVICES) break;

        uint8_t protocol = usbDev->interfaceProtocol[iface];
        if (protocol != PRINTER_UNIDIRECTIONAL &&
            protocol != PRINTER_BIDIRECTIONAL &&
            protocol != PRINTER_1284_4_BIDIR) {
            continue;
        }

        PrinterDevice* dev = &s_devices[s_deviceCount];
        memzero(dev, sizeof(PrinterDevice));

        dev->usbAddress   = usbAddress;
        dev->interfaceNum = iface;
        dev->protocol     = protocol;

        if (!find_printer_endpoints(usbDev, protocol,
                                    &dev->bulkOutEP, &dev->bulkOutMaxPkt,
                                    &dev->bulkInEP, &dev->bulkInMaxPkt)) {
            continue;
        }

        // Fetch IEEE 1284 Device ID
        uint8_t idBuf[MAX_DEVICE_ID_LEN];
        memzero(idBuf, sizeof(idBuf));
        usb::TransferStatus st = printer_get_device_id(
            usbAddress, iface, idBuf, sizeof(idBuf));
        if (st == usb::XFER_SUCCESS) {
            parse_device_id(dev, idBuf, sizeof(idBuf));
        }

        // Get initial port status
        printer_get_port_status(usbAddress, iface, &dev->portStatus);

        dev->active = true;
        s_deviceCount++;
        claimed = true;
    }

    return claimed;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_PRINTER_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            s_devices[i].active = false;
            s_deviceCount--;
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const PrinterDevice* get_device(uint8_t index)
{
    if (index >= MAX_PRINTER_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

// ----------------------------------------------------------------
// Printer operations
// ----------------------------------------------------------------

usb::TransferStatus get_device_id(uint8_t devIndex,
                                   char* buffer,
                                   uint16_t maxLen,
                                   uint16_t* idLen)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return usb::XFER_ERROR;
    PrinterDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint8_t raw[MAX_DEVICE_ID_LEN];
    memzero(raw, sizeof(raw));

    usb::TransferStatus st = printer_get_device_id(
        dev->usbAddress, dev->interfaceNum, raw, sizeof(raw));

    if (st == usb::XFER_SUCCESS) {
        parse_device_id(dev, raw, sizeof(raw));
        uint16_t copyLen = dev->deviceIdLen;
        if (copyLen >= maxLen) copyLen = static_cast<uint16_t>(maxLen - 1);
        memcopy(buffer, dev->deviceId, copyLen);
        buffer[copyLen] = '\0';
        if (idLen) *idLen = copyLen;
    }
    return st;
}

usb::TransferStatus get_port_status(uint8_t devIndex, uint8_t* status)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return usb::XFER_ERROR;
    PrinterDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = printer_get_port_status(
        dev->usbAddress, dev->interfaceNum, &dev->portStatus);
    if (status) *status = dev->portStatus;
    return st;
}

usb::TransferStatus soft_reset(uint8_t devIndex)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return usb::XFER_ERROR;
    PrinterDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;
    return printer_soft_reset(dev->usbAddress, dev->interfaceNum);
}

usb::TransferStatus write(uint8_t devIndex,
                           const void* data, uint16_t len,
                           uint16_t* written)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return usb::XFER_ERROR;
    PrinterDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                   const_cast<void*>(data), len, written);
}

usb::TransferStatus read(uint8_t devIndex,
                          void* data, uint16_t maxLen,
                          uint16_t* bytesRead)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return usb::XFER_ERROR;
    PrinterDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    if (dev->protocol == PRINTER_UNIDIRECTIONAL) {
        return usb::XFER_NOT_SUPPORTED;
    }

    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkInEP,
                                   data, maxLen, bytesRead);
}

bool is_online(uint8_t devIndex)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return false;
    if (!s_devices[devIndex].active) return false;
    return (s_devices[devIndex].portStatus & PORT_STATUS_SELECTED) != 0;
}

bool is_paper_empty(uint8_t devIndex)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return true;
    if (!s_devices[devIndex].active) return true;
    return (s_devices[devIndex].portStatus & PORT_STATUS_PAPER_EMPTY) != 0;
}

bool has_error(uint8_t devIndex)
{
    if (devIndex >= MAX_PRINTER_DEVICES) return true;
    if (!s_devices[devIndex].active) return true;
    return (s_devices[devIndex].portStatus & PORT_STATUS_NOT_ERROR) == 0;
}

} // namespace usb_printer
} // namespace kernel
