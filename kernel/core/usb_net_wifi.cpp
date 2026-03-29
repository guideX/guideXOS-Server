// USB Wi-Fi Network Driver — Implementation
//
// Provides the common Wi-Fi state machine, 802.11 frame handling,
// EAPOL/WPA key exchange framework, and vendor command abstraction.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_net_wifi.h"
#include "include/kernel/usb.h"
#include "include/kernel/usb_net.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_net_wifi {

// ================================================================
// Internal state
// ================================================================

static WifiDevice s_devices[MAX_WIFI_DEVICES];
static uint8_t    s_count = 0;

// Shared TX/RX buffers for 802.11 frame assembly/disassembly
static uint8_t s_txBuf[2400];
static uint8_t s_rxBuf[2400];

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

static bool mem_equal(const void* a, const void* b, uint32_t len)
{
    const uint8_t* pa = static_cast<const uint8_t*>(a);
    const uint8_t* pb = static_cast<const uint8_t*>(b);
    for (uint32_t i = 0; i < len; ++i) {
        if (pa[i] != pb[i]) return false;
    }
    return true;
}

static uint8_t str_len(const char* s)
{
    uint8_t len = 0;
    while (s && s[len]) ++len;
    return len;
}

// Byte-swap 16-bit value (host-to-big-endian)
static uint16_t htobe16(uint16_t v)
{
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

// Byte-swap 16-bit value (big-endian-to-host)
static uint16_t be16toh(uint16_t v)
{
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

// ================================================================
// 802.11 LLC/SNAP helpers
// ================================================================

static const uint8_t LLC_SNAP_HEADER[] = {
    0xAA, 0xAA, 0x03,   // DSAP, SSAP, Control
    0x00, 0x00, 0x00     // OUI
};
static const uint8_t LLC_SNAP_LEN = 6;  // without etherType

// ================================================================
// Vendor command — generic implementation
//
// Sends a vendor-specific command over the control endpoint.
// Chip-specific backends would override or extend this.
// ================================================================

static usb::TransferStatus send_vendor_cmd(WifiDevice* dev,
                                            VendorCmdType cmd,
                                            const void* data,
                                            uint16_t dataLen)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0x41; // Host-to-device, vendor, interface
    setup.bRequest      = static_cast<uint8_t>(cmd);
    setup.wValue        = 0;
    setup.wIndex        = dev->interfaceNum;
    setup.wLength       = dataLen;
    return usb::control_transfer(dev->usbAddress, &setup,
                                 const_cast<void*>(data), dataLen);
}

static usb::TransferStatus read_vendor_resp(WifiDevice* dev,
                                             VendorCmdType cmd,
                                             void* data,
                                             uint16_t dataLen)
{
    usb::SetupPacket setup;
    setup.bmRequestType = 0xC1; // Device-to-host, vendor, interface
    setup.bRequest      = static_cast<uint8_t>(cmd);
    setup.wValue        = 0;
    setup.wIndex        = dev->interfaceNum;
    setup.wLength       = dataLen;
    return usb::control_transfer(dev->usbAddress, &setup,
                                 data, dataLen);
}

// ================================================================
// WPA/WPA2 key derivation stubs
//
// Full PBKDF2-SHA1 and PRF implementations would go here.
// These stubs allow the framework to compile and the key
// exchange state machine to run; actual crypto would need
// HMAC-SHA1/SHA256 implementations.
// ================================================================

// PBKDF2-SHA1 for WPA passphrase ? PMK derivation
// (stub — produces a deterministic but incorrect key)
static void derive_pmk(const char* passphrase, uint8_t passLen,
                        const char* ssid, uint8_t ssidLen,
                        uint8_t* pmk)
{
    // Stub: XOR-fold passphrase and SSID into 32 bytes.
    // A real implementation would use PBKDF2(HMAC-SHA1, pass, ssid, 4096, 32).
    memzero(pmk, WPA_KEY_LEN);
    for (uint8_t i = 0; i < passLen && i < WPA_KEY_LEN; ++i) {
        pmk[i] ^= static_cast<uint8_t>(passphrase[i]);
    }
    for (uint8_t i = 0; i < ssidLen && i < WPA_KEY_LEN; ++i) {
        pmk[i] ^= static_cast<uint8_t>(ssid[i]);
    }
}

// PRF-512 for PTK derivation from PMK + nonces + addresses
// (stub — produces a deterministic but incorrect key)
static void derive_ptk(const uint8_t* pmk,
                        const uint8_t* anonce,
                        const uint8_t* snonce,
                        const uint8_t* aa,     // authenticator address
                        const uint8_t* sa,     // supplicant address
                        uint8_t* ptk)
{
    // Stub: XOR PMK with nonces into 64 bytes.
    // A real implementation would use PRF-512(PMK, "Pairwise key expansion",
    //   min(AA,SA) || max(AA,SA) || min(ANonce,SNonce) || max(ANonce,SNonce)).
    memzero(ptk, 64);
    for (uint8_t i = 0; i < 32; ++i) {
        ptk[i]      = pmk[i] ^ anonce[i];
        ptk[i + 32] = pmk[i] ^ snonce[i];
    }
    (void)aa;
    (void)sa;
}

// ================================================================
// EAPOL frame handling
// ================================================================

static bool is_eapol_frame(const uint8_t* data, uint16_t len)
{
    // EAPOL frames use EtherType 0x888E
    if (len < usb_net::ETH_HLEN + 4) return false;
    const usb_net::EthernetHeader* eth =
        reinterpret_cast<const usb_net::EthernetHeader*>(data);
    return (be16toh(eth->etherType) == 0x888E);
}

// Process incoming EAPOL key frame (WPA 4-way handshake)
static void handle_eapol(WifiDevice* dev, const uint8_t* frame,
                          uint16_t len)
{
    if (len < usb_net::ETH_HLEN + sizeof(EAPOLKeyFrame)) return;

    const EAPOLKeyFrame* key = reinterpret_cast<const EAPOLKeyFrame*>(
        frame + usb_net::ETH_HLEN);

    if (key->packetType != 0x03) return; // not a key frame

    uint16_t keyInfo = be16toh(key->keyInfo);
    bool pairwise = (keyInfo & 0x0008) != 0;
    bool ack      = (keyInfo & 0x0080) != 0;
    bool mic      = (keyInfo & 0x0100) != 0;
    bool install  = (keyInfo & 0x0040) != 0;

    if (pairwise && ack && !mic) {
        // Message 1/4: ANonce from AP
        memcpy_bytes(dev->anonce, key->keyNonce, 32);

        // Generate SNonce (stub: use simple counter-based generation)
        for (uint8_t i = 0; i < 32; ++i) {
            dev->snonce[i] = static_cast<uint8_t>(dev->txSeqNum + i);
        }

        // Derive PTK
        derive_ptk(dev->pmk, dev->anonce, dev->snonce,
                   dev->bssid, dev->macAddress, dev->ptk);

        // Send Message 2/4 (stub — would build and send EAPOL frame)
        dev->ptkValid = false; // not yet confirmed
    }
    else if (pairwise && ack && mic && install) {
        // Message 3/4: PTK confirmed, install keys
        dev->ptkValid = true;

        // Send Message 4/4 (stub — would build and send EAPOL frame)
        dev->state = WIFI_CONNECTED;
    }
    else if (!pairwise && ack && mic) {
        // Group key handshake Message 1/2
        // Install GTK (stub)
        dev->gtkValid = true;
    }
}

// ================================================================
// 802.11 frame construction (Ethernet ? 802.11 data)
// ================================================================

static uint16_t build_data_frame(WifiDevice* dev,
                                  const uint8_t* ethFrame,
                                  uint16_t ethLen,
                                  uint8_t* outBuf,
                                  uint16_t outMaxLen)
{
    if (ethLen < usb_net::ETH_HLEN) return 0;

    const usb_net::EthernetHeader* eth =
        reinterpret_cast<const usb_net::EthernetHeader*>(ethFrame);

    // 802.11 header (24 bytes) + LLC/SNAP (8 bytes) + payload
    uint16_t payloadLen = static_cast<uint16_t>(ethLen - usb_net::ETH_HLEN);
    uint16_t totalLen = static_cast<uint16_t>(
        sizeof(Dot11FrameHeader) + sizeof(Dot11LLCSnap) + payloadLen);

    if (totalLen > outMaxLen) return 0;

    memzero(outBuf, totalLen);

    // 802.11 header
    Dot11FrameHeader* hdr =
        reinterpret_cast<Dot11FrameHeader*>(outBuf);

    hdr->frameControl = htobe16(static_cast<uint16_t>(
        FC_TYPE_DATA | FC_SUBTYPE_DATA | FC_TO_DS));
    hdr->durationId = 0;

    // ToDS: addr1=BSSID, addr2=SA (our MAC), addr3=DA (destination)
    memcpy_bytes(hdr->addr1, dev->bssid, 6);
    memcpy_bytes(hdr->addr2, dev->macAddress, 6);
    memcpy_bytes(hdr->addr3, eth->destMAC, 6);

    hdr->seqControl = htobe16(static_cast<uint16_t>(
        (dev->txSeqNum++ & 0x0FFF) << 4));

    // LLC/SNAP header
    uint16_t offset = sizeof(Dot11FrameHeader);
    Dot11LLCSnap* snap =
        reinterpret_cast<Dot11LLCSnap*>(&outBuf[offset]);
    snap->dsap     = 0xAA;
    snap->ssap     = 0xAA;
    snap->control  = 0x03;
    snap->oui[0]   = 0x00;
    snap->oui[1]   = 0x00;
    snap->oui[2]   = 0x00;
    snap->etherType = eth->etherType; // already in network byte order

    offset += sizeof(Dot11LLCSnap);

    // Payload (Ethernet frame body after header)
    memcpy_bytes(&outBuf[offset], &ethFrame[usb_net::ETH_HLEN], payloadLen);

    return totalLen;
}

// ================================================================
// 802.11 frame extraction (802.11 data ? Ethernet)
// ================================================================

static uint16_t extract_data_frame(WifiDevice* dev,
                                    const uint8_t* dot11Frame,
                                    uint16_t dot11Len,
                                    uint8_t* ethBuf,
                                    uint16_t ethMaxLen)
{
    if (dot11Len < sizeof(Dot11FrameHeader) + sizeof(Dot11LLCSnap))
        return 0;

    const Dot11FrameHeader* hdr =
        reinterpret_cast<const Dot11FrameHeader*>(dot11Frame);

    uint16_t fc = be16toh(hdr->frameControl);

    // Only handle data frames
    if ((fc & 0x000C) != FC_TYPE_DATA) return 0;

    // Determine source and destination based on ToDS/FromDS
    const uint8_t* srcMAC  = nullptr;
    const uint8_t* destMAC = nullptr;
    bool toDS   = (fc & FC_TO_DS) != 0;
    bool fromDS = (fc & FC_FROM_DS) != 0;

    if (!toDS && fromDS) {
        // FromDS: addr1=DA, addr2=BSSID, addr3=SA
        destMAC = hdr->addr1;
        srcMAC  = hdr->addr3;
    }
    else if (toDS && !fromDS) {
        // ToDS: addr1=BSSID, addr2=SA, addr3=DA
        destMAC = hdr->addr3;
        srcMAC  = hdr->addr2;
    }
    else if (!toDS && !fromDS) {
        // IBSS: addr1=DA, addr2=SA, addr3=BSSID
        destMAC = hdr->addr1;
        srcMAC  = hdr->addr2;
    }
    else {
        // WDS (4-address): not handled
        return 0;
    }

    uint16_t offset = sizeof(Dot11FrameHeader);

    // Verify LLC/SNAP
    const Dot11LLCSnap* snap =
        reinterpret_cast<const Dot11LLCSnap*>(&dot11Frame[offset]);
    if (snap->dsap != 0xAA || snap->ssap != 0xAA || snap->control != 0x03)
        return 0;

    offset += sizeof(Dot11LLCSnap);

    uint16_t payloadLen = static_cast<uint16_t>(dot11Len - offset);
    uint16_t ethLen = static_cast<uint16_t>(usb_net::ETH_HLEN + payloadLen);

    if (ethLen > ethMaxLen) return 0;

    // Build Ethernet header
    usb_net::EthernetHeader* eth =
        reinterpret_cast<usb_net::EthernetHeader*>(ethBuf);
    memcpy_bytes(eth->destMAC, destMAC, 6);
    memcpy_bytes(eth->srcMAC, srcMAC, 6);
    eth->etherType = snap->etherType; // already network byte order

    // Copy payload
    memcpy_bytes(&ethBuf[usb_net::ETH_HLEN], &dot11Frame[offset], payloadLen);

    (void)dev;
    return ethLen;
}

// ================================================================
// Find endpoints
// ================================================================

static bool find_endpoints(const usb::Device* usbDev, WifiDevice* wifi)
{
    bool foundBI = false, foundBO = false;

    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (!ep.active) continue;

        if (ep.type == usb::TRANSFER_BULK &&
            ep.dir == usb::DIR_DEVICE_TO_HOST && !foundBI) {
            wifi->bulkInEP     = ep.address;
            wifi->bulkInMaxPkt = ep.maxPacketSize;
            foundBI = true;
        }
        if (ep.type == usb::TRANSFER_BULK &&
            ep.dir == usb::DIR_HOST_TO_DEVICE && !foundBO) {
            wifi->bulkOutEP     = ep.address;
            wifi->bulkOutMaxPkt = ep.maxPacketSize;
            foundBO = true;
        }
        if (ep.type == usb::TRANSFER_INTERRUPT &&
            ep.dir == usb::DIR_DEVICE_TO_HOST &&
            wifi->notifyEP == 0) {
            wifi->notifyEP     = ep.address;
            wifi->notifyMaxPkt = ep.maxPacketSize;
        }
    }

    return foundBI && foundBO;
}

