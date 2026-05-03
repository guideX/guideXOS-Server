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

namespace kernel {
namespace fs_fat {

// ================================================================
// Internal state
// ================================================================

static FATVolume s_volumes[MAX_FAT_VOLUMES];
static FATFile   s_files[MAX_OPEN_FILES];
static uint8_t   s_volumeCount = 0;

// Sector buffer for reading metadata (one sector at a time)
static uint8_t   s_secBuf[4096]; // supports up to 4096-byte sectors

// Directory iteration state
static struct {
    bool     active;
    uint8_t  volIdx;
    uint32_t cluster;
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

// ================================================================
// FAT32 cluster ? sector translation
// ================================================================

static uint32_t cluster_to_sector(const FATVolume& vol, uint32_t cluster)
{
    return vol.firstDataSector +
           (cluster - 2) * vol.sectorsPerCluster;
}

// ================================================================
// FAT32: read next cluster from the FAT table
// ================================================================

static uint32_t fat32_next_cluster(const FATVolume& vol, uint32_t cluster)
{
    // Each FAT entry is 4 bytes.  Determine which sector of the
    // FAT contains the entry.
    uint32_t fatOffset   = cluster * 4;
    uint32_t fatSector   = vol.reservedSectors + (fatOffset / vol.bytesPerSector);
    uint32_t entryOffset = fatOffset % vol.bytesPerSector;

    block::Status st = block::read_sectors(vol.blockDevIndex, fatSector, 1, s_secBuf);
    if (st != block::BLOCK_OK) return FAT32_CLUSTER_END;

    uint32_t val = *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]);
    return val & FAT32_CLUSTER_MASK;
}

// ================================================================
// exFAT: read next cluster from the FAT table
// ================================================================

static uint32_t exfat_next_cluster(const FATVolume& vol, uint32_t cluster)
{
    uint32_t fatOffset   = cluster * 4;
    uint32_t sectorSize  = 1u << vol.exfatBytesPerSectorShift;
    uint32_t fatSector   = vol.exfatFatOffset + (fatOffset / sectorSize);
    uint32_t entryOffset = fatOffset % sectorSize;

    block::Status st = block::read_sectors(vol.blockDevIndex, fatSector, 1, s_secBuf);
    if (st != block::BLOCK_OK) return 0xFFFFFFFF;

    return *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]);
}

// ================================================================
// Get next cluster (type-dispatched)
// ================================================================

static uint32_t next_cluster(const FATVolume& vol, uint32_t cluster)
{
    if (vol.type == FAT_TYPE_FAT32) return fat32_next_cluster(vol, cluster);
    if (vol.type == FAT_TYPE_EXFAT) return exfat_next_cluster(vol, cluster);
    return 0xFFFFFFFF;
}

static bool is_end_of_chain(const FATVolume& vol, uint32_t cluster)
{
    if (vol.type == FAT_TYPE_FAT32) return cluster >= FAT32_CLUSTER_END;
    if (vol.type == FAT_TYPE_EXFAT) return cluster >= 0xFFFFFFF8;
    return true;
}

// ================================================================
// Mount — detect FAT32 or exFAT and fill volume descriptor
// ================================================================

