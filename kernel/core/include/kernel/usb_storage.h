// USB Mass Storage Class Driver
//
// Supports:
//   - Bulk-Only Transport (BBB) protocol
//   - SCSI Transparent Command Set (subclass 0x06)
//   - SCSI commands: INQUIRY, TEST UNIT READY, READ CAPACITY,
//     READ(10), WRITE(10), REQUEST SENSE
//
// Reference: USB Mass Storage Class Bulk-Only Transport 1.0,
//            SCSI Primary Commands (SPC-4), SCSI Block Commands (SBC-3)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_USB_STORAGE_H
#define KERNEL_USB_STORAGE_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_storage {

// ================================================================
// Command Block Wrapper (CBW) — 31 bytes
// ================================================================

static const uint32_t CBW_SIGNATURE = 0x43425355; // "USBC"

#if defined(__GNUC__) || defined(__clang__)
#define STOR_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define STOR_PACKED
#endif

struct CommandBlockWrapper {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;          // bit 7: 0=OUT, 1=IN
    uint8_t  bCBWLUN;             // bits [3:0]
    uint8_t  bCBWCBLength;        // length of CBWCB (1-16)
    uint8_t  CBWCB[16];           // SCSI command block
} STOR_PACKED;

// ================================================================
// Command Status Wrapper (CSW) — 13 bytes
// ================================================================

static const uint32_t CSW_SIGNATURE = 0x53425355; // "USBS"

struct CommandStatusWrapper {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;          // 0=pass, 1=fail, 2=phase error
} STOR_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef STOR_PACKED

// CSW status values
enum CSWStatus : uint8_t {
    CSW_STATUS_PASSED      = 0x00,
    CSW_STATUS_FAILED      = 0x01,
    CSW_STATUS_PHASE_ERROR = 0x02,
};

// ================================================================
// SCSI command opcodes
// ================================================================

enum SCSIOpcode : uint8_t {
    SCSI_TEST_UNIT_READY = 0x00,
    SCSI_REQUEST_SENSE   = 0x03,
    SCSI_INQUIRY         = 0x12,
    SCSI_MODE_SENSE_6    = 0x1A,
    SCSI_READ_CAPACITY   = 0x25,
    SCSI_READ_10         = 0x28,
    SCSI_WRITE_10        = 0x2A,
};

// ================================================================
// SCSI INQUIRY response (36 bytes minimum)
// ================================================================

struct SCSIInquiryData {
    uint8_t peripheralQualifier_deviceType;
    uint8_t rmb;
    uint8_t version;
    uint8_t responseDataFormat;
    uint8_t additionalLength;
    uint8_t flags5;
    uint8_t flags6;
    uint8_t flags7;
    uint8_t vendorId[8];
    uint8_t productId[16];
    uint8_t productRev[4];
};

// ================================================================
// SCSI READ CAPACITY(10) response (8 bytes)
// ================================================================

struct SCSIReadCapacity {
    uint32_t lastLBA;       // big-endian
    uint32_t blockSize;     // big-endian
};

// ================================================================
// SCSI REQUEST SENSE response (18 bytes)
// ================================================================

struct SCSISenseData {
    uint8_t  responseCode;
    uint8_t  obsolete;
    uint8_t  senseKey;
    uint8_t  information[4];
    uint8_t  additionalSenseLength;
    uint8_t  commandSpecific[4];
    uint8_t  asc;
    uint8_t  ascq;
    uint8_t  fruc;
    uint8_t  senseKeySpecific[3];
};

// ================================================================
// Mass Storage device instance
// ================================================================

struct StorageDevice {
    bool     active;
    uint8_t  usbAddress;
    uint8_t  interfaceNum;
    uint8_t  bulkInEP;        // bulk IN endpoint address
    uint8_t  bulkOutEP;       // bulk OUT endpoint address
    uint16_t bulkInMaxPkt;
    uint16_t bulkOutMaxPkt;
    uint8_t  lun;
    uint32_t lastLBA;
    uint32_t blockSize;
    uint32_t cbwTag;           // incrementing tag for CBW/CSW matching
    SCSIInquiryData inquiry;
    bool     ready;
};

static const uint8_t MAX_STORAGE_DEVICES = 4;

// ================================================================
// Public API
// ================================================================

// Initialise the mass storage class driver.
void init();

// Probe a USB device; if it has a mass storage interface (SCSI,
// Bulk-Only), claim it.  Returns true if claimed.
bool probe(uint8_t usbAddress);

// Release all mass storage interfaces on a device (on detach).
void release(uint8_t usbAddress);

// Return the number of active storage devices.
uint8_t device_count();

// Return info for a storage device by index (0-based).
const StorageDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Block I/O
// ----------------------------------------------------------------

// Read contiguous sectors from a device.
// Returns XFER_SUCCESS on success.
usb::TransferStatus read_sectors(uint8_t devIndex,
                                 uint32_t lba,
                                 uint16_t count,
                                 void* buffer);

// Write contiguous sectors to a device.
usb::TransferStatus write_sectors(uint8_t devIndex,
                                  uint32_t lba,
                                  uint16_t count,
                                  const void* buffer);

// Test if the device media is ready.
usb::TransferStatus test_unit_ready(uint8_t devIndex);

// Perform SCSI REQUEST SENSE to retrieve error information.
usb::TransferStatus request_sense(uint8_t devIndex,
                                  SCSISenseData* sense);

} // namespace usb_storage
} // namespace kernel

#endif // KERNEL_USB_STORAGE_H