// ================================================================
// Wi-Fi device identification
//
// USB Wi-Fi adapters typically use:
//   - class 0xFF (vendor-specific) with known VID/PID
//   - class 0xEF subclass 0x02 protocol 0x01 (RNDIS Wi-Fi, rare)
// ================================================================

static bool is_wifi_device(const usb::Device* usbDev)
{
    // Check for vendor-specific class with wireless-related subclass
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        uint8_t cls = usbDev->interfaceClass[i];
        uint8_t sub = usbDev->interfaceSubClass[i];

        // Vendor-specific class — common for Wi-Fi chipsets
        if (cls == usb::CLASS_VENDOR_SPECIFIC) {
            // Check device-level class for wireless hint
            if (usbDev->devDesc.bDeviceClass == usb::CLASS_VENDOR_SPECIFIC ||
                usbDev->devDesc.bDeviceClass == usb::CLASS_WIRELESS ||
                usbDev->devDesc.bDeviceClass == 0x00) {
                // Known Wi-Fi vendor IDs (subset)
                uint16_t vid = usbDev->devDesc.idVendor;
                if (vid == 0x0BDA ||   // Realtek
                    vid == 0x0CF3 ||   // Atheros/Qualcomm
                    vid == 0x148F ||   // Ralink/MediaTek
                    vid == 0x0B05 ||   // ASUS (Realtek/Atheros based)
                    vid == 0x2357 ||   // TP-Link
                    vid == 0x7392 ||   // Edimax
                    vid == 0x0846) {   // NETGEAR
                    return true;
                }
            }
        }

        // Wireless controller class with RNDIS-like Wi-Fi
        if (cls == usb::CLASS_WIRELESS && sub == 0x01) {
            return true;
        }
    }

    return false;
}