static bool try_mount_fat32(uint8_t blockDevIdx, FATVolume& vol)
{
    block::Status st = block::read_sectors(blockDevIdx, 0, 1, s_secBuf);
    if (st != block::BLOCK_OK) return false;

    const FAT32_BPB* bpb = reinterpret_cast<const FAT32_BPB*>(s_secBuf);

    // Basic sanity checks
    if (bpb->bytesPerSector < 512 || bpb->bytesPerSector > 4096) return false;
    if (bpb->sectorsPerCluster == 0) return false;
    if (bpb->numFATs == 0) return false;
    if (bpb->fatSize32 == 0) return false;

    // Check for "FAT32   " signature
    if (!str_equal(bpb->fsType, "FAT32   ", 8)) return false;

    vol.type              = FAT_TYPE_FAT32;
    vol.blockDevIndex     = blockDevIdx;
    vol.bytesPerSector    = bpb->bytesPerSector;
    vol.sectorsPerCluster = bpb->sectorsPerCluster;
    vol.reservedSectors   = bpb->reservedSectors;
    vol.numFATs           = bpb->numFATs;
    vol.fatSizeSectors    = bpb->fatSize32;
    vol.rootCluster       = bpb->rootCluster;

    vol.totalSectors = (bpb->totalSectors32 != 0)
                       ? bpb->totalSectors32
                       : bpb->totalSectors16;

    vol.firstDataSector = vol.reservedSectors +
                          (vol.numFATs * vol.fatSizeSectors);

    uint32_t dataSectors = vol.totalSectors - vol.firstDataSector;
    vol.totalDataClusters = dataSectors / vol.sectorsPerCluster;

    // Copy volume label
    memcopy(vol.volumeLabel, bpb->volumeLabel, 11);
    vol.volumeLabel[11] = '\0';

    vol.mounted = true;
    return true;
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

    vol.bytesPerSector    = 1u << bs->bytesPerSectorShift;
    vol.sectorsPerCluster = 1u << bs->sectorsPerClusterShift;
    vol.firstDataSector   = bs->clusterHeapOffset;

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
    uint32_t lba;
    if (vol.type == FAT_TYPE_FAT32) {
        lba = cluster_to_sector(vol, cluster) + sectorOffset;
    } else {
        // exFAT
        lba = vol.exfatClusterHeapOffset +
              (cluster - 2) * vol.sectorsPerCluster + sectorOffset;
    }
    return block::read_sectors(vol.blockDevIndex, lba, 1, buffer);
}

static block::Status write_cluster_sector(const FATVolume& vol,
                                          uint32_t cluster,
                                          uint32_t sectorOffset,
                                          const void* buffer)
{
    uint32_t lba;
    if (vol.type == FAT_TYPE_FAT32) {
        lba = cluster_to_sector(vol, cluster) + sectorOffset;
    } else {
        lba = vol.exfatClusterHeapOffset +
              (cluster - 2) * vol.sectorsPerCluster + sectorOffset;
    }
    return block::write_sectors(vol.blockDevIndex, lba, 1, buffer);
}

static block::Status write_fat32_entry(const FATVolume& vol, uint32_t cluster, uint32_t value)
{
    uint32_t fatOffset = cluster * 4;
    uint32_t fatSector = vol.reservedSectors + (fatOffset / vol.bytesPerSector);
    uint32_t entryOffset = fatOffset % vol.bytesPerSector;

    block::Status st = block::read_sectors(vol.blockDevIndex, fatSector, 1, s_secBuf);
    if (st != block::BLOCK_OK) return st;

    *reinterpret_cast<uint32_t*>(&s_secBuf[entryOffset]) = value & FAT32_CLUSTER_MASK;
    st = block::write_sectors(vol.blockDevIndex, fatSector, 1, s_secBuf);
    if (st != block::BLOCK_OK) return st;

    for (uint32_t fatIndex = 1; fatIndex < vol.numFATs; ++fatIndex) {
        uint32_t mirrorSector = fatSector + fatIndex * vol.fatSizeSectors;
        st = block::write_sectors(vol.blockDevIndex, mirrorSector, 1, s_secBuf);
        if (st != block::BLOCK_OK) return st;
    }

    return block::BLOCK_OK;
}

static uint32_t allocate_fat32_cluster(FATVolume& vol)
{
    uint32_t entriesPerSector = vol.bytesPerSector / 4;
    for (uint32_t fatSectorOffset = 0; fatSectorOffset < vol.fatSizeSectors; ++fatSectorOffset) {
        uint32_t fatSector = vol.reservedSectors + fatSectorOffset;
        if (block::read_sectors(vol.blockDevIndex, fatSector, 1, s_secBuf) != block::BLOCK_OK) {
            return 0;
        }

        for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
            uint32_t cluster = fatSectorOffset * entriesPerSector + entryIndex;
            if (cluster < 2 || cluster >= vol.totalDataClusters + 2) continue;

            uint32_t value = *reinterpret_cast<uint32_t*>(&s_secBuf[entryIndex * 4]) & FAT32_CLUSTER_MASK;
            if (value != FAT32_CLUSTER_FREE) continue;

            if (write_fat32_entry(vol, cluster, FAT32_CLUSTER_END) != block::BLOCK_OK) {
                return 0;
            }

            return cluster;
        }
    }

    return 0;
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

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_volumes, sizeof(s_volumes));
    memzero(s_files, sizeof(s_files));
    memzero(&s_dirIter, sizeof(s_dirIter));
    s_volumeCount = 0;
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

    // Try FAT32 first, then exFAT
    if (try_mount_fat32(blockDevIndex, vol)) {
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
    s_dirIter.sectorInCluster = 0;
    s_dirIter.entryInSector   = 0;
    return true;
}

