#include "include/kernel/partition_table.h"
#include "include/kernel/storage_manager.h"
#include "include/kernel/block_device.h"

namespace kernel {
namespace storage {

namespace {

static const uint32_t MBR_PARTITION_TABLE_OFFSET = 446;
static const uint32_t MBR_ENTRY_SIZE = 16;
static const uint32_t MBR_SIGNATURE_OFFSET = 510;
static const uint32_t GPT_HEADER_MIN_SIZE = 92;

struct GptHeader {
    uint32_t revision;
    uint32_t headerSize;
    uint64_t currentLba;
    uint64_t backupLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint8_t diskGuid[16];
    uint64_t entriesLba;
    uint32_t entryCount;
    uint32_t entrySize;
    uint32_t entriesCrc;
};

struct Interval {
    uint64_t start;
    uint64_t end;
};

enum RawProbeResult : uint8_t {
    RAW_PROBE_CLEAR = 0,
    RAW_PROBE_AMBIGUOUS,
    RAW_PROBE_UNREADABLE,
};

static uint16_t read_u16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t read_u64(const uint8_t* p)
{
    return static_cast<uint64_t>(read_u32(p)) |
           (static_cast<uint64_t>(read_u32(p + 4)) << 32);
}

static void copy_bytes(void* dst, const void* src, size_t length)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < length; ++i) d[i] = s[i];
}

static void clear_bytes(void* dst, size_t length)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < length; ++i) d[i] = 0;
}

static bool bytes_equal(const uint8_t* a, const uint8_t* b, size_t length)
{
    for (size_t i = 0; i < length; ++i) if (a[i] != b[i]) return false;
    return true;
}

static bool bytes_zero(const uint8_t* p, size_t length)
{
    for (size_t i = 0; i < length; ++i) if (p[i] != 0) return false;
    return true;
}

static bool is_extended_type(uint8_t type)
{
    return type == 0x05 || type == 0x0F || type == 0x85;
}

