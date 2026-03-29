// USB CDC-ECM Network Driver — Implementation
//
// Handles ECM device probing, descriptor parsing, packet filter
// management, notification polling, and Ethernet frame I/O.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_net_ecm.h"
#include "include/kernel/usb.h"
#include "include/kernel/usb_net.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_net_ecm {

// ================================================================
// Internal state
// ================================================================

static ECMDevice s_devices[MAX_ECM_DEVICES];
static uint8_t   s_count = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcpy_bytes(void* dst, const void* src, uint32_t len)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

// Parse a hex nibble from an ASCII character
static uint8_t hex_nibble(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
    if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
    return 0;
}

// Parse MAC address from a USB string descriptor (Unicode hex digits).
// The string descriptor contains 12 Unicode hex characters.
static bool parse_mac_from_string(uint8_t usbAddr, uint8_t strIndex,
                                  uint8_t* mac)
{
    if (strIndex == 0) return false;

    uint8_t buf[40];
    memzero(buf, sizeof(buf));

    usb::TransferStatus st = usb::get_descriptor(
        usbAddr, usb::DESC_STRING, strIndex, 0x0409,
        buf, sizeof(buf));

    if (st != usb::XFER_SUCCESS) return false;

    uint8_t len = buf[0];
    if (len < 26) return false; // 2 + 12*2 bytes minimum for 12 hex chars

    // USB string descriptor: buf[0]=length, buf[1]=type, buf[2..]=UTF-16LE chars
    for (uint8_t i = 0; i < 6; ++i) {
        uint8_t hi = buf[2 + i * 4];     // high nibble (low byte of UTF-16LE)
        uint8_t lo = buf[2 + i * 4 + 2]; // low nibble
        mac[i] = static_cast<uint8_t>((hex_nibble(hi) << 4) | hex_nibble(lo));
    }

    return true;
}

// ================================================================
// Find endpoints for ECM (bulk IN, bulk OUT, interrupt IN)
// ================================================================

static bool find_endpoints(const usb::Device* usbDev, ECMDevice* ecm)
{
    bool foundBI = false, foundBO = false;

    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (!ep.active) continue;

        if (ep.type == usb::TRANSFER_BULK &&
            ep.dir == usb::DIR_DEVICE_TO_HOST && !foundBI) {
            ecm->bulkInEP     = ep.address;
            ecm->bulkInMaxPkt = ep.maxPacketSize;
            foundBI = true;
        }
        if (ep.type == usb::TRANSFER_BULK &&
            ep.dir == usb::DIR_HOST_TO_DEVICE && !foundBO) {
            ecm->bulkOutEP     = ep.address;
            ecm->bulkOutMaxPkt = ep.maxPacketSize;
            foundBO = true;
        }
        if (ep.type == usb::TRANSFER_INTERRUPT &&
            ep.dir == usb::DIR_DEVICE_TO_HOST &&
            ecm->notifyEP == 0) {
            ecm->notifyEP     = ep.address;
            ecm->notifyMaxPkt = ep.maxPacketSize;
        }
    }

    return foundBI && foundBO;
}

// ================================================================
// Parse ECM functional descriptors from configuration descriptor
// ================================================================

