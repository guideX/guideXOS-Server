// FAT32 / exFAT Filesystem Driver — Implementation
//
// Reads the BPB / boot sector to determine FAT type, caches the
// FAT table for cluster chain traversal, and provides directory
// listing and file I/O through the block device layer.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/fs_fat.h"
#include "include/kernel/block_device.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace fs_fat {

// ================================================================
// Internal state
// ================================================================

static FATVolume s_volumes[MAX_FAT_VOLUMES];
static FATFile   s_files[MAX_OPEN_FILES];
static uint8_t   s_volumeCount = 0;
static block::Status s_lastIoStatus = block::BLOCK_OK;
static TraversalStatus s_lastTraversalStatus = TRAVERSAL_OK;
static DeleteStatus s_lastDeleteStatus = DELETE_OK;
static bool s_trashTraceActive = false;
static uint64_t s_trashTraceGeneration = 0;
static uint64_t s_fatScanIterations = 0;
static uint64_t s_allocatedClusters = 0;

// A v0.1 file operation must fail boundedly even when the FAT contains a
// cycle.  Large copies stream through a modest buffer, while this bound
// prevents a corrupt volume from turning one synchronous UI event into a
// whole-volume walk.
static const uint32_t kMaxSafeChainSteps = 65536u;
static const uint32_t kMaxSafeDirectoryChainSteps = 4096u;
static const uint32_t kMaxResizeOwnershipScanSteps = 1048576u;
static const uint32_t kMaxResizeOwnershipDirectories = 4096u;
// Resize is synchronous and the filesystem already uses shared sector and
// directory scratch buffers, so a bounded static chain list avoids heap use.
static uint32_t s_resizeChain[kMaxSafeChainSteps];
static uint32_t s_resizeDirectoryQueue[kMaxResizeOwnershipDirectories];
static uint8_t s_resizeDirBuf[4096];

// Sector buffer for reading metadata (one sector at a time)
static uint8_t   s_secBuf[4096]; // supports up to 4096-byte sectors

// Directory iteration state
static struct {
    bool     active;
    uint8_t  volIdx;
    uint32_t cluster;
    uint32_t clusterSteps;
    uint32_t sectorInCluster;
    uint32_t entryInSector;
} s_dirIter;

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

static void memfill(void* dst, uint8_t value, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) d[i] = value;
}

static bool str_equal(const char* a, const char* b, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool is_fat_partition_type(uint8_t partType)
{
    return partType == 0x01 || partType == 0x04 || partType == 0x06 ||
           partType == 0x0B || partType == 0x0C || partType == 0x0E;
}

static block::Status read_volume_sector(const FATVolume& vol, uint64_t lba, void* buffer)
{
    s_lastIoStatus = block::read_sectors(vol.blockDevIndex, vol.partitionOffset + lba, 1, buffer);
    return s_lastIoStatus;
}

static block::Status write_volume_sector(const FATVolume& vol, uint64_t lba, const void* buffer)
{
    if (s_trashTraceActive) {
        serial::puts("TRASH_BLOCK_WRITE_BEGIN gen=");
        serial::put_hex64(s_trashTraceGeneration);
        serial::puts(" lba=0x");
        serial::put_hex64(vol.partitionOffset + lba);
        serial::puts(" relative=0x");
        serial::put_hex64(lba);
        serial::puts(" device=0x");
        serial::put_hex8(vol.blockDevIndex);
        serial::puts("\n");
    }
    s_lastIoStatus = block::write_sectors(vol.blockDevIndex, vol.partitionOffset + lba, 1, buffer);
    if (s_trashTraceActive) {
        serial::puts("TRASH_BLOCK_WRITE_LBA gen=");
        serial::put_hex64(s_trashTraceGeneration);
        serial::puts(" lba=0x");
        serial::put_hex64(vol.partitionOffset + lba);
        serial::puts(" relative=0x");
        serial::put_hex64(lba);
        serial::puts(" result=0x");
        serial::put_hex8(static_cast<uint8_t>(s_lastIoStatus));
        serial::puts("\n");
    }
    if (s_trashTraceActive) {
        serial::puts("FAT_BLOCK_WRITE_LBA gen=");
        serial::put_hex64(s_trashTraceGeneration);
        serial::puts(" lba=0x");
        serial::put_hex64(vol.partitionOffset + lba);
        serial::puts(" relative=0x");
        serial::put_hex64(lba);
        serial::puts(" device=0x");
        serial::put_hex8(vol.blockDevIndex);
        serial::puts(" result=0x");
        serial::put_hex8(static_cast<uint8_t>(s_lastIoStatus));
        serial::puts("\n");
    }
    return s_lastIoStatus;
}

static block::Status flush_volume_io(const FATVolume& vol)
{
    s_lastIoStatus = block::flush(vol.blockDevIndex);
    return s_lastIoStatus;
}

// ================================================================
// FAT cluster ? sector translation
// ================================================================

static uint32_t cluster_to_sector(const FATVolume& vol, uint32_t cluster)
{
    const uint64_t sector = static_cast<uint64_t>(vol.firstDataSector) +
                            static_cast<uint64_t>(cluster - 2) *
                            vol.sectorsPerCluster;
    return sector > 0xFFFFFFFFull ? 0 : static_cast<uint32_t>(sector);
}

static uint32_t fat_entry_size(const FATVolume& vol)
{
    return vol.type == FAT_TYPE_FAT16 ? 2u : 4u;
}

static uint32_t fat_free_value(const FATVolume& vol)
{
    return vol.type == FAT_TYPE_FAT16 ? FAT16_CLUSTER_FREE : FAT32_CLUSTER_FREE;
}

static uint32_t fat_end_value(const FATVolume& vol)
{
    return vol.type == FAT_TYPE_FAT16 ? FAT16_CLUSTER_END : FAT32_CLUSTER_END;
}

static uint32_t fat_cluster_mask(const FATVolume& vol)
{
    return vol.type == FAT_TYPE_FAT16 ? FAT16_CLUSTER_MASK : FAT32_CLUSTER_MASK;
}

static bool is_valid_data_cluster(const FATVolume& vol, uint32_t cluster)
{
    uint32_t clusterCount = vol.type == FAT_TYPE_EXFAT
        ? vol.exfatClusterCount
        : vol.totalDataClusters;
    return cluster >= 2 && clusterCount != 0 &&
           (cluster - 2) < clusterCount;
}

static uint32_t max_chain_steps(const FATVolume& vol)
{
    uint32_t clusterCount = vol.type == FAT_TYPE_EXFAT
        ? vol.exfatClusterCount
        : vol.totalDataClusters;
    if (clusterCount == 0) return 1;
    return clusterCount < kMaxSafeChainSteps ? clusterCount : kMaxSafeChainSteps;
}

static uint32_t directory_chain_step_limit(const FATVolume& vol)
{
    const uint32_t volumeLimit = max_chain_steps(vol);
    return volumeLimit < kMaxSafeDirectoryChainSteps
        ? volumeLimit : kMaxSafeDirectoryChainSteps;
}

static bool is_bad_cluster(const FATVolume& vol, uint32_t cluster)
{
    if (vol.type == FAT_TYPE_FAT16) return cluster == FAT16_CLUSTER_BAD;
    if (vol.type == FAT_TYPE_FAT32) return cluster == FAT32_CLUSTER_BAD;
    return vol.type == FAT_TYPE_EXFAT && cluster == 0xFFFFFFF7u;
}

static void set_traversal_status(TraversalStatus status)
{
    s_lastTraversalStatus = status;
}

static FileWriteStatus write_failure_status()
{
    if (s_lastIoStatus == block::BLOCK_ERR_TIMEOUT) return FILE_WRITE_IO_TIMEOUT;
    switch (s_lastTraversalStatus) {
        case TRAVERSAL_CHAIN_CYCLE:
        case TRAVERSAL_CHAIN_STEP_LIMIT:
        case TRAVERSAL_TRUNCATED_CHAIN:
        case TRAVERSAL_INVALID_CLUSTER:
        case TRAVERSAL_BAD_CLUSTER:
            return FILE_WRITE_CORRUPT_CHAIN;
        case TRAVERSAL_NO_PROGRESS:
            return FILE_WRITE_NO_PROGRESS;
        default:
            return s_lastIoStatus == block::BLOCK_OK
                ? FILE_WRITE_ALLOCATION_FAILED : FILE_WRITE_IO_ERROR;
    }
}

static bool cluster_byte_count(const FATVolume& vol, uint32_t* outBytes)
{
    if (!outBytes || vol.bytesPerSector == 0 || vol.sectorsPerCluster == 0) {
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(vol.bytesPerSector) *
                           vol.sectorsPerCluster;
    if (bytes == 0 || bytes > 0xFFFFFFFFull) return false;
    *outBytes = static_cast<uint32_t>(bytes);
    return true;
}

// Floyd's tortoise/hare check keeps cycle detection bounded without a
// heap-sized visited-cluster table.  Reaching EOC before the limit is the
// normal terminating case; reaching the limit without EOC is handled by the
// caller's step invariant.
static uint32_t fat_next_cluster(const FATVolume& vol, uint32_t cluster)
{
    if (is_bad_cluster(vol, cluster)) {
        set_traversal_status(TRAVERSAL_BAD_CLUSTER);
        return fat_end_value(vol);
    }
    if (!is_valid_data_cluster(vol, cluster)) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return fat_end_value(vol);
    }

    uint32_t entrySize = fat_entry_size(vol);
    uint32_t fatOffset   = cluster * entrySize;
    uint32_t fatSector   = vol.reservedSectors + (fatOffset / vol.bytesPerSector);
    uint32_t entryOffset = fatOffset % vol.bytesPerSector;

    block::Status st = read_volume_sector(vol, fatSector, s_secBuf);
    if (st != block::BLOCK_OK) {
        set_traversal_status(TRAVERSAL_IO_ERROR);
        return fat_end_value(vol);
    }

    if (entrySize == 2) {
        uint32_t val = *reinterpret_cast<uint16_t*>(&s_secBuf[entryOffset]);
        return val & fat_cluster_mask(vol);
    }

    uint32_t val = *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]);
    return val & fat_cluster_mask(vol);
}

// ================================================================
// exFAT: read next cluster from the FAT table
// ================================================================

static uint32_t exfat_next_cluster(const FATVolume& vol, uint32_t cluster)
{
    if (is_bad_cluster(vol, cluster)) {
        set_traversal_status(TRAVERSAL_BAD_CLUSTER);
        return 0xFFFFFFFF;
    }
    if (!is_valid_data_cluster(vol, cluster)) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return 0xFFFFFFFF;
    }

    uint32_t fatOffset   = cluster * 4;
    uint32_t sectorSize  = 1u << vol.exfatBytesPerSectorShift;
    uint32_t fatSector   = vol.exfatFatOffset + (fatOffset / sectorSize);
    uint32_t entryOffset = fatOffset % sectorSize;

    block::Status st = read_volume_sector(vol, fatSector, s_secBuf);
    if (st != block::BLOCK_OK) {
        set_traversal_status(TRAVERSAL_IO_ERROR);
        return 0xFFFFFFFF;
    }

    return *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]);
}

// ================================================================
// Get next cluster (type-dispatched)
// ================================================================

static uint32_t next_cluster(const FATVolume& vol, uint32_t cluster)
{
    if (vol.type == FAT_TYPE_FAT16 || vol.type == FAT_TYPE_FAT32) return fat_next_cluster(vol, cluster);
    if (vol.type == FAT_TYPE_EXFAT) return exfat_next_cluster(vol, cluster);
    return 0xFFFFFFFF;
}

static bool is_end_of_chain(const FATVolume& vol, uint32_t cluster)
{
    // A fresh FAT32 artifact can expose a free/zero link after an empty root
    // directory. Treat malformed/free links as a bounded chain end.
    if (!is_valid_data_cluster(vol, cluster)) return true;
    if (vol.type == FAT_TYPE_FAT16) return cluster >= FAT16_CLUSTER_END;
    if (vol.type == FAT_TYPE_FAT32) return cluster >= FAT32_CLUSTER_END;
    if (vol.type == FAT_TYPE_EXFAT) return cluster >= 0xFFFFFFF8;
    return true;
}

// Floyd's tortoise/hare check keeps cycle detection bounded without a
// heap-sized visited-cluster table. Reaching EOC before the limit is the
// normal terminating case; reaching the limit without EOC is handled by the
// caller's step invariant.
static bool chain_cycle_detected(const FATVolume& vol, uint32_t firstCluster,
                                 uint32_t stepLimit)
{
    if (stepLimit == 0 || is_end_of_chain(vol, firstCluster) ||
        !is_valid_data_cluster(vol, firstCluster)) return false;

    uint32_t slow = firstCluster;
    uint32_t fast = firstCluster;
    for (uint32_t step = 0; step < stepLimit; ++step) {
        if (is_end_of_chain(vol, slow) || !is_valid_data_cluster(vol, slow)) return false;
        slow = next_cluster(vol, slow);

        if (is_end_of_chain(vol, fast) || !is_valid_data_cluster(vol, fast)) return false;
        fast = next_cluster(vol, fast);
        if (is_end_of_chain(vol, fast) || !is_valid_data_cluster(vol, fast)) return false;
        fast = next_cluster(vol, fast);
        if (slow == fast && !is_end_of_chain(vol, slow)) return true;
    }
    return false;
}

// ================================================================
// Mount — detect FAT32 or exFAT and fill volume descriptor
// ================================================================

static bool try_mount_fat32_boot_sector(uint8_t blockDevIdx, uint64_t partitionOffset, FATVolume& vol, const uint8_t* bootSector)
{
    if (!bootSector) return false;

    const FAT32_BPB* bpb = reinterpret_cast<const FAT32_BPB*>(bootSector);

#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    kernel::serial::puts("[FAT] boot-sector probe lba=");
    kernel::serial::put_hex32(static_cast<uint32_t>(partitionOffset));
    kernel::serial::puts(" bps=");
    kernel::serial::put_hex16(bpb->bytesPerSector);
    kernel::serial::puts(" spc=");
    kernel::serial::put_hex8(bpb->sectorsPerCluster);
    kernel::serial::puts(" fat16=");
    kernel::serial::put_hex16(bpb->fatSize16);
    kernel::serial::puts(" fat32=");
    kernel::serial::put_hex32(bpb->fatSize32);
    kernel::serial::puts(" rootEntries=");
    kernel::serial::put_hex16(bpb->rootEntryCount);
    kernel::serial::puts(" fsType=");
    kernel::serial::puts(bpb->fsType);
    kernel::serial::putc('\n');
#endif

    // Basic sanity checks
    if (bpb->bytesPerSector < 512 || bpb->bytesPerSector > 4096) return false;
    if (bpb->sectorsPerCluster == 0) return false;
    if (bpb->numFATs == 0) return false;
    if (bpb->fatSize32 == 0) {
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        kernel::serial::puts("[FAT] boot-sector reject reason=fat32-size-zero\n");
#endif
        return false;
    }

    // Check for "FAT32   " signature
    if (!str_equal(bpb->fsType, "FAT32   ", 8)) {
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        kernel::serial::puts("[FAT] boot-sector reject reason=fat32-signature-mismatch\n");
#endif
        return false;
    }

    vol.type              = FAT_TYPE_FAT32;
    vol.blockDevIndex     = blockDevIdx;
    vol.partitionOffset   = partitionOffset;
    vol.bytesPerSector    = bpb->bytesPerSector;
    vol.sectorsPerCluster = bpb->sectorsPerCluster;
    vol.reservedSectors   = bpb->reservedSectors;
    vol.numFATs           = bpb->numFATs;
    vol.fatSizeSectors    = bpb->fatSize32;
    vol.rootCluster       = bpb->rootCluster;
    vol.rootDirFirstSector = 0;
    vol.rootDirSectors     = 0;

    vol.totalSectors = (bpb->totalSectors32 != 0)
                       ? bpb->totalSectors32
                       : bpb->totalSectors16;

    if (vol.totalSectors == 0) return false;

    vol.firstDataSector = vol.reservedSectors +
                          (vol.numFATs * vol.fatSizeSectors);

    if (vol.totalSectors <= vol.firstDataSector) return false;
    uint32_t dataSectors = vol.totalSectors - vol.firstDataSector;
    vol.totalDataClusters = dataSectors / vol.sectorsPerCluster;
    if (vol.totalDataClusters == 0 || !cluster_byte_count(vol, &dataSectors)) return false;
    vol.nextFreeCluster = 2;

    // Copy volume label
    memcopy(vol.volumeLabel, bpb->volumeLabel, 11);
    vol.volumeLabel[11] = '\0';

    vol.mounted = true;
    return true;
}

