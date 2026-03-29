// USB Network Subsystem — Unified Implementation
//
// Dispatches to the appropriate backend driver (CDC-ECM, RNDIS, or
// Wi-Fi) based on USB device interface descriptors.  Provides a
// single entry point for upper-layer protocols to send/receive
// Ethernet frames regardless of the underlying transport.
//
// Probe order: CDC-ECM ? RNDIS ? Wi-Fi
// (ECM is preferred over RNDIS when both are available, as ECM is
// the simpler and more standards-compliant protocol.)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_net.h"
#include "include/kernel/usb_net_ecm.h"
#include "include/kernel/usb_net_rndis.h"
#include "include/kernel/usb_net_wifi.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_net {

// ================================================================
// Internal state
// ================================================================

static NetDevice s_devices[MAX_NET_DEVICES];
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

// ================================================================
// Register a new NetDevice from a backend driver
// ================================================================

static NetDevice* alloc_device()
{
    if (s_count >= MAX_NET_DEVICES) return nullptr;

    for (uint8_t i = 0; i < MAX_NET_DEVICES; ++i) {
        if (!s_devices[i].active) {
            memzero(&s_devices[i], sizeof(NetDevice));
            return &s_devices[i];
        }
    }
    return nullptr;
}

// Fill NetDevice from an ECM backend device
static void fill_from_ecm(NetDevice* nd, uint8_t backendIdx)
{
    const usb_net_ecm::ECMDevice* ecm = usb_net_ecm::get_device(backendIdx);
    if (!ecm) return;

    nd->type          = NET_TYPE_ECM;
    nd->usbAddress    = ecm->usbAddress;
    nd->interfaceNum  = ecm->commInterface;
    nd->bulkInEP      = ecm->bulkInEP;
    nd->bulkOutEP     = ecm->bulkOutEP;
    nd->notifyEP      = ecm->notifyEP;
    nd->bulkInMaxPkt  = ecm->bulkInMaxPkt;
    nd->bulkOutMaxPkt = ecm->bulkOutMaxPkt;
    nd->notifyMaxPkt  = ecm->notifyMaxPkt;
    nd->mtu           = ecm->maxSegmentSize > ETH_HLEN ?
                        static_cast<uint16_t>(ecm->maxSegmentSize - ETH_HLEN) :
                        ETH_MTU;
    nd->packetFilter  = ecm->packetFilter;
    nd->link          = ecm->linkUp ? LINK_UP : LINK_DOWN;
    nd->backendIndex  = backendIdx;

    memcpy_bytes(nd->macAddress, ecm->macAddress, ETH_ALEN);

    // Determine speed from bit rate
    if (ecm->dlBitRate >= 1000000000) nd->speed = SPEED_1000MBPS;
    else if (ecm->dlBitRate >= 100000000) nd->speed = SPEED_100MBPS;
    else if (ecm->dlBitRate >= 10000000) nd->speed = SPEED_10MBPS;
    else nd->speed = SPEED_UNKNOWN;
}

// Fill NetDevice from an RNDIS backend device
static void fill_from_rndis(NetDevice* nd, uint8_t backendIdx)
{
    const usb_net_rndis::RNDISDevice* rndis =
        usb_net_rndis::get_device(backendIdx);
    if (!rndis) return;

    nd->type          = NET_TYPE_RNDIS;
    nd->usbAddress    = rndis->usbAddress;
    nd->interfaceNum  = rndis->commInterface;
    nd->bulkInEP      = rndis->bulkInEP;
    nd->bulkOutEP     = rndis->bulkOutEP;
    nd->notifyEP      = rndis->notifyEP;
    nd->bulkInMaxPkt  = rndis->bulkInMaxPkt;
    nd->bulkOutMaxPkt = rndis->bulkOutMaxPkt;
    nd->notifyMaxPkt  = rndis->notifyMaxPkt;
    nd->mtu           = rndis->mtu;
    nd->packetFilter  = rndis->packetFilter;
    nd->link          = rndis->linkUp ? LINK_UP : LINK_DOWN;
    nd->backendIndex  = backendIdx;

    memcpy_bytes(nd->macAddress, rndis->macAddress, ETH_ALEN);

    // linkSpeed is in 100 bps units
    uint32_t bps = rndis->linkSpeed * 100;
    if (bps >= 1000000000) nd->speed = SPEED_1000MBPS;
    else if (bps >= 100000000) nd->speed = SPEED_100MBPS;
    else if (bps >= 10000000) nd->speed = SPEED_10MBPS;
    else nd->speed = SPEED_UNKNOWN;
}