static bool parse_ecm_descriptors(uint8_t usbAddr, ECMDevice* ecm)
{
    // Read full configuration descriptor
    uint8_t cfgBuf[256];
    memzero(cfgBuf, sizeof(cfgBuf));

    usb::TransferStatus st = usb::get_descriptor(
        usbAddr, usb::DESC_CONFIGURATION, 0, 0,
        cfgBuf, sizeof(cfgBuf));

    if (st != usb::XFER_SUCCESS) return false;

    uint16_t totalLen = static_cast<uint16_t>(cfgBuf[2] | (cfgBuf[3] << 8));
    if (totalLen > sizeof(cfgBuf)) totalLen = sizeof(cfgBuf);

    bool foundEthernet = false;
    bool foundUnion = false;

    uint16_t offset = 0;
    while (offset + 2 < totalLen) {
        uint8_t dLen  = cfgBuf[offset];
        uint8_t dType = cfgBuf[offset + 1];

        if (dLen < 2) break;

        if (dType == CDC_DESC_TYPE_CS_INTERFACE && offset + 3 < totalLen) {
            uint8_t subtype = cfgBuf[offset + 2];

            if (subtype == CDC_SUBTYPE_ETHERNET && dLen >= 13) {
                const ECMFunctionalDesc* desc =
                    reinterpret_cast<const ECMFunctionalDesc*>(&cfgBuf[offset]);
                ecm->maxSegmentSize = desc->wMaxSegmentSize;
                ecm->numMCFilters   = desc->wNumberMCFilters;
                ecm->statsCaps      = desc->bmEthernetStatistics;

                // Parse MAC from string descriptor
                parse_mac_from_string(usbAddr, desc->iMACAddress,
                                      ecm->macAddress);
                foundEthernet = true;
            }
            else if (subtype == CDC_SUBTYPE_UNION && dLen >= 5) {
                const CDCUnionDesc* udesc =
                    reinterpret_cast<const CDCUnionDesc*>(&cfgBuf[offset]);
                ecm->commInterface = udesc->bControlInterface;
                ecm->dataInterface = udesc->bSubordinateInterface0;
                foundUnion = true;
            }
        }

        offset += dLen;
    }

    if (!foundEthernet) {
        // Set defaults
        ecm->maxSegmentSize = usb_net::ETH_FRAME_MAX;
    }

    if (!foundUnion) {
        // Assume data interface follows comm interface
        ecm->dataInterface = static_cast<uint8_t>(ecm->commInterface + 1);
    }

    return true;
}

// ================================================================
// Select the data alternate setting (activate bulk endpoints)
// ================================================================

static usb::TransferStatus select_data_alt_setting(uint8_t usbAddr,
                                                    uint8_t dataIface,
                                                    uint8_t altSetting)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x01; // Host-to-device, standard, interface
    setup.bRequest      = 0x0B; // SET_INTERFACE
    setup.wValue        = altSetting;
    setup.wIndex        = dataIface;
    setup.wLength       = 0;
    return usb::control_transfer(usbAddr, &setup, nullptr, 0);
}

// ================================================================
// Class-specific requests
// ================================================================

static usb::TransferStatus ecm_set_packet_filter(uint8_t usbAddr,
                                                  uint8_t commIface,
                                                  uint16_t filter)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = ECM_SET_ETHERNET_PKT_FILTER;
    setup.wValue        = filter;
    setup.wIndex        = commIface;
    setup.wLength       = 0;
    return usb::control_transfer(usbAddr, &setup, nullptr, 0);
}

static usb::TransferStatus ecm_set_multicast(uint8_t usbAddr,
                                              uint8_t commIface,
                                              const uint8_t* addrs,
                                              uint8_t count)
{
    uint16_t dataLen = static_cast<uint16_t>(count * 6);
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21;
    setup.bRequest      = ECM_SET_ETHERNET_MULTICAST;
    setup.wValue        = count;
    setup.wIndex        = commIface;
    setup.wLength       = dataLen;
    return usb::control_transfer(usbAddr, &setup,
                                 const_cast<uint8_t*>(addrs), dataLen);
}

// ================================================================
// Notification handling
// ================================================================

static void handle_notification(ECMDevice* dev, const uint8_t* buf,
                                uint16_t len)
{
    if (len < 8) return;

    uint8_t notifCode = buf[1];

    if (notifCode == ECM_NOTIFY_NETWORK_CONNECTION) {
        uint16_t wValue = static_cast<uint16_t>(buf[4] | (buf[5] << 8));
        dev->linkUp = (wValue != 0);
    }
    else if (notifCode == ECM_NOTIFY_SPEED_CHANGE && len >= 16) {
        // Payload starts at offset 8
        const uint8_t* payload = &buf[8];
        dev->dlBitRate = static_cast<uint32_t>(
            payload[0] | (payload[1] << 8) |
            (payload[2] << 16) | (payload[3] << 24));
        dev->ulBitRate = static_cast<uint32_t>(
            payload[4] | (payload[5] << 8) |
            (payload[6] << 16) | (payload[7] << 24));
    }
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_count = 0;
}