static bool try_mount_fat16_boot_sector(uint8_t blockDevIdx, uint64_t partitionOffset, FATVolume& vol, const uint8_t* bootSector)
{
    if (!bootSector) return false;

    const FAT12_16_BPB* bpb = reinterpret_cast<const FAT12_16_BPB*>(bootSector);

#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
    kernel::serial::puts("[FAT] boot-sector probe lba=");
    kernel::serial::put_hex32(static_cast<uint32_t>(partitionOffset));
    kernel::serial::puts(" bps=");
    kernel::serial::put_hex16(bpb->bytesPerSector);
    kernel::serial::puts(" spc=");
    kernel::serial::put_hex8(bpb->sectorsPerCluster);
    kernel::serial::puts(" rootEntries=");
    kernel::serial::put_hex16(bpb->rootEntryCount);
    kernel::serial::puts(" fat16=");
    kernel::serial::put_hex16(bpb->fatSize16);
    kernel::serial::puts(" fsType=");
    kernel::serial::puts(bpb->fsType);
    kernel::serial::putc('\n');
#endif

    if (bpb->bytesPerSector < 512 || bpb->bytesPerSector > 4096) return false;
    if (bpb->sectorsPerCluster == 0) return false;
    if (bpb->numFATs == 0) return false;
    if (bpb->rootEntryCount == 0) return false;
    if (bpb->fatSize16 == 0) {
#if defined(GXOS_DESKTOP_CLEANUP_RUNTIME_PASS)
        kernel::serial::puts("[FAT] boot-sector reject reason=fat16-size-zero\n");
#endif
        return false;
    }

    vol.type              = FAT_TYPE_FAT16;
    vol.blockDevIndex     = blockDevIdx;
    vol.partitionOffset   = partitionOffset;
    vol.bytesPerSector    = bpb->bytesPerSector;
    vol.sectorsPerCluster = bpb->sectorsPerCluster;
    vol.reservedSectors   = bpb->reservedSectors;
    vol.numFATs           = bpb->numFATs;
    vol.fatSizeSectors    = bpb->fatSize16;
    vol.rootCluster       = 0;
    vol.rootDirFirstSector = vol.reservedSectors + (vol.numFATs * vol.fatSizeSectors);
    vol.rootDirSectors     = ((static_cast<uint32_t>(bpb->rootEntryCount) * 32) + (vol.bytesPerSector - 1)) / vol.bytesPerSector;

    vol.totalSectors = (bpb->totalSectors32 != 0)
                       ? bpb->totalSectors32
                       : bpb->totalSectors16;
    if (vol.totalSectors == 0) return false;

    vol.firstDataSector = vol.rootDirFirstSector + vol.rootDirSectors;
    if (vol.totalSectors <= vol.firstDataSector) return false;

    uint32_t dataSectors = vol.totalSectors - vol.firstDataSector;
    vol.totalDataClusters = dataSectors / vol.sectorsPerCluster;
    if (vol.totalDataClusters == 0 || !cluster_byte_count(vol, &dataSectors)) return false;
    vol.nextFreeCluster = 2;

    memcopy(vol.volumeLabel, bpb->volumeLabel, 11);
    vol.volumeLabel[11] = '\0';

    vol.mounted = true;
    return true;
}

static bool try_mount_fat_boot_sector(uint8_t blockDevIdx, uint64_t partitionOffset, FATVolume& vol, const uint8_t* bootSector)
{
    if (try_mount_fat32_boot_sector(blockDevIdx, partitionOffset, vol, bootSector)) {
        return true;
    }
    return try_mount_fat16_boot_sector(blockDevIdx, partitionOffset, vol, bootSector);
}

static bool try_mount_fat(uint8_t blockDevIdx, FATVolume& vol)
{
    block::Status st = block::read_sectors(blockDevIdx, 0, 1, s_secBuf);
    if (st != block::BLOCK_OK) return false;

    if (try_mount_fat_boot_sector(blockDevIdx, 0, vol, s_secBuf)) {
        return true;
    }

    // Could be an MBR with a FAT partition. Probe the primary partition table
    // and let the boot sector at the partition start decide the actual format.
    if (s_secBuf[510] == 0x55 && s_secBuf[511] == 0xAA) {
        for (uint32_t partIndex = 0; partIndex < 4; ++partIndex) {
            uint32_t entry = 446 + partIndex * 16;
            uint8_t partType = s_secBuf[entry + 4];
            if (!is_fat_partition_type(partType)) continue;

            uint32_t startLBA = *reinterpret_cast<uint32_t*>(&s_secBuf[entry + 8]);
            if (startLBA == 0) continue;

            if (block::read_sectors(blockDevIdx, startLBA, 1, s_secBuf) != block::BLOCK_OK) {
                continue;
            }

            if (try_mount_fat_boot_sector(blockDevIdx, startLBA, vol, s_secBuf)) {
                return true;
            }
        }
    }

    return false;
}

static bool try_mount_exfat(uint8_t blockDevIdx, FATVolume& vol)
{
    block::Status st = block::read_sectors(blockDevIdx, 0, 1, s_secBuf);
    if (st != block::BLOCK_OK) return false;

    const ExFAT_BootSector* bs = reinterpret_cast<const ExFAT_BootSector*>(s_secBuf);

    // Check "EXFAT   " signature
    if (!str_equal(bs->fsName, "EXFAT   ", 8)) return false;
    if (bs->bootSignature != 0xAA55) return false;

    vol.type                       = FAT_TYPE_EXFAT;
    vol.blockDevIndex              = blockDevIdx;
    vol.exfatVolumeLength          = bs->volumeLength;
    vol.exfatFatOffset             = bs->fatOffset;
    vol.exfatFatLength             = bs->fatLength;
    vol.exfatClusterHeapOffset     = bs->clusterHeapOffset;
    vol.exfatClusterCount          = bs->clusterCount;
    vol.exfatBytesPerSectorShift   = bs->bytesPerSectorShift;
    vol.exfatSectorsPerClusterShift = bs->sectorsPerClusterShift;
    vol.numFATs                    = bs->numFATs;
    vol.rootCluster                = bs->rootDirCluster;
    vol.partitionOffset            = bs->partitionOffset;
    vol.rootDirFirstSector         = 0;
    vol.rootDirSectors             = 0;

    vol.bytesPerSector    = 1u << bs->bytesPerSectorShift;
    vol.sectorsPerCluster = 1u << bs->sectorsPerClusterShift;
    vol.firstDataSector   = bs->clusterHeapOffset;
    vol.totalDataClusters = bs->clusterCount;
    vol.nextFreeCluster   = 2;

    vol.volumeLabel[0] = '\0';
    vol.mounted = true;
    return true;
}

// ================================================================
// Read a sector from a cluster
// ================================================================

static block::Status read_cluster_sector(const FATVolume& vol,
                                         uint32_t cluster,
                                         uint32_t sectorOffset,
                                         void* buffer)
{
    if (!is_valid_data_cluster(vol, cluster) ||
        sectorOffset >= vol.sectorsPerCluster) {
        return block::BLOCK_ERR_INVALID;
    }

    uint32_t lba;
    if (vol.type == FAT_TYPE_FAT16 || vol.type == FAT_TYPE_FAT32) {
        lba = cluster_to_sector(vol, cluster) + sectorOffset;
    } else {
        // exFAT
        lba = vol.exfatClusterHeapOffset +
              (cluster - 2) * vol.sectorsPerCluster + sectorOffset;
    }
    return read_volume_sector(vol, lba, buffer);
}

static block::Status write_cluster_sector(const FATVolume& vol,
                                          uint32_t cluster,
                                          uint32_t sectorOffset,
                                          const void* buffer)
{
    if (!is_valid_data_cluster(vol, cluster) ||
        sectorOffset >= vol.sectorsPerCluster) {
        return block::BLOCK_ERR_INVALID;
    }

    uint32_t lba;
    if (vol.type == FAT_TYPE_FAT16 || vol.type == FAT_TYPE_FAT32) {
        lba = cluster_to_sector(vol, cluster) + sectorOffset;
    } else {
        lba = vol.exfatClusterHeapOffset +
              (cluster - 2) * vol.sectorsPerCluster + sectorOffset;
    }
    return write_volume_sector(vol, lba, buffer);
}

static block::Status write_fat_entry(const FATVolume& vol, uint32_t cluster, uint32_t value)
{
    if ((vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) ||
        !is_valid_data_cluster(vol, cluster) || vol.bytesPerSector == 0 ||
        vol.fatSizeSectors == 0 || vol.numFATs == 0) {
        s_lastIoStatus = block::BLOCK_ERR_INVALID;
        return s_lastIoStatus;
    }

    uint32_t entrySize = fat_entry_size(vol);
    const uint64_t fatOffset = static_cast<uint64_t>(cluster) * entrySize;
    const uint64_t fatSectorOffset = fatOffset / vol.bytesPerSector;
    const uint32_t entryOffset = static_cast<uint32_t>(fatOffset % vol.bytesPerSector);
    if (fatSectorOffset >= vol.fatSizeSectors ||
        entryOffset + entrySize > vol.bytesPerSector) {
        s_lastIoStatus = block::BLOCK_ERR_INVALID;
        return s_lastIoStatus;
    }
    const uint64_t fatSector64 = static_cast<uint64_t>(vol.reservedSectors) +
                                 fatSectorOffset;
    if (fatSector64 > 0xFFFFFFFFull || fatSector64 >= vol.totalSectors) {
        s_lastIoStatus = block::BLOCK_ERR_INVALID;
        return s_lastIoStatus;
    }
    const uint32_t fatSector = static_cast<uint32_t>(fatSector64);

    block::Status st = read_volume_sector(vol, fatSector, s_secBuf);
    if (st != block::BLOCK_OK) return st;

    if (entrySize == 2) {
        *reinterpret_cast<uint16_t*>(&s_secBuf[entryOffset]) = static_cast<uint16_t>(value & FAT16_CLUSTER_MASK);
    } else {
        uint32_t& current = *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]);
        current = (current & ~FAT32_CLUSTER_MASK) | (value & FAT32_CLUSTER_MASK);
    }
    st = write_volume_sector(vol, fatSector, s_secBuf);
    if (st != block::BLOCK_OK) return st;

    for (uint32_t fatIndex = 1; fatIndex < vol.numFATs; ++fatIndex) {
        const uint64_t mirrorSector64 = fatSector64 +
            static_cast<uint64_t>(fatIndex) * vol.fatSizeSectors;
        if (mirrorSector64 >= vol.totalSectors || mirrorSector64 > 0xFFFFFFFFull) {
            s_lastIoStatus = block::BLOCK_ERR_INVALID;
            return s_lastIoStatus;
        }
        const uint32_t mirrorSector = static_cast<uint32_t>(mirrorSector64);
        st = write_volume_sector(vol, mirrorSector, s_secBuf);
        if (st != block::BLOCK_OK) return st;
    }

    return block::BLOCK_OK;
}

static FileResizeStatus resize_io_status()
{
    return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
        ? FILE_RESIZE_IO_TIMEOUT : FILE_RESIZE_IO_ERROR;
}

// Unlike next_cluster(), this reader does not turn free, reserved, bad, or
// out-of-range values into an apparent end-of-chain. Resize must distinguish
// every malformed link before changing ownership.
static FileResizeStatus read_resize_link(const FATVolume& vol,
                                         uint32_t cluster,
                                         uint32_t* outNext,
                                         bool* outEndOfChain)
{
    if (!outNext || !outEndOfChain || vol.type != FAT_TYPE_FAT32 ||
        !is_valid_data_cluster(vol, cluster) || vol.bytesPerSector == 0) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }

    const uint64_t fatOffset = static_cast<uint64_t>(cluster) * 4u;
    const uint64_t sectorOffset = fatOffset / vol.bytesPerSector;
    const uint32_t entryOffset = static_cast<uint32_t>(fatOffset % vol.bytesPerSector);
    if (sectorOffset >= vol.fatSizeSectors ||
        entryOffset + 4u > vol.bytesPerSector) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }
    const uint64_t sector64 = static_cast<uint64_t>(vol.reservedSectors) + sectorOffset;
    if (sector64 >= vol.totalSectors || sector64 > 0xFFFFFFFFull) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }

    if (read_volume_sector(vol, static_cast<uint32_t>(sector64), s_secBuf) != block::BLOCK_OK) {
        set_traversal_status(TRAVERSAL_IO_ERROR);
        return resize_io_status();
    }

    const uint32_t value = *reinterpret_cast<const uint32_t*>(&s_secBuf[entryOffset]) &
                           FAT32_CLUSTER_MASK;
    if (value >= FAT32_CLUSTER_END) {
        *outNext = 0;
        *outEndOfChain = true;
        set_traversal_status(TRAVERSAL_END_OF_CHAIN);
        return FILE_RESIZE_OK;
    }
    if (value == FAT32_CLUSTER_FREE) {
        set_traversal_status(TRAVERSAL_TRUNCATED_CHAIN);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }
    if (value == FAT32_CLUSTER_BAD) {
        set_traversal_status(TRAVERSAL_BAD_CLUSTER);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }
    if (value >= 0x0FFFFFF0u || !is_valid_data_cluster(vol, value)) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }

    *outNext = value;
    *outEndOfChain = false;
    set_traversal_status(TRAVERSAL_OK);
    return FILE_RESIZE_OK;
}

static void resize_chain_sift_down(uint32_t start, uint32_t endExclusive)
{
    uint32_t root = start;
    for (;;) {
        const uint64_t left64 = static_cast<uint64_t>(root) * 2u + 1u;
        if (left64 >= endExclusive) return;
        uint32_t child = static_cast<uint32_t>(left64);
        if (child + 1u < endExclusive && s_resizeChain[child] < s_resizeChain[child + 1u]) {
            ++child;
        }
        if (s_resizeChain[root] >= s_resizeChain[child]) return;
        const uint32_t tmp = s_resizeChain[root];
        s_resizeChain[root] = s_resizeChain[child];
        s_resizeChain[child] = tmp;
        root = child;
    }
}

static void sort_resize_chain(uint32_t count)
{
    for (uint32_t start = count / 2u; start > 0; --start) {
        resize_chain_sift_down(start - 1u, count);
    }
    for (uint32_t end = count; end > 1u; --end) {
        const uint32_t tmp = s_resizeChain[0];
        s_resizeChain[0] = s_resizeChain[end - 1u];
        s_resizeChain[end - 1u] = tmp;
        resize_chain_sift_down(0, end - 1u);
    }
}

static bool resize_chain_contains(uint32_t count, uint32_t cluster)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        if (s_resizeChain[middle] == cluster) return true;
        if (s_resizeChain[middle] < cluster) low = middle + 1u;
        else high = middle;
    }
    return false;
}

