// USB Core Subsystem — Implementation
//
// Device enumeration, address assignment, descriptor parsing,
// and class-driver dispatch.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb {

// ================================================================
// Internal state
// ================================================================

static Device s_devices[MAX_DEVICES];
static uint8_t s_deviceCount = 0;
static uint8_t s_nextAddress = 1;
static bool    s_initialised = false;

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
// Allocate next device address (1-127)
// ================================================================

static uint8_t alloc_address()
{
    for (uint8_t i = 1; i < MAX_DEVICES; ++i) {
        uint8_t candidate = static_cast<uint8_t>(((s_nextAddress - 1 + i) % (MAX_DEVICES - 1)) + 1);
        bool in_use = false;
        for (uint8_t j = 0; j < MAX_DEVICES; ++j) {
            if (s_devices[j].present && s_devices[j].address == candidate) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            s_nextAddress = static_cast<uint8_t>(candidate + 1);
            if (s_nextAddress > MAX_DEVICES - 1) s_nextAddress = 1;
            return candidate;
        }
    }
    return 0; // no free addresses
}

// ================================================================
// Find a free slot in the device table
// ================================================================

static int find_free_slot()
{
    for (uint8_t i = 0; i < MAX_DEVICES; ++i) {
        if (!s_devices[i].present) return static_cast<int>(i);
    }
    return -1;
}

// ================================================================
// Parse a configuration descriptor and populate device info
// ================================================================

static void parse_config(Device* dev, const uint8_t* data, uint16_t totalLen)
{
    uint16_t offset = 0;
    dev->numInterfaces = 0;

    while (offset + 2 <= totalLen) {
        uint8_t bLength = data[offset];
        uint8_t bType   = data[offset + 1];

        if (bLength == 0) break; // malformed — avoid infinite loop

        if (bType == DESC_INTERFACE && bLength >= 9 &&
            offset + bLength <= totalLen) {
            const InterfaceDescriptor* iface =
                reinterpret_cast<const InterfaceDescriptor*>(&data[offset]);

            if (dev->numInterfaces < MAX_INTERFACES_PER_DEVICE) {
                uint8_t idx = dev->numInterfaces;
                dev->interfaceClass[idx]    = iface->bInterfaceClass;
                dev->interfaceSubClass[idx] = iface->bInterfaceSubClass;
                dev->interfaceProtocol[idx] = iface->bInterfaceProtocol;
                dev->numInterfaces++;
            }
        }
        else if (bType == DESC_ENDPOINT && bLength >= 7 &&
                 offset + bLength <= totalLen) {
            const EndpointDescriptor* ep =
                reinterpret_cast<const EndpointDescriptor*>(&data[offset]);

            uint8_t epNum = ep->bEndpointAddress & 0x0F;
            uint8_t dirBit = (ep->bEndpointAddress & 0x80) ? 1 : 0;
            uint8_t idx = static_cast<uint8_t>(epNum * 2 + dirBit);

            if (idx < MAX_ENDPOINTS * 2) {
                dev->endpoints[idx].address       = ep->bEndpointAddress;
                dev->endpoints[idx].type           = static_cast<TransferType>(ep->bmAttributes & 0x03);
                dev->endpoints[idx].dir            = dirBit ? DIR_DEVICE_TO_HOST : DIR_HOST_TO_DEVICE;
                dev->endpoints[idx].maxPacketSize  = ep->wMaxPacketSize & 0x07FF;
                dev->endpoints[idx].interval       = ep->bInterval;
                dev->endpoints[idx].active         = true;
            }
        }

        offset += bLength;
    }
}

// ================================================================
// Enumerate a single device on a root-hub port
// ================================================================