bool read_dir(uint8_t volumeIndex, DirEntry* out)
{
    if (!s_dirIter.active || s_dirIter.volIdx != volumeIndex) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    uint32_t entriesPerSector = vol.bytesPerSector / 32;

    while (true) {
        if (is_end_of_chain(vol, s_dirIter.cluster)) {
            s_dirIter.active = false;
            return false;
        }

        // Read current sector
        block::Status st = read_cluster_sector(vol,
            s_dirIter.cluster, s_dirIter.sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) {
            s_dirIter.active = false;
            return false;
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
            if (static_cast<uint8_t>(de->name[0]) == 0xE5) continue;

            // Skip LFN and volume ID entries
            if (de->attr == ATTR_LFN) continue;
            if (de->attr & ATTR_VOLUME_ID) continue;

            // Valid entry — fill output
            memzero(out, sizeof(DirEntry));
            short_name_to_string(de->name, out->name);
            out->fileSize     = de->fileSize;
            out->firstCluster = (static_cast<uint32_t>(de->firstClusterHi) << 16) |
                                de->firstClusterLo;
            out->attr         = de->attr;
            out->crtDate      = de->crtDate;
            out->crtTime      = de->crtTime;
            out->wrtDate      = de->wrtDate;
            out->wrtTime      = de->wrtTime;
            out->isDir        = (de->attr & ATTR_DIRECTORY) != 0;
            return true;
        }

        // Advance to next sector in cluster
        s_dirIter.entryInSector = 0;
        ++s_dirIter.sectorInCluster;
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
            return i;
        }
    }
    return 0xFF;
}

uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len)
{
    if (fileHandle >= MAX_OPEN_FILES) return 0;
    FATFile& f = s_files[fileHandle];
    if (!f.open) return 0;

    FATVolume& vol = s_volumes[f.volumeIndex];
    uint32_t bytesRead = 0;
    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t clusterBytes = vol.sectorsPerCluster * vol.bytesPerSector;

    while (bytesRead < len && f.currentOffset < f.fileSize) {
        if (is_end_of_chain(vol, f.currentCluster)) break;

        // Offset within current cluster
        uint32_t offsetInCluster = f.currentOffset % clusterBytes;
        uint32_t sectorInCluster = offsetInCluster / vol.bytesPerSector;
        uint32_t offsetInSector  = offsetInCluster % vol.bytesPerSector;

        block::Status st = read_cluster_sector(vol,
            f.currentCluster, sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) break;

        uint32_t available = vol.bytesPerSector - offsetInSector;
        uint32_t remaining = f.fileSize - f.currentOffset;
        uint32_t wanted    = len - bytesRead;
        uint32_t toCopy    = available;
        if (toCopy > remaining) toCopy = remaining;
        if (toCopy > wanted)    toCopy = wanted;

        memcopy(dst + bytesRead, &s_secBuf[offsetInSector], toCopy);
        bytesRead        += toCopy;
        f.currentOffset  += toCopy;

        // Advance to next cluster if we've consumed the current one
        if ((f.currentOffset % clusterBytes) == 0 && f.currentOffset > 0) {
            f.currentCluster = next_cluster(vol, f.currentCluster);
        }
    }

    return bytesRead;
}