static FileResizeStatus reject_volume_cross_links(const FATVolume& vol,
                                                  uint32_t targetSector,
                                                  uint32_t targetOffset,
                                                  uint32_t chainCount)
{
    if (chainCount == 0) return FILE_RESIZE_OK;
    if (!is_valid_data_cluster(vol, vol.rootCluster)) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }

    const uint32_t entriesPerSector = vol.bytesPerSector / 32u;
    if (entriesPerSector == 0 || vol.bytesPerSector % 32u != 0 ||
        vol.bytesPerSector > sizeof(s_resizeDirBuf)) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return FILE_RESIZE_CORRUPT_CHAIN;
    }

    uint32_t queueCount = 1;
    uint32_t queueIndex = 0;
    uint32_t workSteps = 0;
    s_resizeDirectoryQueue[0] = vol.rootCluster;
    const uint32_t stepLimit = directory_chain_step_limit(vol);
    while (queueIndex < queueCount) {
        const uint32_t firstDirectoryCluster = s_resizeDirectoryQueue[queueIndex++];
        for (uint32_t prior = 0; prior + 1u < queueIndex; ++prior) {
            if (s_resizeDirectoryQueue[prior] == firstDirectoryCluster) {
                set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
                return FILE_RESIZE_CORRUPT_CHAIN;
            }
        }

        uint32_t directoryCluster = firstDirectoryCluster;
        bool directoryEnded = false;
        bool chainEnded = false;
        for (uint32_t step = 0; step < stepLimit; ++step) {
            if (!is_valid_data_cluster(vol, directoryCluster)) {
                set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
                return FILE_RESIZE_CORRUPT_CHAIN;
            }
            if (++workSteps > kMaxResizeOwnershipScanSteps) {
                return FILE_RESIZE_UNSUPPORTED_SIZE;
            }
            if (resize_chain_contains(chainCount, directoryCluster)) {
                return FILE_RESIZE_CROSS_LINK;
            }

            for (uint32_t sectorInCluster = 0;
                 sectorInCluster < vol.sectorsPerCluster; ++sectorInCluster) {
                if (++workSteps > kMaxResizeOwnershipScanSteps) {
                    return FILE_RESIZE_UNSUPPORTED_SIZE;
                }
                const uint32_t sector = cluster_to_sector(vol, directoryCluster) + sectorInCluster;
                if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
                    set_traversal_status(TRAVERSAL_IO_ERROR);
                    return resize_io_status();
                }
                memcopy(s_resizeDirBuf, s_secBuf, vol.bytesPerSector);
                if (directoryEnded) continue;
                for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                    const uint32_t offset = entryIndex * 32u;
                    const FAT32_DirEntry* entry =
                        reinterpret_cast<const FAT32_DirEntry*>(&s_resizeDirBuf[offset]);
                    if (static_cast<uint8_t>(entry->name[0]) == 0x00) {
                        directoryEnded = true;
                        break;
                    }
                    if (static_cast<uint8_t>(entry->name[0]) == 0xE5 ||
                        entry->attr == ATTR_LFN || (entry->attr & ATTR_VOLUME_ID)) {
                        continue;
                    }
                    if (sector == targetSector && offset == targetOffset) continue;

                    // FAT directories contain self and parent entries. They
                    // are not child directories and must not be enqueued.
                    if (entry->name[0] == '.' &&
                        (entry->name[1] == ' ' || entry->name[1] == '.')) {
                        continue;
                    }

                    const uint32_t firstCluster =
                        ((static_cast<uint32_t>(entry->firstClusterHi) << 16) |
                         entry->firstClusterLo) & FAT32_CLUSTER_MASK;
                    if (firstCluster == 0) continue;
                    if (entry->attr & ATTR_DIRECTORY) {
                        if (!is_valid_data_cluster(vol, firstCluster)) {
                            set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
                            return FILE_RESIZE_CORRUPT_CHAIN;
                        }
                        for (uint32_t prior = 0; prior < queueCount; ++prior) {
                            if (s_resizeDirectoryQueue[prior] == firstCluster) {
                                set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
                                return FILE_RESIZE_CORRUPT_CHAIN;
                            }
                        }
                        if (queueCount >= kMaxResizeOwnershipDirectories) {
                            return FILE_RESIZE_UNSUPPORTED_SIZE;
                        }
                        s_resizeDirectoryQueue[queueCount++] = firstCluster;
                        continue;
                    }

                    uint32_t peerCluster = firstCluster;
                    const uint32_t peerLimit = max_chain_steps(vol);
                    bool peerEnded = false;
                    for (uint32_t peerStep = 0; peerStep < peerLimit; ++peerStep) {
                        if (++workSteps > kMaxResizeOwnershipScanSteps) {
                            return FILE_RESIZE_UNSUPPORTED_SIZE;
                        }
                        if (resize_chain_contains(chainCount, peerCluster)) {
                            return FILE_RESIZE_CROSS_LINK;
                        }
                        uint32_t peerNext = 0;
                        bool peerEndOfChain = false;
                        const FileResizeStatus peerStatus = read_resize_link(
                            vol, peerCluster, &peerNext, &peerEndOfChain);
                        if (peerStatus != FILE_RESIZE_OK) return peerStatus;
                        if (peerEndOfChain) {
                            peerEnded = true;
                            break;
                        }
                        peerCluster = peerNext;
                    }
                    if (!peerEnded) {
                        set_traversal_status(TRAVERSAL_CHAIN_STEP_LIMIT);
                        return FILE_RESIZE_CORRUPT_CHAIN;
                    }
                }
            }

            if (++workSteps > kMaxResizeOwnershipScanSteps) {
                return FILE_RESIZE_UNSUPPORTED_SIZE;
            }
            uint32_t next = 0;
            bool endOfChain = false;
            const FileResizeStatus linkStatus = read_resize_link(
                vol, directoryCluster, &next, &endOfChain);
            if (linkStatus != FILE_RESIZE_OK) return linkStatus;
            if (endOfChain) {
                chainEnded = true;
                break;
            }
            directoryCluster = next;
        }
        if (!chainEnded) {
            set_traversal_status(TRAVERSAL_CHAIN_STEP_LIMIT);
            return FILE_RESIZE_CORRUPT_CHAIN;
        }
    }
    return FILE_RESIZE_OK;
}

static FileResizeStatus release_resize_chain(FATVolume& vol,
                                             uint32_t firstCluster,
                                             uint32_t clusterCount)
{
    uint32_t cluster = firstCluster;
    for (uint32_t index = 0; index < clusterCount; ++index) {
        uint32_t next = 0;
        bool endOfChain = false;
        const FileResizeStatus linkStatus = read_resize_link(
            vol, cluster, &next, &endOfChain);
        if (linkStatus != FILE_RESIZE_OK) return linkStatus;
        if ((index + 1u == clusterCount) != endOfChain) {
            set_traversal_status(TRAVERSAL_TRUNCATED_CHAIN);
            return FILE_RESIZE_CORRUPT_CHAIN;
        }
        if (write_fat_entry(vol, cluster, FAT32_CLUSTER_FREE) != block::BLOCK_OK) {
            return resize_io_status();
        }
        vol.nextFreeCluster = cluster;
        if (!endOfChain) cluster = next;
    }
    return FILE_RESIZE_OK;
}

static uint32_t allocate_cluster(FATVolume& vol)
{
    if (vol.totalDataClusters == 0) return 0;
    uint32_t entrySize = fat_entry_size(vol);
    const uint32_t previousHint = vol.nextFreeCluster;
    const uint32_t firstCluster =
        is_valid_data_cluster(vol, vol.nextFreeCluster) ? vol.nextFreeCluster : 2;
    serial::puts("LFPASTE_ALLOC_BEGIN previous=0x");
    serial::put_hex32(previousHint);
    serial::puts(" start=0x");
    serial::put_hex32(firstCluster);
    serial::puts("\n");

    // Search each valid data-cluster slot at most once, beginning at the last
    // successful allocation. This makes allocation O(number of candidates)
    // for an operation, rather than restarting at cluster 2 each time.
    for (uint32_t scan = 0; scan < vol.totalDataClusters; ++scan) {
        const uint32_t relative =
            (static_cast<uint64_t>(firstCluster - 2) + scan) % vol.totalDataClusters;
        const uint32_t cluster = relative + 2;
        const uint32_t fatOffset = cluster * entrySize;
        const uint32_t fatSectorOffset = fatOffset / vol.bytesPerSector;
        const uint32_t entryOffset = fatOffset % vol.bytesPerSector;
        if (fatSectorOffset >= vol.fatSizeSectors ||
            read_volume_sector(vol, vol.reservedSectors + fatSectorOffset, s_secBuf) != block::BLOCK_OK) {
            return 0;
        }
        ++s_fatScanIterations;

        uint32_t value;
        if (entrySize == 2) {
            value = *reinterpret_cast<uint16_t*>(&s_secBuf[entryOffset]) & FAT16_CLUSTER_MASK;
        } else {
            value = *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]) & FAT32_CLUSTER_MASK;
        }
        if (value != fat_free_value(vol)) continue;

        if (write_fat_entry(vol, cluster, fat_end_value(vol)) != block::BLOCK_OK) {
            return 0;
        }

        vol.nextFreeCluster = cluster == vol.totalDataClusters + 1 ? 2 : cluster + 1;
        ++s_allocatedClusters;
        serial::puts("LFPASTE_ALLOC_END new=0x");
        serial::put_hex32(cluster);
        serial::puts(" scans=0x");
        serial::put_hex64(scan + 1);
        serial::puts(" totalScans=0x");
        serial::put_hex64(s_fatScanIterations);
        serial::puts(" status=OK\n");
        return cluster;
    }

    serial::puts("LFPASTE_ALLOC_END new=0x00000000 scans=0x");
    serial::put_hex64(vol.totalDataClusters);
    serial::puts(" totalScans=0x");
    serial::put_hex64(s_fatScanIterations);
    serial::puts(" status=NO_SPACE\n");
    return 0;
}

static void release_allocated_cluster(FATVolume& vol, uint32_t cluster)
{
    if (!is_valid_data_cluster(vol, cluster)) return;
    // Best-effort rollback. The original create path leaked the newly
    // allocated cluster when directory-sector publication failed.
    write_fat_entry(vol, cluster, fat_free_value(vol));
    serial::puts("LFPASTE_CLUSTER_RELEASE cluster=0x");
    serial::put_hex32(cluster);
    serial::puts(" hint=0x");
    serial::put_hex32(vol.nextFreeCluster);
    serial::puts(" mode=forward-hint\n");
}

static bool release_cluster_chain(FATVolume& vol, uint32_t firstCluster)
{
    if (chain_cycle_detected(vol, firstCluster, max_chain_steps(vol))) {
        set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
        return false;
    }
    uint32_t cluster = firstCluster;
    uint32_t steps = 0;
    while (is_valid_data_cluster(vol, cluster) && steps < max_chain_steps(vol)) {
        ++steps;
        const uint32_t next = next_cluster(vol, cluster);
        if (write_fat_entry(vol, cluster, fat_free_value(vol)) != block::BLOCK_OK) return false;
        serial::puts("LFPASTE_CLUSTER_RELEASE cluster=0x");
        serial::put_hex32(cluster);
        serial::puts(" hint=0x");
        serial::put_hex32(vol.nextFreeCluster);
        serial::puts(" mode=forward-hint\n");
        if (is_end_of_chain(vol, next)) return true;
        if (!is_valid_data_cluster(vol, next)) {
            set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
            return false;
        }
        cluster = next;
    }
    set_traversal_status(TRAVERSAL_CHAIN_STEP_LIMIT);
    return false;
}

// ================================================================
// 8.3 short name to readable string
// ================================================================

static void short_name_to_string(const char* raw, char* out)
{
    int pos = 0;
    // Base name (first 8 chars, trim trailing spaces)
    for (int i = 0; i < 8; ++i) {
        if (raw[i] != ' ') out[pos++] = raw[i];
    }
    // Extension (chars 8-10, trim trailing spaces)
    bool hasExt = false;
    for (int i = 8; i < 11; ++i) {
        if (raw[i] != ' ') { hasExt = true; break; }
    }
    if (hasExt) {
        out[pos++] = '.';
        for (int i = 8; i < 11; ++i) {
            if (raw[i] != ' ') out[pos++] = raw[i];
        }
    }
    out[pos] = '\0';
}

