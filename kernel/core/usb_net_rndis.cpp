// USB RNDIS Network Driver — Implementation
//
// Handles RNDIS device initialization, OID query/set, data packet
// encapsulation/decapsulation, keepalive, and status indications.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_net_rndis.h"
#include "include/kernel/usb.h"
#include "include/kernel/usb_net.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_net_rndis {

// ================================================================
// Internal state
// ================================================================

static RNDISDevice s_devices[MAX_RNDIS_DEVICES];
static uint8_t     s_count = 0;

// Shared I/O buffer for control messages (must be large enough for
// RNDIS_INITIALIZE_CMPLT, QUERY_CMPLT + payload, etc.)
static uint8_t s_ctrlBuf[256];

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

static uint32_t next_request_id(RNDISDevice* dev)
{
    return ++dev->requestId;
}

// ================================================================
// RNDIS control message exchange
//
// Send: control OUT (class, interface) with the RNDIS message.
// Receive response: control IN (class, interface).
// ================================================================

static usb::TransferStatus rndis_send_control(uint8_t usbAddr,
                                               uint8_t iface,
                                               const void* msg,
                                               uint16_t msgLen)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x21; // Host-to-device, class, interface
    setup.bRequest      = 0x00; // SEND_ENCAPSULATED_COMMAND
    setup.wValue        = 0;
    setup.wIndex        = iface;
    setup.wLength       = msgLen;
    return usb::control_transfer(usbAddr, &setup,
                                 const_cast<void*>(msg), msgLen);
}

static usb::TransferStatus rndis_get_response(uint8_t usbAddr,
                                               uint8_t iface,
                                               void* buf,
                                               uint16_t bufLen)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xA1; // Device-to-host, class, interface
    setup.bRequest      = 0x01; // GET_ENCAPSULATED_RESPONSE
    setup.wValue        = 0;
    setup.wIndex        = iface;
    setup.wLength       = bufLen;
    return usb::control_transfer(usbAddr, &setup, buf, bufLen);
}

// Wait for RESPONSE_AVAILABLE notification on the interrupt endpoint,
// then read the response.  For simplicity, also try a direct read
// (some devices respond immediately).
static usb::TransferStatus rndis_command(RNDISDevice* dev,
                                          const void* cmd,
                                          uint16_t cmdLen,
                                          void* resp,
                                          uint16_t respLen)
{
    usb::TransferStatus st = rndis_send_control(
        dev->usbAddress, dev->commInterface, cmd, cmdLen);
    if (st != usb::XFER_SUCCESS) return st;

    // Poll notification endpoint for RESPONSE_AVAILABLE
    if (dev->notifyEP != 0) {
        uint8_t notBuf[16];
        uint16_t recvd = 0;
        // Best-effort poll; some devices respond without notification
        usb::hci::interrupt_transfer(dev->usbAddress, dev->notifyEP,
                                     notBuf, sizeof(notBuf), &recvd);
    }

    return rndis_get_response(dev->usbAddress, dev->commInterface,
                              resp, respLen);
}

// ================================================================
// RNDIS INITIALIZE
// ================================================================

static bool rndis_initialize(RNDISDevice* dev)
{
    RNDISInitMsg initMsg;
    memzero(&initMsg, sizeof(initMsg));
    initMsg.msgType        = RNDIS_MSG_INIT;
    initMsg.msgLength      = sizeof(RNDISInitMsg);
    initMsg.requestId      = next_request_id(dev);
    initMsg.majorVersion   = 1;
    initMsg.minorVersion   = 0;
    initMsg.maxTransferSize = RNDIS_MAX_TRANSFER_SIZE;

    memzero(s_ctrlBuf, sizeof(s_ctrlBuf));
    usb::TransferStatus st = rndis_command(dev, &initMsg,
        static_cast<uint16_t>(sizeof(initMsg)),
        s_ctrlBuf, sizeof(s_ctrlBuf));

    if (st != usb::XFER_SUCCESS) return false;

    const RNDISInitComplete* resp =
        reinterpret_cast<const RNDISInitComplete*>(s_ctrlBuf);

    if (resp->msgType != RNDIS_MSG_INIT_COMPLETE) return false;
    if (resp->status != RNDIS_STATUS_SUCCESS) return false;

    dev->maxTransferSize = resp->maxTransferSize;
    if (dev->maxTransferSize == 0 || dev->maxTransferSize > 16384) {
        dev->maxTransferSize = RNDIS_MAX_TRANSFER_SIZE;
    }

    return true;
}