static bool is_gpt_signature(const uint8_t* sector)
{
    static const uint8_t signature[8] = {'E','F','I',' ','P','A','R','T'};
    return bytes_equal(sector, signature, sizeof(signature));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(0) - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static bool read_sector(uint8_t deviceIndex, uint64_t lba,
                        uint8_t* buffer, uint32_t sectorSize)
{
    return read_logical_sector(deviceIndex, lba, buffer, sectorSize) ==
           block::BLOCK_OK;
}

static void utf8_append(char* out, size_t capacity, size_t& length,
                        uint32_t codepoint)
{
    uint8_t encoded[4];
    uint8_t count = 0;
    if (codepoint <= 0x7F) {
        encoded[0] = static_cast<uint8_t>(codepoint);
        count = 1;
    } else if (codepoint <= 0x7FF) {
        encoded[0] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
        encoded[1] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
        count = 2;
    } else if (codepoint <= 0xFFFF) {
        encoded[0] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
        encoded[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
        count = 3;
    } else {
        encoded[0] = static_cast<uint8_t>(0xF0 | (codepoint >> 18));
        encoded[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
        count = 4;
    }
    if (length + count >= capacity) return;
    for (uint8_t i = 0; i < count; ++i) out[length++] = static_cast<char>(encoded[i]);
    out[length] = '\0';
}

static void decode_gpt_name(const uint8_t* entry, char* out, size_t capacity)
{
    if (capacity == 0) return;
    size_t outLength = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < 36; ++i) {
        uint32_t cp = read_u16(entry + 56 + static_cast<uint32_t>(i) * 2);
        if (cp == 0) break;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 < 36) {
                const uint32_t low = read_u16(entry + 56 + static_cast<uint32_t>(i + 1) * 2);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                } else {
                    cp = 0xFFFD;
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        utf8_append(out, capacity, outLength, cp);
    }
}

static bool parse_gpt_header(uint8_t deviceIndex, uint64_t lba,
                             bool primary, uint64_t totalSectors,
                             uint32_t sectorSize, GptHeader& header,
                             PartitionError& error, bool& signatureFound)
{
    alignas(4096) uint8_t sector[MAX_LOGICAL_SECTOR_SIZE];
    signatureFound = false;
    if (!read_sector(deviceIndex, lba, sector, sectorSize)) {
        error = PARTITION_ERROR_READ_FAILED;
        return false;
    }
    if (!is_gpt_signature(sector)) return false;
    signatureFound = true;

    header.revision = read_u32(sector + 8);
    header.headerSize = read_u32(sector + 12);
    if (header.revision != 0x00010000u) {
        error = PARTITION_ERROR_UNSUPPORTED_GPT_REVISION;
        return false;
    }
    if (header.headerSize < GPT_HEADER_MIN_SIZE || header.headerSize > sectorSize) {
        error = PARTITION_ERROR_GPT_HEADER;
        return false;
    }

    const uint32_t expectedCrc = read_u32(sector + 16);
    uint32_t crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, sector, 16);
    const uint8_t zeros[4] = {0,0,0,0};
    crc = crc32_update(crc, zeros, sizeof(zeros));
    crc = crc32_update(crc, sector + 20, header.headerSize - 20);
    crc ^= 0xFFFFFFFFu;
    if (crc != expectedCrc) {
        error = PARTITION_ERROR_GPT_HEADER_CRC;
        return false;
    }

    header.currentLba = read_u64(sector + 24);
    header.backupLba = read_u64(sector + 32);
    header.firstUsableLba = read_u64(sector + 40);
    header.lastUsableLba = read_u64(sector + 48);
    copy_bytes(header.diskGuid, sector + 56, sizeof(header.diskGuid));
    header.entriesLba = read_u64(sector + 72);
    header.entryCount = read_u32(sector + 80);
    header.entrySize = read_u32(sector + 84);
    header.entriesCrc = read_u32(sector + 88);

    const uint64_t lastLba = totalSectors - 1;
    const uint64_t expectedCurrent = primary ? 1 : lastLba;
    const uint64_t expectedBackup = primary ? lastLba : 1;
    if (header.currentLba != expectedCurrent || header.backupLba != expectedBackup ||
        bytes_zero(header.diskGuid, sizeof(header.diskGuid)) ||
        header.firstUsableLba < 2 ||
        header.firstUsableLba > header.lastUsableLba ||
        header.lastUsableLba >= lastLba ||
        header.entryCount == 0 || header.entryCount > MAX_PARSED_PARTITIONS ||
        header.entrySize < 128 || header.entrySize > GPT_MAX_ENTRY_SIZE ||
        (header.entrySize & 7u) != 0) {
        error = header.entryCount > MAX_PARSED_PARTITIONS
            ? PARTITION_ERROR_TOO_MANY_PARTITIONS : PARTITION_ERROR_GPT_HEADER;
        return false;
    }

    const uint64_t arrayBytes = static_cast<uint64_t>(header.entryCount) * header.entrySize;
    if (arrayBytes == 0 || arrayBytes > GPT_MAX_ARRAY_BYTES) {
        error = PARTITION_ERROR_GPT_ARRAY;
        return false;
    }
    const uint64_t arraySectors = (arrayBytes + sectorSize - 1) / sectorSize;
    if (arraySectors == 0 ||
        !checked_lba_range(totalSectors, header.entriesLba, arraySectors)) {
        error = PARTITION_ERROR_GPT_ARRAY;
        return false;
    }
    const uint64_t arrayEnd = header.entriesLba + arraySectors;
    if (primary) {
        if (header.entriesLba <= header.currentLba || arrayEnd > header.firstUsableLba) {
            error = PARTITION_ERROR_GPT_ARRAY;
            return false;
        }
    } else if (header.entriesLba <= header.lastUsableLba || arrayEnd > header.currentLba) {
        error = PARTITION_ERROR_GPT_ARRAY;
        return false;
    }

    error = PARTITION_ERROR_NONE;
    return true;
}

static bool parse_gpt_array(uint8_t deviceIndex, const GptHeader& header,
                            uint64_t totalSectors, uint32_t sectorSize,
                            PartitionEntry* output, uint16_t& outputCount,
                            PartitionError& error)
{
    Interval intervals[MAX_PARSED_PARTITIONS];
    uint16_t intervalCount = 0;
    alignas(4096) uint8_t sector[MAX_LOGICAL_SECTOR_SIZE];
    uint8_t entryBytes[GPT_MAX_ENTRY_SIZE];
    uint32_t entryFill = 0;
    uint32_t entriesCompleted = 0;
    const uint64_t arrayBytes = static_cast<uint64_t>(header.entryCount) * header.entrySize;
    const uint64_t arraySectors = (arrayBytes + sectorSize - 1) / sectorSize;
    uint64_t bytesRemaining = arrayBytes;
    uint32_t crc = 0xFFFFFFFFu;
    outputCount = 0;

    for (uint64_t sectorOffset = 0; sectorOffset < arraySectors; ++sectorOffset) {
        if (!read_sector(deviceIndex, header.entriesLba + sectorOffset, sector, sectorSize)) {
            error = PARTITION_ERROR_READ_FAILED;
            return false;
        }
        const uint32_t bytesThisSector = static_cast<uint32_t>(
            bytesRemaining < sectorSize ? bytesRemaining : sectorSize);
        crc = crc32_update(crc, sector, bytesThisSector);

        for (uint32_t byteIndex = 0; byteIndex < bytesThisSector; ++byteIndex) {
            entryBytes[entryFill++] = sector[byteIndex];
            if (entryFill != header.entrySize) continue;

            ++entriesCompleted;
            const uint8_t* typeGuid = entryBytes;
            if (!bytes_zero(typeGuid, 16)) {
                const uint8_t* uniqueGuid = entryBytes + 16;
                const uint64_t start = read_u64(entryBytes + 32);
                const uint64_t end = read_u64(entryBytes + 40);
                const uint64_t attrs = read_u64(entryBytes + 48);
                if (bytes_zero(uniqueGuid, 16) || start > end ||
                    start < header.firstUsableLba || end > header.lastUsableLba ||
                    end >= totalSectors) {
                    error = PARTITION_ERROR_GPT_ENTRY;
                    return false;
                }
                for (uint16_t prior = 0; prior < intervalCount; ++prior) {
                    if (start <= intervals[prior].end && end >= intervals[prior].start) {
                        error = PARTITION_ERROR_GPT_OVERLAP;
                        return false;
                    }
                }
                intervals[intervalCount].start = start;
                intervals[intervalCount].end = end;
                ++intervalCount;

                if (output) {
                    PartitionEntry& part = output[outputCount++];
                    clear_bytes(&part, sizeof(part));
                    part.partitionNumber = static_cast<uint16_t>(entriesCompleted);
                    part.isGpt = true;
                    copy_bytes(part.typeGuid, typeGuid, 16);
                    copy_bytes(part.uniqueGuid, uniqueGuid, 16);
                    part.startLba = start;
                    part.endLba = end;
                    part.sectorCount = end - start + 1;
                    part.attributes = attrs;
                    decode_gpt_name(entryBytes, part.name, sizeof(part.name));
                }
            }
            entryFill = 0;
        }
        bytesRemaining -= bytesThisSector;
    }

    crc ^= 0xFFFFFFFFu;
    if (entriesCompleted != header.entryCount || entryFill != 0) {
        error = PARTITION_ERROR_GPT_ARRAY;
        return false;
    }
    if (crc != header.entriesCrc) {
        error = PARTITION_ERROR_GPT_ARRAY_CRC;
        return false;
    }
    error = PARTITION_ERROR_NONE;
    return true;
}

static bool read_gpt_copy(uint8_t deviceIndex, uint64_t lba, bool primary,
                          uint64_t totalSectors, uint32_t sectorSize,
                          PartitionEntry* output, uint16_t& outputCount,
                          GptHeader& header, PartitionError& error,
                          bool& signatureFound)
{
    outputCount = 0;
    if (!parse_gpt_header(deviceIndex, lba, primary, totalSectors,
                          sectorSize, header, error, signatureFound)) return false;
    return parse_gpt_array(deviceIndex, header, totalSectors, sectorSize,
                           output, outputCount, error);
}

static bool headers_agree(const GptHeader& a, const GptHeader& b)
{
    return a.firstUsableLba == b.firstUsableLba &&
           a.lastUsableLba == b.lastUsableLba &&
           a.entryCount == b.entryCount && a.entrySize == b.entrySize &&
           a.entriesCrc == b.entriesCrc &&
           bytes_equal(a.diskGuid, b.diskGuid, sizeof(a.diskGuid));
}

static RawProbeResult raw_heuristic(uint8_t deviceIndex, uint64_t totalSectors,
                                    uint32_t sectorSize)
{
    alignas(4096) uint8_t sector[MAX_LOGICAL_SECTOR_SIZE];
    const uint64_t probeCount = totalSectors < 16 ? totalSectors : 16;
    for (uint64_t lba = 0; lba < probeCount; ++lba) {
        if (!read_sector(deviceIndex, lba, sector, sectorSize))
            return RAW_PROBE_UNREADABLE;
        if (!bytes_zero(sector, sectorSize)) return RAW_PROBE_AMBIGUOUS;
    }
    if (totalSectors > probeCount) {
        if (!read_sector(deviceIndex, totalSectors - 1, sector, sectorSize))
            return RAW_PROBE_UNREADABLE;
        if (!bytes_zero(sector, sectorSize)) return RAW_PROBE_AMBIGUOUS;
    }
    return RAW_PROBE_CLEAR;
}

static bool parse_mbr(const uint8_t* sector, uint64_t totalSectors,
                      PartitionTableModel& model)
{
    Interval intervals[4];
    uint8_t intervalCount = 0;
    uint8_t nonProtectiveCount = 0;
    uint8_t protectiveCount = 0;
    bool anyEntry = false;
    model.partitionCount = 0;
    model.protectiveMbr = false;
    model.hybridMbr = false;
    model.extendedPartitionsPresent = false;

    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t* raw = sector + MBR_PARTITION_TABLE_OFFSET + i * MBR_ENTRY_SIZE;
        const uint8_t status = raw[0];
        const uint8_t type = raw[4];
        const uint32_t start = read_u32(raw + 8);
        const uint32_t count = read_u32(raw + 12);
        if (status != 0 && status != 0x80) {
            model.error = PARTITION_ERROR_MALFORMED_MBR;
            return false;
        }
        if (type == 0) {
            if (status != 0 || start != 0 || count != 0) {
                model.error = PARTITION_ERROR_MALFORMED_MBR;
                return false;
            }
            continue;
        }
        anyEntry = true;
        if (count == 0 || start == 0 || start >= totalSectors ||
            static_cast<uint64_t>(count) > totalSectors - start) {
            model.error = PARTITION_ERROR_MALFORMED_MBR;
            return false;
        }
        const uint64_t end = static_cast<uint64_t>(start) + count - 1;

        if (type == 0xEE) {
            if (start != 1 || ++protectiveCount > 1) {
                model.error = PARTITION_ERROR_MALFORMED_MBR;
                return false;
            }
            model.protectiveMbr = true;
            continue;
        }

        for (uint8_t prior = 0; prior < intervalCount; ++prior) {
            if (start <= intervals[prior].end && end >= intervals[prior].start) {
                model.error = PARTITION_ERROR_MBR_OVERLAP;
                return false;
            }
        }
        intervals[intervalCount].start = start;
        intervals[intervalCount].end = end;
        ++intervalCount;
        ++nonProtectiveCount;

        if (is_extended_type(type)) model.extendedPartitionsPresent = true;
        PartitionEntry& part = model.partitions[model.partitionCount++];
        clear_bytes(&part, sizeof(part));
        part.partitionNumber = static_cast<uint16_t>(i + 1);
        part.mbrType = type;
        part.bootable = status == 0x80;
        part.startLba = start;
        part.endLba = end;
        part.sectorCount = count;
    }
    model.hybridMbr = model.protectiveMbr && nonProtectiveCount != 0;
    if (model.hybridMbr) model.error = PARTITION_ERROR_HYBRID_MBR;
    else model.error = PARTITION_ERROR_NONE;
    model.tableEntryCount = static_cast<uint16_t>(intervalCount + protectiveCount);
    (void)anyEntry;
    return true;
}