static void copy_c_string(char* dst, uint32_t dstSize, const char* src)
{
    if (!dst || dstSize == 0) return;
    uint32_t i = 0;
    if (src) {
        while (src[i] && i + 1 < dstSize) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static void clear_lfn_name(char* name, uint32_t nameSize)
{
    if (!name || nameSize == 0) return;
    for (uint32_t i = 0; i < nameSize; ++i) name[i] = '\0';
}

static void lfn_put_char(char* name, uint32_t nameSize, uint32_t pos, uint16_t ch)
{
    if (!name || nameSize == 0 || pos + 1 >= nameSize) return;
    if (ch == 0x0000 || ch == 0xFFFF) return;
    name[pos] = (ch < 0x80) ? static_cast<char>(ch) : '?';
}

static void collect_lfn_entry(const FAT32_LFNEntry* lfn, char* name, uint32_t nameSize)
{
    if (!lfn || !name || nameSize == 0) return;
    uint32_t sequence = lfn->order & 0x1F;
    if (sequence == 0) return;
    uint32_t base = (sequence - 1) * 13;
    for (uint32_t i = 0; i < 5; ++i) lfn_put_char(name, nameSize, base + i, lfn->name1[i]);
    for (uint32_t i = 0; i < 6; ++i) lfn_put_char(name, nameSize, base + 5 + i, lfn->name2[i]);
    for (uint32_t i = 0; i < 2; ++i) lfn_put_char(name, nameSize, base + 11 + i, lfn->name3[i]);
}

static void fill_dir_entry_from_fat(const FAT32_DirEntry* de, const char* displayName, DirEntry* out)
{
    memzero(out, sizeof(DirEntry));
    copy_c_string(out->name, sizeof(out->name), displayName);
    out->fileSize     = de->fileSize;
    out->firstCluster = (static_cast<uint32_t>(de->firstClusterHi) << 16) |
                        de->firstClusterLo;
    out->attr         = de->attr;
    out->crtDate      = de->crtDate;
    out->crtTime      = de->crtTime;
    out->wrtDate      = de->wrtDate;
    out->wrtTime      = de->wrtTime;
    out->isDir        = (de->attr & ATTR_DIRECTORY) != 0;
}

static bool is_fat16_root_dir(const FATVolume& vol, uint32_t dirCluster)
{
    return vol.type == FAT_TYPE_FAT16 && dirCluster == 0;
}

static block::Status read_root_dir_sector(const FATVolume& vol, uint32_t sectorIndex, void* buffer)
{
    if (sectorIndex >= vol.rootDirSectors) return block::BLOCK_ERR_INVALID;
    return read_volume_sector(vol, vol.rootDirFirstSector + sectorIndex, buffer);
}

static block::Status write_root_dir_sector(const FATVolume& vol, uint32_t sectorIndex, const void* buffer)
{
    if (sectorIndex >= vol.rootDirSectors) return block::BLOCK_ERR_INVALID;
    return write_volume_sector(vol, vol.rootDirFirstSector + sectorIndex, buffer);
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_volumes, sizeof(s_volumes));
    memzero(s_files, sizeof(s_files));
    memzero(&s_dirIter, sizeof(s_dirIter));
    s_volumeCount = 0;
    s_lastIoStatus = block::BLOCK_OK;
    s_lastTraversalStatus = TRAVERSAL_OK;
    s_lastDeleteStatus = DELETE_OK;
    s_fatScanIterations = 0;
    s_allocatedClusters = 0;
}

void set_trash_trace(bool enabled, uint64_t generation)
{
    s_trashTraceActive = enabled;
    s_trashTraceGeneration = enabled ? generation : 0;
}

uint8_t mount(uint8_t blockDevIndex)
{
    if (s_volumeCount >= MAX_FAT_VOLUMES) return 0xFF;

    // Find a free slot
    uint8_t idx = 0xFF;
    for (uint8_t i = 0; i < MAX_FAT_VOLUMES; ++i) {
        if (!s_volumes[i].mounted) { idx = i; break; }
    }
    if (idx == 0xFF) return 0xFF;

    FATVolume& vol = s_volumes[idx];
    memzero(&vol, sizeof(vol));

    // Try FAT first, then exFAT
    if (try_mount_fat(blockDevIndex, vol)) {
        ++s_volumeCount;
        return idx;
    }
    if (try_mount_exfat(blockDevIndex, vol)) {
        ++s_volumeCount;
        return idx;
    }

    return 0xFF;
}

void unmount(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return;
    if (!s_volumes[volumeIndex].mounted) return;

    // Close any open files on this volume
    for (uint8_t i = 0; i < MAX_OPEN_FILES; ++i) {
        if (s_files[i].open && s_files[i].volumeIndex == volumeIndex)
            s_files[i].open = false;
    }

    s_volumes[volumeIndex].mounted = false;
    if (s_volumeCount > 0) --s_volumeCount;
}

bool open_root_dir(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;

    s_dirIter.active          = true;
    s_dirIter.volIdx          = volumeIndex;
    s_dirIter.cluster         = s_volumes[volumeIndex].rootCluster;
    s_dirIter.clusterSteps    = 0;
    s_dirIter.sectorInCluster = 0;
    s_dirIter.entryInSector   = 0;
    set_traversal_status(TRAVERSAL_OK);
    return true;
}

void close_dir(uint8_t volumeIndex)
{
    if (!s_dirIter.active || s_dirIter.volIdx != volumeIndex) return;
    s_dirIter.active = false;
    s_dirIter.cluster = 0;
    s_dirIter.clusterSteps = 0;
    s_dirIter.sectorInCluster = 0;
    s_dirIter.entryInSector = 0;
    set_traversal_status(TRAVERSAL_OK);
}

bool read_dir(uint8_t volumeIndex, DirEntry* out)
{
    if (!s_dirIter.active || s_dirIter.volIdx != volumeIndex) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    uint32_t entriesPerSector = vol.bytesPerSector / 32;
    char lfnName[256];
    clear_lfn_name(lfnName, sizeof(lfnName));

    while (true) {
        if (is_fat16_root_dir(vol, s_dirIter.cluster)) {
            if (s_dirIter.sectorInCluster >= vol.rootDirSectors) {
                s_dirIter.active = false;
                return false;
            }

            if (read_root_dir_sector(vol, s_dirIter.sectorInCluster, s_secBuf) != block::BLOCK_OK) {
                s_dirIter.active = false;
                return false;
            }
        } else if (is_end_of_chain(vol, s_dirIter.cluster) ||
                   !is_valid_data_cluster(vol, s_dirIter.cluster) ||
                   (s_dirIter.sectorInCluster == 0 &&
                    s_dirIter.entryInSector == 0 &&
                    s_dirIter.clusterSteps >= directory_chain_step_limit(vol))) {
            s_dirIter.active = false;
            if (is_end_of_chain(vol, s_dirIter.cluster)) {
                set_traversal_status(TRAVERSAL_DIRECTORY_END);
            } else if (is_bad_cluster(vol, s_dirIter.cluster)) {
                set_traversal_status(TRAVERSAL_BAD_CLUSTER);
            } else if (!is_valid_data_cluster(vol, s_dirIter.cluster)) {
                set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
            } else {
                set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
            }
            return false;
        } else {
            if (s_dirIter.sectorInCluster == 0 && s_dirIter.entryInSector == 0) {
                ++s_dirIter.clusterSteps;
            }
            // Read current sector
            block::Status st = read_cluster_sector(vol,
                s_dirIter.cluster, s_dirIter.sectorInCluster, s_secBuf);
            if (st != block::BLOCK_OK) {
                s_dirIter.active = false;
                return false;
            }
        }

        while (s_dirIter.entryInSector < entriesPerSector) {
            uint32_t offset = s_dirIter.entryInSector * 32;
            const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(
                &s_secBuf[offset]);

            ++s_dirIter.entryInSector;

            // End of directory
            if (de->name[0] == 0x00) {
                s_dirIter.active = false;
                return false;
            }

            // Deleted entry
            if (static_cast<uint8_t>(de->name[0]) == 0xE5) {
                clear_lfn_name(lfnName, sizeof(lfnName));
                continue;
            }

            if (de->attr == ATTR_LFN) {
                collect_lfn_entry(reinterpret_cast<const FAT32_LFNEntry*>(de), lfnName, sizeof(lfnName));
                continue;
            }
            if (de->attr & ATTR_VOLUME_ID) {
                clear_lfn_name(lfnName, sizeof(lfnName));
                continue;
            }

            char shortName[32];
            short_name_to_string(de->name, shortName);
            const char* displayName = lfnName[0] ? lfnName : shortName;
            fill_dir_entry_from_fat(de, displayName, out);
            clear_lfn_name(lfnName, sizeof(lfnName));
            return true;
        }

        // Advance to next sector in cluster
        s_dirIter.entryInSector = 0;
        ++s_dirIter.sectorInCluster;
        if (is_fat16_root_dir(vol, s_dirIter.cluster)) {
            continue;
        }

        if (s_dirIter.sectorInCluster >= vol.sectorsPerCluster) {
            s_dirIter.sectorInCluster = 0;
            s_dirIter.cluster = next_cluster(vol, s_dirIter.cluster);
        }
    }
}

uint8_t open_file(uint8_t volumeIndex, uint32_t firstCluster,
                  uint32_t fileSize, uint8_t attr)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return 0xFF;
    if (!s_volumes[volumeIndex].mounted) return 0xFF;

    for (uint8_t i = 0; i < MAX_OPEN_FILES; ++i) {
        if (!s_files[i].open) {
            s_files[i].open           = true;
            s_files[i].volumeIndex    = volumeIndex;
            s_files[i].firstCluster   = firstCluster;
            s_files[i].fileSize       = fileSize;
            s_files[i].currentCluster = firstCluster;
            s_files[i].currentOffset  = 0;
            s_files[i].attr           = attr;
            s_files[i].pendingClusterAdvance = false;
            return i;
        }
    }
    return 0xFF;
}

uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len)
{
    if (fileHandle >= MAX_OPEN_FILES || (!buffer && len != 0)) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }
    FATFile& f = s_files[fileHandle];
    if (!f.open) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }

    FATVolume& vol = s_volumes[f.volumeIndex];
    uint32_t bytesRead = 0;
    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t clusterBytes = vol.sectorsPerCluster * vol.bytesPerSector;
    uint32_t clusterVisits = 0;
    set_traversal_status(TRAVERSAL_OK);
    if (clusterBytes == 0) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }

    const uint32_t bytesAvailable = f.currentOffset < f.fileSize
        ? f.fileSize - f.currentOffset : 0;
    const uint32_t bytesRequested = len < bytesAvailable ? len : bytesAvailable;
    const uint32_t offsetInFirstCluster = f.currentOffset % clusterBytes;
    const uint64_t span = static_cast<uint64_t>(offsetInFirstCluster) + bytesRequested;
    uint32_t expectedClusters = static_cast<uint32_t>(
        (span + clusterBytes - 1) / clusterBytes);
    if (expectedClusters == 0 && bytesRequested != 0) expectedClusters = 1;
    const uint32_t stepLimit = expectedClusters < max_chain_steps(vol)
        ? expectedClusters : max_chain_steps(vol);
    if (bytesRequested != 0 && chain_cycle_detected(vol, f.currentCluster, stepLimit)) {
        set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
        return 0;
    }

    while (bytesRead < len && f.currentOffset < f.fileSize) {
        if (is_end_of_chain(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_TRUNCATED_CHAIN);
            break;
        }
        if (is_bad_cluster(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_BAD_CLUSTER);
            break;
        }
        if (!is_valid_data_cluster(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
            break;
        }

        // Offset within current cluster
        uint32_t offsetInCluster = f.currentOffset % clusterBytes;
        if (offsetInCluster == 0) {
            if (clusterVisits >= stepLimit) {
                set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
                break;
            }
            ++clusterVisits;
        }
        uint32_t sectorInCluster = offsetInCluster / vol.bytesPerSector;
        uint32_t offsetInSector  = offsetInCluster % vol.bytesPerSector;

        block::Status st = read_cluster_sector(vol,
            f.currentCluster, sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) {
            set_traversal_status(TRAVERSAL_IO_ERROR);
            break;
        }

        uint32_t available = vol.bytesPerSector - offsetInSector;
        uint32_t remaining = f.fileSize - f.currentOffset;
        uint32_t wanted    = len - bytesRead;
        uint32_t toCopy    = available;
        if (toCopy > remaining) toCopy = remaining;
        if (toCopy > wanted)    toCopy = wanted;
        if (toCopy == 0) {
            set_traversal_status(TRAVERSAL_NO_PROGRESS);
            break;
        }

        memcopy(dst + bytesRead, &s_secBuf[offsetInSector], toCopy);
        bytesRead        += toCopy;
        f.currentOffset  += toCopy;

        // Advance to next cluster if we've consumed the current one
        if ((f.currentOffset % clusterBytes) == 0 && f.currentOffset > 0) {
            f.currentCluster = next_cluster(vol, f.currentCluster);
        }
    }

    if (bytesRead == bytesRequested && bytesRead != 0 &&
        s_lastTraversalStatus == TRAVERSAL_OK) {
        set_traversal_status(TRAVERSAL_END_OF_CHAIN);
    }

    return bytesRead;
}

bool seek_file(uint8_t fileHandle, uint32_t offset)
{
    if (fileHandle >= MAX_OPEN_FILES) return false;
    FATFile& f = s_files[fileHandle];
    if (!f.open) return false;

    // Seeking past EOF is allowed by the VFS contract.  A subsequent read
    // returns zero; for offsets inside the file, rebuild the cluster cursor
    // so the next FAT read starts at the requested byte rather than at the
    // position where the file was opened.
    set_traversal_status(TRAVERSAL_OK);
    f.currentOffset = offset;
    f.currentCluster = f.firstCluster;
    f.pendingClusterAdvance = false;
    if (offset == 0 || f.fileSize == 0 || offset >= f.fileSize) return true;

    FATVolume& vol = s_volumes[f.volumeIndex];
    const uint32_t clusterBytes = vol.sectorsPerCluster * vol.bytesPerSector;
    if (clusterBytes == 0) return false;

    const uint32_t clusterIndex = offset / clusterBytes;
    const uint32_t stepLimit = max_chain_steps(vol);
    for (uint32_t i = 0; i < clusterIndex && i < stepLimit; ++i) {
        if (is_end_of_chain(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_TRUNCATED_CHAIN);
            return false;
        }
        if (is_bad_cluster(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_BAD_CLUSTER);
            return false;
        }
        if (!is_valid_data_cluster(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
            return false;
        }
        f.currentCluster = next_cluster(vol, f.currentCluster);
    }
    if (clusterIndex > stepLimit) {
        set_traversal_status(TRAVERSAL_CHAIN_STEP_LIMIT);
        return false;
    }
    return true;
}

static bool extend_file_chain(FATVolume& vol, uint32_t previous,
                              uint32_t offset, uint32_t* outNext)
{
    if (!outNext || !is_valid_data_cluster(vol, previous)) {
        set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
        return false;
    }
    uint32_t next = next_cluster(vol, previous);
    if (is_end_of_chain(vol, next)) {
        serial::puts("LFPASTE_ALLOC_BEGIN previous=0x");
        serial::put_hex32(previous);
        serial::puts(" start=0x");
        serial::put_hex32(vol.nextFreeCluster);
        serial::puts(" offset=0x");
        serial::put_hex32(offset);
        serial::puts("\n");
        const uint32_t newCluster = allocate_cluster(vol);
        if (newCluster == 0) {
            set_traversal_status(TRAVERSAL_NO_PROGRESS);
            return false;
        }
        // allocate_cluster() already published the new cluster as
        // end-of-chain; link only the old tail here.
        if (write_fat_entry(vol, previous, newCluster) != block::BLOCK_OK) {
            set_traversal_status(TRAVERSAL_IO_ERROR);
            return false;
        }
        serial::puts("LFPASTE_CHAIN_LINK from=0x");
        serial::put_hex32(previous);
        serial::puts(" to=0x");
        serial::put_hex32(newCluster);
        serial::puts("\n");
        next = newCluster;
    }
    if (next == previous || is_bad_cluster(vol, next) ||
        !is_valid_data_cluster(vol, next)) {
        set_traversal_status(next == previous
            ? TRAVERSAL_CHAIN_CYCLE : TRAVERSAL_INVALID_CLUSTER);
        return false;
    }
    *outNext = next;
    return true;
}

static uint32_t write_file_cursor(FATFile& f, const void* buffer, uint32_t len)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (!f.open || (!buffer && len != 0) || len == 0) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }
    if (f.attr & ATTR_READ_ONLY || f.volumeIndex >= MAX_FAT_VOLUMES) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }

    FATVolume& vol = s_volumes[f.volumeIndex];
    if (vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }

    uint32_t clusterBytes = 0;
    if (!cluster_byte_count(vol, &clusterBytes)) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }
    const uint64_t endOffset = static_cast<uint64_t>(f.currentOffset) + len;
    if (endOffset > 0xFFFFFFFFull || f.currentOffset > f.fileSize) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }

    const uint32_t offsetInFirstCluster = f.currentOffset % clusterBytes;
    const uint64_t span = static_cast<uint64_t>(offsetInFirstCluster) + len;
    const uint32_t expectedClusters = static_cast<uint32_t>(
        (span + clusterBytes - 1) / clusterBytes);
    const uint32_t stepLimit = expectedClusters < max_chain_steps(vol)
        ? expectedClusters : max_chain_steps(vol);
    if (chain_cycle_detected(vol, f.currentCluster, stepLimit)) {
        set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
        return 0;
    }

    serial::puts("LFPASTE_BEGIN size=0x");
    serial::put_hex32(len);
    serial::puts(" buffer=0x");
    serial::put_hex32(0x10000u);
    serial::puts(" offset=0x");
    serial::put_hex32(f.currentOffset);
    serial::puts(" clusterBytes=0x");
    serial::put_hex32(clusterBytes);
    serial::puts(" stepLimit=0x");
    serial::put_hex32(stepLimit);
    serial::puts("\n");

    const uint8_t* src = static_cast<const uint8_t*>(buffer);
    uint32_t bytesWritten = 0;
    uint32_t clusterVisits = 0;
    uint32_t nextProgress = 64u * 1024u;
    set_traversal_status(TRAVERSAL_OK);

    // A previous bounded write may have ended exactly on a cluster boundary.
    // Keep the tail cluster until the next write is known to need another
    // cluster, then extend once and advance the cursor.
    if (f.pendingClusterAdvance) {
        uint32_t next = 0;
        if (!extend_file_chain(vol, f.currentCluster, f.currentOffset, &next)) return 0;
        f.currentCluster = next;
        f.pendingClusterAdvance = false;
    }

    while (bytesWritten < len) {
        if (is_end_of_chain(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_TRUNCATED_CHAIN);
            break;
        }
        if (is_bad_cluster(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_BAD_CLUSTER);
            break;
        }
        if (!is_valid_data_cluster(vol, f.currentCluster)) {
            set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
            break;
        }

        const uint32_t offsetInCluster = f.currentOffset % clusterBytes;
        if (offsetInCluster == 0) {
            if (clusterVisits >= stepLimit) {
                set_traversal_status(TRAVERSAL_CHAIN_STEP_LIMIT);
                break;
            }
            ++clusterVisits;
        }
        const uint32_t sectorInCluster = offsetInCluster / vol.bytesPerSector;
        const uint32_t offsetInSector = offsetInCluster % vol.bytesPerSector;
        block::Status st = read_cluster_sector(vol, f.currentCluster,
                                               sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) {
            set_traversal_status(TRAVERSAL_IO_ERROR);
            break;
        }

        uint32_t toCopy = vol.bytesPerSector - offsetInSector;
        if (toCopy > len - bytesWritten) toCopy = len - bytesWritten;
        if (toCopy == 0) {
            set_traversal_status(TRAVERSAL_NO_PROGRESS);
            break;
        }

        memcopy(&s_secBuf[offsetInSector], src + bytesWritten, toCopy);
        st = write_cluster_sector(vol, f.currentCluster, sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) {
            set_traversal_status(TRAVERSAL_IO_ERROR);
            break;
        }

        bytesWritten += toCopy;
        f.currentOffset += toCopy;
        if (f.currentOffset > f.fileSize) f.fileSize = f.currentOffset;

        if (bytesWritten >= nextProgress || bytesWritten == len) {
            serial::puts("LFPASTE_PROGRESS offset=0x");
            serial::put_hex32(f.currentOffset);
            serial::puts(" requested=0x");
            serial::put_hex32(len);
            serial::puts(" written=0x");
            serial::put_hex32(bytesWritten);
            serial::puts(" cluster=0x");
            serial::put_hex32(f.currentCluster);
            serial::puts("\n");
            while (nextProgress <= bytesWritten && nextProgress <= 0xFFFFFFFFu - 64u * 1024u) {
                nextProgress += 64u * 1024u;
            }
        }

        if (bytesWritten < len && (f.currentOffset % clusterBytes) == 0) {
            const uint32_t previous = f.currentCluster;
            uint32_t next = 0;
            if (!extend_file_chain(vol, previous, f.currentOffset, &next)) break;
            f.currentCluster = next;
            f.pendingClusterAdvance = false;
        }
    }

    if (bytesWritten == len && s_lastTraversalStatus == TRAVERSAL_OK) {
        set_traversal_status(TRAVERSAL_END_OF_CHAIN);
        f.pendingClusterAdvance = (f.currentOffset % clusterBytes) == 0;
    }
    serial::puts("LFPASTE_WRITE_END offset=0x");
    serial::put_hex32(f.currentOffset);
    serial::puts(" requested=0x");
    serial::put_hex32(len);
    serial::puts(" actual=0x");
    serial::put_hex32(bytesWritten);
    serial::puts(" status=");
    serial::puts(traversal_status_name(last_traversal_status()));
    serial::puts("\n");
    return bytesWritten;
}