// ================================================================
// RNDIS QUERY OID
// ================================================================

static usb::TransferStatus rndis_query_oid(RNDISDevice* dev,
                                            uint32_t oid,
                                            void* outBuf,
                                            uint32_t outBufLen,
                                            uint32_t* outLen)
{
    RNDISQueryMsg qMsg;
    memzero(&qMsg, sizeof(qMsg));
    qMsg.msgType          = RNDIS_MSG_QUERY;
    qMsg.msgLength        = sizeof(RNDISQueryMsg);
    qMsg.requestId        = next_request_id(dev);
    qMsg.oid              = oid;
    qMsg.infoBufferLength = 0;
    qMsg.infoBufferOffset = 0;
    qMsg.deviceVcHandle   = 0;

    memzero(s_ctrlBuf, sizeof(s_ctrlBuf));
    usb::TransferStatus st = rndis_command(dev, &qMsg,
        static_cast<uint16_t>(sizeof(qMsg)),
        s_ctrlBuf, sizeof(s_ctrlBuf));

    if (st != usb::XFER_SUCCESS) return st;

    const RNDISQueryComplete* resp =
        reinterpret_cast<const RNDISQueryComplete*>(s_ctrlBuf);

    if (resp->msgType != RNDIS_MSG_QUERY_COMPLETE) return usb::XFER_ERROR;
    if (resp->status != RNDIS_STATUS_SUCCESS) return usb::XFER_ERROR;

    uint32_t infoLen = resp->infoBufferLength;
    uint32_t infoOff = resp->infoBufferOffset;

    // infoBufferOffset is relative to the start of requestId (byte 8)
    uint32_t absOff = infoOff + 8;

    if (infoLen > outBufLen) infoLen = outBufLen;
    if (absOff + infoLen > sizeof(s_ctrlBuf)) return usb::XFER_ERROR;

    memcpy_bytes(outBuf, &s_ctrlBuf[absOff], infoLen);
    if (outLen) *outLen = infoLen;

    return usb::XFER_SUCCESS;
}

// ================================================================
// RNDIS SET OID
// ================================================================

static usb::TransferStatus rndis_set_oid(RNDISDevice* dev,
                                          uint32_t oid,
                                          const void* data,
                                          uint32_t dataLen)
{
    // Build SET message in s_ctrlBuf
    uint32_t totalLen = sizeof(RNDISSetMsg) + dataLen;
    if (totalLen > sizeof(s_ctrlBuf)) return usb::XFER_ERROR;

    memzero(s_ctrlBuf, sizeof(s_ctrlBuf));

    RNDISSetMsg* sMsg = reinterpret_cast<RNDISSetMsg*>(s_ctrlBuf);
    sMsg->msgType          = RNDIS_MSG_SET;
    sMsg->msgLength        = totalLen;
    sMsg->requestId        = next_request_id(dev);
    sMsg->oid              = oid;
    sMsg->infoBufferLength = dataLen;
    sMsg->infoBufferOffset = 20; // offset from requestId to end of header
    sMsg->deviceVcHandle   = 0;

    if (dataLen > 0 && data) {
        memcpy_bytes(&s_ctrlBuf[sizeof(RNDISSetMsg)], data, dataLen);
    }

    uint8_t respBuf[32];
    memzero(respBuf, sizeof(respBuf));

    usb::TransferStatus st = rndis_command(dev, s_ctrlBuf,
        static_cast<uint16_t>(totalLen), respBuf, sizeof(respBuf));

    if (st != usb::XFER_SUCCESS) return st;

    const RNDISSetComplete* resp =
        reinterpret_cast<const RNDISSetComplete*>(respBuf);

    if (resp->msgType != RNDIS_MSG_SET_COMPLETE) return usb::XFER_ERROR;
    if (resp->status != RNDIS_STATUS_SUCCESS) return usb::XFER_ERROR;

    return usb::XFER_SUCCESS;
}

// ================================================================
// Query MAC address
// ================================================================

static bool query_mac_address(RNDISDevice* dev)
{
    uint8_t mac[6];
    uint32_t len = 0;

    usb::TransferStatus st = rndis_query_oid(
        dev, OID_802_3_PERMANENT_ADDRESS, mac, 6, &len);

    if (st != usb::XFER_SUCCESS || len < 6) return false;

    memcpy_bytes(dev->macAddress, mac, 6);
    return true;
}

