#ifndef KERNEL_FAT32_FORMATTER_H
#define KERNEL_FAT32_FORMATTER_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

// Supported end-to-end FAT32 formatter geometries. Keep the legacy 512-byte
// constant for callers that specifically describe the original format.
static const uint32_t FAT32_FORMAT_SECTOR_SIZE = 512;
static const uint32_t FAT32_FORMAT_4KN_SECTOR_SIZE = 4096;
static const uint32_t FAT32_FORMAT_MAX_CLUSTER_BYTES = 32768u;
static inline bool fat32_format_sector_size_supported(uint32_t bytesPerSector)
{
    return bytesPerSector == FAT32_FORMAT_SECTOR_SIZE ||
        bytesPerSector == FAT32_FORMAT_4KN_SECTOR_SIZE;
}
// The rollback log stores only sector addresses plus a prior-state tag. The
// formatter accepts at most seven distinct sectors today; the eighth slot is
// a fail-closed bound for future metadata additions.
static const uint32_t FAT32_FORMAT_MAX_ROLLBACK_ENTRIES = 8u;
static const uint32_t FAT32_FORMAT_ROLLBACK_RECORD_BYTES = 8u;
static const uint32_t FAT32_FORMAT_ROLLBACK_MAX_BYTES =
    FAT32_FORMAT_MAX_ROLLBACK_ENTRIES * FAT32_FORMAT_ROLLBACK_RECORD_BYTES;
static const uint32_t FAT32_FORMAT_MIN_CLUSTERS = 65525u;
// FAT32 data cluster numbers 0x0FFFFFF0..0x0FFFFFF7 are reserved or mark bad
// clusters. The last valid data cluster number is therefore 0x0FFFFFEF.
static const uint32_t FAT32_FORMAT_MAX_CLUSTERS = 0x0FFFFFEEu;
static const uint32_t FAT32_FORMAT_MAX_SECTORS_PER_CLUSTER =
    FAT32_FORMAT_MAX_CLUSTER_BYTES / FAT32_FORMAT_SECTOR_SIZE;
// A single reusable, bounded buffer backs the full-partition blank scan.
// Its size is independent of partition capacity and is also the request
// ceiling when a transport does not advertise a smaller transfer limit.
static const uint32_t FAT32_FORMAT_SCAN_BUFFER_MAX_BYTES = 1024u * 1024u;

enum Fat32FormatStatus : uint8_t {
    FAT32_FORMAT_READY = 0,
    FAT32_FORMAT_SUCCESS,
    FAT32_FORMAT_OPERATION_BUSY,
    FAT32_FORMAT_INVALID_REQUEST,
    FAT32_FORMAT_DEVICE_MISSING,
    FAT32_FORMAT_REGISTRY_CHANGED,
    FAT32_FORMAT_IDENTITY_CHANGED,
    FAT32_FORMAT_PARTITION_DISAPPEARED,
    FAT32_FORMAT_PARTITION_IDENTITY_CHANGED,
    FAT32_FORMAT_INVALID_TABLE,
    FAT32_FORMAT_INVALID_GEOMETRY,
    FAT32_FORMAT_READ_UNAVAILABLE,
    FAT32_FORMAT_WRITE_UNAVAILABLE,
    FAT32_FORMAT_READ_ONLY,
    FAT32_FORMAT_DURABILITY_UNKNOWN,
    FAT32_FORMAT_FLUSH_UNAVAILABLE,
    FAT32_FORMAT_FLUSH_FAILED,
    FAT32_FORMAT_MOUNTED,
    FAT32_FORMAT_ROOT_BACKING,
    FAT32_FORMAT_BOOT_BACKING,
    FAT32_FORMAT_BOOT_IDENTITY_UNKNOWN,
    FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE,
    FAT32_FORMAT_TOO_SMALL,
    FAT32_FORMAT_LAYOUT_OVERFLOW,
    FAT32_FORMAT_FILESYSTEM_ALREADY_RECOGNIZED,
    FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA,
    FAT32_FORMAT_LABEL_INVALID,
    FAT32_FORMAT_VOLUME_ID_UNAVAILABLE,
    // Retained for result-code compatibility; the scalable formatter no
    // longer returns this former geometry blocker.
    FAT32_FORMAT_ROLLBACK_BUFFER_LIMIT,
    FAT32_FORMAT_SNAPSHOT_FAILED,
    FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID,
    FAT32_FORMAT_METADATA_WRITE_FAILED,
    FAT32_FORMAT_VERIFICATION_FAILED,
    FAT32_FORMAT_RESCAN_FAILED,
    FAT32_FORMAT_ROLLBACK_FAILED,
    FAT32_FORMAT_REFORMAT_TARGET_UNSUPPORTED,
    FAT32_FORMAT_REFORMAT_MARKER_FAILED,
    FAT32_FORMAT_REFORMAT_INCOMPLETE,
    FAT32_FORMAT_CANCELED,
};

