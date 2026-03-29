// USB Wi-Fi Network Driver
//
// Provides a generic framework for USB Wi-Fi adapters.  Most USB
// Wi-Fi chipsets (Realtek RTL8xxxU, Atheros AR9271/QCA, MediaTek,
// Ralink) use vendor-specific bulk interfaces with firmware-defined
// command/event protocols.  This driver provides:
//
//   - Common state machine: IDLE ? SCANNING ? ASSOCIATING ? CONNECTED
//   - SSID/passphrase management
//   - Scan request / result storage
//   - WPA/WPA2 4-way handshake key derivation stubs
//   - 802.11 data frame encapsulation/decapsulation
//   - Vendor command abstraction for chip-specific firmware interaction
//
// Architecture-independent — works through the USB HCI abstraction
// on x86, amd64, ARM, SPARC v8, SPARC v9, and IA-64.
//
// Note: Full Wi-Fi functionality requires chip-specific firmware
// blobs and vendor command sets.  This module provides the framework
// and common protocol handling; chip-specific backends would be added
// as sub-modules.
//
// Reference: IEEE 802.11-2016, WPA2 (IEEE 802.11i)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_NET_WIFI_H
#define KERNEL_USB_NET_WIFI_H

#include "kernel/types.h"
#include "kernel/usb.h"
#include "kernel/usb_net.h"

namespace kernel {
namespace usb_net_wifi {

// ================================================================
// Wi-Fi constants
// ================================================================

static const uint8_t  SSID_MAX_LEN       = 32;
static const uint8_t  MAX_SCAN_RESULTS   = 16;
static const uint8_t  MAX_WIFI_DEVICES   = 2;
static const uint8_t  BSSID_LEN          = 6;
static const uint8_t  WPA_KEY_LEN        = 32;   // PMK length
static const uint16_t WIFI_FRAME_MAX     = 2346; // max 802.11 MSDU

// ================================================================
// Wi-Fi state machine
// ================================================================

enum WifiState : uint8_t {
    WIFI_IDLE          = 0,
    WIFI_SCANNING      = 1,
    WIFI_SCAN_COMPLETE = 2,
    WIFI_ASSOCIATING   = 3,
    WIFI_CONNECTED     = 4,
    WIFI_DISCONNECTING = 5,
    WIFI_ERROR         = 6,
};

// ================================================================
// Security / authentication types
// ================================================================

enum WifiSecurity : uint8_t {
    WIFI_SEC_OPEN     = 0,
    WIFI_SEC_WEP      = 1,
    WIFI_SEC_WPA      = 2,
    WIFI_SEC_WPA2     = 3,
    WIFI_SEC_WPA3     = 4,   // future
};

// ================================================================
// 802.11 band
// ================================================================

enum WifiBand : uint8_t {
    BAND_2GHZ = 0,
    BAND_5GHZ = 1,
};

// ================================================================
// Scan result entry
// ================================================================

struct ScanResult {
    bool     valid;
    uint8_t  bssid[BSSID_LEN];
    char     ssid[SSID_MAX_LEN + 1]; // null-terminated
    uint8_t  ssidLen;
    uint8_t  channel;
    int8_t   rssi;                    // dBm
    WifiSecurity security;
    WifiBand band;
};

// ================================================================
// Vendor command abstraction
//
// Different chipsets use different command formats over bulk or
// control endpoints.  This structure encodes a generic command
// buffer that chip-specific backends can interpret.
// ================================================================

static const uint16_t VENDOR_CMD_MAX_LEN = 256;

enum VendorCmdType : uint8_t {
    VCMD_SCAN_START    = 0x01,
    VCMD_SCAN_RESULTS  = 0x02,
    VCMD_JOIN          = 0x03,
    VCMD_AUTH          = 0x04,
    VCMD_ASSOC         = 0x05,
    VCMD_DEAUTH        = 0x06,
    VCMD_SET_CHANNEL   = 0x07,
    VCMD_SET_KEY       = 0x08,
    VCMD_GET_SIGNAL    = 0x09,
    VCMD_POWER_SAVE    = 0x0A,
    VCMD_FW_DOWNLOAD   = 0x0B,
};

// ================================================================
// 802.11 frame header (minimal, for data frames)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define WIFI_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define WIFI_PACKED
#endif

struct Dot11FrameHeader {
    uint16_t frameControl;
    uint16_t durationId;
    uint8_t  addr1[6];           // receiver / destination
    uint8_t  addr2[6];           // transmitter / source
    uint8_t  addr3[6];           // BSSID / destination
    uint16_t seqControl;
} WIFI_PACKED;

// 802.11 LLC/SNAP header (used to carry Ethernet frames)
struct Dot11LLCSnap {
    uint8_t  dsap;               // 0xAA
    uint8_t  ssap;               // 0xAA
    uint8_t  control;            // 0x03
    uint8_t  oui[3];             // 0x00, 0x00, 0x00
    uint16_t etherType;          // big-endian
} WIFI_PACKED;

// EAPOL (802.1X) key frame header — for WPA handshake
struct EAPOLKeyFrame {
    uint8_t  protocolVersion;
    uint8_t  packetType;         // 0x03 = Key
    uint16_t packetBodyLength;   // big-endian
    uint8_t  descriptorType;     // 0x02 = RSN key descriptor
    uint16_t keyInfo;            // big-endian
    uint16_t keyLength;
    uint8_t  replayCounter[8];
    uint8_t  keyNonce[32];
    uint8_t  keyIV[16];
    uint8_t  keyRSC[8];
    uint8_t  reserved[8];
    uint8_t  keyMIC[16];
    uint16_t keyDataLength;      // big-endian
} WIFI_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef WIFI_PACKED

// Frame control field bits
static const uint16_t FC_TYPE_DATA       = 0x0008;
static const uint16_t FC_SUBTYPE_DATA    = 0x0000;
static const uint16_t FC_TO_DS           = 0x0100;
static const uint16_t FC_FROM_DS         = 0x0200;
static const uint16_t FC_PROTECTED       = 0x4000;

// ================================================================
// Wi-Fi device state
// ================================================================

struct WifiDevice {
    bool          active;
    uint8_t       usbAddress;
    uint8_t       interfaceNum;
    uint8_t       bulkInEP;
    uint8_t       bulkOutEP;
    uint8_t       notifyEP;        // interrupt or second bulk for events
    uint16_t      bulkInMaxPkt;
    uint16_t      bulkOutMaxPkt;
    uint16_t      notifyMaxPkt;