// ================================================================
// Query link speed
// ================================================================

static void query_link_speed(RNDISDevice* dev)
{
    uint32_t speed = 0;
    uint32_t len = 0;

    usb::TransferStatus st = rndis_query_oid(
        dev, OID_GEN_LINK_SPEED, &speed, 4, &len);

    if (st == usb::XFER_SUCCESS && len >= 4) {
        dev->linkSpeed = speed;
    }
}

// ================================================================
// Query media connect status
// ================================================================

static void query_media_status(RNDISDevice* dev)
{
    uint32_t status = 0;
    uint32_t len = 0;

    usb::TransferStatus st = rndis_query_oid(
        dev, OID_GEN_MEDIA_CONNECT_STATUS, &status, 4, &len);

    if (st == usb::XFER_SUCCESS && len >= 4) {
        dev->linkUp = (status == 0); // 0 = connected, 1 = disconnected
    }
}

// ================================================================
// Query max frame size
// ================================================================

static void query_max_frame_size(RNDISDevice* dev)
{
    uint32_t frameSize = 0;
    uint32_t len = 0;

    usb::TransferStatus st = rndis_query_oid(
        dev, OID_GEN_MAXIMUM_FRAME_SIZE, &frameSize, 4, &len);

    if (st == usb::XFER_SUCCESS && len >= 4) {
        dev->mtu = static_cast<uint16_t>(frameSize > 0xFFFF ? 1500 : frameSize);
    }
}

// ================================================================
// Find endpoints
// ================================================================

static bool find_endpoints(const usb::Device* usbDev, RNDISDevice* rndis)
{
    bool foundBI = false, foundBO = false;

    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (!ep.active) continue;

        if (ep.type == usb::TRANSFER_BULK &&
            ep.dir == usb::DIR_DEVICE_TO_HOST && !foundBI) {
            rndis->bulkInEP     = ep.address;
            rndis->bulkInMaxPkt = ep.maxPacketSize;
            foundBI = true;
        }
        if (ep.type == usb::TRANSFER_BULK &&
            ep.dir == usb::DIR_HOST_TO_DEVICE && !foundBO) {
            rndis->bulkOutEP     = ep.address;
            rndis->bulkOutMaxPkt = ep.maxPacketSize;
            foundBO = true;
        }
        if (ep.type == usb::TRANSFER_INTERRUPT &&
            ep.dir == usb::DIR_DEVICE_TO_HOST &&
            rndis->notifyEP == 0) {
            rndis->notifyEP     = ep.address;
            rndis->notifyMaxPkt = ep.maxPacketSize;
        }
    }

    return foundBI && foundBO;
}

// ================================================================
// Device detection heuristic
//
// RNDIS devices present as:
//   (1) class=0xE0, subclass=0x01, protocol=0x03
//   (2) class=0xEF, subclass=0x04, protocol=0x01 (IAD, composite)
//   (3) class=0x02 (CDC), subclass=0x02 (ACM), protocol=0xFF
//       (some Android devices)
// ================================================================

static bool is_rndis_interface(uint8_t cls, uint8_t sub, uint8_t proto)
{
    // Wireless controller class, RNDIS subclass
    if (cls == usb::CLASS_WIRELESS && sub == 0x01 && proto == 0x03)
        return true;

    // Miscellaneous class, RNDIS composite
    if (cls == usb::CLASS_MISC && sub == 0x04 && proto == 0x01)
        return true;

    // CDC ACM with vendor-specific protocol (Android tethering)
    if (cls == usb::CLASS_CDC && sub == usb::CDC_SUBCLASS_ACM && proto == 0xFF)
        return true;

    return false;
}

// ================================================================
// Keepalive handling
// ================================================================

static void handle_keepalive(RNDISDevice* dev)
{
    RNDISKeepaliveCmplt resp;
    memzero(&resp, sizeof(resp));
    resp.msgType   = RNDIS_MSG_KEEPALIVE_CMPLT;
    resp.msgLength = sizeof(RNDISKeepaliveCmplt);
    resp.requestId = 0; // will be filled from received keepalive
    resp.status    = RNDIS_STATUS_SUCCESS;

    rndis_send_control(dev->usbAddress, dev->commInterface,
                       &resp, static_cast<uint16_t>(sizeof(resp)));
}