uint32_t write_file(uint8_t fileHandle, const void* buffer, uint32_t len)
{
    if (fileHandle >= MAX_OPEN_FILES) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return 0;
    }
    return write_file_cursor(s_files[fileHandle], buffer, len);
}

void close_file(uint8_t fileHandle)
{
    if (fileHandle >= MAX_OPEN_FILES) return;
    s_files[fileHandle].open = false;
}

const FATFile* get_open_file(uint8_t fileHandle)
{
    if (fileHandle >= MAX_OPEN_FILES || !s_files[fileHandle].open) return nullptr;
    return &s_files[fileHandle];
}

const FATVolume* get_volume(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return nullptr;
    if (!s_volumes[volumeIndex].mounted) return nullptr;
    return &s_volumes[volumeIndex];
}

bool flush(uint8_t volumeIndex, block::Status* outBlockStatus)
{
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !s_volumes[volumeIndex].mounted) {
        if (outBlockStatus) *outBlockStatus = block::BLOCK_ERR_INVALID;
        return false;
    }
    const block::Status status = flush_volume_io(s_volumes[volumeIndex]);
    if (outBlockStatus) *outBlockStatus = status;
    return status == block::BLOCK_OK;
}

// ================================================================
// Directory traversal by path (new functions for VFS integration)
// ================================================================

// Case-insensitive character comparison for FAT32
static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static bool name_matches(const char* a, const char* b)
{
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;  // Both must be at end
}

static bool find_in_directory_at(uint8_t volumeIndex, uint32_t dirCluster, const char* name,
                                 DirEntry* out, uint32_t* outSector, uint32_t* outOffset);

static uint32_t str_len(const char* s)
{
    uint32_t len = 0;
    if (s) {
        while (s[len]) ++len;
    }
    return len;
}

static void copy_string(char* dst, const char* src, uint32_t maxLen)
{
    uint32_t i = 0;
    if (src) {
        while (src[i] && i + 1 < maxLen) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static bool make_short_name(const char* name, char out[11])
{
    if (!name || !*name) return false;

    memfill(out, ' ', 11);
    uint32_t dot = 0xFFFFFFFF;
    uint32_t len = str_len(name);
    for (uint32_t i = 0; i < len; ++i) {
        if (name[i] == '.') {
            dot = i;
            break;
        }
    }

    uint32_t baseLen = (dot == 0xFFFFFFFF) ? len : dot;
    uint32_t extStart = (dot == 0xFFFFFFFF) ? len : dot + 1;
    if (baseLen == 0 || baseLen > 8 || len - extStart > 3) return false;

    for (uint32_t i = 0; i < baseLen; ++i) {
        char c = to_upper(name[i]);
        if (c == ' ' || c == '/' || c == '\\') return false;
        out[i] = c;
    }

    for (uint32_t i = 0; extStart + i < len; ++i) {
        char c = to_upper(name[extStart + i]);
        if (c == ' ' || c == '.' || c == '/' || c == '\\') return false;
        out[8 + i] = c;
    }

    return true;
}

static bool split_parent_and_name(uint8_t volumeIndex, const char* path, uint32_t* outParentCluster, char* outName, uint32_t outNameSize)
{
    if (!path || !outParentCluster || !outName || outNameSize == 0) return false;
    FATVolume& vol = s_volumes[volumeIndex];

    const char* p = path;
    if (*p == '/') ++p;
    if (*p == '\0') return false;

    uint32_t parentCluster = vol.rootCluster;
    while (*p) {
        char component[128];
        uint32_t i = 0;
        while (*p && *p != '/' && i + 1 < sizeof(component)) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        while (*p == '/') ++p;
        if (component[0] == '\0') continue;

        if (*p == '\0') {
            copy_string(outName, component, outNameSize);
            *outParentCluster = parentCluster;
            return true;
        }

        DirEntry entry;
        uint32_t sector = 0;
        uint32_t offset = 0;
        if (!find_in_directory_at(volumeIndex, parentCluster, component, &entry, &sector, &offset)) {
            return false;
        }
        if (!entry.isDir) return false;
        parentCluster = entry.firstCluster;
    }

    return false;
}

static bool find_free_dir_entry(uint8_t volumeIndex, uint32_t dirCluster, uint32_t* outSector, uint32_t* outOffset)
{
    FATVolume& vol = s_volumes[volumeIndex];
    uint32_t entriesPerSector = vol.bytesPerSector / 32;
    // Prefer the directory end marker. A deleted short entry can still have
    // stale long-name slots immediately before it, which would make a newly
    // created file appear under the deleted file's name during lookup.
    uint32_t deletedSector = 0;
    uint32_t deletedOffset = 0;
    bool haveDeletedEntry = false;

    if (is_fat16_root_dir(vol, dirCluster)) {
        for (uint32_t sectorIndex = 0; sectorIndex < vol.rootDirSectors; ++sectorIndex) {
            uint32_t sector = vol.rootDirFirstSector + sectorIndex;
            if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
                return false;
            }

            for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                uint32_t offset = entryIndex * 32;
                const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(&s_secBuf[offset]);
                if (de->name[0] == 0x00) {
                    *outSector = sector;
                    *outOffset = offset;
                    return true;
                }
                if (static_cast<uint8_t>(de->name[0]) == 0xE5 && !haveDeletedEntry) {
                    deletedSector = sector;
                    deletedOffset = offset;
                    haveDeletedEntry = true;
                }
            }
        }
        if (haveDeletedEntry) {
            *outSector = deletedSector;
            *outOffset = deletedOffset;
            return true;
        }
        return false;
    }

    uint32_t cluster = dirCluster;
    uint32_t clusterSteps = 0;

    while (!is_end_of_chain(vol, cluster) &&
           is_valid_data_cluster(vol, cluster) &&
           clusterSteps < directory_chain_step_limit(vol)) {
        ++clusterSteps;
        for (uint32_t sectorInCluster = 0; sectorInCluster < vol.sectorsPerCluster; ++sectorInCluster) {
            uint32_t sector = cluster_to_sector(vol, cluster) + sectorInCluster;
            if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
                return false;
            }

            for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                uint32_t offset = entryIndex * 32;
                const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(&s_secBuf[offset]);
                if (de->name[0] == 0x00) {
                    *outSector = sector;
                    *outOffset = offset;
                    return true;
                }
                if (static_cast<uint8_t>(de->name[0]) == 0xE5 && !haveDeletedEntry) {
                    deletedSector = sector;
                    deletedOffset = offset;
                    haveDeletedEntry = true;
                }
            }
        }

        cluster = next_cluster(vol, cluster);
    }

    if (haveDeletedEntry) {
        *outSector = deletedSector;
        *outOffset = deletedOffset;
        return true;
    }
    return false;
}

static bool write_file_clusters(FATVolume& vol, uint32_t firstCluster, const void* buffer, uint32_t len)
{
    if (len == 0) {
        set_traversal_status(TRAVERSAL_OK);
        return true;
    }
    if (!buffer || !is_valid_data_cluster(vol, firstCluster)) {
        set_traversal_status(TRAVERSAL_INVALID_ARGUMENT);
        return false;
    }

    FATFile temp{};
    temp.open = true;
    temp.volumeIndex = static_cast<uint8_t>(&vol - s_volumes);
    temp.firstCluster = firstCluster;
    temp.fileSize = 0;
    temp.currentCluster = firstCluster;
    temp.currentOffset = 0;
    temp.attr = ATTR_ARCHIVE;
    temp.pendingClusterAdvance = false;
    return write_file_cursor(temp, buffer, len) == len;
}

static bool find_in_directory_at(uint8_t volumeIndex, uint32_t dirCluster, const char* name,
                                 DirEntry* out, uint32_t* outSector, uint32_t* outOffset)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!name || !out || !outSector || !outOffset) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    char lfnName[256];
    clear_lfn_name(lfnName, sizeof(lfnName));

    uint32_t entriesPerSector = vol.bytesPerSector / 32;

    if (is_fat16_root_dir(vol, dirCluster)) {
        for (uint32_t sectorIndex = 0; sectorIndex < vol.rootDirSectors; ++sectorIndex) {
            uint32_t sector = vol.rootDirFirstSector + sectorIndex;
            if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
                return false;
            }

            for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                uint32_t offset = entryIndex * 32;
                const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(&s_secBuf[offset]);

                if (de->name[0] == 0x00) return false;
                if (static_cast<uint8_t>(de->name[0]) == 0xE5) {
                    clear_lfn_name(lfnName, sizeof(lfnName));
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    collect_lfn_entry(reinterpret_cast<const FAT32_LFNEntry*>(de), lfnName, sizeof(lfnName));
                    continue;
                }
                if (de->attr & ATTR_VOLUME_ID) {
                    clear_lfn_name(lfnName, sizeof(lfnName));
                    continue;
                }

                char shortName[32];
                short_name_to_string(de->name, shortName);
                const char* displayName = lfnName[0] ? lfnName : shortName;
                if (!name_matches(displayName, name) && !name_matches(shortName, name)) {
                    clear_lfn_name(lfnName, sizeof(lfnName));
                    continue;
                }

                fill_dir_entry_from_fat(de, displayName, out);
                *outSector = sector;
                *outOffset = offset;
                return true;
            }
        }

        return false;
    }

    uint32_t cluster = dirCluster;
    uint32_t clusterSteps = 0;

    while (!is_end_of_chain(vol, cluster) &&
           is_valid_data_cluster(vol, cluster) &&
           clusterSteps < directory_chain_step_limit(vol)) {
        ++clusterSteps;
        for (uint32_t sectorInCluster = 0; sectorInCluster < vol.sectorsPerCluster; ++sectorInCluster) {
            uint32_t sector = cluster_to_sector(vol, cluster) + sectorInCluster;
            if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
                return false;
            }

            for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                uint32_t offset = entryIndex * 32;
                const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(&s_secBuf[offset]);

                if (de->name[0] == 0x00) return false;
                if (static_cast<uint8_t>(de->name[0]) == 0xE5) {
                    clear_lfn_name(lfnName, sizeof(lfnName));
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    collect_lfn_entry(reinterpret_cast<const FAT32_LFNEntry*>(de), lfnName, sizeof(lfnName));
                    continue;
                }
                if (de->attr & ATTR_VOLUME_ID) {
                    clear_lfn_name(lfnName, sizeof(lfnName));
                    continue;
                }

                char shortName[32];
                short_name_to_string(de->name, shortName);
                const char* displayName = lfnName[0] ? lfnName : shortName;
                if (!name_matches(displayName, name) && !name_matches(shortName, name)) {
                    clear_lfn_name(lfnName, sizeof(lfnName));
                    continue;
                }

                fill_dir_entry_from_fat(de, displayName, out);
                *outSector = sector;
                *outOffset = offset;
                return true;
            }
        }

        cluster = next_cluster(vol, cluster);
    }

    return false;
}

static bool find_path_entry_at(uint8_t volumeIndex, const char* path,
                               DirEntry* out, uint32_t* outSector, uint32_t* outOffset)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!path || !out || !outSector || !outOffset) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    const char* p = path;
    if (*p == '/') ++p;

    uint32_t currentCluster = vol.rootCluster;
    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;

    while (*p) {
        char component[128];
        int i = 0;
        while (*p && *p != '/' && i < 127) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        while (*p == '/') ++p;
        if (component[0] == '\0') continue;

        if (!find_in_directory_at(volumeIndex, currentCluster, component, &entry, &sector, &offset)) {
            return false;
        }

        if (*p == '\0') {
            memcopy(out, &entry, sizeof(DirEntry));
            *outSector = sector;
            *outOffset = offset;
            return true;
        }

        if (!entry.isDir) return false;
        currentCluster = entry.firstCluster;
    }

    return false;
}

static uint32_t cluster_chain_capacity(const FATVolume& vol, uint32_t firstCluster,
                                       uint32_t requestedBytes)
{
    if (requestedBytes == 0) return 0;
    const uint32_t clusterBytes = vol.sectorsPerCluster * vol.bytesPerSector;
    if (clusterBytes == 0) return 0;
    const uint32_t requiredClusters = static_cast<uint32_t>(
        (static_cast<uint64_t>(requestedBytes) + clusterBytes - 1) / clusterBytes);
    if (chain_cycle_detected(vol, firstCluster, requiredClusters)) {
        set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
        return 0;
    }
    uint32_t clusters = 0;
    uint32_t cluster = firstCluster;
    while (!is_end_of_chain(vol, cluster) &&
           is_valid_data_cluster(vol, cluster) &&
           clusters < requiredClusters &&
           clusters < max_chain_steps(vol)) {
        ++clusters;
        cluster = next_cluster(vol, cluster);
    }
    const uint64_t capacity = static_cast<uint64_t>(clusters) * clusterBytes;
    return capacity > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(capacity);
}

bool open_dir(uint8_t volumeIndex, uint32_t dirCluster)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    
    s_dirIter.active          = true;
    s_dirIter.volIdx          = volumeIndex;
    s_dirIter.cluster         = dirCluster;
    s_dirIter.clusterSteps    = 0;
    s_dirIter.sectorInCluster = 0;
    s_dirIter.entryInSector   = 0;
    set_traversal_status(TRAVERSAL_OK);
    return true;
}

bool find_in_dir(uint8_t volumeIndex, const char* name, DirEntry* out)
{
    if (!s_dirIter.active || s_dirIter.volIdx != volumeIndex) return false;
    if (!name || !out) return false;
    
    DirEntry entry;
    while (read_dir(volumeIndex, &entry)) {
        if (name_matches(entry.name, name)) {
            memcopy(out, &entry, sizeof(DirEntry));
            s_dirIter.active = false;  // Close iteration
            return true;
        }
    }
    
    return false;
}

