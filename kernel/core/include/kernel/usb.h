// USB Core Subsystem
//
// Provides USB specification constants, descriptor structures,
// transfer abstractions, and a platform-independent device model.
// Architecture-specific host-controller drivers implement the
// low-level HCI interface declared here.
//
// Reference: USB 2.0 Specification (usb.org)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_H
#define KERNEL_USB_H

#include "kernel/types.h"

namespace kernel {
namespace usb {

// ================================================================
// USB specification constants
// ================================================================

static const uint8_t  MAX_DEVICES       = 127;
static const uint8_t  MAX_ENDPOINTS     = 16;   // per direction
static const uint16_t DEFAULT_MAX_PKT   = 64;

// ================================================================
// Descriptor types (bDescriptorType)
// ================================================================

enum DescriptorType : uint8_t {
    DESC_DEVICE            = 0x01,
    DESC_CONFIGURATION     = 0x02,
    DESC_STRING            = 0x03,
    DESC_INTERFACE         = 0x04,
    DESC_ENDPOINT          = 0x05,
    DESC_DEVICE_QUALIFIER  = 0x06,
    DESC_OTHER_SPEED_CFG   = 0x07,
    DESC_INTERFACE_POWER   = 0x08,
    DESC_HID               = 0x21,
    DESC_HID_REPORT        = 0x22,
    DESC_HID_PHYSICAL      = 0x23,
};

// ================================================================
// USB class codes (bDeviceClass / bInterfaceClass)
// ================================================================

enum ClassCode : uint8_t {
    CLASS_PER_INTERFACE    = 0x00,
    CLASS_AUDIO            = 0x01,
    CLASS_CDC              = 0x02,
    CLASS_HID              = 0x03,
    CLASS_PHYSICAL         = 0x05,
    CLASS_IMAGE            = 0x06,
    CLASS_PRINTER          = 0x07,
    CLASS_MASS_STORAGE     = 0x08,
    CLASS_HUB              = 0x09,
    CLASS_CDC_DATA         = 0x0A,
    CLASS_SMART_CARD       = 0x0B,
    CLASS_VIDEO            = 0x0E,
    CLASS_AUDIO_VIDEO      = 0x10,
    CLASS_WIRELESS         = 0xE0,
    CLASS_MISC             = 0xEF,
    CLASS_APP_SPECIFIC     = 0xFE,
    CLASS_VENDOR_SPECIFIC  = 0xFF,
};

// ================================================================
// HID subclass / protocol codes
// ================================================================

enum HIDSubclass : uint8_t {
    HID_SUBCLASS_NONE = 0x00,
    HID_SUBCLASS_BOOT = 0x01,
};

enum HIDProtocol : uint8_t {
    HID_PROTOCOL_NONE     = 0x00,
    HID_PROTOCOL_KEYBOARD = 0x01,
    HID_PROTOCOL_MOUSE    = 0x02,
};

// ================================================================
// Mass Storage subclass / protocol codes
// ================================================================

enum MSCSubclass : uint8_t {
    MSC_SUBCLASS_SCSI_NOT_REPORTED = 0x00,
    MSC_SUBCLASS_RBC               = 0x01,
    MSC_SUBCLASS_MMC5              = 0x02,  // ATAPI
    MSC_SUBCLASS_QIC157            = 0x03,
    MSC_SUBCLASS_UFI               = 0x04,
    MSC_SUBCLASS_SFF8070I          = 0x05,
    MSC_SUBCLASS_SCSI_TRANSPARENT  = 0x06,
};

enum MSCProtocol : uint8_t {
    MSC_PROTOCOL_CBI_WITH_CC  = 0x00,
    MSC_PROTOCOL_CBI_NO_CC    = 0x01,
    MSC_PROTOCOL_BULK_ONLY    = 0x50,
};

// ================================================================
// CDC subclass codes
// ================================================================

enum CDCSubclass : uint8_t {
    CDC_SUBCLASS_DIRECT_LINE = 0x01,
    CDC_SUBCLASS_ACM         = 0x02,  // Abstract Control Model (serial)
    CDC_SUBCLASS_TELEPHONE   = 0x03,
    CDC_SUBCLASS_MULTI_CHAN  = 0x04,
    CDC_SUBCLASS_CAPI        = 0x05,
    CDC_SUBCLASS_ETHERNET    = 0x06,  // ECM
    CDC_SUBCLASS_ATM         = 0x07,
    CDC_SUBCLASS_WIRELESS    = 0x08,
    CDC_SUBCLASS_NCM         = 0x0D,
};

// ================================================================
// Transfer types
// ================================================================

enum TransferType : uint8_t {
    TRANSFER_CONTROL     = 0,
    TRANSFER_ISOCHRONOUS = 1,
    TRANSFER_BULK        = 2,
    TRANSFER_INTERRUPT   = 3,
};

// ================================================================
// Transfer direction
// ================================================================

enum Direction : uint8_t {
    DIR_HOST_TO_DEVICE = 0,
    DIR_DEVICE_TO_HOST = 1,
};

// ================================================================
// Transfer status
// ================================================================

enum TransferStatus : uint8_t {
    XFER_SUCCESS        = 0,
    XFER_ERROR          = 1,
    XFER_STALL          = 2,
    XFER_TIMEOUT        = 3,
    XFER_NOT_SUPPORTED  = 4,
    XFER_NAK            = 5,
    XFER_DATA_OVERRUN   = 6,
    XFER_DATA_UNDERRUN  = 7,
    XFER_BUFFER_ERROR   = 8,
    XFER_CANCELLED      = 9,
};

// ================================================================
// Device speed
// ================================================================

enum DeviceSpeed : uint8_t {
    SPEED_LOW   = 0,   // 1.5 Mbps
    SPEED_FULL  = 1,   // 12  Mbps
    SPEED_HIGH  = 2,   // 480 Mbps
};

// ================================================================
// Standard request codes (bRequest)
// ================================================================

enum StandardRequest : uint8_t {
    REQ_GET_STATUS        = 0x00,
    REQ_CLEAR_FEATURE     = 0x01,
    REQ_SET_FEATURE       = 0x03,
    REQ_SET_ADDRESS       = 0x05,
    REQ_GET_DESCRIPTOR    = 0x06,
    REQ_SET_DESCRIPTOR    = 0x07,
    REQ_GET_CONFIGURATION = 0x08,
    REQ_SET_CONFIGURATION = 0x09,
    REQ_GET_INTERFACE     = 0x0A,
    REQ_SET_INTERFACE     = 0x0B,
    REQ_SYNCH_FRAME       = 0x0C,
};

// ================================================================
// Setup packet (8 bytes, USB 2.0 §9.3)
// ================================================================

struct SetupPacket {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

// ================================================================
// Standard descriptors (packed for direct parsing from wire data)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define USB_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define USB_PACKED
#endif

struct DeviceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} USB_PACKED;

struct ConfigDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} USB_PACKED;

struct InterfaceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} USB_PACKED;

