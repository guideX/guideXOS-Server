// USB Mass Storage Class Driver — Implementation
//
// Bulk-Only Transport (BBB) with SCSI Transparent Command Set.
// Handles INQUIRY, TEST UNIT READY, READ CAPACITY(10),
// READ(10), WRITE(10), and REQUEST SENSE.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/usb_storage.h"
#include "include/kernel/usb.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace usb_storage {

// ================================================================
// Internal state
// ================================================================

static StorageDevice s_devices[MAX_STORAGE_DEVICES];
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

// Big-endian helpers (SCSI uses BE for multi-byte fields)
static uint32_t be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

static void put_be16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

// ================================================================
// Bulk-Only Transport helpers
// ================================================================

static usb::TransferStatus send_cbw(StorageDevice* dev,
                                    const CommandBlockWrapper* cbw)
{
    uint16_t sent = 0;
    return usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkOutEP,
        const_cast<CommandBlockWrapper*>(cbw),
        sizeof(CommandBlockWrapper), &sent);
}

static usb::TransferStatus receive_csw(StorageDevice* dev,
                                       CommandStatusWrapper* csw)
{
    uint16_t recvd = 0;
    usb::TransferStatus st = usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkInEP,
        csw, sizeof(CommandStatusWrapper), &recvd);

    if (st != usb::XFER_SUCCESS) return st;
    if (recvd < sizeof(CommandStatusWrapper)) return usb::XFER_DATA_UNDERRUN;
    if (csw->dCSWSignature != CSW_SIGNATURE) return usb::XFER_ERROR;
    return usb::XFER_SUCCESS;
}

static usb::TransferStatus bulk_in_data(StorageDevice* dev,
                                        void* buf, uint16_t len,
                                        uint16_t* received)
{
    return usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkInEP,
        buf, len, received);
}

static usb::TransferStatus bulk_out_data(StorageDevice* dev,
                                         const void* buf, uint16_t len,
                                         uint16_t* sent)
{
    return usb::hci::bulk_transfer(
        dev->usbAddress, dev->bulkOutEP,
        const_cast<void*>(buf), len, sent);
}

// ================================================================
// Build a CBW for a SCSI command
// ================================================================

static void build_cbw(StorageDevice* dev,
                      CommandBlockWrapper* cbw,
                      uint8_t direction,      // 0x80 = IN, 0x00 = OUT
                      uint32_t dataLen,
                      const uint8_t* scsiCmd,
                      uint8_t scsiCmdLen)
{
    memzero(cbw, sizeof(CommandBlockWrapper));
    cbw->dCBWSignature          = CBW_SIGNATURE;
    cbw->dCBWTag                = dev->cbwTag++;
    cbw->dCBWDataTransferLength = dataLen;
    cbw->bmCBWFlags             = direction;
    cbw->bCBWLUN                = dev->lun;
    cbw->bCBWCBLength           = scsiCmdLen;
    memcopy(cbw->CBWCB, scsiCmd, scsiCmdLen);
}

// ================================================================
// Execute a complete BOT transaction (CBW ? Data ? CSW)
// ================================================================