    // MAC address (from firmware or OTP)
    uint8_t       macAddress[6];

    // State machine
    WifiState     state;

    // Connection info
    uint8_t       bssid[BSSID_LEN];
    char          ssid[SSID_MAX_LEN + 1];
    uint8_t       ssidLen;
    uint8_t       channel;
    WifiSecurity  security;
    int8_t        rssi;

    // Key material (WPA/WPA2)
    uint8_t       pmk[WPA_KEY_LEN];      // Pairwise Master Key
    uint8_t       ptk[64];               // Pairwise Transient Key
    bool          ptkValid;
    uint8_t       gtk[32];               // Group Temporal Key
    bool          gtkValid;
    uint8_t       anonce[32];            // Authenticator nonce
    uint8_t       snonce[32];            // Supplicant nonce

    // Scan results
    ScanResult    scanResults[MAX_SCAN_RESULTS];
    uint8_t       scanCount;

    // Sequence number for transmitted frames
    uint16_t      txSeqNum;

    // Vendor-specific chip identification
    uint16_t      vendorId;
    uint16_t      productId;
};

// ================================================================
// Public API
// ================================================================

void init();

// Probe a USB device for Wi-Fi adapter interface.
// Returns true and fills parentIndex with the usb_net device index.
bool probe(uint8_t usbAddress, uint8_t* parentIndex);

void release(uint8_t usbAddress);

// Poll for events (scan complete, assoc response, data frames).
void poll(uint8_t devIndex);

// ----------------------------------------------------------------
// Scanning
// ----------------------------------------------------------------

// Start a scan (async — poll() will update scan results).
usb::TransferStatus scan_start(uint8_t devIndex);

// Get current state (check for WIFI_SCAN_COMPLETE).
WifiState get_state(uint8_t devIndex);

// Get number of scan results.
uint8_t scan_result_count(uint8_t devIndex);

// Get a scan result by index.
const ScanResult* get_scan_result(uint8_t devIndex, uint8_t resultIdx);

// ----------------------------------------------------------------
// Connection management
// ----------------------------------------------------------------

// Connect to an AP with the given SSID and passphrase.
// For WIFI_SEC_OPEN, passphrase can be nullptr.
usb::TransferStatus connect(uint8_t devIndex,
                             const char* ssid, uint8_t ssidLen,
                             WifiSecurity security,
                             const char* passphrase, uint8_t passLen);

// Disconnect from the current AP.
usb::TransferStatus disconnect(uint8_t devIndex);

// Check if connected.
bool is_connected(uint8_t devIndex);

// Get current signal strength (dBm).
int8_t get_rssi(uint8_t devIndex);

// ----------------------------------------------------------------
// Data frame I/O
//
// These handle 802.11-to-Ethernet conversion internally.
// Callers send/receive standard Ethernet frames.
// ----------------------------------------------------------------

usb::TransferStatus send(uint8_t devIndex,
                          const void* frame, uint16_t len);

usb::TransferStatus receive(uint8_t devIndex,
                             void* frame, uint16_t maxLen,
                             uint16_t* received);

// ----------------------------------------------------------------
// Low-level vendor command interface
// ----------------------------------------------------------------

usb::TransferStatus vendor_command(uint8_t devIndex,
                                    VendorCmdType cmd,
                                    const void* data, uint16_t dataLen,
                                    void* resp, uint16_t respLen,
                                    uint16_t* respActual);

// Get device info
const WifiDevice* get_device(uint8_t devIndex);

uint8_t count();

} // namespace usb_net_wifi
} // namespace kernel

#endif // KERNEL_USB_NET_WIFI_H
