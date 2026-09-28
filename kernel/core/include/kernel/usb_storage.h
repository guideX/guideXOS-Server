#ifndef KERNEL_USB_STORAGE_H
#define KERNEL_USB_STORAGE_H

#include "kernel/types.h"
#include "kernel/usb.h"

namespace kernel {
namespace usb_storage {

static const uint32_t CBW_SIGNATURE = 0x43425355;
static const uint32_t CSW_SIGNATURE = 0x53425355;

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
    uint8_t bmCBWFlags;
    uint8_t bCBWLUN;
    uint8_t bCBWCBLength;
    uint8_t CBWCB[16];
} STOR_PACKED;

struct CommandStatusWrapper {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t bCSWStatus;
} STOR_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif
#undef STOR_PACKED

enum CSWStatus : uint8_t {
    CSW_STATUS_PASSED = 0,
    CSW_STATUS_FAILED = 1,
    CSW_STATUS_PHASE_ERROR = 2,
};

enum SCSIOpcode : uint8_t {
    SCSI_TEST_UNIT_READY = 0x00,
    SCSI_REQUEST_SENSE = 0x03,
    SCSI_INQUIRY = 0x12,
    SCSI_MODE_SENSE_6 = 0x1A,
    SCSI_READ_CAPACITY_10 = 0x25,
    SCSI_READ_10 = 0x28,
    SCSI_WRITE_10 = 0x2A,
    SCSI_SYNCHRONIZE_CACHE_10 = 0x35,
    SCSI_READ_16 = 0x88,
    SCSI_WRITE_16 = 0x8A,
    SCSI_SERVICE_ACTION_IN_16 = 0x9E,
};

enum SyncCacheState : uint8_t {
    SYNC_CACHE_UNKNOWN = 0,
    SYNC_CACHE_SUCCEEDED,
    SYNC_CACHE_UNSUPPORTED,
    SYNC_CACHE_FAILED,
};

static const uint8_t SCSI_SERVICE_ACTION_READ_CAPACITY_16 = 0x10;

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

struct SCSISenseData {
    uint8_t responseCode;
    uint8_t obsolete;
    uint8_t senseKey;
    uint8_t information[4];
    uint8_t additionalSenseLength;
    uint8_t commandSpecific[4];
    uint8_t asc;
    uint8_t ascq;
    uint8_t fruc;
    uint8_t senseKeySpecific[3];
};

struct StorageDevice {
    bool active;
    bool ready;
    bool capacityValid;
    SyncCacheState syncCacheState;
    bool writeProtected;
    bool blockRegistered;
    bool transportFaulted;
    uint8_t usbAddress;
    uint8_t interfaceNum;
    uint8_t usbPort;
    uint8_t bulkInEP;
    uint8_t bulkOutEP;
    uint16_t bulkInMaxPkt;
    uint16_t bulkOutMaxPkt;
    uint16_t usbVendorId;
    uint16_t usbProductId;
    uint8_t lun;
    uint64_t lastLBA;
    uint32_t blockSize;
    uint32_t cbwTag;
    uint64_t blockRegistrationId;
    uint8_t blockDeviceIndex;
    uint8_t lastOpcode;
    uint8_t lastBotStage;
    uint8_t lastCswStatus;
    uint8_t lastTransferStatus;
    uint8_t lastBlockStatus;
    SCSISenseData lastSense;
    bool lastSenseValid;
    SCSIInquiryData inquiry;
    char model[40];
    char serial[24];
};

static const uint8_t MAX_STORAGE_DEVICES = 4;
static const uint32_t MAX_BOT_TRANSFER_BYTES = 64u * 1024u;

void init();
bool probe(uint8_t usbAddress);
void release(uint8_t usbAddress);
uint8_t device_count();
const StorageDevice* get_device(uint8_t index);

usb::TransferStatus read_sectors(uint8_t devIndex, uint64_t lba,
                                 uint32_t count, void* buffer);
usb::TransferStatus write_sectors(uint8_t devIndex, uint64_t lba,
                                  uint32_t count, const void* buffer);
usb::TransferStatus test_unit_ready(uint8_t devIndex);
usb::TransferStatus request_sense(uint8_t devIndex, SCSISenseData* sense);
usb::TransferStatus synchronize_cache(uint8_t devIndex);

} // namespace usb_storage
} // namespace kernel

#endif