static usb::TransferStatus bot_transfer(StorageDevice* dev,
                                        uint8_t direction,
                                        const uint8_t* scsiCmd,
                                        uint8_t scsiCmdLen,
                                        void* data,
                                        uint32_t dataLen)
{
    CommandBlockWrapper cbw;
    build_cbw(dev, &cbw, direction, dataLen, scsiCmd, scsiCmdLen);

    // Send CBW
    usb::TransferStatus st = send_cbw(dev, &cbw);
    if (st != usb::XFER_SUCCESS) return st;

    // Data phase (if any)
    if (dataLen > 0 && data != nullptr) {
        uint16_t xferred = 0;
        if (direction == 0x80) {
            // Data IN — may need to transfer in chunks
            uint8_t* ptr = static_cast<uint8_t*>(data);
            uint32_t remaining = dataLen;
            while (remaining > 0) {
                uint16_t chunk = (remaining > 0xFFFF) ? 0xFFFF :
                                 static_cast<uint16_t>(remaining);
                st = bulk_in_data(dev, ptr, chunk, &xferred);
                if (st != usb::XFER_SUCCESS) break;
                ptr += xferred;
                remaining -= xferred;
                if (xferred < chunk) break; // short transfer
            }
        } else {
            // Data OUT
            const uint8_t* ptr = static_cast<const uint8_t*>(data);
            uint32_t remaining = dataLen;
            while (remaining > 0) {
                uint16_t chunk = (remaining > 0xFFFF) ? 0xFFFF :
                                 static_cast<uint16_t>(remaining);
                st = bulk_out_data(dev, ptr, chunk, &xferred);
                if (st != usb::XFER_SUCCESS) break;
                ptr += xferred;
                remaining -= xferred;
                if (xferred < chunk) break;
            }
        }
        if (st != usb::XFER_SUCCESS) {
            // Attempt to read CSW anyway to clear the error
            CommandStatusWrapper csw;
            receive_csw(dev, &csw);
            return st;
        }
    }

    // Receive CSW
    CommandStatusWrapper csw;
    st = receive_csw(dev, &csw);
    if (st != usb::XFER_SUCCESS) return st;

    if (csw.dCSWTag != cbw.dCBWTag) return usb::XFER_ERROR;
    if (csw.bCSWStatus == CSW_STATUS_FAILED) return usb::XFER_STALL;
    if (csw.bCSWStatus == CSW_STATUS_PHASE_ERROR) return usb::XFER_ERROR;

    return usb::XFER_SUCCESS;
}

// ================================================================
// SCSI command builders
// ================================================================

static usb::TransferStatus do_inquiry(StorageDevice* dev)
{
    uint8_t cmd[6];
    memzero(cmd, sizeof(cmd));
    cmd[0] = SCSI_INQUIRY;
    cmd[4] = 36; // allocation length

    return bot_transfer(dev, 0x80, cmd, 6,
                        &dev->inquiry, 36);
}

static usb::TransferStatus do_test_unit_ready(StorageDevice* dev)
{
    uint8_t cmd[6];
    memzero(cmd, sizeof(cmd));
    cmd[0] = SCSI_TEST_UNIT_READY;

    return bot_transfer(dev, 0x00, cmd, 6, nullptr, 0);
}

static usb::TransferStatus do_read_capacity(StorageDevice* dev)
{
    uint8_t cmd[10];
    memzero(cmd, sizeof(cmd));
    cmd[0] = SCSI_READ_CAPACITY;

    uint8_t resp[8];
    usb::TransferStatus st = bot_transfer(dev, 0x80, cmd, 10, resp, 8);
    if (st == usb::XFER_SUCCESS) {
        dev->lastLBA   = be32(&resp[0]);
        dev->blockSize = be32(&resp[4]);
    }
    return st;
}

static usb::TransferStatus do_request_sense(StorageDevice* dev,
                                            SCSISenseData* sense)
{
    uint8_t cmd[6];
    memzero(cmd, sizeof(cmd));
    cmd[0] = SCSI_REQUEST_SENSE;
    cmd[4] = 18; // allocation length

    return bot_transfer(dev, 0x80, cmd, 6, sense, 18);
}

// ================================================================
// Find bulk IN and bulk OUT endpoints
// ================================================================