static bool enumerate_device(uint8_t port, DeviceSpeed speed)
{
    int slot = find_free_slot();
    if (slot < 0) return false;

    uint8_t addr = alloc_address();
    if (addr == 0) return false;

    Device* dev = &s_devices[slot];
    memzero(dev, sizeof(Device));

    // 1. Read device descriptor at address 0 (8 bytes first to get bMaxPacketSize0)
    DeviceDescriptor shortDesc;
    memzero(&shortDesc, sizeof(shortDesc));

    SetupPacket setup;
    setup.bmRequestType = 0x80; // Device-to-host, standard, device
    setup.bRequest      = REQ_GET_DESCRIPTOR;
    setup.wValue        = static_cast<uint16_t>(DESC_DEVICE << 8);
    setup.wIndex        = 0;
    setup.wLength       = 8;

    TransferStatus st = hci::control_transfer(0, &setup, &shortDesc, 8);
    if (st != XFER_SUCCESS) return false;

    // 2. Set address
    SetupPacket setAddr;
    setAddr.bmRequestType = 0x00; // Host-to-device, standard, device
    setAddr.bRequest      = REQ_SET_ADDRESS;
    setAddr.wValue        = addr;
    setAddr.wIndex        = 0;
    setAddr.wLength       = 0;

    st = hci::control_transfer(0, &setAddr, nullptr, 0);
    if (st != XFER_SUCCESS) return false;

    // Small delay for device to process SET_ADDRESS
    for (volatile int i = 0; i < 50000; ++i) {}

    // 3. Read full device descriptor at new address
    setup.wLength = sizeof(DeviceDescriptor);
    st = hci::control_transfer(addr, &setup, &dev->devDesc, sizeof(DeviceDescriptor));
    if (st != XFER_SUCCESS) return false;

    dev->present      = true;
    dev->address      = addr;
    dev->speed        = speed;
    dev->hubPort      = port;
    dev->currentConfig = 0;

    // 4. Read configuration descriptor (first 9 bytes to get wTotalLength)
    uint8_t cfgBuf[256];
    memzero(cfgBuf, sizeof(cfgBuf));

    SetupPacket getCfg;
    getCfg.bmRequestType = 0x80;
    getCfg.bRequest      = REQ_GET_DESCRIPTOR;
    getCfg.wValue        = static_cast<uint16_t>(DESC_CONFIGURATION << 8);
    getCfg.wIndex        = 0;
    getCfg.wLength       = 9;

    st = hci::control_transfer(addr, &getCfg, cfgBuf, 9);
    if (st != XFER_SUCCESS) {
        dev->present = false;
        return false;
    }

    const ConfigDescriptor* cfgDesc =
        reinterpret_cast<const ConfigDescriptor*>(cfgBuf);
    uint16_t totalLen = cfgDesc->wTotalLength;
    if (totalLen > sizeof(cfgBuf)) totalLen = sizeof(cfgBuf);

    // Read full configuration
    getCfg.wLength = totalLen;
    st = hci::control_transfer(addr, &getCfg, cfgBuf, totalLen);
    if (st != XFER_SUCCESS) {
        dev->present = false;
        return false;
    }

    parse_config(dev, cfgBuf, totalLen);

    // 5. Set configuration
    st = set_configuration(addr, cfgDesc->bConfigurationValue);
    if (st != XFER_SUCCESS) {
        dev->present = false;
        return false;
    }
    dev->currentConfig = cfgDesc->bConfigurationValue;

    s_deviceCount++;
    return true;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
    s_nextAddress = 1;
    s_initialised = false;

    if (!hci::init()) return;

    s_initialised = true;

    // Enumerate devices on each root-hub port
    uint8_t ports = hci::port_count();
    for (uint8_t p = 0; p < ports; ++p) {
        if (hci::port_connected(p)) {
            DeviceSpeed spd = hci::port_reset(p);
            enumerate_device(p, spd);
        }
    }
}

void poll()
{
    if (!s_initialised) return;

    // Check for newly attached / detached devices
    uint8_t ports = hci::port_count();
    for (uint8_t p = 0; p < ports; ++p) {
        bool connected = hci::port_connected(p);

        // Find existing device on this port
        int existingSlot = -1;
        for (uint8_t i = 0; i < MAX_DEVICES; ++i) {
            if (s_devices[i].present && s_devices[i].hubPort == p) {
                existingSlot = static_cast<int>(i);
                break;
            }
        }

        if (connected && existingSlot < 0) {
            // New device — enumerate
            DeviceSpeed spd = hci::port_reset(p);
            enumerate_device(p, spd);
        }
        else if (!connected && existingSlot >= 0) {
            // Device removed
            s_devices[existingSlot].present = false;
            s_deviceCount--;
        }
    }
}

const Device* get_device(uint8_t address)
{
    for (uint8_t i = 0; i < MAX_DEVICES; ++i) {
        if (s_devices[i].present && s_devices[i].address == address) {
            return &s_devices[i];
        }
    }
    return nullptr;
}

uint8_t device_count()
{
    return s_deviceCount;
}

TransferStatus control_transfer(uint8_t deviceAddr,
                                const SetupPacket* setup,
                                void* data,
                                uint16_t dataLen)
{
    if (!s_initialised) return XFER_NOT_SUPPORTED;
    return hci::control_transfer(deviceAddr, setup, data, dataLen);
}

TransferStatus get_descriptor(uint8_t deviceAddr,
                              uint8_t descType,
                              uint8_t descIndex,
                              uint16_t langId,
                              void* buffer,
                              uint16_t bufLen)
{
    SetupPacket setup;
    setup.bmRequestType = 0x80;
    setup.bRequest      = REQ_GET_DESCRIPTOR;
    setup.wValue        = static_cast<uint16_t>((descType << 8) | descIndex);
    setup.wIndex        = langId;
    setup.wLength       = bufLen;

    return control_transfer(deviceAddr, &setup, buffer, bufLen);
}

TransferStatus set_configuration(uint8_t deviceAddr, uint8_t configValue)
{
    SetupPacket setup;
    setup.bmRequestType = 0x00;
    setup.bRequest      = REQ_SET_CONFIGURATION;
    setup.wValue        = configValue;
    setup.wIndex        = 0;
    setup.wLength       = 0;

    return control_transfer(deviceAddr, &setup, nullptr, 0);
}

TransferStatus set_address(uint8_t deviceAddr, uint8_t newAddr)
{
    SetupPacket setup;
    setup.bmRequestType = 0x00;
    setup.bRequest      = REQ_SET_ADDRESS;
    setup.wValue        = newAddr;
    setup.wIndex        = 0;
    setup.wLength       = 0;

    return control_transfer(deviceAddr, &setup, nullptr, 0);
}

} // namespace usb
} // namespace kernel
