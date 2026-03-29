// USB CDC Class Driver — Implementation
//
// ACM (serial) and ECM (Ethernet) subclass support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_cdc.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_cdc {

// ================================================================
// Internal state
// ================================================================

static CDCACMDevice s_acmDevices[MAX_CDC_ACM_DEVICES];
static CDCECMDevice s_ecmDevices[MAX_CDC_ECM_DEVICES];
static uint8_t s_acmCount = 0;
static uint8_t s_ecmCount = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

// ================================================================
// Find bulk IN, bulk OUT, and interrupt IN endpoints
// ================================================================

static bool find_bulk_and_notify(const usb::Device* usbDev,
                                 uint8_t* bulkIn, uint16_t* biPkt,
                                 uint8_t* bulkOut, uint16_t* boPkt,
                                 uint8_t* notifyEP, uint16_t* nPkt)
{
    bool foundBI = false, foundBO = false, foundNot = false;

    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (!ep.active) continue;

        if (ep.type == usb::TRANSFER_BULK && ep.dir == usb::DIR_DEVICE_TO_HOST && !foundBI) {
            *bulkIn = ep.address; *biPkt = ep.maxPacketSize; foundBI = true;
        }
        if (ep.type == usb::TRANSFER_BULK && ep.dir == usb::DIR_HOST_TO_DEVICE && !foundBO) {
            *bulkOut = ep.address; *boPkt = ep.maxPacketSize; foundBO = true;
        }
        if (ep.type == usb::TRANSFER_INTERRUPT && ep.dir == usb::DIR_DEVICE_TO_HOST && !foundNot) {
            *notifyEP = ep.address; *nPkt = ep.maxPacketSize; foundNot = true;
        }
    }

    return foundBI && foundBO;
}

// ================================================================
// CDC class-specific control requests
// ================================================================

static usb::TransferStatus cdc_set_line_coding(uint8_t addr,
                                               uint8_t iface,
                                               const LineCoding* lc)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = CDC_REQ_SET_LINE_CODING;
    setup.wValue        = 0;
    setup.wIndex        = iface;
    setup.wLength       = sizeof(LineCoding);
    return usb::control_transfer(addr, &setup,
                                 const_cast<LineCoding*>(lc),
                                 sizeof(LineCoding));
}

static usb::TransferStatus cdc_get_line_coding(uint8_t addr,
                                               uint8_t iface,
                                               LineCoding* lc)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = CDC_REQ_GET_LINE_CODING;
    setup.wValue        = 0;
    setup.wIndex        = iface;
    setup.wLength       = sizeof(LineCoding);
    return usb::control_transfer(addr, &setup, lc, sizeof(LineCoding));
}

static usb::TransferStatus cdc_set_control_line_state(uint8_t addr,
                                                      uint8_t iface,
                                                      uint16_t state)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21;
    setup.bRequest      = CDC_REQ_SET_CONTROL_LINE_STATE;
    setup.wValue        = state;
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

static usb::TransferStatus cdc_send_break(uint8_t addr,
                                          uint8_t iface,
                                          uint16_t durationMs)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21;
    setup.bRequest      = CDC_REQ_SEND_BREAK;
    setup.wValue        = durationMs;
    setup.wIndex        = iface;
    setup.wLength       = 0;
    return usb::control_transfer(addr, &setup, nullptr, 0);
}

// ================================================================
// Probe helpers
// ================================================================