uint32_t write_file(uint8_t fileHandle, const void* buffer, uint32_t len)
{
    if (fileHandle >= MAX_OPEN_FILES) return 0;
    FATFile& f = s_files[fileHandle];
    if (!f.open) return 0;
    if (!buffer || len == 0) return 0;
    if (f.attr & ATTR_READ_ONLY) return 0;

    FATVolume& vol = s_volumes[f.volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return 0;

    uint32_t bytesWritten = 0;
    const uint8_t* src = static_cast<const uint8_t*>(buffer);
    uint32_t clusterBytes = vol.sectorsPerCluster * vol.bytesPerSector;

    while (bytesWritten < len && f.currentOffset < f.fileSize) {
        if (is_end_of_chain(vol, f.currentCluster)) break;

        uint32_t offsetInCluster = f.currentOffset % clusterBytes;
        uint32_t sectorInCluster = offsetInCluster / vol.bytesPerSector;
        uint32_t offsetInSector  = offsetInCluster % vol.bytesPerSector;

        block::Status st = read_cluster_sector(vol,
            f.currentCluster, sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) break;

        uint32_t available = vol.bytesPerSector - offsetInSector;
        uint32_t remaining = f.fileSize - f.currentOffset;
        uint32_t wanted    = len - bytesWritten;
        uint32_t toCopy    = available;
        if (toCopy > remaining) toCopy = remaining;
        if (toCopy > wanted)    toCopy = wanted;

        memcopy(&s_secBuf[offsetInSector], src + bytesWritten, toCopy);

        st = write_cluster_sector(vol, f.currentCluster, sectorInCluster, s_secBuf);
        if (st != block::BLOCK_OK) break;

        bytesWritten    += toCopy;
        f.currentOffset += toCopy;

        if ((f.currentOffset % clusterBytes) == 0 && f.currentOffset > 0) {
            f.currentCluster = next_cluster(vol, f.currentCluster);
        }
    }

    return bytesWritten;
}

void close_file(uint8_t fileHandle)
{
    if (fileHandle >= MAX_OPEN_FILES) return;
    s_files[fileHandle].open = false;
}

const FATVolume* get_volume(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return nullptr;
    if (!s_volumes[volumeIndex].mounted) return nullptr;
    return &s_volumes[volumeIndex];
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
    uint32_t cluster = dirCluster;

    while (!is_end_of_chain(vol, cluster)) {
        for (uint32_t sectorInCluster = 0; sectorInCluster < vol.sectorsPerCluster; ++sectorInCluster) {
            uint32_t sector = cluster_to_sector(vol, cluster) + sectorInCluster;
            if (block::read_sectors(vol.blockDevIndex, sector, 1, s_secBuf) != block::BLOCK_OK) {
                return false;
            }

            uint32_t entriesPerSector = vol.bytesPerSector / 32;
            for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                uint32_t offset = entryIndex * 32;
                const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(&s_secBuf[offset]);
                if (de->name[0] == 0x00 || static_cast<uint8_t>(de->name[0]) == 0xE5) {
                    *outSector = sector;
                    *outOffset = offset;
                    return true;
                }
            }
        }

        cluster = next_cluster(vol, cluster);
    }

    return false;
}

static bool write_file_clusters(FATVolume& vol, uint32_t firstCluster, const void* buffer, uint32_t len)
{
    if (len == 0) return true;
    if (!buffer) return false;

    const uint8_t* src = static_cast<const uint8_t*>(buffer);
    uint32_t cluster = firstCluster;
    uint32_t written = 0;

    while (written < len && !is_end_of_chain(vol, cluster)) {
        for (uint32_t sectorInCluster = 0; sectorInCluster < vol.sectorsPerCluster && written < len; ++sectorInCluster) {
            memzero(s_secBuf, vol.bytesPerSector);
            uint32_t toCopy = len - written;
            if (toCopy > vol.bytesPerSector) toCopy = vol.bytesPerSector;
            memcopy(s_secBuf, src + written, toCopy);
            if (write_cluster_sector(vol, cluster, sectorInCluster, s_secBuf) != block::BLOCK_OK) {
                return false;
            }
            written += toCopy;
        }

        if (written < len) {
            uint32_t next = next_cluster(vol, cluster);
            if (is_end_of_chain(vol, next)) {
                uint32_t newCluster = allocate_fat32_cluster(vol);
                if (newCluster == 0) return false;
                if (write_fat32_entry(vol, cluster, newCluster) != block::BLOCK_OK) return false;
                if (write_fat32_entry(vol, newCluster, FAT32_CLUSTER_END) != block::BLOCK_OK) return false;
                next = newCluster;
            }
            cluster = next;
        }
    }

    return written == len;
}