// ================================================================
// Handle indicate status messages on the bulk IN path
// ================================================================

static void handle_indicate_status(RNDISDevice* dev,
                                    const uint8_t* buf,
                                    uint32_t len)
{
    if (len < sizeof(RNDISIndicateMsg)) return;

    const RNDISIndicateMsg* ind =
        reinterpret_cast<const RNDISIndicateMsg*>(buf);

    if (ind->status == RNDIS_STATUS_MEDIA_CONNECT) {
        dev->linkUp = true;
        query_link_speed(dev);
    }
    else if (ind->status == RNDIS_STATUS_MEDIA_DISCONNECT) {
        dev->linkUp = false;
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

    // Scan interfaces for RNDIS
    bool found = false;
    uint8_t commIface = 0;

    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (is_rndis_interface(usbDev->interfaceClass[i],
                               usbDev->interfaceSubClass[i],
                               usbDev->interfaceProtocol[i])) {
            commIface = i;
            found = true;
            break;
        }
    }

    if (!found) return false;
    if (s_count >= MAX_RNDIS_DEVICES) return false;

    uint8_t idx = s_count;
    RNDISDevice* dev = &s_devices[idx];
    memzero(dev, sizeof(RNDISDevice));

    dev->usbAddress    = usbAddress;
    dev->commInterface = commIface;
    dev->dataInterface = static_cast<uint8_t>(commIface + 1);
    dev->mtu           = usb_net::ETH_MTU;

    // Find endpoints
    if (!find_endpoints(usbDev, dev)) return false;

    // RNDIS initialization sequence
    if (!rndis_initialize(dev)) return false;

    // Query MAC address
    if (!query_mac_address(dev)) {
        // Some devices return the MAC only after setting the packet filter
    }

    // Set packet filter: directed + broadcast
    uint32_t filter = usb_net::FILTER_DIRECTED | usb_net::FILTER_BROADCAST;
    rndis_set_oid(dev, OID_GEN_CURRENT_PACKET_FILTER, &filter, 4);
    dev->packetFilter = static_cast<uint16_t>(filter);

    // If MAC query failed before, try again after filter set
    if (dev->macAddress[0] == 0 && dev->macAddress[1] == 0 &&
        dev->macAddress[2] == 0 && dev->macAddress[3] == 0 &&
        dev->macAddress[4] == 0 && dev->macAddress[5] == 0) {
        query_mac_address(dev);
    }

    // Query link state and speed
    query_media_status(dev);
    query_link_speed(dev);
    query_max_frame_size(dev);

    dev->active = true;
    s_count++;

    if (parentIndex) *parentIndex = idx;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_RNDIS_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            // Send RNDIS_HALT
            RNDISHaltMsg halt;
            memzero(&halt, sizeof(halt));
            halt.msgType   = RNDIS_MSG_HALT;
            halt.msgLength = sizeof(RNDISHaltMsg);
            halt.requestId = next_request_id(&s_devices[i]);

            rndis_send_control(usbAddress, s_devices[i].commInterface,
                               &halt, static_cast<uint16_t>(sizeof(halt)));

            s_devices[i].active = false;
            s_count--;
        }
    }
}

void poll(uint8_t devIndex)
{
    if (devIndex >= MAX_RNDIS_DEVICES) return;
    RNDISDevice* dev = &s_devices[devIndex];
    if (!dev->active) return;

    // Poll notification endpoint
    if (dev->notifyEP != 0) {
        uint8_t notBuf[16];
        uint16_t recvd = 0;
        usb::TransferStatus st = usb::hci::interrupt_transfer(
            dev->usbAddress, dev->notifyEP,
            notBuf, sizeof(notBuf), &recvd);

        if (st == usb::XFER_SUCCESS && recvd >= 8) {
            // RESPONSE_AVAILABLE notification — read the control response
            memzero(s_ctrlBuf, sizeof(s_ctrlBuf));
            st = rndis_get_response(dev->usbAddress, dev->commInterface,
                                    s_ctrlBuf, sizeof(s_ctrlBuf));

            if (st == usb::XFER_SUCCESS) {
                const RNDISMsgHeader* hdr =
                    reinterpret_cast<const RNDISMsgHeader*>(s_ctrlBuf);

                if (hdr->msgType == RNDIS_MSG_INDICATE) {
                    handle_indicate_status(dev, s_ctrlBuf, hdr->msgLength);
                }
                else if (hdr->msgType == RNDIS_MSG_KEEPALIVE) {
                    handle_keepalive(dev);
                }
            }
        }
    }
}