static bool probe_acm(const usb::Device* usbDev, uint8_t usbAddr, uint8_t iface)
{
    if (s_acmCount >= MAX_CDC_ACM_DEVICES) return false;

    CDCACMDevice* dev = &s_acmDevices[s_acmCount];
    memzero(dev, sizeof(CDCACMDevice));

    dev->usbAddress    = usbAddr;
    dev->commInterface = iface;
    dev->dataInterface = static_cast<uint8_t>(iface + 1); // typically next interface

    if (!find_bulk_and_notify(usbDev,
                              &dev->bulkInEP, &dev->bulkInMaxPkt,
                              &dev->bulkOutEP, &dev->bulkOutMaxPkt,
                              &dev->notifyEP, &dev->notifyMaxPkt)) {
        return false;
    }

    // Set default line coding: 115200 8N1
    dev->lineCoding.dwDTERate   = 115200;
    dev->lineCoding.bCharFormat = 0;
    dev->lineCoding.bParityType = PARITY_NONE;
    dev->lineCoding.bDataBits   = 8;

    cdc_set_line_coding(usbAddr, iface, &dev->lineCoding);

    // Assert DTR + RTS
    dev->controlLineState = LINE_STATE_DTR | LINE_STATE_RTS;
    cdc_set_control_line_state(usbAddr, iface, dev->controlLineState);

    dev->active = true;
    s_acmCount++;
    return true;
}

static bool probe_ecm(const usb::Device* usbDev, uint8_t usbAddr, uint8_t iface)
{
    if (s_ecmCount >= MAX_CDC_ECM_DEVICES) return false;

    CDCECMDevice* dev = &s_ecmDevices[s_ecmCount];
    memzero(dev, sizeof(CDCECMDevice));

    dev->usbAddress    = usbAddr;
    dev->commInterface = iface;
    dev->dataInterface = static_cast<uint8_t>(iface + 1);

    uint8_t dummyNotify = 0;
    uint16_t dummyNPkt = 0;
    if (!find_bulk_and_notify(usbDev,
                              &dev->bulkInEP, &dev->bulkInMaxPkt,
                              &dev->bulkOutEP, &dev->bulkOutMaxPkt,
                              &dummyNotify, &dummyNPkt)) {
        return false;
    }
    dev->notifyEP = dummyNotify;

    dev->maxSegmentSize = 1514; // default Ethernet MTU + header
    dev->connected = false;

    dev->active = true;
    s_ecmCount++;
    return true;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_acmDevices, sizeof(s_acmDevices));
    memzero(s_ecmDevices, sizeof(s_ecmDevices));
    s_acmCount = 0;
    s_ecmCount = 0;
}

bool probe(uint8_t usbAddress)
{
    const usb::Device* usbDev = usb::get_device(usbAddress);
    if (!usbDev) return false;

    bool claimed = false;

    for (uint8_t iface = 0; iface < usbDev->numInterfaces; ++iface) {
        if (usbDev->interfaceClass[iface] != usb::CLASS_CDC) continue;

        uint8_t sub = usbDev->interfaceSubClass[iface];

        if (sub == usb::CDC_SUBCLASS_ACM) {
            if (probe_acm(usbDev, usbAddress, iface)) claimed = true;
        }
        else if (sub == usb::CDC_SUBCLASS_ETHERNET) {
            if (probe_ecm(usbDev, usbAddress, iface)) claimed = true;
        }
    }

    return claimed;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_CDC_ACM_DEVICES; ++i) {
        if (s_acmDevices[i].active && s_acmDevices[i].usbAddress == usbAddress) {
            s_acmDevices[i].active = false;
            s_acmCount--;
        }
    }
    for (uint8_t i = 0; i < MAX_CDC_ECM_DEVICES; ++i) {
        if (s_ecmDevices[i].active && s_ecmDevices[i].usbAddress == usbAddress) {
            s_ecmDevices[i].active = false;
            s_ecmCount--;
        }
    }
}

// ----------------------------------------------------------------
// ACM (serial) I/O
// ----------------------------------------------------------------

uint8_t acm_count() { return s_acmCount; }

usb::TransferStatus acm_get_line_coding(uint8_t devIndex, LineCoding* lc)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return usb::XFER_ERROR;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = cdc_get_line_coding(dev->usbAddress,
                                                  dev->commInterface, lc);
    if (st == usb::XFER_SUCCESS) {
        dev->lineCoding = *lc;
    }
    return st;
}