static bool find_in_directory_at(uint8_t volumeIndex, uint32_t dirCluster, const char* name,
                                 DirEntry* out, uint32_t* outSector, uint32_t* outOffset)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!name || !out || !outSector || !outOffset) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    uint32_t cluster = dirCluster;

    while (!is_end_of_chain(vol, cluster)) {
        for (uint32_t sectorInCluster = 0; sectorInCluster < vol.sectorsPerCluster; ++sectorInCluster) {
            uint32_t sector = cluster_to_sector(vol, cluster) + sectorInCluster;
            if (block::read_sectors(vol.blockDevIndex, sector, 1, s_secBuf) != block::BLOCK_OK) {
                return false;
            }

            uint32_t entriesPerSector = vol.bytesPerSector / 32;
            for (uint32_t entryIndex = 0; entryIndex < entriesPerSector; ++entryIndex) {
                uint32_t offset = entryIndex * 32;
                const FAT32_DirEntry* de = reinterpret_cast<const FAT32_DirEntry*>(&s_secBuf[offset]);

                if (de->name[0] == 0x00) return false;
                if (static_cast<uint8_t>(de->name[0]) == 0xE5) continue;
                if (de->attr == ATTR_LFN) continue;
                if (de->attr & ATTR_VOLUME_ID) continue;

                char entryName[32];
                short_name_to_string(de->name, entryName);
                if (!name_matches(entryName, name)) continue;

                memzero(out, sizeof(DirEntry));
                memcopy(out->name, entryName, sizeof(entryName));
                out->fileSize     = de->fileSize;
                out->firstCluster = (static_cast<uint32_t>(de->firstClusterHi) << 16) |
                                    de->firstClusterLo;
                out->attr         = de->attr;
                out->crtDate      = de->crtDate;
                out->crtTime      = de->crtTime;
                out->wrtDate      = de->wrtDate;
                out->wrtTime      = de->wrtTime;
                out->isDir        = (de->attr & ATTR_DIRECTORY) != 0;
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

static uint32_t cluster_chain_capacity(const FATVolume& vol, uint32_t firstCluster)
{
    uint32_t clusters = 0;
    uint32_t cluster = firstCluster;
    while (!is_end_of_chain(vol, cluster) && clusters <= vol.totalDataClusters) {
        ++clusters;
        cluster = next_cluster(vol, cluster);
    }
    return clusters * vol.sectorsPerCluster * vol.bytesPerSector;
}

bool open_dir(uint8_t volumeIndex, uint32_t dirCluster)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    
    s_dirIter.active          = true;
    s_dirIter.volIdx          = volumeIndex;
    s_dirIter.cluster         = dirCluster;
    s_dirIter.sectorInCluster = 0;
    s_dirIter.entryInSector   = 0;
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
    
    // Skip leading slash
    const char* p = path;
    if (*p == '/') ++p;
    
    // Start at root cluster
    uint32_t currentCluster = vol.rootCluster;
    DirEntry entry;
    
    while (*p) {
        // Extract next path component
        char component[128];
        int i = 0;
        while (*p && *p != '/' && i < 127) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        // Skip trailing slashes
        while (*p == '/') ++p;
        
        // Skip empty components
        if (component[0] == '\0') continue;
        
        // Search in current directory
        if (!open_dir(volumeIndex, currentCluster)) {
            return false;
        }
        
        if (!find_in_dir(volumeIndex, component, &entry)) {
            return false;  // Not found
        }
        
        // Check if this is the final component
        if (*p == '\0') {
            // Found it!
            memcopy(out, &entry, sizeof(DirEntry));
            return true;
        }
        
        // Not the final component - must be a directory
        if (!entry.isDir) {
            return false;  // Path component is not a directory
        }
        
        // Continue with this directory
        currentCluster = entry.firstCluster;
    }
    
    return false;
}

bool overwrite_path(uint8_t volumeIndex, const char* path, const void* buffer, uint32_t len)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!path || (!buffer && len != 0)) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return false;

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        return false;
    }
    if (entry.isDir || (entry.attr & ATTR_READ_ONLY)) return false;

    uint32_t capacity = cluster_chain_capacity(vol, entry.firstCluster);
    if (len > capacity) return false;
    if (!write_file_clusters(vol, entry.firstCluster, buffer, len)) return false;

    if (block::read_sectors(vol.blockDevIndex, sector, 1, s_secBuf) != block::BLOCK_OK) {
        return false;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    de->fileSize = len;

    return block::write_sectors(vol.blockDevIndex, sector, 1, s_secBuf) == block::BLOCK_OK;
}

