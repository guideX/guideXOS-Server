// Block Device Abstraction Layer
//
// Provides a unified interface for all block-level storage:
//   - ATA / SATA (PIO & AHCI)
//   - NVMe
//   - USB Mass Storage (via usb_storage driver)
//
// Higher-level filesystem drivers (FAT32, ext4, UFS, …) use this
// abstraction to perform sector I/O without knowing the transport.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_BLOCK_DEVICE_H
#define KERNEL_BLOCK_DEVICE_H

#include "kernel/types.h"

namespace kernel {
namespace block {

// ================================================================
// Block device transport types
// ================================================================

enum DeviceType : uint8_t {
    BDEV_NONE       = 0,
    BDEV_ATA_PIO    = 1,    // Legacy ATA (PIO mode via port I/O)
    BDEV_AHCI       = 2,    // SATA / AHCI (DMA, MMIO)
    BDEV_NVME       = 3,    // NVMe (PCIe, MMIO)
    BDEV_USB_MASS   = 4,    // USB Mass Storage (Bulk-Only)
    BDEV_RAMDISK    = 5,    // In-memory block device
};

// Firmware boot-source matching must be explicit. Transport type, registry
// index, and partition flags are not sufficient to infer this value.
enum BootProvenance : uint8_t {
    BOOT_PROVENANCE_UNKNOWN = 0,
    BOOT_PROVENANCE_BOOT_BACKING,
    BOOT_PROVENANCE_DEFINITELY_NOT_BOOT,
};

// ================================================================
// Status codes
// ================================================================

enum Status : uint8_t {
    BLOCK_OK            = 0,
    BLOCK_ERR_IO        = 1,    // general I/O error
    BLOCK_ERR_TIMEOUT   = 2,
    BLOCK_ERR_NO_MEDIA  = 3,
    BLOCK_ERR_NOT_READY = 4,
    BLOCK_ERR_INVALID   = 5,    // bad parameter
    BLOCK_ERR_UNSUPPORTED = 6,
    BLOCK_ERR_READ_ONLY = 7,    // media is write protected
    BLOCK_ERR_WRITE_UNCERTAIN = 8, // a submitted write lacks validated completion
    BLOCK_ERR_DURABILITY_UNVERIFIED = 9, // flush did not prove durability
};

// ================================================================
// Read / write callbacks (set by each transport driver)
// ================================================================

// Read  'count' sectors starting at LBA into 'buffer'.
// Write 'count' sectors starting at LBA from 'buffer'.
typedef Status (*ReadSectorsFn)(uint8_t devIndex,
                                uint64_t lba,
                                uint32_t count,
                                void* buffer);
typedef Status (*WriteSectorsFn)(uint8_t devIndex,
                                 uint64_t lba,
                                 uint32_t count,
                                 const void* buffer);

// Complete pending writeback/cache work. A missing callback is unsupported
// unless writeCompletionDurable is explicitly set on the device.
typedef Status (*FlushFn)(uint8_t devIndex);

enum FlushOutcome : uint8_t {
    FLUSH_OUTCOME_INVALID = 0,
    FLUSH_OUTCOME_SUPPORTED_SUCCEEDED,
    FLUSH_OUTCOME_SYNCHRONOUS_DURABLE,
    FLUSH_OUTCOME_UNSUPPORTED_UNKNOWN,
    FLUSH_OUTCOME_FAILED,
};

struct FlushReport {
    FlushOutcome outcome;
    Status status;
    bool semanticsKnown;
};

enum NvmeVwcState : uint8_t {
    NVME_VWC_UNKNOWN = 0,
    NVME_VWC_NOT_PRESENT,
    NVME_VWC_PRESENT_DISABLED,
    NVME_VWC_PRESENT_ENABLED,
};

enum UsbWriteOutcome : uint8_t {
    USB_WRITE_OUTCOME_NONE = 0,
    USB_WRITE_NOT_SUBMITTED,
    USB_WRITE_SUBMITTED_UNKNOWN,
    USB_WRITE_COMPLETED,
    USB_WRITE_FAILED,
    USB_WRITE_REMOVED,
    USB_WRITE_REMOVED_UNKNOWN,
    USB_WRITE_PARTIAL,
};

enum OperationKind : uint8_t {
    OPERATION_NONE = 0,
    OPERATION_READ,
    OPERATION_WRITE,
    OPERATION_FLUSH,
};

// Transport drivers may add a small bounded status snapshot for the most
// recent callback. ATA uses this for command phase and task-file registers.
struct TransportIoDiagnostic {
    bool valid;
    uint8_t stage;
    uint8_t statusRegister;
    uint8_t errorRegister;
    bool errorRegisterValid;
    uint64_t failingLba;
    uint32_t completedSectors;
    uint32_t dataSectorsTransferred;
    uint8_t commandOpcode;
    uint8_t statusCode;
    uint8_t commandSlot;
    uint32_t transportStatus;
    uint32_t transportError;
    uint8_t cswStatus;
    uint8_t senseKey;
    uint8_t senseAsc;
    uint8_t senseAscq;
    uint8_t usbSyncCacheState;
    // NVMe completion details are populated only by transports that expose
    // them. They remain raw so Diagnostics can render controller-specific
    // status without weakening the common block result contract.
    uint16_t nvmeQueueId;
    uint16_t nvmeCommandId;
    uint16_t nvmeSubmissionHead;
    uint16_t nvmeRawStatus;
    uint8_t nvmeStatusCodeType;
    uint8_t nvmeStatusCode;
    uint32_t nvmeControllerStatus;
    bool nvmeSubmissionOccurred;
    uint8_t usbWriteOutcome;
    uint16_t usbVendorId;
    uint16_t usbProductId;
    uint8_t usbInterface;
    uint8_t usbLun;
};

typedef bool (*GetTransportIoDiagnosticFn)(uint8_t driverIndex,
    TransportIoDiagnostic& out);

// ================================================================
// Block device descriptor
// ================================================================

struct BlockDevice {
    bool          active;
    bool          forcedOffline;
    uint64_t      registrationId; // Assigned by the registry; never reused.
    DeviceType    type;
    uint8_t       driverIndex;    // index within the transport driver
    uint64_t      totalSectors;
    uint32_t      sectorSize;     // typically 512 or 4096
    uint32_t      physicalSectorSize; // zero when the transport cannot report it
    char          name[32];       // human-readable, e.g. "ata0", "nvme0n1"
    ReadSectorsFn  readFn;
    WriteSectorsFn writeFn;
    FlushFn       flushFn;
    // A flush callback is useful only when its meaning is documented by the
    // transport. Completion-durable is reserved for stable writes without an
    // explicit flush (not for ordinary synchronous IO).
    bool          flushSemanticsKnown;
    bool          writeCompletionDurable;
    bool          removableKnown;
    bool          removable;
    char          model[40];
    char          serial[24];
    BootProvenance bootProvenance;
    // Controller/device path components are valid only when their validity
    // flag is set. Names and registry slots are never identity evidence.
    bool          pciLocationValid;
    uint32_t      pciSegment;
    uint8_t       pciBus;
    uint8_t       pciDevice;
    uint8_t       pciFunction;
    bool          ahciPortValid;
    uint8_t       ahciPort;
    bool          ataTargetValid;
    uint8_t       ataChannel;
    uint8_t       ataTarget;
    uint32_t      namespaceId;
    char          firmwareRevision[9];
    bool          namespaceNguidValid;
    uint8_t       namespaceNguid[16];
    bool          namespaceEui64Valid;
    uint8_t       namespaceEui64[8];
    uint32_t      nvmeControllerVersion;
    uint8_t       nvmeMdts;
    NvmeVwcState  nvmeVwcState;
    // USB Mass Storage fingerprint. The current HCI provides root-port
    // topology only; registrationId still identifies each attachment.
    bool          usbIdentityValid;
    uint16_t      usbVendorId;
    uint16_t      usbProductId;
    uint8_t       usbPort;
    uint8_t       usbInterface;
    uint8_t       usbLun;
    uint8_t       usbSyncCacheState;
    // Optional transport DMA limits. Zero means the registry does not declare
    // a constraint; new storage callers should use checked I/O helpers.
    uint16_t      requiredBufferAlignment;
    uint32_t      maxTransferBytes;
    GetTransportIoDiagnosticFn getIoDiagnosticFn;
};

struct OperationDiagnostic {
    bool valid;
    bool deviceRegistered;
    bool deviceOnline;
    bool callbackInvoked;
    uint8_t globalIndex;
    uint8_t driverIndex;
    uint64_t registrationId;
    DeviceType transport;
    uint32_t logicalSectorSize;
    uint64_t requestedLba;
    uint32_t requestedSectors;
    OperationKind operation;
    Status status;
    TransportIoDiagnostic transportDiagnostic;
};

struct OperationCounters {
    uint64_t readOperations;
    uint64_t writeOperations;
    uint64_t sectorsWritten;
    uint64_t flushAttempts;
};

static const uint8_t MAX_BLOCK_DEVICES = 16;

// ================================================================
// Public API
// ================================================================

// Initialise the block device layer (call once at boot).
void init();

// Register a new block device.  Returns the global device index,
// or 0xFF on failure.
uint8_t register_device(const BlockDevice& dev);

// Copy a stable descriptor snapshot while holding the registry lock.
bool copy_device(uint8_t index, BlockDevice& out);

// Refuses a normal removal while a storage operation has pinned the entry.
bool unregister_device(uint8_t index);
bool registration_is_present(uint8_t index, uint64_t registrationId);
bool unregister_device_if_matches(uint8_t index, uint64_t registrationId);

// Marks a device unavailable after physical removal/fatal transport failure.
// A pinned entry remains as an offline tombstone until the last pin is released.
bool mark_device_offline(uint8_t index, uint64_t registrationId);
// Clear write capability for one exact registration after authoritative
// write-protect sense. This mutation advances the registry generation.
bool mark_device_read_only(uint8_t index, uint64_t registrationId);

// Pin/unpin a specific registration. Pin identity is an opaque 64-bit
// incarnation token, independent of a reusable global slot.
bool pin_device(uint8_t index, uint64_t registrationId);
void unpin_device(uint8_t index, uint64_t registrationId);

// Return the number of active block devices.
uint8_t device_count();

// Return a device descriptor by global index (nullptr if invalid).
const BlockDevice* get_device(uint8_t index);

// Changes whenever a block device is registered, unregistered, or the registry
// is reinitialized. Equality is used for revalidation; ordering is not
// meaningful across uint64_t wrap.
uint64_t registry_generation();

// Bounded snapshot of the most recent block callback/validation result.
bool last_operation_diagnostic(OperationDiagnostic& out);
// Bounded cumulative counts since block::init(), intended for diagnostics and
// before/after operation comparisons. Successful sectors are counted only
// on success, or from a transport's bounded completed-sector count after a
// failed multi-sector callback when that transport can report partial progress.
void operation_counters(OperationCounters& out);

// ----------------------------------------------------------------
// Sector I/O (delegates to the transport callbacks)
// ----------------------------------------------------------------

Status read_sectors(uint8_t devIndex,
                    uint64_t lba,
                    uint32_t count,
                    void* buffer);

// Buffer-length-aware sector I/O for new storage-management callers.
Status read_sectors_checked(uint8_t devIndex, uint64_t lba, uint32_t count,
                            void* buffer, size_t bufferBytes);

Status write_sectors(uint8_t devIndex,
                     uint64_t lba,
                     uint32_t count,
                     const void* buffer);

Status write_sectors_checked(uint8_t devIndex, uint64_t lba, uint32_t count,
                             const void* buffer, size_t bufferBytes);

// Flush transport/device write caches. Missing callbacks return unsupported
// unless the descriptor explicitly declares synchronous durable completion.
Status flush(uint8_t devIndex);

// A truthful flush result for code that needs to establish persistence.
FlushReport flush_with_result(uint8_t devIndex);

} // namespace block
} // namespace kernel

#endif // KERNEL_BLOCK_DEVICE_H