usb::TransferStatus send(uint8_t devIndex,
                          const void* frame, uint16_t len)
{
    if (devIndex >= MAX_RNDIS_DEVICES) return usb::XFER_ERROR;
    RNDISDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    // Build RNDIS data packet: 44-byte header + Ethernet frame
    uint32_t totalLen = RNDIS_PACKET_HEADER_SIZE + len;
    if (totalLen > dev->maxTransferSize) return usb::XFER_ERROR;

    // Use a stack buffer for the encapsulated packet
    uint8_t pktBuf[2048];
    if (totalLen > sizeof(pktBuf)) return usb::XFER_ERROR;

    memzero(pktBuf, RNDIS_PACKET_HEADER_SIZE);

    RNDISPacketMsg* pkt = reinterpret_cast<RNDISPacketMsg*>(pktBuf);
    pkt->msgType    = RNDIS_MSG_PACKET;
    pkt->msgLength  = totalLen;
    pkt->dataOffset = 36; // offset from &dataOffset to data (44 - 8 = 36)
    pkt->dataLength = len;

    memcpy_bytes(&pktBuf[RNDIS_PACKET_HEADER_SIZE], frame, len);

    uint16_t sent = 0;
    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                   pktBuf, static_cast<uint16_t>(totalLen),
                                   &sent);
}

usb::TransferStatus receive(uint8_t devIndex,
                             void* frame, uint16_t maxLen,
                             uint16_t* received)
{
    if (devIndex >= MAX_RNDIS_DEVICES) return usb::XFER_ERROR;
    RNDISDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    // Read into a temporary buffer to strip the RNDIS header
    uint8_t rxBuf[2048];
    uint16_t recvd = 0;

    usb::TransferStatus st = usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkInEP,
        rxBuf, sizeof(rxBuf), &recvd);

    if (st != usb::XFER_SUCCESS || recvd < RNDIS_PACKET_HEADER_SIZE)
        return (st == usb::XFER_SUCCESS) ? usb::XFER_NAK : st;

    const RNDISMsgHeader* hdr =
        reinterpret_cast<const RNDISMsgHeader*>(rxBuf);

    // Handle non-data messages that arrive on the bulk endpoint
    if (hdr->msgType == RNDIS_MSG_INDICATE) {
        handle_indicate_status(dev, rxBuf, hdr->msgLength);
        if (received) *received = 0;
        return usb::XFER_NAK;
    }

    if (hdr->msgType != RNDIS_MSG_PACKET) {
        if (received) *received = 0;
        return usb::XFER_NAK;
    }

    const RNDISPacketMsg* pkt =
        reinterpret_cast<const RNDISPacketMsg*>(rxBuf);

    // dataOffset is from &dataOffset (byte 8) to the start of data
    uint32_t dataStart = 8 + pkt->dataOffset;
    uint32_t dataLen   = pkt->dataLength;

    if (dataStart + dataLen > recvd) return usb::XFER_ERROR;
    if (dataLen > maxLen) dataLen = maxLen;

    memcpy_bytes(frame, &rxBuf[dataStart], dataLen);
    if (received) *received = static_cast<uint16_t>(dataLen);

    return usb::XFER_SUCCESS;
}

usb::TransferStatus set_packet_filter(uint8_t devIndex, uint16_t filter)
{
    if (devIndex >= MAX_RNDIS_DEVICES) return usb::XFER_ERROR;
    RNDISDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    uint32_t f = filter;
    usb::TransferStatus st = rndis_set_oid(
        dev, OID_GEN_CURRENT_PACKET_FILTER, &f, 4);

    if (st == usb::XFER_SUCCESS) {
        dev->packetFilter = filter;
    }
    return st;
}

const RNDISDevice* get_device(uint8_t devIndex)
{
    if (devIndex >= MAX_RNDIS_DEVICES) return nullptr;
    if (!s_devices[devIndex].active) return nullptr;
    return &s_devices[devIndex];
}

uint8_t count()
{
    return s_count;
}

} // namespace usb_net_rndis
} // namespace kernel