usb::TransferStatus acm_set_line_coding(uint8_t devIndex, const LineCoding* lc)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return usb::XFER_ERROR;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = cdc_set_line_coding(dev->usbAddress,
                                                  dev->commInterface, lc);
    if (st == usb::XFER_SUCCESS) {
        dev->lineCoding = *lc;
    }
    return st;
}

usb::TransferStatus acm_set_control_lines(uint8_t devIndex, uint16_t state)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return usb::XFER_ERROR;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = cdc_set_control_line_state(dev->usbAddress,
                                                         dev->commInterface, state);
    if (st == usb::XFER_SUCCESS) {
        dev->controlLineState = state;
    }
    return st;
}

usb::TransferStatus acm_send_break(uint8_t devIndex, uint16_t durationMs)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return usb::XFER_ERROR;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;
    return cdc_send_break(dev->usbAddress, dev->commInterface, durationMs);
}

usb::TransferStatus acm_write(uint8_t devIndex,
                               const void* data, uint16_t len,
                               uint16_t* written)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return usb::XFER_ERROR;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                   const_cast<void*>(data), len, written);
}

usb::TransferStatus acm_read(uint8_t devIndex,
                              void* data, uint16_t len,
                              uint16_t* bytesRead)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return usb::XFER_ERROR;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkInEP,
                                   data, len, bytesRead);
}

void acm_poll_notifications(uint8_t devIndex)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return;
    CDCACMDevice* dev = &s_acmDevices[devIndex];
    if (!dev->active || dev->notifyEP == 0) return;

    uint8_t buf[16];
    uint16_t recvd = 0;

    usb::TransferStatus st = usb::hci::interrupt_transfer(
        dev->usbAddress, dev->notifyEP, buf, sizeof(buf), &recvd);

    if (st == usb::XFER_SUCCESS && recvd >= 10) {
        // Notification header: bmRequestType(1) bNotification(1) wValue(2) wIndex(2) wLength(2) data...
        uint8_t notif = buf[1];
        if (notif == CDC_NOTIFY_SERIAL_STATE && recvd >= 10) {
            dev->serialState = static_cast<uint16_t>(buf[8] | (buf[9] << 8));
        }
    }
}

uint16_t acm_get_serial_state(uint8_t devIndex)
{
    if (devIndex >= MAX_CDC_ACM_DEVICES) return 0;
    return s_acmDevices[devIndex].serialState;
}

// ----------------------------------------------------------------
// ECM Ethernet I/O
// ----------------------------------------------------------------

uint8_t ecm_count() { return s_ecmCount; }

usb::TransferStatus ecm_send(uint8_t devIndex,
                              const void* frame, uint16_t len)
{
    if (devIndex >= MAX_CDC_ECM_DEVICES) return usb::XFER_ERROR;
    CDCECMDevice* dev = &s_ecmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t sent = 0;
    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                   const_cast<void*>(frame), len, &sent);
}

usb::TransferStatus ecm_receive(uint8_t devIndex,
                                 void* frame, uint16_t maxLen,
                                 uint16_t* received)
{
    if (devIndex >= MAX_CDC_ECM_DEVICES) return usb::XFER_ERROR;
    CDCECMDevice* dev = &s_ecmDevices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkInEP,
                                   frame, maxLen, received);
}

const uint8_t* ecm_get_mac(uint8_t devIndex)
{
    if (devIndex >= MAX_CDC_ECM_DEVICES) return nullptr;
    if (!s_ecmDevices[devIndex].active) return nullptr;
    return s_ecmDevices[devIndex].macAddress;
}

bool ecm_is_connected(uint8_t devIndex)
{
    if (devIndex >= MAX_CDC_ECM_DEVICES) return false;
    return s_ecmDevices[devIndex].connected;
}

} // namespace usb_cdc
} // namespace kernel