static bool find_bulk_endpoints(const usb::Device* usbDev,
                                uint8_t* bulkIn, uint16_t* bulkInPkt,
                                uint8_t* bulkOut, uint16_t* bulkOutPkt)
{
    bool foundIn = false, foundOut = false;

    for (uint8_t i = 0; i < usb::MAX_ENDPOINTS * 2; ++i) {
        const usb::Endpoint& ep = usbDev->endpoints[i];
        if (!ep.active || ep.type != usb::TRANSFER_BULK) continue;

        if (ep.dir == usb::DIR_DEVICE_TO_HOST && !foundIn) {
            *bulkIn    = ep.address;
            *bulkInPkt = ep.maxPacketSize;
            foundIn = true;
        }
        if (ep.dir == usb::DIR_HOST_TO_DEVICE && !foundOut) {
            *bulkOut    = ep.address;
            *bulkOutPkt = ep.maxPacketSize;
            foundOut = true;
        }
    }

    return foundIn && foundOut;
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
        if (usbDev->interfaceClass[iface] != usb::CLASS_MASS_STORAGE) continue;
        if (usbDev->interfaceSubClass[iface] != usb::MSC_SUBCLASS_SCSI_TRANSPARENT) continue;
        if (usbDev->interfaceProtocol[iface] != usb::MSC_PROTOCOL_BULK_ONLY) continue;
        if (s_deviceCount >= MAX_STORAGE_DEVICES) break;

        StorageDevice* dev = &s_devices[s_deviceCount];
        memzero(dev, sizeof(StorageDevice));

        dev->usbAddress   = usbAddress;
        dev->interfaceNum = iface;
        dev->lun          = 0;
        dev->cbwTag       = 1;

        if (!find_bulk_endpoints(usbDev,
                                 &dev->bulkInEP, &dev->bulkInMaxPkt,
                                 &dev->bulkOutEP, &dev->bulkOutMaxPkt)) {
            continue;
        }

        // Perform initial SCSI handshake
        // 1. INQUIRY
        if (do_inquiry(dev) != usb::XFER_SUCCESS) continue;

        // 2. TEST UNIT READY (may fail initially; retry a few times)
        for (int retry = 0; retry < 5; ++retry) {
            if (do_test_unit_ready(dev) == usb::XFER_SUCCESS) {
                dev->ready = true;
                break;
            }
            // Clear sense data on failure
            SCSISenseData sense;
            do_request_sense(dev, &sense);
            // Small delay between retries
            for (volatile int d = 0; d < 100000; ++d) {}
        }

        // 3. READ CAPACITY
        if (dev->ready) {
            do_read_capacity(dev);
        }

        dev->active = true;
        s_deviceCount++;
        claimed = true;
    }

    return claimed;
}

void release(uint8_t usbAddress)
{
    for (uint8_t i = 0; i < MAX_STORAGE_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].usbAddress == usbAddress) {
            s_devices[i].active = false;
            s_deviceCount--;
        }
    }
}

uint8_t device_count() { return s_deviceCount; }

const StorageDevice* get_device(uint8_t index)
{
    if (index >= MAX_STORAGE_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

usb::TransferStatus read_sectors(uint8_t devIndex,
                                 uint32_t lba,
                                 uint16_t count,
                                 void* buffer)
{
    if (devIndex >= MAX_STORAGE_DEVICES) return usb::XFER_ERROR;
    StorageDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->ready) return usb::XFER_ERROR;

    uint8_t cmd[10];
    memzero(cmd, sizeof(cmd));
    cmd[0] = SCSI_READ_10;
    put_be32(&cmd[2], lba);
    put_be16(&cmd[7], count);

    uint32_t dataLen = static_cast<uint32_t>(count) * dev->blockSize;
    return bot_transfer(dev, 0x80, cmd, 10, buffer, dataLen);
}

usb::TransferStatus write_sectors(uint8_t devIndex,
                                  uint32_t lba,
                                  uint16_t count,
                                  const void* buffer)
{
    if (devIndex >= MAX_STORAGE_DEVICES) return usb::XFER_ERROR;
    StorageDevice* dev = &s_devices[devIndex];
    if (!dev->active || !dev->ready) return usb::XFER_ERROR;

    uint8_t cmd[10];
    memzero(cmd, sizeof(cmd));
    cmd[0] = SCSI_WRITE_10;
    put_be32(&cmd[2], lba);
    put_be16(&cmd[7], count);

    uint32_t dataLen = static_cast<uint32_t>(count) * dev->blockSize;
    return bot_transfer(dev, 0x00, cmd, 10,
                        const_cast<void*>(buffer), dataLen);
}

usb::TransferStatus test_unit_ready(uint8_t devIndex)
{
    if (devIndex >= MAX_STORAGE_DEVICES) return usb::XFER_ERROR;
    StorageDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;
    return do_test_unit_ready(dev);
}

usb::TransferStatus request_sense(uint8_t devIndex, SCSISenseData* sense)
{
    if (devIndex >= MAX_STORAGE_DEVICES) return usb::XFER_ERROR;
    StorageDevice* dev = &s_devices[devIndex];
    if (!dev->active) return usb::XFER_ERROR;
    return do_request_sense(dev, sense);
}

} // namespace usb_storage
} // namespace kernel