bool lookup_path(uint8_t volumeIndex, const char* path, DirEntry* out)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!path || !out) return false;
    
    FATVolume& vol = s_volumes[volumeIndex];

    // Handle empty path or root
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        // Return root directory info
        memzero(out, sizeof(DirEntry));
        out->name[0] = '/';
        out->name[1] = '\0';
        out->firstCluster = vol.rootCluster;
        out->fileSize = 0;
        out->attr = ATTR_DIRECTORY;
        out->isDir = true;
        return true;
    }

    // Path lookup is deliberately independent of the public directory
    // iterator.  VFS callers may enumerate one directory while validating a
    // child package; reusing s_dirIter here would destroy that caller's
    // cursor before its next readdir().
    uint32_t sector = 0;
    uint32_t offset = 0;
    return find_path_entry_at(volumeIndex, path, out, &sector, &offset);
}

FileWriteStatus overwrite_path_status(uint8_t volumeIndex, const char* path,
                                       const void* buffer, uint32_t len,
                                       block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path || (!buffer && len != 0)) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    if (!s_volumes[volumeIndex].mounted) return FILE_WRITE_NOT_MOUNTED;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) {
        return FILE_WRITE_UNSUPPORTED_TYPE;
    }

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        return FILE_WRITE_NOT_FOUND;
    }
    if (entry.isDir) return FILE_WRITE_INVALID_ARGUMENT;
    if (entry.attr & ATTR_READ_ONLY) return FILE_WRITE_READ_ONLY;

    uint32_t capacity = cluster_chain_capacity(vol, entry.firstCluster, len);
    if (len > capacity) return FILE_WRITE_NO_SPACE;
    if (!write_file_clusters(vol, entry.firstCluster, buffer, len)) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_OK ? FILE_WRITE_NO_SPACE : FILE_WRITE_IO_ERROR;
    }

    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return FILE_WRITE_IO_ERROR;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    de->fileSize = len;

    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return FILE_WRITE_IO_ERROR;
    }
    if (flush_volume_io(vol) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }
    return FILE_WRITE_OK;
}

bool overwrite_path(uint8_t volumeIndex, const char* path, const void* buffer, uint32_t len)
{
    return overwrite_path_status(volumeIndex, path, buffer, len, nullptr) == FILE_WRITE_OK;
}

FileWriteStatus create_file_path_status(uint8_t volumeIndex, const char* path,
                                        const void* buffer, uint32_t len,
                                        block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path || (!buffer && len != 0)) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    if (!s_volumes[volumeIndex].mounted) return FILE_WRITE_NOT_MOUNTED;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) {
        return FILE_WRITE_UNSUPPORTED_TYPE;
    }

    DirEntry existing;
    if (lookup_path(volumeIndex, path, &existing)) return FILE_WRITE_ALREADY_EXISTS;

    uint32_t parentCluster = 0;
    char fileName[128];
    if (!split_parent_and_name(volumeIndex, path, &parentCluster, fileName, sizeof(fileName))) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_OK ? FILE_WRITE_NOT_FOUND : FILE_WRITE_IO_ERROR;
    }

    char shortName[11];
    serial::puts("FAT_FILE_DIR_ENTRY_BEGIN path=");
    serial::puts(path);
    serial::puts(" parentCluster=0x");
    serial::put_hex32(parentCluster);
    serial::puts(" name=");
    serial::puts(fileName);
    serial::puts("\n");
    if (!make_short_name(fileName, shortName)) {
        serial::puts("FAT_FILE_DIR_ENTRY_RESULT=FAT_FILE_WRITE_INVALID_NAME\n");
        return FILE_WRITE_INVALID_NAME;
    }

    serial::puts("FPASTE_FAT_ALLOCATE_BEGIN bytes=0x");
    serial::put_hex32(len);
    serial::puts("\n");
    uint32_t firstCluster = 0;
    if (len != 0) firstCluster = allocate_cluster(vol);
    if (len != 0 && firstCluster == 0) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_OK ? FILE_WRITE_NO_FREE_CLUSTER : FILE_WRITE_IO_ERROR;
    }

    serial::puts("[FAT_FILE_CREATE_ALLOCATE] path=");
    serial::puts(path);
    serial::puts(" firstCluster=0x");
    serial::put_hex32(firstCluster);
    serial::puts(" bytes=0x");
    serial::put_hex32(len);
    serial::puts("\n");
    if (firstCluster != 0) {
        serial::puts("FPASTE_FAT_ALLOCATE_OK cluster=0x");
        serial::put_hex32(firstCluster);
        serial::puts("\n");
    }

    serial::puts("FPASTE_FAT_CHAIN_WRITE_BEGIN bytes=0x");
    serial::put_hex32(len);
    serial::puts("\n");
    if (len != 0 && !write_file_clusters(vol, firstCluster, buffer, len)) {
        const block::Status failedStatus = s_lastIoStatus;
        release_cluster_chain(vol, firstCluster);
        s_lastIoStatus = failedStatus;
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return write_failure_status();
    }
    serial::puts("FPASTE_FAT_CHAIN_WRITE_END status=");
    serial::puts(traversal_status_name(last_traversal_status()));
    serial::puts("\n");

    serial::puts("[FAT_FILE_CREATE_WRITE] path=");
    serial::puts(path);
    serial::puts(" firstCluster=0x");
    serial::put_hex32(firstCluster);
    serial::puts(" bytes=0x");
    serial::put_hex32(len);
    serial::puts(" status=BLOCK_OK\n");

    uint32_t sector = 0;
    uint32_t offset = 0;
    serial::puts("FPASTE_DIRECTORY_METADATA_BEGIN\n");
    if (!find_free_dir_entry(volumeIndex, parentCluster, &sector, &offset)) {
        const block::Status failedStatus = s_lastIoStatus;
        if (firstCluster != 0) release_cluster_chain(vol, firstCluster);
        s_lastIoStatus = failedStatus;
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return failedStatus == block::BLOCK_OK ? FILE_WRITE_NO_FREE_ENTRY : FILE_WRITE_IO_ERROR;
    }
    serial::puts("FPASTE_DIRECTORY_METADATA_SLOT_OK sector=0x");
    serial::put_hex32(sector);
    serial::puts(" offset=0x");
    serial::put_hex32(offset);
    serial::puts("\n");

    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        const block::Status failedStatus = s_lastIoStatus;
        if (firstCluster != 0) release_cluster_chain(vol, firstCluster);
        s_lastIoStatus = failedStatus;
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return FILE_WRITE_IO_ERROR;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    memzero(de, sizeof(FAT32_DirEntry));
    memcopy(de->name, shortName, 11);
    de->attr = ATTR_ARCHIVE;
    de->firstClusterHi = static_cast<uint16_t>((firstCluster >> 16) & 0xFFFF);
    de->firstClusterLo = static_cast<uint16_t>(firstCluster & 0xFFFF);
    de->fileSize = len;

    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        const block::Status failedStatus = s_lastIoStatus;
        if (firstCluster != 0) release_cluster_chain(vol, firstCluster);
        s_lastIoStatus = failedStatus;
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return FILE_WRITE_IO_ERROR;
    }
    serial::puts("FAT_FILE_DIR_ENTRY_RESULT=FAT_FILE_WRITE_OK sector=0x");
    serial::put_hex32(sector);
    serial::puts(" offset=0x");
    serial::put_hex32(offset);
    serial::puts("\n");
    serial::puts("[FAT_FILE_CREATE_PUBLISH] path=");
    serial::puts(path);
    serial::puts(" size=0x");
    serial::put_hex32(len);
    serial::puts(" firstCluster=0x");
    serial::put_hex32(firstCluster);
    serial::puts(" status=FILE_WRITE_OK\n");
    serial::puts("FPASTE_DIRECTORY_METADATA_END status=FAT_FILE_WRITE_OK\n");
    serial::puts("LFPASTE_SIZE_UPDATE size=0x");
    serial::put_hex32(len);
    serial::puts("\n");
    serial::puts("LFPASTE_FLUSH_BEGIN stage=create\n");
    if (flush_volume_io(vol) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return FILE_WRITE_IO_TIMEOUT;
    }
    return FILE_WRITE_OK;
}

bool create_file_path(uint8_t volumeIndex, const char* path, const void* buffer, uint32_t len)
{
    return create_file_path_status(volumeIndex, path, buffer, len, nullptr) == FILE_WRITE_OK;
}

FileWriteStatus update_file_size_path_status(uint8_t volumeIndex,
                                             const char* path,
                                             uint32_t fileSize,
                                             block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path) return FILE_WRITE_INVALID_ARGUMENT;
    if (!s_volumes[volumeIndex].mounted) return FILE_WRITE_NOT_MOUNTED;

    FATVolume& vol = s_volumes[volumeIndex];
    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        return FILE_WRITE_NOT_FOUND;
    }
    if (entry.isDir) return FILE_WRITE_INVALID_ARGUMENT;
    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }
    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    de->fileSize = fileSize;
    serial::puts("LFPASTE_SIZE_UPDATE size=0x");
    serial::put_hex32(fileSize);
    serial::puts(" sector=0x");
    serial::put_hex32(sector);
    serial::puts(" offset=0x");
    serial::put_hex32(offset);
    serial::puts("\n");
    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }
    return FILE_WRITE_OK;
}

FileWriteStatus update_file_metadata_path_status(uint8_t volumeIndex,
                                                 const char* path,
                                                 uint32_t firstCluster,
                                                 uint32_t fileSize,
                                                 block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    if (!s_volumes[volumeIndex].mounted) return FILE_WRITE_NOT_MOUNTED;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return FILE_WRITE_UNSUPPORTED_TYPE;
    if ((firstCluster != 0 && !is_valid_data_cluster(vol, firstCluster)) ||
        (fileSize != 0 && firstCluster == 0)) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        if (s_lastIoStatus != block::BLOCK_OK) {
            return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
                ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
        }
        return FILE_WRITE_NOT_FOUND;
    }
    if (entry.isDir) return FILE_WRITE_INVALID_ARGUMENT;
    if (entry.attr & ATTR_READ_ONLY) return FILE_WRITE_READ_ONLY;
    if (offset + sizeof(FAT32_DirEntry) > vol.bytesPerSector) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }

    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }
    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    de->firstClusterHi = static_cast<uint16_t>((firstCluster >> 16) & 0xFFFFu);
    de->firstClusterLo = static_cast<uint16_t>(firstCluster & 0xFFFFu);
    de->fileSize = fileSize;
    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }
    return FILE_WRITE_OK;
}

FileWriteStatus ensure_file_cluster_for_write_path_status(
    uint8_t volumeIndex, const char* path, uint8_t fileHandle,
    block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path || fileHandle >= MAX_OPEN_FILES) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    if (!s_volumes[volumeIndex].mounted) return FILE_WRITE_NOT_MOUNTED;
    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return FILE_WRITE_UNSUPPORTED_TYPE;

    FATFile& file = s_files[fileHandle];
    if (!file.open || file.volumeIndex != volumeIndex || file.fileSize != 0 ||
        file.currentOffset != 0) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    if (file.attr & ATTR_READ_ONLY) return FILE_WRITE_READ_ONLY;
    if (file.firstCluster != 0) return FILE_WRITE_OK;

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_OK
            ? FILE_WRITE_NOT_FOUND
            : (s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
                ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR);
    }
    if (entry.isDir) return FILE_WRITE_INVALID_ARGUMENT;
    if (entry.attr & ATTR_READ_ONLY) return FILE_WRITE_READ_ONLY;
    if (entry.fileSize != 0 || entry.firstCluster != 0) {
        return FILE_WRITE_INVALID_ARGUMENT;
    }

    const uint32_t firstCluster = allocate_cluster(vol);
    if (firstCluster == 0) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        if (s_lastIoStatus == block::BLOCK_ERR_TIMEOUT) return FILE_WRITE_IO_TIMEOUT;
        if (s_lastIoStatus != block::BLOCK_OK) return FILE_WRITE_IO_ERROR;
        return FILE_WRITE_NO_FREE_CLUSTER;
    }

    if (offset + sizeof(FAT32_DirEntry) > vol.bytesPerSector) {
        release_allocated_cluster(vol, firstCluster);
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        const block::Status failedStatus = s_lastIoStatus;
        release_allocated_cluster(vol, firstCluster);
        s_lastIoStatus = failedStatus;
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return failedStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    if (de->fileSize != 0 ||
        (((static_cast<uint32_t>(de->firstClusterHi) << 16) |
          de->firstClusterLo) & FAT32_CLUSTER_MASK) != 0) {
        release_allocated_cluster(vol, firstCluster);
        return FILE_WRITE_INVALID_ARGUMENT;
    }
    de->firstClusterHi = static_cast<uint16_t>((firstCluster >> 16) & 0xFFFFu);
    de->firstClusterLo = static_cast<uint16_t>(firstCluster & 0xFFFFu);
    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        // The failed write may still have reached the medium. Keep the
        // cluster allocated so a partially published directory entry cannot
        // point at a cluster that has already been freed.
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        close_file(fileHandle);
        return s_lastIoStatus == block::BLOCK_ERR_TIMEOUT
            ? FILE_WRITE_IO_TIMEOUT : FILE_WRITE_IO_ERROR;
    }

    file.firstCluster = firstCluster;
    file.currentCluster = firstCluster;
    file.pendingClusterAdvance = false;
    return FILE_WRITE_OK;
}

static FileResizeStatus resize_file_write_status(FileWriteStatus status)
{
    switch (status) {
        case FILE_WRITE_OK: return FILE_RESIZE_OK;
        case FILE_WRITE_NOT_MOUNTED: return FILE_RESIZE_NOT_MOUNTED;
        case FILE_WRITE_UNSUPPORTED_TYPE: return FILE_RESIZE_UNSUPPORTED_TYPE;
        case FILE_WRITE_NOT_FOUND: return FILE_RESIZE_NOT_FOUND;
        case FILE_WRITE_READ_ONLY: return FILE_RESIZE_READ_ONLY;
        case FILE_WRITE_IO_TIMEOUT: return FILE_RESIZE_IO_TIMEOUT;
        case FILE_WRITE_IO_ERROR: return FILE_RESIZE_IO_ERROR;
        case FILE_WRITE_CORRUPT_CHAIN: return FILE_RESIZE_CORRUPT_CHAIN;
        default: return FILE_RESIZE_INVALID_ARGUMENT;
    }
}