static void reset_model(PartitionTableModel& model)
{
    clear_bytes(&model, sizeof(model));
    model.scheme = PARTITION_SCHEME_INVALID;
    model.state = DISK_STATE_UNREADABLE;
    model.error = PARTITION_ERROR_NONE;
}

} // namespace

uint32_t crc32(const void* data, size_t length)
{
    if (!data && length != 0) return 0;
    uint32_t crc = crc32_update(0xFFFFFFFFu,
        static_cast<const uint8_t*>(data), length);
    return crc ^ 0xFFFFFFFFu;
}

bool parse_partition_table(uint8_t deviceIndex, PartitionTableModel& model)
{
    reset_model(model);
    DeviceCapabilities capabilities;
    if (!query_device_capabilities(deviceIndex, capabilities) ||
        !capabilities.geometryValid || !capabilities.capacityValid ||
        !capabilities.readable) {
        model.error = PARTITION_ERROR_INVALID_GEOMETRY;
        model.state = DISK_STATE_UNREADABLE;
        return false;
    }

    const uint64_t totalSectors = capabilities.totalLogicalSectors;
    const uint32_t sectorSize = capabilities.logicalSectorSize;
    if (totalSectors < 2) {
        model.error = PARTITION_ERROR_INVALID_GEOMETRY;
        return false;
    }

    alignas(4096) uint8_t mbrSector[MAX_LOGICAL_SECTOR_SIZE];
    if (!read_sector(deviceIndex, 0, mbrSector, sectorSize)) {
        model.error = PARTITION_ERROR_READ_FAILED;
        return false;
    }
    const bool mbrSignature = mbrSector[MBR_SIGNATURE_OFFSET] == 0x55 &&
                              mbrSector[MBR_SIGNATURE_OFFSET + 1] == 0xAA;
    model.mbrDiskSignature = read_u32(mbrSector + 440);
    bool mbrStructurallyValid = false;
    bool protectiveMbr = false;
    if (mbrSignature) {
        mbrStructurallyValid = parse_mbr(mbrSector, totalSectors, model);
        protectiveMbr = model.protectiveMbr;
    }

    GptHeader primaryHeader;
    GptHeader backupHeader;
    PartitionError primaryError = PARTITION_ERROR_NONE;
    PartitionError backupError = PARTITION_ERROR_NONE;
    bool primarySignature = false;
    bool backupSignature = false;
    uint16_t primaryCount = 0;
    uint16_t backupCount = 0;
    bool primaryValid = read_gpt_copy(deviceIndex, 1, true, totalSectors,
        sectorSize, model.partitions, primaryCount, primaryHeader,
        primaryError, primarySignature);
    bool backupValid = read_gpt_copy(deviceIndex, totalSectors - 1, false,
        totalSectors, sectorSize, primaryValid ? nullptr : model.partitions,
        backupCount, backupHeader, backupError, backupSignature);

    model.primaryGptValid = primaryValid;
    model.backupGptValid = backupValid;
    model.protectiveMbr = protectiveMbr;
    if (primaryValid) copy_bytes(model.primaryDiskGuid, primaryHeader.diskGuid, 16);
    if (backupValid) copy_bytes(model.backupDiskGuid, backupHeader.diskGuid, 16);

    if (primaryValid || backupValid || primarySignature || backupSignature || protectiveMbr) {
        model.scheme = PARTITION_SCHEME_GPT;
        if (primaryValid) {
            model.partitionCount = primaryCount;
            model.tableEntryCount = static_cast<uint16_t>(primaryHeader.entryCount);
            model.gptEntrySize = primaryHeader.entrySize;
            const uint64_t primaryArrayBytes =
                static_cast<uint64_t>(primaryHeader.entryCount) *
                primaryHeader.entrySize;
            model.gptEntryArraySectors = static_cast<uint32_t>(
                (primaryArrayBytes + sectorSize - 1) / sectorSize);
            model.primaryGptEntryArrayLba = primaryHeader.entriesLba;
            if (backupValid)
                model.backupGptEntryArrayLba = backupHeader.entriesLba;
            model.firstUsableLba = primaryHeader.firstUsableLba;
            model.lastUsableLba = primaryHeader.lastUsableLba;
        } else if (backupValid) {
            model.partitionCount = backupCount;
            model.tableEntryCount = static_cast<uint16_t>(backupHeader.entryCount);
            model.gptEntrySize = backupHeader.entrySize;
            const uint64_t backupArrayBytes =
                static_cast<uint64_t>(backupHeader.entryCount) *
                backupHeader.entrySize;
            model.gptEntryArraySectors = static_cast<uint32_t>(
                (backupArrayBytes + sectorSize - 1) / sectorSize);
            model.backupGptEntryArrayLba = backupHeader.entriesLba;
            model.firstUsableLba = backupHeader.firstUsableLba;
            model.lastUsableLba = backupHeader.lastUsableLba;
        } else {
            model.partitionCount = 0;
            const bool unsupported =
                primaryError == PARTITION_ERROR_UNSUPPORTED_GPT_REVISION ||
                backupError == PARTITION_ERROR_UNSUPPORTED_GPT_REVISION ||
                primaryError == PARTITION_ERROR_TOO_MANY_PARTITIONS ||
                backupError == PARTITION_ERROR_TOO_MANY_PARTITIONS;
            const bool unreadable = !primarySignature && !backupSignature &&
                (primaryError == PARTITION_ERROR_READ_FAILED ||
                 backupError == PARTITION_ERROR_READ_FAILED);
            model.state = unsupported ? DISK_STATE_UNSUPPORTED_PARTITION_SCHEME
                : (unreadable ? DISK_STATE_UNREADABLE
                              : DISK_STATE_INVALID_PARTITION_TABLE);
            model.scheme = model.state == DISK_STATE_UNSUPPORTED_PARTITION_SCHEME
                ? PARTITION_SCHEME_UNSUPPORTED : PARTITION_SCHEME_INVALID;
            model.error = primaryError != PARTITION_ERROR_NONE ? primaryError : backupError;
            return true;
        }

        model.gptCopiesAgree = primaryValid && backupValid &&
                               headers_agree(primaryHeader, backupHeader);
        if (model.hybridMbr) {
            model.state = DISK_STATE_UNSUPPORTED_PARTITION_SCHEME;
            model.scheme = PARTITION_SCHEME_UNSUPPORTED;
            model.error = PARTITION_ERROR_HYBRID_MBR;
        } else if (model.gptCopiesAgree &&
                   (!protectiveMbr || mbrStructurallyValid)) {
            model.state = DISK_STATE_VALID_GPT;
            model.error = PARTITION_ERROR_NONE;
        } else {
            model.state = DISK_STATE_GPT_DEGRADED;
            model.error = protectiveMbr && !mbrStructurallyValid
                ? PARTITION_ERROR_MALFORMED_MBR
                : (primaryValid && backupValid
                    ? PARTITION_ERROR_GPT_COPIES_DISAGREE
                    : (primaryError != PARTITION_ERROR_NONE ? primaryError : backupError));
        }
        return true;
    }

    if (mbrSignature) {
        if (!mbrStructurallyValid) {
            model.scheme = PARTITION_SCHEME_INVALID;
            model.state = DISK_STATE_INVALID_PARTITION_TABLE;
            return true;
        }
        if (model.hybridMbr) {
            model.scheme = PARTITION_SCHEME_UNSUPPORTED;
            model.state = DISK_STATE_UNSUPPORTED_PARTITION_SCHEME;
            model.error = PARTITION_ERROR_HYBRID_MBR;
            return true;
        }
        if (model.protectiveMbr) {
            model.scheme = PARTITION_SCHEME_INVALID;
            model.state = DISK_STATE_INVALID_PARTITION_TABLE;
            model.error = PARTITION_ERROR_GPT_HEADER;
            return true;
        }
        if (model.extendedPartitionsPresent) {
            model.scheme = PARTITION_SCHEME_UNSUPPORTED;
            model.state = DISK_STATE_UNSUPPORTED_PARTITION_SCHEME;
            model.error = PARTITION_ERROR_EXTENDED_PARTITIONS_UNSUPPORTED;
        } else {
            model.scheme = PARTITION_SCHEME_MBR;
            model.state = DISK_STATE_VALID_MBR;
        }
        return true;
    }

    model.partitionCount = 0;
    model.tableEntryCount = 0;
    const RawProbeResult rawProbe = raw_heuristic(deviceIndex, totalSectors, sectorSize);
    if (rawProbe == RAW_PROBE_CLEAR) {
        model.scheme = PARTITION_SCHEME_NONE_RAW;
        model.state = DISK_STATE_NOT_INITIALIZED;
        model.error = PARTITION_ERROR_NONE;
    } else if (rawProbe == RAW_PROBE_UNREADABLE) {
        model.scheme = PARTITION_SCHEME_INVALID;
        model.state = DISK_STATE_UNREADABLE;
        model.error = PARTITION_ERROR_READ_FAILED;
    } else {
        model.scheme = PARTITION_SCHEME_INVALID;
        model.state = DISK_STATE_INVALID_PARTITION_TABLE;
        model.error = PARTITION_ERROR_AMBIGUOUS_RAW_STATE;
    }
    return true;
}

const char* disk_state_name(DiskState state)
{
    switch (state) {
        case DISK_STATE_UNREADABLE: return "Unreadable";
        case DISK_STATE_NOT_INITIALIZED: return "Not Initialized";
        case DISK_STATE_VALID_MBR: return "MBR";
        case DISK_STATE_VALID_GPT: return "GPT";
        case DISK_STATE_GPT_DEGRADED: return "GPT Degraded";
        case DISK_STATE_INVALID_PARTITION_TABLE: return "Invalid Partition Table";
        case DISK_STATE_UNSUPPORTED_PARTITION_SCHEME: return "Unsupported Partition Scheme";
        default: return "Unknown";
    }
}

const char* partition_scheme_name(PartitionScheme scheme)
{
    switch (scheme) {
        case PARTITION_SCHEME_NONE_RAW: return "Raw";
        case PARTITION_SCHEME_MBR: return "MBR";
        case PARTITION_SCHEME_GPT: return "GPT";
        case PARTITION_SCHEME_INVALID: return "Invalid";
        case PARTITION_SCHEME_UNSUPPORTED: return "Unsupported";
        default: return "Unknown";
    }
}

} // namespace storage
} // namespace kernel