enum Fat32ReformatState : uint8_t {
    FAT32_REFORMAT_BEFORE_DESTRUCTIVE_COMMIT = 0,
    FAT32_REFORMAT_IN_PROGRESS,
    FAT32_REFORMAT_NEW_FILESYSTEM_WRITTEN_NOT_DURABLE,
    FAT32_REFORMAT_DURABLE,
    FAT32_REFORMAT_DURABLE_BUT_REFRESH_FAILED,
};

enum Fat32FormatStage : uint8_t {
    FAT32_FORMAT_STAGE_IDLE = 0,
    FAT32_FORMAT_STAGE_VALIDATING,
    FAT32_FORMAT_STAGE_REVALIDATING_PARTITION,
    FAT32_FORMAT_STAGE_CALCULATING_LAYOUT,
    FAT32_FORMAT_STAGE_SNAPSHOTTING,
    FAT32_FORMAT_STAGE_WRITING_METADATA,
    FAT32_FORMAT_STAGE_FLUSHING,
    FAT32_FORMAT_STAGE_VERIFYING,
    FAT32_FORMAT_STAGE_RESCANNING,
    FAT32_FORMAT_STAGE_COMPLETED,
    FAT32_FORMAT_STAGE_FAILED,
    FAT32_FORMAT_STAGE_STATE_UNCERTAIN,
    FAT32_FORMAT_STAGE_SNAPSHOT_FILESYSTEM_METADATA,
    FAT32_FORMAT_STAGE_WRITE_FAT,
    FAT32_FORMAT_STAGE_WRITE_ROOT,
    FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA,
    FAT32_FORMAT_STAGE_WRITE_FSINFO,
    FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR,
    FAT32_FORMAT_STAGE_FLUSH,
    FAT32_FORMAT_STAGE_VERIFY,
    FAT32_FORMAT_STAGE_RESCAN,
    FAT32_FORMAT_STAGE_ROLLBACK_WRITE,
    FAT32_FORMAT_STAGE_ROLLBACK_FLUSH,
    FAT32_FORMAT_STAGE_ROLLBACK_VERIFY,
    FAT32_FORMAT_STAGE_ACQUIRE_LEASE,
    FAT32_FORMAT_STAGE_PIN_TARGET,
};

enum Fat32ExistingState : uint8_t {
    FAT32_EXISTING_UNKNOWN = 0,
    FAT32_EXISTING_CLEAN,
    FAT32_EXISTING_RECOGNIZED_FILESYSTEM,
    FAT32_EXISTING_AMBIGUOUS_DATA,
    FAT32_EXISTING_UNREADABLE,
    FAT32_EXISTING_INTERRUPTED_REFORMAT,
};

enum Fat32FinalProbeState : uint8_t {
    FAT32_FINAL_PROBE_UNKNOWN = 0,
    FAT32_FINAL_PROBE_UNFORMATTED,
    FAT32_FINAL_PROBE_FAT32,
};

struct Fat32FormatGeometry {
    uint32_t bytesPerSector;
    uint32_t sectorsPerCluster;
    uint32_t clusterSizeBytes;
    uint32_t reservedSectorCount;
    uint32_t fatCount;
    uint32_t fatSizeSectors;
    uint32_t firstFatSector;
    uint32_t secondFatSector;
    uint32_t firstDataSector;
    uint32_t rootCluster;
    uint32_t clusterCount;
    uint32_t freeClusterCount;
    uint32_t totalSectors;
    uint32_t fsInfoSector;
    uint32_t backupFsInfoSector;
    uint32_t backupBootSector;
    uint32_t volumeId;
    uint32_t hiddenSectors;
};

struct Fat32FormatRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme partitionScheme;
    PartitionEntry partitionSnapshot;
    // Stable disk identity for the selected table: GPT disk GUID or MBR
    // signature. It prevents a valid partition tuple on a replacement table
    // from being mistaken for the original selection.
    uint8_t gptDiskGuid[16];
    uint32_t mbrDiskSignature;
    char volumeLabel[12];
    uint64_t expectedRegistryGeneration;
#if defined(KERNEL_STORAGE_TEST)
    bool testVolumeIdProvided;
    uint32_t testVolumeId;
    bool testForceRescanFailure;
#endif
};