FileResizeStatus truncate_file_path_status(uint8_t volumeIndex,
                                           const char* path,
                                           uint8_t fileHandle,
                                           uint32_t newSize,
                                           block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    set_traversal_status(TRAVERSAL_OK);
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path || fileHandle >= MAX_OPEN_FILES) {
        return FILE_RESIZE_INVALID_ARGUMENT;
    }
    if (!s_volumes[volumeIndex].mounted) return FILE_RESIZE_NOT_MOUNTED;
    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return FILE_RESIZE_UNSUPPORTED_TYPE;

    FATFile& file = s_files[fileHandle];
    if (!file.open || file.volumeIndex != volumeIndex) return FILE_RESIZE_INVALID_ARGUMENT;
    if (file.attr & ATTR_READ_ONLY) return FILE_RESIZE_READ_ONLY;

    DirEntry entry;
    uint32_t entrySector = 0;
    uint32_t entryOffset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &entrySector, &entryOffset)) {
        if (s_lastIoStatus != block::BLOCK_OK) {
            if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
            return resize_io_status();
        }
        return FILE_RESIZE_NOT_FOUND;
    }
    if (entry.isDir) return FILE_RESIZE_IS_DIRECTORY;
    if (entry.attr & ATTR_READ_ONLY) return FILE_RESIZE_READ_ONLY;
    if (entry.fileSize != file.fileSize || entry.firstCluster != file.firstCluster) {
        return FILE_RESIZE_BUSY;
    }
    if (newSize > file.fileSize) return FILE_RESIZE_GROW_NOT_SUPPORTED;
    if (newSize == file.fileSize &&
        (newSize != 0 || file.firstCluster == 0)) {
        return FILE_RESIZE_OK;
    }

    uint32_t clusterBytes = 0;
    if (!cluster_byte_count(vol, &clusterBytes)) {
        return FILE_RESIZE_UNSUPPORTED_SIZE;
    }
    const uint32_t maxSteps = max_chain_steps(vol);
    uint32_t oldClusterCount = 0;
    if (file.fileSize != 0) {
        const uint64_t count64 =
            (static_cast<uint64_t>(file.fileSize) + clusterBytes - 1u) / clusterBytes;
        if (count64 == 0 || count64 > maxSteps || count64 > kMaxSafeChainSteps) {
            return FILE_RESIZE_UNSUPPORTED_SIZE;
        }
        oldClusterCount = static_cast<uint32_t>(count64);
        if (file.firstCluster == 0) return FILE_RESIZE_CORRUPT_CHAIN;
        uint32_t cluster = file.firstCluster;
        for (uint32_t index = 0; index < oldClusterCount; ++index) {
            if (!is_valid_data_cluster(vol, cluster)) {
                set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
                return FILE_RESIZE_CORRUPT_CHAIN;
            }
            s_resizeChain[index] = cluster;
            uint32_t next = 0;
            bool endOfChain = false;
            const FileResizeStatus linkStatus = read_resize_link(
                vol, cluster, &next, &endOfChain);
            if (linkStatus != FILE_RESIZE_OK) {
                if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
                return linkStatus;
            }
            const bool expectedEnd = index + 1u == oldClusterCount;
            if (endOfChain != expectedEnd) {
                set_traversal_status(TRAVERSAL_TRUNCATED_CHAIN);
                return FILE_RESIZE_CORRUPT_CHAIN;
            }
            if (!endOfChain) cluster = next;
        }
    } else if (file.firstCluster != 0) {
        uint32_t cluster = file.firstCluster;
        bool ended = false;
        for (; oldClusterCount < maxSteps; ++oldClusterCount) {
            if (!is_valid_data_cluster(vol, cluster)) {
                set_traversal_status(TRAVERSAL_INVALID_CLUSTER);
                return FILE_RESIZE_CORRUPT_CHAIN;
            }
            s_resizeChain[oldClusterCount] = cluster;
            uint32_t next = 0;
            bool endOfChain = false;
            const FileResizeStatus linkStatus = read_resize_link(
                vol, cluster, &next, &endOfChain);
            if (linkStatus != FILE_RESIZE_OK) {
                if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
                return linkStatus;
            }
            if (endOfChain) {
                ++oldClusterCount;
                ended = true;
                break;
            }
            cluster = next;
        }
        if (!ended) {
            set_traversal_status(TRAVERSAL_CHAIN_STEP_LIMIT);
            return FILE_RESIZE_CORRUPT_CHAIN;
        }
    }

    if (oldClusterCount > 0) {
        sort_resize_chain(oldClusterCount);
        for (uint32_t i = 1; i < oldClusterCount; ++i) {
            if (s_resizeChain[i - 1] == s_resizeChain[i]) {
                set_traversal_status(TRAVERSAL_CHAIN_CYCLE);
                return FILE_RESIZE_CORRUPT_CHAIN;
            }
        }
    }

    const uint32_t keepClusterCount = newSize == 0 ? 0u : static_cast<uint32_t>(
        (static_cast<uint64_t>(newSize) + clusterBytes - 1u) / clusterBytes);
    if (keepClusterCount > oldClusterCount) return FILE_RESIZE_CORRUPT_CHAIN;

    // The directory entry is the target-size publication point. Preserve the
    // original chain order before sorting it for cross-link checks.
    uint32_t keptTailCluster = 0;
    uint32_t detachedFirstCluster = 0;
    if (keepClusterCount != 0) {
        // Rewalk the already validated chain to recover its ordered tail and
        // the start of the detached suffix without keeping a second 256 KiB
        // buffer in kernel memory.
        uint32_t cluster = file.firstCluster;
        for (uint32_t index = 0; index < keepClusterCount; ++index) {
            keptTailCluster = cluster;
            uint32_t next = 0;
            bool endOfChain = false;
            const FileResizeStatus linkStatus = read_resize_link(
                vol, cluster, &next, &endOfChain);
            if (linkStatus != FILE_RESIZE_OK) {
                if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
                return linkStatus;
            }
            if (index + 1u == keepClusterCount) {
                if (!endOfChain) detachedFirstCluster = next;
            } else {
                if (endOfChain) return FILE_RESIZE_CORRUPT_CHAIN;
                cluster = next;
            }
        }
    } else {
        detachedFirstCluster = file.firstCluster;
    }
    const uint32_t detachedClusterCount = oldClusterCount - keepClusterCount;

    if (detachedClusterCount != 0) {
        const FileResizeStatus crossLinkStatus = reject_volume_cross_links(
            vol, entrySector, entryOffset, oldClusterCount);
        if (crossLinkStatus != FILE_RESIZE_OK) {
            if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
            return crossLinkStatus;
        }
    }

    uint32_t newCursorCluster = 0;
    bool newPendingClusterAdvance = false;
    if (newSize == 0) {
        newCursorCluster = 0;
    } else if (file.currentOffset < newSize) {
        const uint32_t cursorIndex = file.currentOffset / clusterBytes;
        uint32_t cluster = file.firstCluster;
        for (uint32_t index = 0; index < cursorIndex; ++index) {
            uint32_t next = 0;
            bool endOfChain = false;
            const FileResizeStatus linkStatus = read_resize_link(
                vol, cluster, &next, &endOfChain);
            if (linkStatus != FILE_RESIZE_OK || endOfChain) {
                if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
                return linkStatus == FILE_RESIZE_OK
                    ? FILE_RESIZE_CORRUPT_CHAIN : linkStatus;
            }
            cluster = next;
        }
        newCursorCluster = cluster;
    } else if (file.currentOffset == newSize) {
        newCursorCluster = keptTailCluster;
        newPendingClusterAdvance = (newSize % clusterBytes) == 0;
    } else {
        newCursorCluster = file.firstCluster;
    }

    const uint32_t newFirstCluster = keepClusterCount == 0
        ? 0u : file.firstCluster;
    block::Status metadataBlockStatus = block::BLOCK_OK;
    const FileWriteStatus metadataStatus = update_file_metadata_path_status(
        volumeIndex, path, newFirstCluster, newSize, &metadataBlockStatus);
    if (metadataStatus != FILE_WRITE_OK) {
        if (outBlockStatus) *outBlockStatus = metadataBlockStatus;
        if (metadataStatus == FILE_WRITE_IO_ERROR ||
            metadataStatus == FILE_WRITE_IO_TIMEOUT) {
            close_file(fileHandle);
        }
        return resize_file_write_status(metadataStatus);
    }

    file.firstCluster = newFirstCluster;
    file.fileSize = newSize;
    file.currentCluster = newCursorCluster;
    file.pendingClusterAdvance = newPendingClusterAdvance;

    if (detachedClusterCount != 0) {
        if (keepClusterCount != 0 &&
            write_fat_entry(vol, keptTailCluster, fat_end_value(vol)) != block::BLOCK_OK) {
            if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
            close_file(fileHandle);
            return resize_io_status();
        }
        const FileResizeStatus releaseStatus = release_resize_chain(
            vol, detachedFirstCluster, detachedClusterCount);
        if (releaseStatus != FILE_RESIZE_OK) {
            if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
            close_file(fileHandle);
            return releaseStatus;
        }
    }
    return FILE_RESIZE_OK;
}

bool create_directory_path(uint8_t volumeIndex, const char* path)
{
    return create_directory_path_status(volumeIndex, path, nullptr) == DIRECTORY_CREATE_OK;
}

const char* directory_create_status_name(DirectoryCreateStatus status)
{
    switch (status) {
        case DIRECTORY_CREATE_OK: return "FAT_CREATE_OK";
        case DIRECTORY_CREATE_INVALID_ARGUMENT: return "FAT_CREATE_INVALID_ARGUMENT";
        case DIRECTORY_CREATE_NOT_MOUNTED: return "FAT_CREATE_NOT_MOUNTED";
        case DIRECTORY_CREATE_UNSUPPORTED_TYPE: return "FAT_CREATE_UNSUPPORTED_TYPE";
        case DIRECTORY_CREATE_ALREADY_EXISTS: return "FAT_CREATE_ALREADY_EXISTS";
        case DIRECTORY_CREATE_PARENT_NOT_FOUND: return "FAT_CREATE_PARENT_NOT_FOUND";
        case DIRECTORY_CREATE_INVALID_NAME: return "FAT_CREATE_INVALID_NAME";
        case DIRECTORY_CREATE_NO_FREE_CLUSTER: return "FAT_CREATE_NO_FREE_CLUSTER";
        case DIRECTORY_CREATE_NO_FREE_ENTRY: return "FAT_CREATE_NO_FREE_ENTRY";
        case DIRECTORY_CREATE_IO_ERROR: return "FAT_CREATE_IO_ERROR";
        default: return "FAT_CREATE_UNKNOWN";
    }
}

const char* file_write_status_name(FileWriteStatus status)
{
    switch (status) {
        case FILE_WRITE_OK: return "FAT_FILE_WRITE_OK";
        case FILE_WRITE_INVALID_ARGUMENT: return "FAT_FILE_WRITE_INVALID_ARGUMENT";
        case FILE_WRITE_NOT_MOUNTED: return "FAT_FILE_WRITE_NOT_MOUNTED";
        case FILE_WRITE_UNSUPPORTED_TYPE: return "FAT_FILE_WRITE_UNSUPPORTED_TYPE";
        case FILE_WRITE_NOT_FOUND: return "FAT_FILE_WRITE_NOT_FOUND";
        case FILE_WRITE_ALREADY_EXISTS: return "FAT_FILE_WRITE_ALREADY_EXISTS";
        case FILE_WRITE_INVALID_NAME: return "FAT_FILE_WRITE_INVALID_NAME";
        case FILE_WRITE_NO_FREE_CLUSTER: return "FAT_FILE_WRITE_NO_FREE_CLUSTER";
        case FILE_WRITE_NO_FREE_ENTRY: return "FAT_FILE_WRITE_NO_FREE_ENTRY";
        case FILE_WRITE_IO_ERROR: return "FAT_FILE_WRITE_IO_ERROR";
        case FILE_WRITE_READ_ONLY: return "FAT_FILE_WRITE_READ_ONLY";
        case FILE_WRITE_NO_SPACE: return "FAT_FILE_WRITE_NO_SPACE";
        case FILE_WRITE_IO_TIMEOUT: return "FAT_FILE_WRITE_IO_TIMEOUT";
        case FILE_WRITE_CORRUPT_CHAIN: return "FAT_FILE_WRITE_CORRUPT_CHAIN";
        case FILE_WRITE_NO_PROGRESS: return "FAT_FILE_WRITE_NO_PROGRESS";
        case FILE_WRITE_ALLOCATION_FAILED: return "FAT_FILE_WRITE_ALLOCATION_FAILED";
        default: return "FAT_FILE_WRITE_UNKNOWN";
    }
}

const char* file_resize_status_name(FileResizeStatus status)
{
    switch (status) {
        case FILE_RESIZE_OK: return "FAT_FILE_RESIZE_OK";
        case FILE_RESIZE_INVALID_ARGUMENT: return "FAT_FILE_RESIZE_INVALID_ARGUMENT";
        case FILE_RESIZE_NOT_MOUNTED: return "FAT_FILE_RESIZE_NOT_MOUNTED";
        case FILE_RESIZE_UNSUPPORTED_TYPE: return "FAT_FILE_RESIZE_UNSUPPORTED_TYPE";
        case FILE_RESIZE_NOT_FOUND: return "FAT_FILE_RESIZE_NOT_FOUND";
        case FILE_RESIZE_IS_DIRECTORY: return "FAT_FILE_RESIZE_IS_DIRECTORY";
        case FILE_RESIZE_READ_ONLY: return "FAT_FILE_RESIZE_READ_ONLY";
        case FILE_RESIZE_UNSUPPORTED_SIZE: return "FAT_FILE_RESIZE_UNSUPPORTED_SIZE";
        case FILE_RESIZE_GROW_NOT_SUPPORTED: return "FAT_FILE_RESIZE_GROW_NOT_SUPPORTED";
        case FILE_RESIZE_BUSY: return "FAT_FILE_RESIZE_BUSY";
        case FILE_RESIZE_CORRUPT_CHAIN: return "FAT_FILE_RESIZE_CORRUPT_CHAIN";
        case FILE_RESIZE_CROSS_LINK: return "FAT_FILE_RESIZE_CROSS_LINK";
        case FILE_RESIZE_IO_ERROR: return "FAT_FILE_RESIZE_IO_ERROR";
        case FILE_RESIZE_IO_TIMEOUT: return "FAT_FILE_RESIZE_IO_TIMEOUT";
        default: return "FAT_FILE_RESIZE_UNKNOWN";
    }
}

const char* traversal_status_name(TraversalStatus status)
{
    switch (status) {
        case TRAVERSAL_OK: return "FAT_TRAVERSAL_OK";
        case TRAVERSAL_END_OF_CHAIN: return "FAT_TRAVERSAL_END_OF_CHAIN";
        case TRAVERSAL_DIRECTORY_END: return "FAT_TRAVERSAL_DIRECTORY_END";
        case TRAVERSAL_INVALID_ARGUMENT: return "FAT_TRAVERSAL_INVALID_ARGUMENT";
        case TRAVERSAL_INVALID_CLUSTER: return "FAT_TRAVERSAL_INVALID_CLUSTER";
        case TRAVERSAL_BAD_CLUSTER: return "FAT_TRAVERSAL_BAD_CLUSTER";
        case TRAVERSAL_CHAIN_CYCLE: return "FAT_TRAVERSAL_CHAIN_CYCLE";
        case TRAVERSAL_CHAIN_STEP_LIMIT: return "FAT_TRAVERSAL_CHAIN_STEP_LIMIT";
        case TRAVERSAL_TRUNCATED_CHAIN: return "FAT_TRAVERSAL_TRUNCATED_CHAIN";
        case TRAVERSAL_NO_PROGRESS: return "FAT_TRAVERSAL_NO_PROGRESS";
        case TRAVERSAL_IO_ERROR: return "FAT_TRAVERSAL_IO_ERROR";
        default: return "FAT_TRAVERSAL_UNKNOWN";
    }
}

TraversalStatus last_traversal_status()
{
    return s_lastTraversalStatus;
}

block::Status last_io_status()
{
    return s_lastIoStatus;
}

const char* delete_status_name(DeleteStatus status)
{
    switch (status) {
        case DELETE_OK: return "FAT_DELETE_OK";
        case DELETE_INVALID_ARGUMENT: return "FAT_DELETE_INVALID_ARGUMENT";
        case DELETE_NOT_MOUNTED: return "FAT_DELETE_NOT_MOUNTED";
        case DELETE_NOT_FOUND: return "FAT_DELETE_NOT_FOUND";
        case DELETE_WRONG_TYPE: return "FAT_DELETE_WRONG_TYPE";
        case DELETE_READ_ONLY: return "FAT_DELETE_READ_ONLY";
        case DELETE_DIRECTORY_NOT_EMPTY: return "FAT_DELETE_DIRECTORY_NOT_EMPTY";
        case DELETE_CORRUPT_DIRECTORY: return "FAT_DELETE_CORRUPT_DIRECTORY";
        case DELETE_CORRUPT_CHAIN: return "FAT_DELETE_CORRUPT_CHAIN";
        case DELETE_IO_ERROR: return "FAT_DELETE_IO_ERROR";
        default: return "FAT_DELETE_UNKNOWN";
    }
}

DeleteStatus last_delete_status()
{
    return s_lastDeleteStatus;
}