bool create_file_path(uint8_t volumeIndex, const char* path, const void* buffer, uint32_t len)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!path || (!buffer && len != 0)) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return false;

    DirEntry existing;
    if (lookup_path(volumeIndex, path, &existing)) return false;

    uint32_t parentCluster = 0;
    char fileName[128];
    if (!split_parent_and_name(volumeIndex, path, &parentCluster, fileName, sizeof(fileName))) {
        return false;
    }

    char shortName[11];
    if (!make_short_name(fileName, shortName)) return false;

    uint32_t firstCluster = allocate_fat32_cluster(vol);
    if (firstCluster == 0) return false;

    if (!write_file_clusters(vol, firstCluster, buffer, len)) return false;

    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_free_dir_entry(volumeIndex, parentCluster, &sector, &offset)) {
        return false;
    }

    if (block::read_sectors(vol.blockDevIndex, sector, 1, s_secBuf) != block::BLOCK_OK) {
        return false;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    memzero(de, sizeof(FAT32_DirEntry));
    memcopy(de->name, shortName, 11);
    de->attr = ATTR_ARCHIVE;
    de->firstClusterHi = static_cast<uint16_t>((firstCluster >> 16) & 0xFFFF);
    de->firstClusterLo = static_cast<uint16_t>(firstCluster & 0xFFFF);
    de->fileSize = len;

    return block::write_sectors(vol.blockDevIndex, sector, 1, s_secBuf) == block::BLOCK_OK;
}

bool delete_path(uint8_t volumeIndex, const char* path, bool directory)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!path) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return false;

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_path_entry_at(volumeIndex, path, &entry, &sector, &offset)) {
        return false;
    }
    if (entry.isDir != directory) return false;
    if (entry.attr & ATTR_READ_ONLY) return false;

    if (block::read_sectors(vol.blockDevIndex, sector, 1, s_secBuf) != block::BLOCK_OK) {
        return false;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    de->name[0] = static_cast<char>(0xE5);

    return block::write_sectors(vol.blockDevIndex, sector, 1, s_secBuf) == block::BLOCK_OK;
}

bool rename_path(uint8_t volumeIndex, const char* oldPath, const char* newPath)
{
    if (volumeIndex >= MAX_FAT_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;
    if (!oldPath || !newPath) return false;

    FATVolume& vol = s_volumes[volumeIndex];
    if (vol.type != FAT_TYPE_FAT32) return false;

    DirEntry existing;
    if (lookup_path(volumeIndex, newPath, &existing)) return false;

    uint32_t oldParent = 0;
    uint32_t newParent = 0;
    char oldName[128];
    char newName[128];
    if (!split_parent_and_name(volumeIndex, oldPath, &oldParent, oldName, sizeof(oldName))) return false;
    if (!split_parent_and_name(volumeIndex, newPath, &newParent, newName, sizeof(newName))) return false;
    if (oldParent != newParent) return false;

    char shortName[11];
    if (!make_short_name(newName, shortName)) return false;

    DirEntry entry;
    uint32_t sector = 0;
    uint32_t offset = 0;
    if (!find_in_directory_at(volumeIndex, oldParent, oldName, &entry, &sector, &offset)) {
        return false;
    }
    if (entry.attr & ATTR_READ_ONLY) return false;

    if (block::read_sectors(vol.blockDevIndex, sector, 1, s_secBuf) != block::BLOCK_OK) {
        return false;
    }

    FAT32_DirEntry* de = reinterpret_cast<FAT32_DirEntry*>(&s_secBuf[offset]);
    memcopy(de->name, shortName, 11);

    return block::write_sectors(vol.blockDevIndex, sector, 1, s_secBuf) == block::BLOCK_OK;
}

} // namespace fs_fat
} // namespace kernel
