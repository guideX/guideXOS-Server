#ifndef KERNEL_PARTITION_TABLE_H
#define KERNEL_PARTITION_TABLE_H

#include "kernel/types.h"

namespace kernel {
namespace storage {

static const uint16_t MAX_PARSED_PARTITIONS = 128;
static const uint16_t GPT_MAX_ENTRY_SIZE = 512;
static const uint32_t GPT_MAX_ARRAY_BYTES =
    static_cast<uint32_t>(MAX_PARSED_PARTITIONS) * GPT_MAX_ENTRY_SIZE;

enum PartitionScheme : uint8_t {
    PARTITION_SCHEME_NONE_RAW = 0,
    PARTITION_SCHEME_MBR,
    PARTITION_SCHEME_GPT,
    PARTITION_SCHEME_INVALID,
    PARTITION_SCHEME_UNSUPPORTED,
};

enum DiskState : uint8_t {
    DISK_STATE_UNREADABLE = 0,
    DISK_STATE_NOT_INITIALIZED,
    DISK_STATE_VALID_MBR,
    DISK_STATE_VALID_GPT,
    DISK_STATE_GPT_DEGRADED,
    DISK_STATE_INVALID_PARTITION_TABLE,
    DISK_STATE_UNSUPPORTED_PARTITION_SCHEME,
};

enum PartitionError : uint8_t {
    PARTITION_ERROR_NONE = 0,
    PARTITION_ERROR_INVALID_GEOMETRY,
    PARTITION_ERROR_READ_FAILED,
    PARTITION_ERROR_BAD_MBR_SIGNATURE,
    PARTITION_ERROR_MALFORMED_MBR,
    PARTITION_ERROR_MBR_OVERLAP,
    PARTITION_ERROR_HYBRID_MBR,
    PARTITION_ERROR_GPT_HEADER,
    PARTITION_ERROR_GPT_HEADER_CRC,
    PARTITION_ERROR_GPT_ARRAY,
    PARTITION_ERROR_GPT_ARRAY_CRC,
    PARTITION_ERROR_GPT_ENTRY,
    PARTITION_ERROR_GPT_OVERLAP,
    PARTITION_ERROR_GPT_COPIES_DISAGREE,
    PARTITION_ERROR_GPT_GEOMETRY_MISMATCH,
    PARTITION_ERROR_UNSUPPORTED_GPT_REVISION,
    PARTITION_ERROR_TOO_MANY_PARTITIONS,
    PARTITION_ERROR_EXTENDED_PARTITIONS_UNSUPPORTED,
    PARTITION_ERROR_AMBIGUOUS_RAW_STATE,
};

// Per-copy diagnostics keep CRC failures, unreadable media, unsupported
// metadata, and device-size mismatches distinct. Only one fully valid copy
// can authorize repair of its peer.
enum GptCopyState : uint8_t {
    GPT_COPY_NOT_PRESENT = 0,
    GPT_COPY_VALID,
    GPT_COPY_HEADER_INVALID,
    GPT_COPY_HEADER_CRC_INVALID,
    GPT_COPY_ARRAY_INVALID,
    GPT_COPY_ARRAY_CRC_INVALID,
    GPT_COPY_BOUNDS_INVALID,
    GPT_COPY_UNREADABLE,
    GPT_COPY_UNSUPPORTED,
    GPT_COPY_GEOMETRY_MISMATCH,
};

struct PartitionEntry {
    // One-based slot in the on-disk partition table (MBR primary slot or GPT
    // entry slot). Presentation may sort entries by physical LBA while keeping
    // this number stable for Properties.
    uint16_t partitionNumber;
    uint8_t mbrType;
    bool isGpt;
    bool bootable;
    uint8_t typeGuid[16];
    uint8_t uniqueGuid[16];
    uint64_t startLba;
    uint64_t endLba;
    uint64_t sectorCount;
    uint64_t attributes;
    char name[64];
};

struct PartitionTableModel {
    PartitionScheme scheme;
    DiskState state;
    PartitionError error;
    uint16_t partitionCount;
    uint16_t tableEntryCount;
    // Validated on-disk GPT array geometry. These fields are populated only
    // from a valid primary/backup header and let bounded storage operations
    // update the existing layout without assuming DM3's default geometry.
    uint32_t gptEntrySize;
    uint32_t gptEntryArraySectors;
    uint32_t primaryGptHeaderCrc32;
    uint32_t backupGptHeaderCrc32;
    uint32_t primaryGptEntryArrayCrc32;
    uint32_t backupGptEntryArrayCrc32;
    uint64_t primaryGptEntryArrayLba;
    uint64_t backupGptEntryArrayLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    bool protectiveMbr;
    bool hybridMbr;
    bool extendedPartitionsPresent;
    bool primaryGptValid;
    bool backupGptValid;
    bool gptCopiesAgree;
    bool gptCopiesConflict;
    bool protectiveMbrValid;
    GptCopyState primaryGptCopyState;
    GptCopyState backupGptCopyState;
    PartitionError primaryGptError;
    PartitionError backupGptError;
    uint8_t primaryDiskGuid[16];
    uint8_t backupDiskGuid[16];
    uint32_t mbrDiskSignature;
    PartitionEntry partitions[MAX_PARSED_PARTITIONS];
};

// Standard reflected IEEE CRC32 (polynomial 0xEDB88320), with the usual
// initial/final XOR.  Passing null with a nonzero length returns zero.
uint32_t crc32(const void* data, size_t length);

// Read-only parser.  It uses the device's logical-sector geometry and stores
// at most MAX_PARSED_PARTITIONS GPT entries.
bool parse_partition_table(uint8_t deviceIndex, PartitionTableModel& model);

const char* disk_state_name(DiskState state);
const char* partition_scheme_name(PartitionScheme scheme);

} // namespace storage
} // namespace kernel

#endif