// ================================================================
// Read MAC address from firmware
// ================================================================

static bool read_mac_address(WifiDevice* dev)
{
    // Try vendor command to read MAC from firmware/EEPROM
    uint8_t mac[6];
    uint16_t recvd = 0;

    usb::TransferStatus st = read_vendor_resp(
        dev, VCMD_GET_SIGNAL, // reuse as generic read
        mac, 6);

    if (st == usb::XFER_SUCCESS && recvd >= 6) {
        memcpy_bytes(dev->macAddress, mac, 6);
        return true;
    }

    // Fallback: try to read from device descriptor string
    uint8_t buf[40];
    memzero(buf, sizeof(buf));
    st = usb::get_descriptor(dev->usbAddress, usb::DESC_STRING, 1, 0x0409,
                             buf, sizeof(buf));
    (void)st;

    // If we still have no MAC, generate a locally-administered one
    // based on USB address (for development/testing)
    if (dev->macAddress[0] == 0 && dev->macAddress[1] == 0 &&
        dev->macAddress[2] == 0) {
        dev->macAddress[0] = 0x02; // locally administered
        dev->macAddress[1] = 0x00;
        dev->macAddress[2] = 0x00;
        dev->macAddress[3] = 0x00;
        dev->macAddress[4] = 0x00;
        dev->macAddress[5] = dev->usbAddress;
    }

    return true;
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

    if (!is_wifi_device(usbDev)) return false;
    if (s_count >= MAX_WIFI_DEVICES) return false;

    uint8_t idx = s_count;
    WifiDevice* dev = &s_devices[idx];
    memzero(dev, sizeof(WifiDevice));

    dev->usbAddress = usbAddress;
    dev->vendorId   = usbDev->devDesc.idVendor;
    dev->productId  = usbDev->devDesc.idProduct;

    // Find the vendor-specific interface
    for (uint8_t i = 0; i < usbDev->numInterfaces; ++i) {
        if (usbDev->interfaceClass[i] == usb::CLASS_VENDOR_SPECIFIC ||
            usbDev->interfaceClass[i] == usb::CLASS_WIRELESS) {
            dev->interfaceNum = i;
            break;
        }
    }

    if (!find_endpoints(usbDev, dev)) return false;

    // Read MAC address
    read_mac_address(dev);

    // Some adapters need firmware download before they become operational.
    // This is highly chip-specific and would be handled by sub-modules.
    // send_vendor_cmd(dev, VCMD_FW_DOWNLOAD, firmware, fwLen);

    dev->state  = WIFI_IDLE;
    dev->active = true;
    s_count++;

    if (parentIndex) *parentIndex = idx;
    return true;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_WIFI_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            if (s_devices[i].state == WIFI_CONNECTED) {
                // Send deauth
                send_vendor_cmd(&s_devices[i], VCMD_DEAUTH, nullptr, 0);
            }
            s_devices[i].active = false;
            s_devices[i].state  = WIFI_IDLE;
            s_count--;
        }
    }
}