bool probe(uint8_t usbAddress, uint8_t* parentIndex)
{
    const usb::Device* usbDev = usb::get_device(usbAddress);
    if (!usbDev) return false;

    // Look for CDC ECM interface
    bool found = false;
    uint8_t commIface = 0;

    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] == usb::CLASS_CDC &&
            usbDev->interfaceSubClass[i] == usb::CDC_SUBCLASS_ETHERNET) {
            commIface = i;
            found = true;
            break;
        }
    }

    if (!found) return false;
    if (s_count >= MAX_ECM_DEVICES) return false;

    uint8_t idx = s_count;
    ECMDevice* dev = &s_devices[idx];
    memzero(dev, sizeof(ECMDevice));

    dev->usbAddress    = usbAddress;
    dev->commInterface = commIface;
    dev->dataInterface = static_cast<uint8_t>(commIface + 1);

    // Parse ECM descriptors for MAC, segment size, etc.
    parse_ecm_descriptors(usbAddress, dev);

    // Find bulk and notification endpoints
    if (!find_endpoints(usbDev, dev)) return false;

    // Select alternate setting 1 on the data interface to activate
    // the bulk endpoints (ECM starts with alt setting 0 = no endpoints)
    select_data_alt_setting(usbAddress, dev->dataInterface, 1);

    // Set default packet filter: directed + broadcast
    dev->packetFilter = usb_net::FILTER_DIRECTED | usb_net::FILTER_BROADCAST;
    ecm_set_packet_filter(usbAddress, dev->commInterface, dev->packetFilter);

    dev->linkUp = false;
    dev->active = true;
    s_count++;

    if (parentIndex) *parentIndex = idx;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_ECM_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            // Deactivate data interface (select alt setting 0)
            select_data_alt_setting(usbAddress, s_devices[i].dataInterface, 0);
            s_devices[i].active = false;
            s_count--;
        }
    }
}

void poll(uint8_t devIndex)
{
    if (devIndex >= MAX_ECM_DEVICES) return;
    ECMDevice* dev = &s_devices[devIndex];
    if (!dev->active || dev->notifyEP == 0) return;

    uint8_t buf[32];
    uint16_t recvd = 0;

    usb::TransferStatus st = usb::hci::interrupt_transfer(
        dev->usbAddress, dev->notifyEP, buf, sizeof(buf), &recvd);

    if (st == usb::XFER_SUCCESS && recvd >= 8) {
        handle_notification(dev, buf, recvd);
    }
}

usb::TransferStatus send(uint8_t devIndex,
                          const void* frame, uint16_t len)
{
    if (devIndex >= MAX_ECM_DEVICES) return usb::XFER_ERROR;
    ECMDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint16_t sent = 0;
    usb::TransferStatus st = usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkOutEP,
        const_cast<void*>(frame), len, &sent);

    // If the transfer length is an exact multiple of the max packet
    // size, send a zero-length packet to terminate
    if (st == usb::XFER_SUCCESS && dev->bulkOutMaxPkt > 0 &&
        (len % dev->bulkOutMaxPkt) == 0) {
        uint16_t zlpSent = 0;
        usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                nullptr, 0, &zlpSent);
    }

    return st;
}

usb::TransferStatus receive(uint8_t devIndex,
                             void* frame, uint16_t maxLen,
                             uint16_t* received)
{
    if (devIndex >= MAX_ECM_DEVICES) return usb::XFER_ERROR;
    ECMDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    return usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkInEP,
        frame, maxLen, received);
}

usb::TransferStatus set_packet_filter(uint8_t devIndex, uint16_t filter)
{
    if (devIndex >= MAX_ECM_DEVICES) return usb::XFER_ERROR;
    ECMDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    usb::TransferStatus st = ecm_set_packet_filter(
        dev->usbAddress, dev->commInterface, filter);

    if (st == usb::XFER_SUCCESS) {
        dev->packetFilter = filter;
    }
    return st;
}

usb::TransferStatus set_multicast_filters(uint8_t devIndex,
                                           const uint8_t* addrs,
                                           uint8_t count)
{
    if (devIndex >= MAX_ECM_DEVICES) return usb::XFER_ERROR;
    ECMDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    if (count > dev->numMCFilters) return usb::XFER_ERROR;

    return ecm_set_multicast(dev->usbAddress, dev->commInterface,
                             addrs, count);
}

const ECMDevice* get_device(uint8_t devIndex)
{
    if (devIndex >= MAX_ECM_DEVICES) return nullptr;
    if (!s_devices[devIndex].active) return nullptr;
    return &s_devices[devIndex];
}

uint8_t count()
{
    return s_count;
}

} // namespace usb_net_ecm
} // namespace kernel