// Fill NetDevice from a Wi-Fi backend device
static void fill_from_wifi(NetDevice* nd, uint8_t backendIdx)
{
    const usb_net_wifi::WifiDevice* wifi =
        usb_net_wifi::get_device(backendIdx);
    if (!wifi) return;

    nd->type          = NET_TYPE_WIFI;
    nd->usbAddress    = wifi->usbAddress;
    nd->interfaceNum  = wifi->interfaceNum;
    nd->bulkInEP      = wifi->bulkInEP;
    nd->bulkOutEP     = wifi->bulkOutEP;
    nd->notifyEP      = wifi->notifyEP;
    nd->bulkInMaxPkt  = wifi->bulkInMaxPkt;
    nd->bulkOutMaxPkt = wifi->bulkOutMaxPkt;
    nd->notifyMaxPkt  = wifi->notifyMaxPkt;
    nd->mtu           = ETH_MTU;
    nd->packetFilter  = FILTER_DIRECTED | FILTER_BROADCAST;
    nd->backendIndex  = backendIdx;

    memcpy_bytes(nd->macAddress, wifi->macAddress, ETH_ALEN);

    if (wifi->state == usb_net_wifi::WIFI_CONNECTED) {
        nd->link = LINK_UP;
    } else if (wifi->state == usb_net_wifi::WIFI_ASSOCIATING ||
               wifi->state == usb_net_wifi::WIFI_SCANNING) {
        nd->link = LINK_CONNECTING;
    } else {
        nd->link = LINK_DOWN;
    }

    nd->speed = SPEED_UNKNOWN;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_count = 0;

    usb_net_ecm::init();
    usb_net_rndis::init();
    usb_net_wifi::init();
}

bool probe(uint8_t usbAddress)
{
    if (s_count >= MAX_NET_DEVICES) return false;

    // Try CDC-ECM first (standards-compliant, preferred)
    uint8_t backendIdx = 0;
    if (usb_net_ecm::probe(usbAddress, &backendIdx)) {
        NetDevice* nd = alloc_device();
        if (!nd) {
            usb_net_ecm::release(usbAddress);
            return false;
        }
        fill_from_ecm(nd, backendIdx);
        nd->active = true;
        s_count++;
        return true;
    }

    // Try RNDIS (Microsoft, Android tethering)
    if (usb_net_rndis::probe(usbAddress, &backendIdx)) {
        NetDevice* nd = alloc_device();
        if (!nd) {
            usb_net_rndis::release(usbAddress);
            return false;
        }
        fill_from_rndis(nd, backendIdx);
        nd->active = true;
        s_count++;
        return true;
    }

    // Try Wi-Fi
    if (usb_net_wifi::probe(usbAddress, &backendIdx)) {
        NetDevice* nd = alloc_device();
        if (!nd) {
            usb_net_wifi::release(usbAddress);
            return false;
        }
        fill_from_wifi(nd, backendIdx);
        nd->active = true;
        s_count++;
        return true;
    }

    return false;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_NET_DEVICES; ++i) {
        if (!s_devices[i].active) continue;
        if (s_devices[i].usbAddress != usbAddress) continue;

        switch (s_devices[i].type) {
        case NET_TYPE_ECM:
            usb_net_ecm::release(usbAddress);
            break;
        case NET_TYPE_RNDIS:
            usb_net_rndis::release(usbAddress);
            break;
        case NET_TYPE_WIFI:
            usb_net_wifi::release(usbAddress);
            break;
        default:
            break;
        }

        s_devices[i].active = false;
        s_count--;
    }
}

void poll()
{
    for (uint8_t i = 0; i < MAX_NET_DEVICES; ++i) {
        if (!s_devices[i].active) continue;

        switch (s_devices[i].type) {
        case NET_TYPE_ECM:
            usb_net_ecm::poll(s_devices[i].backendIndex);
            fill_from_ecm(&s_devices[i], s_devices[i].backendIndex);
            break;
        case NET_TYPE_RNDIS:
            usb_net_rndis::poll(s_devices[i].backendIndex);
            fill_from_rndis(&s_devices[i], s_devices[i].backendIndex);
            break;
        case NET_TYPE_WIFI:
            usb_net_wifi::poll(s_devices[i].backendIndex);
            fill_from_wifi(&s_devices[i], s_devices[i].backendIndex);
            break;
        default:
            break;
        }
    }
}

uint8_t device_count()
{
    return s_count;
}