void poll(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active) return;

    // Poll for events from the device (scan results, assoc responses,
    // EAPOL frames, etc.)
    if (dev->notifyEP != 0) {
        uint8_t evtBuf[64];
        uint16_t recvd = 0;

        usb::TransferStatus st = usb::hci::interrupt_transfer(
            dev->usbAddress, dev->notifyEP,
            evtBuf, sizeof(evtBuf), &recvd);

        if (st == usb::XFER_SUCCESS && recvd > 0) {
            // Interpret chip-specific event (stub)
            // For scan complete events:
            if (dev->state == WIFI_SCANNING) {
                // Check if scan is complete (chip-specific event code)
                // For now, transition to SCAN_COMPLETE after any event
                dev->state = WIFI_SCAN_COMPLETE;
            }
        }
    }

    // Check for incoming data frames (may include EAPOL)
    if (dev->state == WIFI_ASSOCIATING || dev->state == WIFI_CONNECTED) {
        uint16_t recvd = 0;
        usb::TransferStatus st = usb::hci::bulk_transfer(
            dev->usbAddress, dev->bulkInEP,
            s_rxBuf, sizeof(s_rxBuf), &recvd);

        if (st == usb::XFER_SUCCESS && recvd > 0) {
            // Check for EAPOL frames (WPA handshake)
            // Some chips deliver data in 802.11 format, others as Ethernet
            if (recvd >= usb_net::ETH_HLEN && is_eapol_frame(s_rxBuf, recvd)) {
                handle_eapol(dev, s_rxBuf, recvd);
            }
        }
    }
}