static void initialize_directory_entry(FAT32_DirEntry* entry,
                                        const char* shortName,
                                        uint32_t firstCluster)
{
    memzero(entry, sizeof(FAT32_DirEntry));
    memcopy(entry->name, shortName, 11);
    entry->attr = ATTR_DIRECTORY;
    entry->firstClusterHi = static_cast<uint16_t>((firstCluster >> 16) & 0xFFFF);
    entry->firstClusterLo = static_cast<uint16_t>(firstCluster & 0xFFFF);
    entry->fileSize = 0;
}

DirectoryCreateStatus create_directory_path_status(uint8_t volumeIndex,
                                                    const char* path,
                                                    block::Status* outBlockStatus)
{
    s_lastIoStatus = block::BLOCK_OK;
    if (outBlockStatus) *outBlockStatus = block::BLOCK_OK;
    if (volumeIndex >= MAX_FAT_VOLUMES || !path) {
        return DIRECTORY_CREATE_INVALID_ARGUMENT;
    }
    if (!s_volumes[volumeIndex].mounted) {
        return DIRECTORY_CREATE_NOT_MOUNTED;
    }

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) {
        return DIRECTORY_CREATE_UNSUPPORTED_TYPE;
    }

    DirEntry existing;
    if (lookup_path(volumeIndex, path, &existing)) {
        return DIRECTORY_CREATE_ALREADY_EXISTS;
    }

    uint32_t parentCluster = 0;
    char dirName[128];
    if (!split_parent_and_name(volumeIndex, path, &parentCluster, dirName, sizeof(dirName))) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_OK
            ? DIRECTORY_CREATE_PARENT_NOT_FOUND : DIRECTORY_CREATE_IO_ERROR;
    }

    char shortName[11];
    if (!make_short_name(dirName, shortName)) {
        return DIRECTORY_CREATE_INVALID_NAME;
    }

    uint32_t firstCluster = allocate_cluster(vol);
    if (firstCluster == 0) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return s_lastIoStatus == block::BLOCK_OK
            ? DIRECTORY_CREATE_NO_FREE_CLUSTER : DIRECTORY_CREATE_IO_ERROR;
    }

    serial::puts("LFPASTE_DIR_CLUSTER_INIT_BEGIN cluster=0x");
    serial::put_hex32(firstCluster);
    serial::puts(" sectors=0x");
    serial::put_hex32(vol.sectorsPerCluster);
    serial::puts(" bytes=0x");
    serial::put_hex32(vol.bytesPerSector);
    serial::puts("\n");
    // Allocate and clear the directory cluster before publishing its parent
    // entry. FAT directory entries for . and .. make the result interoperable
    // with external FAT readers and preserve the current driver's cluster
    // traversal semantics.
    for (uint32_t sectorInCluster = 0; sectorInCluster < vol.sectorsPerCluster; ++sectorInCluster) {
        memzero(s_secBuf, vol.bytesPerSector);
        if (sectorInCluster == 0) {
            static const char dotName[11] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
            static const char dotDotName[11] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
            FAT32_DirEntry* dot = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[0]);
            FAT32_DirEntry* dotDot = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[32]);
            initialize_directory_entry(dot, dotName, firstCluster);
            initialize_directory_entry(dotDot, dotDotName, parentCluster);
        }
        if (write_cluster_sector(vol, firstCluster, sectorInCluster, s_secBuf) != block::BLOCK_OK) {
            const block::Status failedStatus = s_lastIoStatus;
            release_allocated_cluster(vol, firstCluster);
            if (outBlockStatus) *outBlockStatus = failedStatus;
            return DIRECTORY_CREATE_IO_ERROR;
        }
    }
    serial::puts("LFPASTE_DIR_CLUSTER_INIT_END cluster=0x");
    serial::put_hex32(firstCluster);
    serial::puts("\n");

    uint32_t sector = 0;
    uint32_t offset = 0;
    serial::puts("LFPASTE_DIR_ENTRY_FIND_BEGIN parent=0x");
    serial::put_hex32(parentCluster);
    serial::puts("\n");
    if (!find_free_dir_entry(volumeIndex, parentCluster, &sector, &offset)) {
        const block::Status failedStatus = s_lastIoStatus;
        release_allocated_cluster(vol, firstCluster);
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return failedStatus == block::BLOCK_OK
            ? DIRECTORY_CREATE_NO_FREE_ENTRY : DIRECTORY_CREATE_IO_ERROR;
    }
    serial::puts("LFPASTE_DIR_ENTRY_FIND_END sector=0x");
    serial::put_hex32(sector);
    serial::puts(" offset=0x");
    serial::put_hex32(offset);
    serial::puts("\n");

    serial::puts("LFPASTE_DIR_METADATA_READ_BEGIN sector=0x");
    serial::put_hex32(sector);
    serial::puts("\n");
    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        const block::Status failedStatus = s_lastIoStatus;
        release_allocated_cluster(vol, firstCluster);
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return DIRECTORY_CREATE_IO_ERROR;
    }
    serial::puts("LFPASTE_DIR_METADATA_READ_END sector=0x");
    serial::put_hex32(sector);
    serial::puts("\n");

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    initialize_directory_entry(de, shortName, firstCluster);
    serial::puts("LFPASTE_DIR_METADATA_WRITE_BEGIN sector=0x");
    serial::put_hex32(sector);
    serial::puts(" offset=0x");
    serial::put_hex32(offset);
    serial::puts("\n");
    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        const block::Status failedStatus = s_lastIoStatus;
        release_allocated_cluster(vol, firstCluster);
        if (outBlockStatus) *outBlockStatus = failedStatus;
        return DIRECTORY_CREATE_IO_ERROR;
    }
    serial::puts("LFPASTE_DIR_METADATA_WRITE_END sector=0x");
    serial::put_hex32(sector);
    serial::puts("\n");

    serial::puts("LFPASTE_FLUSH_BEGIN stage=directory-create\n");
    if (flush_volume_io(vol) != block::BLOCK_OK) {
        if (outBlockStatus) *outBlockStatus = s_lastIoStatus;
        return DIRECTORY_CREATE_IO_ERROR;
    }
    return DIRECTORY_CREATE_OK;
}

static bool directory_is_empty_for_delete(uint8_t volumeIndex, uint32_t firstCluster)
{
    if (!open_dir(volumeIndex, firstCluster)) {
        s_lastDeleteStatus = DELETE_CORRUPT_DIRECTORY;
        return false;
    }

    uint32_t entryCount = 0;
    DirEntry child{};
    while (read_dir(volumeIndex, &child)) {
        if (++entryCount > kMaxSafeChainSteps) {
            s_lastDeleteStatus = DELETE_CORRUPT_DIRECTORY;
            s_dirIter.active = false;
            return false;
        }
        if ((child.name[0] == '.' && child.name[1] == '\0') ||
            (child.name[0] == '.' && child.name[1] == '.' && child.name[2] == '\0')) {
            continue;
        }
        // Most importantly, return before touching the parent directory
        // entry or releasing the directory's cluster chain.
        s_lastDeleteStatus = DELETE_DIRECTORY_NOT_EMPTY;
        s_dirIter.active = false;
        return false;
    }

    const TraversalStatus traversal = last_traversal_status();
    if (traversal != TRAVERSAL_DIRECTORY_END &&
        traversal != TRAVERSAL_END_OF_CHAIN && traversal != TRAVERSAL_OK) {
        s_lastDeleteStatus = (traversal == TRAVERSAL_CHAIN_CYCLE ||
                              traversal == TRAVERSAL_INVALID_CLUSTER ||
                              traversal == TRAVERSAL_BAD_CLUSTER ||
                              traversal == TRAVERSAL_CHAIN_STEP_LIMIT ||
                              traversal == TRAVERSAL_TRUNCATED_CHAIN)
            ? DELETE_CORRUPT_CHAIN : DELETE_CORRUPT_DIRECTORY;
        s_dirIter.active = false;
        return false;
    }
    s_dirIter.active = false;
    return true;
}

bool delete_path(uint8_t volumeIndex, const char* path, bool directory)
{
    s_lastDeleteStatus = DELETE_INVALID_ARGUMENT;
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) {
        s_lastDeleteStatus = DELETE_NOT_MOUNTED;
        return false;
    }
    if (!path) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) {
        s_lastDeleteStatus = DELETE_INVALID_ARGUMENT;
        return false;
    }

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        s_lastDeleteStatus = DELETE_NOT_FOUND;
        return false;
    }
    if (entry.isDir != directory) {
        s_lastDeleteStatus = DELETE_WRONG_TYPE;
        return false;
    }
    if (entry.attr & ATTR_READ_ONLY) {
        s_lastDeleteStatus = DELETE_READ_ONLY;
        return false;
    }

    if (directory && !directory_is_empty_for_delete(volumeIndex, entry.firstCluster)) {
        return false;
    }

    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        s_lastDeleteStatus = DELETE_IO_ERROR;
        return false;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    de->name[0] = static_cast<char>(0xE5);
    if (write_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) {
        s_lastDeleteStatus = DELETE_IO_ERROR;
        return false;
    }

    // A failed Paste rollback must remove the directory entry and return its
    // entire data chain to the free-cluster pool, not merely hide the entry.
    if (!release_cluster_chain(vol, entry.firstCluster)) {
        s_lastDeleteStatus = (last_traversal_status() == TRAVERSAL_CHAIN_CYCLE ||
                              last_traversal_status() == TRAVERSAL_INVALID_CLUSTER ||
                              last_traversal_status() == TRAVERSAL_BAD_CLUSTER ||
                              last_traversal_status() == TRAVERSAL_CHAIN_STEP_LIMIT ||
                              last_traversal_status() == TRAVERSAL_TRUNCATED_CHAIN)
            ? DELETE_CORRUPT_CHAIN : DELETE_IO_ERROR;
        return false;
    }
    if (flush_volume_io(vol) != block::BLOCK_OK) {
        s_lastDeleteStatus = DELETE_IO_ERROR;
        return false;
    }
    s_lastDeleteStatus = DELETE_OK;
    return true;
}

static bool restore_directory_entry(const FATVolume& vol, uint32_t sector,
                                    uint32_t offset, const FAT32_DirEntry& entry)
{
    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) return false;
    memcopy(&s_secBuf[offset], &entry, sizeof(FAT32_DirEntry));
    return write_volume_sector(vol, sector, s_secBuf) == block::BLOCK_OK;
}

static bool hide_directory_entry(const FATVolume& vol, uint32_t sector, uint32_t offset)
{
    if (read_volume_sector(vol, sector, s_secBuf) != block::BLOCK_OK) return false;
    FAT32_DirEntry* entry = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    entry->name[0] = static_cast<char>(0xE5);
    return write_volume_sector(vol, sector, s_secBuf) == block::BLOCK_OK;
}

bool rename_path(uint8_t volumeIndex, const char* oldPath, const char* newPath)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!oldPath || !newPath) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT16 && vol.type != FAT_TYPE_FAT32) return false;
    serial::puts("LFPASTE_RENAME_BEGIN old=");
    serial::puts(oldPath);
    serial::puts(" new=");
    serial::puts(newPath);
    serial::puts("\n");

    DirEntry existing;
    if (lookup_path(volumeIndex, newPath, &existing)) return false;

    uint32_t oldParent = 0;
    uint32_t newParent = 0;
    char oldName[128];
    char newName[128];
    if (!split_parent_and_name(volumeIndex, oldPath, &oldParent, oldName, sizeof(oldName))) return false;
    if (!split_parent_and_name(volumeIndex, newPath, &newParent, newName, sizeof(newName))) return false;

    char shortName[11];
    if (!make_short_name(newName, shortName)) return false;

    DirEntry entry;
    uint32_t oldSector = 0;
    uint32_t oldOffset = 0;
    if (!find_in_directory_at(volumeIndex, oldParent, oldName, &entry, &oldSector, &oldOffset)) {
        return false;
    }
    if (entry.attr & ATTR_READ_ONLY) return false;

    if (oldParent == newParent) {
        if (read_volume_sector(vol, oldSector, s_secBuf) != block::BLOCK_OK) {
            return false;
        }

        FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[oldOffset]);
        FAT32_DirEntry originalEntry;
        memcopy(&originalEntry, de, sizeof(FAT32_DirEntry));
        memcopy(de->name, shortName, 11);

        if (write_volume_sector(vol, oldSector, s_secBuf) != block::BLOCK_OK) return false;
        if (flush_volume_io(vol) == block::BLOCK_OK) return true;
        serial::puts("LFPASTE_RENAME_ROLLBACK_BEGIN same-parent\n");
        restore_directory_entry(vol, oldSector, oldOffset, originalEntry);
        flush_volume_io(vol);
        serial::puts("LFPASTE_RENAME_ROLLBACK_END same-parent\n");
        return false;
    }

    if (read_volume_sector(vol, oldSector, s_secBuf) != block::BLOCK_OK) return false;
    FAT32_DirEntry originalEntry;
    memcopy(&originalEntry, &s_secBuf[oldOffset], sizeof(FAT32_DirEntry));
    FAT32_DirEntry movedEntry;
    memcopy(&movedEntry, &s_secBuf[oldOffset], sizeof(FAT32_DirEntry));
    memcopy(movedEntry.name, shortName, 11);

    uint32_t newSector = 0;
    uint32_t newOffset = 0;
    if (!find_free_dir_entry(volumeIndex, newParent, &newSector, &newOffset)) return false;
    serial::puts("LFPASTE_RENAME_DEST_SLOT sector=0x");
    serial::put_hex32(newSector);
    serial::puts(" offset=0x");
    serial::put_hex32(newOffset);
    serial::puts("\n");

    if (read_volume_sector(vol, newSector, s_secBuf) != block::BLOCK_OK) return false;
    memcopy(&s_secBuf[newOffset], &movedEntry, sizeof(FAT32_DirEntry));
    serial::puts("LFPASTE_RENAME_DEST_WRITE_BEGIN sector=0x");
    serial::put_hex32(newSector);
    serial::puts("\n");
    if (write_volume_sector(vol, newSector, s_secBuf) != block::BLOCK_OK) {
        hide_directory_entry(vol, newSector, newOffset);
        flush_volume_io(vol);
        return false;
    }
    serial::puts("LFPASTE_RENAME_DEST_WRITE_END sector=0x");
    serial::put_hex32(newSector);
    serial::puts("\n");

    if (read_volume_sector(vol, oldSector, s_secBuf) != block::BLOCK_OK) {
        hide_directory_entry(vol, newSector, newOffset);
        flush_volume_io(vol);
        return false;
    }
    FAT32_DirEntry* oldDe = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[oldOffset]);
    oldDe->name[0] = static_cast<char>(0xE5);

    serial::puts("LFPASTE_RENAME_SOURCE_WRITE_BEGIN sector=0x");
    serial::put_hex32(oldSector);
    serial::puts("\n");
    if (write_volume_sector(vol, oldSector, s_secBuf) != block::BLOCK_OK) {
        serial::puts("LFPASTE_RENAME_ROLLBACK_BEGIN source-write\n");
        restore_directory_entry(vol, oldSector, oldOffset, originalEntry);
        hide_directory_entry(vol, newSector, newOffset);
        flush_volume_io(vol);
        serial::puts("LFPASTE_RENAME_ROLLBACK_END source-write\n");
        return false;
    }
    serial::puts("LFPASTE_RENAME_SOURCE_WRITE_END sector=0x");
    serial::put_hex32(oldSector);
    serial::puts("\n");
    if (flush_volume_io(vol) == block::BLOCK_OK) return true;
    serial::puts("LFPASTE_RENAME_ROLLBACK_BEGIN flush\n");
    restore_directory_entry(vol, oldSector, oldOffset, originalEntry);
    hide_directory_entry(vol, newSector, newOffset);
    flush_volume_io(vol);
    serial::puts("LFPASTE_RENAME_ROLLBACK_END flush\n");
    return false;
}

} // namespace fs_fat
} // namespace kernel