const NetDevice* get_device(uint8_t index)
{
    if (index >= MAX_NET_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

// ----------------------------------------------------------------
// Frame-level I/O — dispatch to backend
// ----------------------------------------------------------------

usb::TransferStatus send_frame(uint8_t devIndex,
                                const void* frame,
                                uint16_t len)
{
    if (devIndex >= MAX_NET_DEVICES) return usb::XFER_ERROR;
    NetDevice* nd = &s_devices[devIndex];
    if (!nd->active) return usb::XFER_ERROR;

    usb::TransferStatus st = usb::XFER_ERROR;

    switch (nd->type) {
    case NET_TYPE_ECM:
        st = usb_net_ecm::send(nd->backendIndex, frame, len);
        break;
    case NET_TYPE_RNDIS:
        st = usb_net_rndis::send(nd->backendIndex, frame, len);
        break;
    case NET_TYPE_WIFI:
        st = usb_net_wifi::send(nd->backendIndex, frame, len);
        break;
    default:
        break;
    }

    if (st == usb::XFER_SUCCESS) {
        nd->stats.txFrames++;
        nd->stats.txBytes += len;
    } else {
        nd->stats.txErrors++;
    }

    return st;
}

usb::TransferStatus receive_frame(uint8_t devIndex,
                                   void* frame,
                                   uint16_t maxLen,
                                   uint16_t* received)
{
    if (devIndex >= MAX_NET_DEVICES) return usb::XFER_ERROR;
    NetDevice* nd = &s_devices[devIndex];
    if (!nd->active) return usb::XFER_ERROR;

    uint16_t recvd = 0;
    usb::TransferStatus st = usb::XFER_ERROR;

    switch (nd->type) {
    case NET_TYPE_ECM:
        st = usb_net_ecm::receive(nd->backendIndex, frame, maxLen, &recvd);
        break;
    case NET_TYPE_RNDIS:
        st = usb_net_rndis::receive(nd->backendIndex, frame, maxLen, &recvd);
        break;
    case NET_TYPE_WIFI:
        st = usb_net_wifi::receive(nd->backendIndex, frame, maxLen, &recvd);
        break;
    default:
        break;
    }

    if (st == usb::XFER_SUCCESS && recvd > 0) {
        nd->stats.rxFrames++;
        nd->stats.rxBytes += recvd;
    } else if (st != usb::XFER_SUCCESS && st != usb::XFER_NAK) {
        nd->stats.rxErrors++;
    }

    if (received) *received = recvd;
    return st;
}

// ----------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------

usb::TransferStatus set_packet_filter(uint8_t devIndex,
                                       uint16_t filter)
{
    if (devIndex >= MAX_NET_DEVICES) return usb::XFER_ERROR;
    NetDevice* nd = &s_devices[devIndex];
    if (!nd->active) return usb::XFER_ERROR;

    usb::TransferStatus st = usb::XFER_ERROR;

    switch (nd->type) {
    case NET_TYPE_ECM:
        st = usb_net_ecm::set_packet_filter(nd->backendIndex, filter);
        break;
    case NET_TYPE_RNDIS:
        st = usb_net_rndis::set_packet_filter(nd->backendIndex, filter);
        break;
    case NET_TYPE_WIFI:
        // Wi-Fi packet filter is managed by firmware
        nd->packetFilter = filter;
        st = usb::XFER_SUCCESS;
        break;
    default:
        break;
    }

    if (st == usb::XFER_SUCCESS) {
        nd->packetFilter = filter;
    }

    return st;
}

LinkState get_link_state(uint8_t devIndex)
{
    if (devIndex >= MAX_NET_DEVICES) return LINK_DOWN;
    if (!s_devices[devIndex].active) return LINK_DOWN;
    return s_devices[devIndex].link;
}

LinkSpeed get_link_speed(uint8_t devIndex)
{
    if (devIndex >= MAX_NET_DEVICES) return SPEED_UNKNOWN;
    if (!s_devices[devIndex].active) return SPEED_UNKNOWN;
    return s_devices[devIndex].speed;
}

const uint8_t* get_mac_address(uint8_t devIndex)
{
    if (devIndex >= MAX_NET_DEVICES) return nullptr;
    if (!s_devices[devIndex].active) return nullptr;
    return s_devices[devIndex].macAddress;
}

const NetStats* get_stats(uint8_t devIndex)
{
    if (devIndex >= MAX_NET_DEVICES) return nullptr;
    if (!s_devices[devIndex].active) return nullptr;
    return &s_devices[devIndex].stats;
}

} // namespace usb_net
} // namespace kernel