// ----------------------------------------------------------------
// Scanning
// ----------------------------------------------------------------

usb::TransferStatus scan_start(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return usb::XFER_ERROR;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;
    if (dev->state == WIFI_CONNECTED) return usb::XFER_ERROR;

    dev->scanCount = 0;
    memzero(dev->scanResults, sizeof(dev->scanResults));

    usb::TransferStatus st = send_vendor_cmd(dev, VCMD_SCAN_START,
                                              nullptr, 0);
    if (st == usb::XFER_SUCCESS) {
        dev->state = WIFI_SCANNING;
    }
    return st;
}

WifiState get_state(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return WIFI_ERROR;
    return s_devices[devIndex].state;
}

uint8_t scan_result_count(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return 0;
    return s_devices[devIndex].scanCount;
}

const ScanResult* get_scan_result(uint8_t devIndex, uint8_t resultIdx)
{
    if (devIndex >= MAX_WIFI_DEVICES) return nullptr;
    if (resultIdx >= s_devices[devIndex].scanCount) return nullptr;
    return &s_devices[devIndex].scanResults[resultIdx];
}

// ----------------------------------------------------------------
// Connection management
// ----------------------------------------------------------------

usb::TransferStatus connect(uint8_t devIndex,
                             const char* ssid, uint8_t ssidLen,
                             WifiSecurity security,
                             const char* passphrase, uint8_t passLen)
{
    if (devIndex >= MAX_WIFI_DEVICES) return usb::XFER_ERROR;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    if (ssidLen > SSID_MAX_LEN) return usb::XFER_ERROR;

    // Store connection parameters
    memzero(dev->ssid, sizeof(dev->ssid));
    memcpy_bytes(dev->ssid, ssid, ssidLen);
    dev->ssidLen  = ssidLen;
    dev->security = security;

    // Derive PMK for WPA/WPA2
    if (security == WIFI_SEC_WPA || security == WIFI_SEC_WPA2) {
        if (!passphrase || passLen == 0) return usb::XFER_ERROR;
        derive_pmk(passphrase, passLen, ssid, ssidLen, dev->pmk);
    }

    // Find BSSID from scan results
    bool foundBSSID = false;
    for (uint8_t i = 0; i < dev->scanCount; ++i) {
        const ScanResult* sr = &dev->scanResults[i];
        if (!sr->valid) continue;
        if (sr->ssidLen == ssidLen &&
            mem_equal(sr->ssid, ssid, ssidLen)) {
            memcpy_bytes(dev->bssid, sr->bssid, BSSID_LEN);
            dev->channel = sr->channel;
            foundBSSID = true;
            break;
        }
    }

    // Set channel (if known)
    if (foundBSSID && dev->channel != 0) {
        send_vendor_cmd(dev, VCMD_SET_CHANNEL, &dev->channel, 1);
    }

    // Send join/auth/assoc sequence
    // Build join command with SSID and BSSID
    uint8_t joinBuf[64];
    memzero(joinBuf, sizeof(joinBuf));
    joinBuf[0] = ssidLen;
    memcpy_bytes(&joinBuf[1], ssid, ssidLen);
    if (foundBSSID) {
        memcpy_bytes(&joinBuf[33], dev->bssid, BSSID_LEN);
    }
    joinBuf[39] = static_cast<uint8_t>(security);

    usb::TransferStatus st = send_vendor_cmd(
        dev, VCMD_JOIN, joinBuf,
        static_cast<uint16_t>(40));

    if (st == usb::XFER_SUCCESS) {
        dev->state = (security == WIFI_SEC_OPEN) ?
            WIFI_CONNECTED : WIFI_ASSOCIATING;
    }

    return st;
}