struct Fat32FormatResult {
    Fat32FormatStatus status;
    Fat32FormatStatus failureStatus;
    Fat32FormatStage stage;
    Fat32FormatStage lastStage;
    Fat32FormatStage firstFailedStage;
    Fat32ExistingState existingState;
    Fat32FinalProbeState finalProbeState;
    TargetIdentity targetIdentity;
    PartitionEntry partition;
    Fat32FormatGeometry geometry;
    // Full-volume blank-scan evidence. Requests are sequential and bounded;
    // these counters make the zero-coverage proof and throughput auditable.
    uint64_t scanBytesRead;
    uint64_t scanZeroVerifiedSectors;
    uint64_t scanElapsedTicks;
    uint64_t scanCurrentLba;
    uint64_t scanRelativeLba;
    uint64_t scanFirstNonzeroRelativeLba;
    uint32_t scanReadRequests;
    uint32_t scanSmallestRequestBytes;
    uint32_t scanLargestRequestBytes;
    uint32_t scanFirstNonzeroByteOffset;
    uint8_t scanProgressPercent;
    bool scanCoverageComplete;
    uint64_t sectorsWritten;
    bool writeMayHaveReachedMedia;
    bool failedBeforeWrite;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationKind failedOperation;
    block::OperationDiagnostic failedBlockDiagnostic;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool flushAttempted;
    uint32_t flushAttempts;
    bool persistenceTrusted;
    bool writeAttempted;
    bool verificationPassed;
    bool rollbackAttempted;
    bool rollbackSucceeded;
    uint32_t rollbackEntryCount;
    uint32_t rollbackRecordBytes;
    Fat32FormatStage rollbackStage;
    bool rollbackWriteAttempted;
    uint32_t rollbackSectorsWritten;
    block::Status rollbackWriteStatus;
    bool rollbackFlushAttempted;
    block::FlushOutcome rollbackFlushOutcome;
    block::Status rollbackFlushStatus;
    bool rollbackVerificationPassed;
    bool finalStateUncertain;
    // Quick Reformat has no rollback to the old filesystem. This state is
    // monotonic once metadata writes begin and is separate from the blank
    // KnownZero formatter's rollback result.
    Fat32ReformatState reformatState;
    bool reformatRetry;
    bool reformatInvalidationFlushPassed;
    uint32_t oldVolumeId;
    char oldVolumeLabel[12];
    uint64_t fat1BytesCleared;
    uint64_t fat2BytesCleared;
    uint64_t reservedBytesWritten;
    uint64_t rootClusterBytesWritten;
    uint64_t reformatBytesWritten;
    uint64_t reformatElapsedTicks;
    uint32_t reformatReadRequests;
    uint32_t reformatWriteRequests;
    char diagnostic[160];
};

// Cooperative blank-media format job. The request and result are owned by the
// job for its entire lifetime; one call to step() performs at most one bounded
// scan transfer. The formatter retains the operation lease and device pin
// between calls. Cancellation is accepted only before metadata publication.
enum Fat32FormatJobState : uint8_t {
    FAT32_FORMAT_JOB_IDLE = 0,
    FAT32_FORMAT_JOB_SCANNING,
    FAT32_FORMAT_JOB_READY_TO_COMMIT,
    FAT32_FORMAT_JOB_COMPLETED,
    FAT32_FORMAT_JOB_FAILED,
    FAT32_FORMAT_JOB_CANCELED,
};

struct Fat32FormatJob {
    Fat32FormatRequest request;
    Fat32FormatResult result;
    StorageOperationLease lease;
    uint64_t operationId;
    Fat32FormatGeometry geometry;
    uint64_t scanRelativeLba;
    uint64_t scanStartTicks;
    uint32_t scanMaxSectors;
    Fat32FormatJobState state;
    bool cancelRequested;
    bool commitStarted;
    bool stepActive;
};

// begin() snapshots the immutable request, acquires the destructive-operation
// lease and target pin, and performs bounded preflight reads. step() scans one
// transfer or, on a later call, publishes metadata from completed KnownZero
// evidence. No token is exposed by this API.
Fat32FormatJobState begin_fat32_format_job(
    Fat32FormatJob& job, const Fat32FormatRequest& request);
Fat32FormatJobState step_fat32_format_job(Fat32FormatJob& job);
bool cancel_fat32_format_job(Fat32FormatJob& job);
const char* fat32_format_job_state_name(Fat32FormatJobState state);

// Deterministic calculator and label normalizer are also used by tests and
// future storage clients; neither function performs device I/O.
Fat32FormatStatus calculate_fat32_format_geometry(
    uint64_t partitionStartLba, uint64_t partitionSectorCount,
    uint32_t logicalSectorSize, Fat32FormatGeometry& result);
// Computes a whole-sector batch bound from the fixed scan buffer and the
// common block layer's maximum safe transfer size. A zero transfer limit
// means that the device did not advertise a smaller bound.
uint32_t fat32_scan_batch_sectors(uint32_t logicalSectorSize,
                                  uint32_t maxTransferBytes);
Fat32FormatStatus normalize_fat32_volume_label(
    const char* input, char normalized[11]);

// Preflight includes target/table/partition identity, policy checks, and a
// conservative all-zero partition scan. It never writes.
Fat32FormatStatus probe_fat32_format_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result);

// Revalidates again under the shared operation lease and formats only the
// selected bounded partition extent. It deliberately does not mount it.
Fat32FormatStatus format_fat32_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result);

// Quick Reformat is an explicit FAT32-to-FAT32 destructive contract. It does
// not scan or erase the whole partition and never uses KnownZero rollback.
// Preflight accepts either a valid supported FAT32 filesystem or a partition
// carrying the identity-bound DM26 interrupted-format marker.
Fat32FormatStatus probe_fat32_quick_reformat_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result);
Fat32FormatStatus quick_reformat_fat32_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result);

const char* fat32_format_status_name(Fat32FormatStatus status);
const char* fat32_format_stage_name(Fat32FormatStage stage);
const char* fat32_existing_state_name(Fat32ExistingState state);
const char* fat32_reformat_state_name(Fat32ReformatState state);

} // namespace storage
} // namespace kernel

#endif