struct EndpointDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} USB_PACKED;

struct HIDDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} USB_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef USB_PACKED

// ================================================================
// Endpoint abstraction
// ================================================================

struct Endpoint {
    uint8_t       address;       // endpoint number + direction bit
    TransferType  type;
    Direction     dir;
    uint16_t      maxPacketSize;
    uint8_t       interval;      // polling interval (interrupt/iso)
    bool          active;
};

// ================================================================
// USB device representation
// ================================================================

static const uint8_t MAX_INTERFACES_PER_DEVICE = 8;

struct Device {
    bool             present;
    uint8_t          address;         // assigned USB address (1-127)
    DeviceSpeed      speed;
    uint8_t          hubPort;         // port on parent hub (0 = root)
    DeviceDescriptor devDesc;
    uint8_t          currentConfig;
    Endpoint         endpoints[MAX_ENDPOINTS * 2]; // IN + OUT
    uint8_t          interfaceClass[MAX_INTERFACES_PER_DEVICE];
    uint8_t          interfaceSubClass[MAX_INTERFACES_PER_DEVICE];
    uint8_t          interfaceProtocol[MAX_INTERFACES_PER_DEVICE];
    uint8_t          numInterfaces;
};

// ================================================================
// Host Controller Interface (HCI) — arch-specific implementations
//
// Each architecture provides an implementation of these functions
// that talks to the actual USB host controller hardware (UHCI,
// OHCI, EHCI, xHCI, DWC-OTG, etc.).
// ================================================================

namespace hci {

// Initialise the host controller hardware, reset ports, enable power.
// Returns true if a working host controller was found.
bool init();

// Return true if the HCI is present and initialised.
bool is_available();

// Reset a specific root-hub port and return the device speed, or
// SPEED_LOW if the port has nothing connected.
DeviceSpeed port_reset(uint8_t port);

// Return the number of root-hub ports.
uint8_t port_count();

// Return true if the port has a device connected.
bool port_connected(uint8_t port);

// Issue a control transfer on the default pipe (endpoint 0).
TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen);

// Issue a bulk transfer to/from an endpoint.
TransferStatus bulk_transfer(uint8_t deviceAddr,
                             uint8_t endpointAddr,
                             void* data,
                             uint16_t dataLen,
                             uint16_t* bytesTransferred);

// Issue an interrupt transfer (single poll).
TransferStatus interrupt_transfer(uint8_t deviceAddr,
                                  uint8_t endpointAddr,
                                  void* data,
                                  uint16_t dataLen,
                                  uint16_t* bytesTransferred);

} // namespace hci

// ================================================================
// USB core API
// ================================================================

// Initialise the USB subsystem (probes HCI, enumerates devices).
void init();

// Poll for device attach/detach and service pending transfers.
void poll();

// Return the device at the given address, or nullptr.
const Device* get_device(uint8_t address);

// Return the number of currently attached devices.
uint8_t device_count();

// Perform a control transfer on device endpoint 0.
TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen);

// Read a descriptor from a device.
TransferStatus get_descriptor(uint8_t deviceAddr,
                              uint8_t descType,
                              uint8_t descIndex,
                              uint16_t langId,
                              void* buffer,
                              uint16_t bufLen);

// Set the active configuration on a device.
TransferStatus set_configuration(uint8_t deviceAddr,
                                 uint8_t configValue);

// Assign a USB address to a newly-attached device.
TransferStatus set_address(uint8_t deviceAddr, uint8_t newAddr);

} // namespace usb
} // namespace kernel

#endif // KERNEL_USB_H