usb::TransferStatus disconnect(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return usb::XFER_ERROR;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    dev->state = WIFI_DISCONNECTING;

    usb::TransferStatus st = send_vendor_cmd(dev, VCMD_DEAUTH,
                                              nullptr, 0);

    dev->state    = WIFI_IDLE;
    dev->ptkValid = false;
    dev->gtkValid = false;
    memzero(dev->bssid, BSSID_LEN);

    return st;
}

bool is_connected(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return false;
    return s_devices[devIndex].state == WIFI_CONNECTED;
}

int8_t get_rssi(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return -127;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active) return -127;

    // Query signal strength from firmware
    uint8_t rssiVal = 0;
    uint16_t recvd = 0;
    usb::TransferStatus st = read_vendor_resp(dev, VCMD_GET_SIGNAL,
                                               &rssiVal, 1);

    if (st == usb::XFER_SUCCESS && recvd >= 1) {
        dev->rssi = static_cast<int8_t>(rssiVal);
    }

    return dev->rssi;
}

// ----------------------------------------------------------------
// Data frame I/O
// ----------------------------------------------------------------

usb::TransferStatus send(uint8_t devIndex,
                          const void* frame, uint16_t len)
{
    if (devIndex >= MAX_WIFI_DEVICES) return usb::XFER_ERROR;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active || dev->state != WIFI_CONNECTED)
        return usb::XFER_ERROR;

    // Convert Ethernet frame to 802.11 data frame
    uint16_t dot11Len = build_data_frame(
        dev, static_cast<const uint8_t*>(frame), len,
        s_txBuf, sizeof(s_txBuf));

    if (dot11Len == 0) return usb::XFER_ERROR;

    uint16_t sent = 0;
    return usb::hci::bulk_transfer(dev->usbAddress, dev->bulkOutEP,
                                   s_txBuf, dot11Len, &sent);
}

usb::TransferStatus receive(uint8_t devIndex,
                             void* frame, uint16_t maxLen,
                             uint16_t* received)
{
    if (devIndex >= MAX_WIFI_DEVICES) return usb::XFER_ERROR;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active || dev->state != WIFI_CONNECTED)
        return usb::XFER_ERROR;

    uint16_t recvd = 0;
    usb::TransferStatus st = usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkInEP,
        s_rxBuf, sizeof(s_rxBuf), &recvd);

    if (st != usb::XFER_SUCCESS || recvd == 0) {
        if (received) *received = 0;
        return (st == usb::XFER_SUCCESS) ? usb::XFER_NAK : st;
    }

    // Check for EAPOL frames (group key refresh)
    if (recvd >= usb_net::ETH_HLEN && is_eapol_frame(s_rxBuf, recvd)) {
        handle_eapol(dev, s_rxBuf, recvd);
        if (received) *received = 0;
        return usb::XFER_NAK;
    }

    // Convert 802.11 to Ethernet
    uint16_t ethLen = extract_data_frame(
        dev, s_rxBuf, recvd,
        static_cast<uint8_t*>(frame), maxLen);

    if (ethLen == 0) {
        // Might already be Ethernet format (some chips do conversion)
        if (recvd >= usb_net::ETH_HLEN && recvd <= maxLen) {
            memcpy_bytes(frame, s_rxBuf, recvd);
            ethLen = recvd;
        } else {
            if (received) *received = 0;
            return usb::XFER_NAK;
        }
    }

    if (received) *received = ethLen;
    return usb::XFER_SUCCESS;
}

// ----------------------------------------------------------------
// Vendor command (public interface)
// ----------------------------------------------------------------

usb::TransferStatus vendor_command(uint8_t devIndex,
                                    VendorCmdType cmd,
                                    const void* data, uint16_t dataLen,
                                    void* resp, uint16_t respLen,
                                    uint16_t* respActual)
{
    if (devIndex >= MAX_WIFI_DEVICES) return usb::XFER_ERROR;
    WifiDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;

    if (data && dataLen > 0) {
        usb::TransferStatus st = send_vendor_cmd(dev, cmd, data, dataLen);
        if (st != usb::XFER_SUCCESS) return st;
    }

    if (resp && respLen > 0) {
        usb::TransferStatus st = read_vendor_resp(dev, cmd, resp, respLen);
        if (respActual) *respActual = (st == usb::XFER_SUCCESS) ? respLen : 0;
        return st;
    }

    if (respActual) *respActual = 0;
    return usb::XFER_SUCCESS;
}

const WifiDevice* get_device(uint8_t devIndex)
{
    if (devIndex >= MAX_WIFI_DEVICES) return nullptr;
    if (!s_devices[devIndex].active) return nullptr;
    return &s_devices[devIndex];
}

uint8_t count()
{
    return s_count;
}

} // namespace usb_net_wifi
} // namespace kernel
