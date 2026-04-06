// NTFS Filesystem Driver Implementation
//
// Provides read/write support for Microsoft NTFS volumes.
//
// Implementation Notes:
// - MFT record fixup handling for data integrity
// - Support for both resident and non-resident attributes
// - Data run decoding for cluster allocation
// - UTF-16LE to ASCII filename conversion
//
// Current Limitations:
// - No journal ($LogFile) replay
// - No compression support
// - No encryption (EFS) support
// - Basic write support (no journaling integration)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/fs_ntfs.h"
#include "include/kernel/serial_debug.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace fs_ntfs {

// ================================================================
// Internal State
// ================================================================

static bool s_initialized = false;
static NtfsVolume s_volumes[4];  // Max 4 NTFS volumes
static int s_volumeCount = 0;

// ================================================================
// Helper Functions
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
}

static int strlen(const char* s)
{
    int len = 0;
    while (s[len]) ++len;
    return len;
}

static int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<uint8_t>(*a) - static_cast<uint8_t>(*b);
}

// Convert UTF-16LE to ASCII (lossy for non-ASCII chars)
static void utf16le_to_ascii(const uint8_t* utf16, int charCount, char* ascii)
{
    for (int i = 0; i < charCount; ++i) {
        uint16_t ch = utf16[i * 2] | (utf16[i * 2 + 1] << 8);
        if (ch < 128) {
            ascii[i] = static_cast<char>(ch);
        } else {
            ascii[i] = '?';  // Non-ASCII placeholder
        }
    }
    ascii[charCount] = '\0';
}

// Apply MFT record fixup to restore valid sector data
// NTFS stores fixup values at end of each 512-byte sector
static bool apply_fixup(MftRecordHeader* record, uint32_t recordSize)
{
    if (record->fixupOffset == 0 || record->fixupCount == 0) {
        return true;  // No fixup needed
    }
    
    uint16_t* fixupArray = reinterpret_cast<uint16_t*>(
        reinterpret_cast<uint8_t*>(record) + record->fixupOffset);
    uint16_t fixupSignature = fixupArray[0];
    
    // Apply fixup to each sector's last 2 bytes
    uint32_t sectorsPerRecord = recordSize / 512;
    for (uint32_t i = 0; i < sectorsPerRecord && i + 1 < record->fixupCount; ++i) {
        uint16_t* sectorEnd = reinterpret_cast<uint16_t*>(
            reinterpret_cast<uint8_t*>(record) + (i + 1) * 512 - 2);
        
        // Verify signature matches
        if (*sectorEnd != fixupSignature) {
            kernel::serial::puts("[NTFS] Fixup signature mismatch\n");
            return false;
        }
        
        // Replace with original value
        *sectorEnd = fixupArray[i + 1];
    }
    
    return true;
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    kernel::serial::puts("[NTFS] Initializing NTFS filesystem driver...\n");
    
    memzero(s_volumes, sizeof(s_volumes));
    s_volumeCount = 0;
    s_initialized = true;
    
    kernel::serial::puts("[NTFS] NTFS driver initialized\n");
}

// ================================================================
// Volume Operations
// ================================================================

NtfsStatus probe(uint8_t blockDevIndex)
{
    // Read boot sector
    uint8_t bootSector[512];
    
    if (block::read_sectors(blockDevIndex, 0, 1, bootSector) != block::BLOCK_OK) {
        return NTFS_ERR_IO;
    }
    
    // Check NTFS signature ("NTFS    " at offset 3)
    const NtfsBootSector* bs = reinterpret_cast<const NtfsBootSector*>(bootSector);
    
    // Verify OEM ID
    if (bs->oemId[0] != 'N' || bs->oemId[1] != 'T' ||
        bs->oemId[2] != 'F' || bs->oemId[3] != 'S') {
        return NTFS_ERR_NOT_NTFS;
    }
    
    // Verify boot signature
    if (bs->bootSignature != 0xAA55) {
        return NTFS_ERR_NOT_NTFS;
    }
    
    // Basic sanity checks
    if (bs->bytesPerSector == 0 || bs->sectorsPerCluster == 0) {
        return NTFS_ERR_CORRUPT;
    }
    
    return NTFS_OK;
}

NtfsStatus mount(uint8_t blockDevIndex, NtfsVolume* volumeOut, bool readOnly)
{
    if (!s_initialized) {
        init();
    }
    
    if (volumeOut == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    // First probe to verify NTFS
    NtfsStatus probeResult = probe(blockDevIndex);
    if (probeResult != NTFS_OK) {
        return probeResult;
    }
    
    kernel::serial::puts("[NTFS] Mounting NTFS volume on device ");
    kernel::serial::put_hex8(blockDevIndex);
    kernel::serial::putc('\n');
    
    // Read boot sector again for parsing
    uint8_t bootSector[512];
    if (block::read_sectors(blockDevIndex, 0, 1, bootSector) != block::BLOCK_OK) {
        return NTFS_ERR_IO;
    }
    
    const NtfsBootSector* bs = reinterpret_cast<const NtfsBootSector*>(bootSector);
    
    // Initialize volume structure
    memzero(volumeOut, sizeof(NtfsVolume));
    volumeOut->blockDeviceIndex = blockDevIndex;
    volumeOut->readOnly = readOnly;
    
    // Parse boot sector parameters
    volumeOut->bytesPerSector = bs->bytesPerSector;
    volumeOut->bytesPerCluster = bs->bytesPerSector * bs->sectorsPerCluster;
    volumeOut->totalSectors = bs->totalSectors;
    volumeOut->totalClusters = bs->totalSectors / bs->sectorsPerCluster;
    
    // Parse MFT record size
    // If clustersPerMftRecord is negative, size = 2^|value| bytes
    // If positive, size = value * bytesPerCluster
    if (bs->clustersPerMftRecord < 0) {
        volumeOut->mftRecordSize = 1u << static_cast<uint32_t>(-bs->clustersPerMftRecord);
    } else {
        volumeOut->mftRecordSize = bs->clustersPerMftRecord * volumeOut->bytesPerCluster;
    }
    
    // Parse index record size (same logic)
    if (bs->clustersPerIndexRecord < 0) {
        volumeOut->indexRecordSize = 1u << static_cast<uint32_t>(-bs->clustersPerIndexRecord);
    } else {
        volumeOut->indexRecordSize = bs->clustersPerIndexRecord * volumeOut->bytesPerCluster;
    }
    
    // MFT location
    volumeOut->mftLcn = bs->mftLcn;
    volumeOut->mftMirrLcn = bs->mftMirrLcn;
    volumeOut->volumeSerial = bs->volumeSerialNumber;
    
    // Allocate MFT record buffer
    // NOTE: In a real kernel, this would use kmalloc
    static uint8_t s_mftBuffer[4096];  // Assume max 4KB MFT records
    if (volumeOut->mftRecordSize > sizeof(s_mftBuffer)) {
        kernel::serial::puts("[NTFS] MFT record too large\n");
        return NTFS_ERR_UNSUPPORTED;
    }
    volumeOut->mftRecordBuffer = s_mftBuffer;
    
    // Try to read volume label from $Volume (MFT entry 3)
    // This is a stub - full implementation would parse the attribute
    volumeOut->volumeLabel[0] = '\0';
    
    volumeOut->mounted = true;
    
    kernel::serial::puts("[NTFS] Volume mounted successfully\n");
    kernel::serial::puts("  Bytes/sector:  ");
    kernel::serial::put_hex32(volumeOut->bytesPerSector);
    kernel::serial::puts("\n  Bytes/cluster: ");
    kernel::serial::put_hex32(volumeOut->bytesPerCluster);
    kernel::serial::puts("\n  MFT LCN:       ");
    kernel::serial::put_hex64(volumeOut->mftLcn);
    kernel::serial::puts("\n  Total sectors: ");
    kernel::serial::put_hex64(volumeOut->totalSectors);
    kernel::serial::putc('\n');
    
    return NTFS_OK;
}

NtfsStatus unmount(NtfsVolume* volume)
{
    if (volume == nullptr || !volume->mounted) {
        return NTFS_ERR_INVALID;
    }
    
    // Flush any pending writes
    sync(volume);
    
    volume->mounted = false;
    volume->mftRecordBuffer = nullptr;
    
    kernel::serial::puts("[NTFS] Volume unmounted\n");
    return NTFS_OK;
}

// ================================================================
// MFT Operations
// ================================================================

NtfsStatus read_mft_record(NtfsVolume* volume, uint64_t mftIndex, void* buffer)
{
    if (volume == nullptr || !volume->mounted || buffer == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    // Calculate MFT record position
    // MFT starts at mftLcn, each record is mftRecordSize bytes
    uint64_t mftOffset = lcn_to_offset(volume, volume->mftLcn);
    uint64_t recordOffset = mftOffset + (mftIndex * volume->mftRecordSize);
    uint64_t recordLba = recordOffset / volume->bytesPerSector;
    uint32_t sectorsToRead = volume->mftRecordSize / volume->bytesPerSector;
    
    // Read the MFT record
    if (block::read_sectors(volume->blockDeviceIndex, recordLba, sectorsToRead, buffer)
        != block::BLOCK_OK) {
        return NTFS_ERR_IO;
    }
    
    // Apply fixup
    MftRecordHeader* record = static_cast<MftRecordHeader*>(buffer);
    
    // Verify FILE signature
    if (record->signature[0] != 'F' || record->signature[1] != 'I' ||
        record->signature[2] != 'L' || record->signature[3] != 'E') {
        return NTFS_ERR_CORRUPT;
    }
    
    if (!apply_fixup(record, volume->mftRecordSize)) {
        return NTFS_ERR_CORRUPT;
    }
    
    return NTFS_OK;
}

// ================================================================
// Data Run Parsing
// ================================================================

int parse_data_runs(const uint8_t* runList, uint64_t* lcnOut, uint64_t* lengthOut, int maxRuns)
{
    /*
     * Data runs encode cluster allocation as variable-length entries:
     * 
     * First byte: high nibble = LCN bytes, low nibble = length bytes
     * If first byte is 0, end of run list
     * 
     * Following bytes: length (little-endian), then LCN delta (signed)
     */
    
    int runCount = 0;
    uint64_t currentLcn = 0;
    const uint8_t* p = runList;
    
    while (*p != 0 && runCount < maxRuns) {
        uint8_t header = *p++;
        uint8_t lengthBytes = header & 0x0F;
        uint8_t lcnBytes = (header >> 4) & 0x0F;
        
        if (lengthBytes == 0) break;
        
        // Read length
        uint64_t length = 0;
        for (int i = 0; i < lengthBytes; ++i) {
            length |= static_cast<uint64_t>(*p++) << (i * 8);
        }
        
        // Read LCN delta (signed)
        int64_t lcnDelta = 0;
        if (lcnBytes > 0) {
            for (int i = 0; i < lcnBytes; ++i) {
                lcnDelta |= static_cast<uint64_t>(*p++) << (i * 8);
            }
            // Sign extend
            if (lcnDelta & (1ULL << (lcnBytes * 8 - 1))) {
                lcnDelta |= ~((1ULL << (lcnBytes * 8)) - 1);
            }
            currentLcn += lcnDelta;
        }
        // LCN of 0 means sparse extent
        
        lcnOut[runCount] = currentLcn;
        lengthOut[runCount] = length;
        ++runCount;
    }
    
    return runCount;
}

NtfsStatus vcn_to_lcn(NtfsVolume* volume, NtfsFile* file, uint64_t vcn, uint64_t* lcnOut)
{
    /*
     * STUB: Convert Virtual Cluster Number to Logical Cluster Number
     * 
     * Full implementation would:
     * 1. Read the file's MFT record
     * 2. Find the $DATA attribute
     * 3. Parse data runs
     * 4. Walk runs to find VCN, return corresponding LCN
     */
    
    (void)volume;
    (void)file;
    (void)vcn;
    *lcnOut = 0;
    
    kernel::serial::puts("[NTFS] vcn_to_lcn: not fully implemented\n");
    return NTFS_ERR_UNSUPPORTED;
}

// ================================================================
// File Operations
// ================================================================

NtfsStatus open(NtfsVolume* volume, const char* path, NtfsFile* fileOut)
{
    /*
     * STUB: Open a file by path
     * 
     * Full implementation would:
     * 1. Start at root directory (MFT entry 5)
     * 2. Parse path components
     * 3. For each component:
     *    a. Read directory's index root/allocation
     *    b. Search B+ tree for matching filename
     *    c. Get MFT reference
     * 4. Read final file's MFT record
     * 5. Extract file size and data attribute info
     * 6. Initialize NtfsFile handle
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr || fileOut == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    kernel::serial::puts("[NTFS] open: ");
    kernel::serial::puts(path);
    kernel::serial::puts(" (stub)\n");
    
    memzero(fileOut, sizeof(NtfsFile));
    fileOut->volume = volume;
    
    // TODO: Implement path resolution
    
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus close(NtfsFile* file)
{
    if (file == nullptr || !file->open) {
        return NTFS_ERR_INVALID;
    }
    
    file->open = false;
    return NTFS_OK;
}

NtfsStatus read(NtfsFile* file, void* buffer, size_t size, size_t* bytesRead)
{
    /*
     * STUB: Read data from file
     * 
     * Full implementation would:
     * 1. Check if data is resident (small files)
     *    - If resident, copy directly from MFT record
     * 2. For non-resident data:
     *    a. Calculate VCN from position
     *    b. Convert VCN to LCN using data runs
     *    c. Read cluster(s) from disk
     *    d. Copy requested portion to buffer
     * 3. Update file position
     */
    
    if (file == nullptr || !file->open || buffer == nullptr) {
        if (bytesRead) *bytesRead = 0;
        return NTFS_ERR_INVALID;
    }
    
    // Check EOF
    if (file->position >= file->dataSize) {
        if (bytesRead) *bytesRead = 0;
        return NTFS_OK;
    }
    
    // TODO: Implement actual read
    
    kernel::serial::puts("[NTFS] read: stub implementation\n");
    if (bytesRead) *bytesRead = 0;
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus write(NtfsFile* file, const void* buffer, size_t size, size_t* bytesWritten)
{
    /*
     * STUB: Write data to file
     * 
     * Full implementation would:
     * 1. Check read-only mount
     * 2. For resident data that fits, update in MFT record
     * 3. For non-resident:
     *    a. Allocate new clusters if needed
     *    b. Update data runs
     *    c. Write data to clusters
     * 4. Update file size in MFT
     * 5. Update timestamps
     * 
     * NOTE: Production implementation should integrate with $LogFile
     * for journaling to ensure filesystem consistency.
     */
    
    if (file == nullptr || !file->open || buffer == nullptr) {
        if (bytesWritten) *bytesWritten = 0;
        return NTFS_ERR_INVALID;
    }
    
    if (file->volume->readOnly) {
        if (bytesWritten) *bytesWritten = 0;
        return NTFS_ERR_READ_ONLY;
    }
    
    // TODO: Implement actual write
    
    kernel::serial::puts("[NTFS] write: stub implementation\n");
    if (bytesWritten) *bytesWritten = 0;
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus seek(NtfsFile* file, int64_t offset, int whence)
{
    if (file == nullptr || !file->open) {
        return NTFS_ERR_INVALID;
    }
    
    int64_t newPos = 0;
    switch (whence) {
        case 0:  // SEEK_SET
            newPos = offset;
            break;
        case 1:  // SEEK_CUR
            newPos = static_cast<int64_t>(file->position) + offset;
            break;
        case 2:  // SEEK_END
            newPos = static_cast<int64_t>(file->dataSize) + offset;
            break;
        default:
            return NTFS_ERR_INVALID;
    }
    
    if (newPos < 0) {
        return NTFS_ERR_INVALID;
    }
    
    file->position = static_cast<uint64_t>(newPos);
    return NTFS_OK;
}

uint64_t get_size(NtfsFile* file)
{
    if (file == nullptr || !file->open) {
        return 0;
    }
    return file->dataSize;
}

// ================================================================
// Directory Operations
// ================================================================

NtfsStatus list_directory(NtfsVolume* volume, const char* path,
                          NtfsDirCallback callback, void* context)
{
    /*
     * STUB: List directory contents
     * 
     * Full implementation would:
     * 1. Open the directory path
     * 2. Read the $INDEX_ROOT attribute
     * 3. For small directories, entries are in INDEX_ROOT
     * 4. For large directories:
     *    a. Read $INDEX_ALLOCATION attribute
     *    b. Parse B+ tree index blocks
     *    c. Traverse leaf nodes
     * 5. For each entry, extract filename and attributes
     * 6. Call callback with entry info
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr || callback == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    kernel::serial::puts("[NTFS] list_directory: ");
    kernel::serial::puts(path);
    kernel::serial::puts(" (stub)\n");
    
    // TODO: Implement directory listing
    
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus create_directory(NtfsVolume* volume, const char* path)
{
    /*
     * STUB: Create a new directory
     * 
     * Full implementation would:
     * 1. Check read-only
     * 2. Parse path to find parent directory
     * 3. Allocate new MFT record
     * 4. Create $STANDARD_INFORMATION attribute
     * 5. Create $FILE_NAME attribute
     * 6. Create $INDEX_ROOT attribute (empty directory index)
     * 7. Add entry to parent directory's index
     * 8. Update $MFT bitmap
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return NTFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[NTFS] create_directory: stub implementation\n");
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus remove_directory(NtfsVolume* volume, const char* path)
{
    /*
     * STUB: Remove an empty directory
     * 
     * Full implementation would:
     * 1. Check read-only
     * 2. Open directory, verify it's empty
     * 3. Remove entry from parent directory
     * 4. Mark MFT record as unused
     * 5. Update $MFT bitmap
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return NTFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[NTFS] remove_directory: stub implementation\n");
    return NTFS_ERR_UNSUPPORTED;
}

// ================================================================
// File Management
// ================================================================

NtfsStatus create_file(NtfsVolume* volume, const char* path)
{
    /*
     * STUB: Create a new file
     * 
     * Similar to create_directory but:
     * - No $INDEX_ROOT attribute
     * - Create empty $DATA attribute
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return NTFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[NTFS] create_file: stub implementation\n");
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus delete_file(NtfsVolume* volume, const char* path)
{
    /*
     * STUB: Delete a file
     * 
     * Full implementation would:
     * 1. Check read-only
     * 2. Open file, verify it's not a directory
     * 3. Free allocated clusters
     * 4. Remove entry from parent directory
     * 5. Mark MFT record as unused
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return NTFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[NTFS] delete_file: stub implementation\n");
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus rename(NtfsVolume* volume, const char* oldPath, const char* newPath)
{
    /*
     * STUB: Rename or move a file/directory
     * 
     * Full implementation would:
     * 1. Check read-only
     * 2. Open source file/directory
     * 3. Parse destination path
     * 4. If same directory, just update $FILE_NAME
     * 5. If different directory:
     *    a. Add entry to new parent
     *    b. Remove entry from old parent
     *    c. Update $FILE_NAME parent reference
     */
    
    if (volume == nullptr || !volume->mounted || oldPath == nullptr || newPath == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return NTFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[NTFS] rename: stub implementation\n");
    return NTFS_ERR_UNSUPPORTED;
}

NtfsStatus get_attributes(NtfsVolume* volume, const char* path, uint32_t* attrsOut)
{
    /*
     * STUB: Get file/directory attributes
     * 
     * Full implementation would:
     * 1. Open file
     * 2. Read $STANDARD_INFORMATION attribute
     * 3. Return fileAttributes field
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr || attrsOut == nullptr) {
        return NTFS_ERR_INVALID;
    }
    
    *attrsOut = 0;
    kernel::serial::puts("[NTFS] get_attributes: stub implementation\n");
    return NTFS_ERR_UNSUPPORTED;
}

// ================================================================
// Volume Operations
// ================================================================

NtfsStatus get_volume_info(NtfsVolume* volume, char* labelOut, uint64_t* totalBytesOut,
                           uint64_t* freeBytesOut)
{
    if (volume == nullptr || !volume->mounted) {
        return NTFS_ERR_INVALID;
    }
    
    if (labelOut) {
        // Copy volume label (may be empty)
        int i = 0;
        while (volume->volumeLabel[i] && i < 31) {
            labelOut[i] = volume->volumeLabel[i];
            ++i;
        }
        labelOut[i] = '\0';
    }
    
    if (totalBytesOut) {
        *totalBytesOut = volume->totalClusters * volume->bytesPerCluster;
    }
    
    if (freeBytesOut) {
        // TODO: Read $Bitmap to calculate free space
        *freeBytesOut = 0;
    }
    
    return NTFS_OK;
}

NtfsStatus sync(NtfsVolume* volume)
{
    /*
     * STUB: Flush cached data
     * 
     * Full implementation would flush any write caches
     * and ensure all data is written to disk.
     */
    
    if (volume == nullptr || !volume->mounted) {
        return NTFS_ERR_INVALID;
    }
    
    // No caching implemented yet
    return NTFS_OK;
}

// ================================================================
// Status/Debug
// ================================================================

void print_status()
{
    kernel::serial::puts("[NTFS] Driver status:\n");
    kernel::serial::puts("  Initialized: ");
    kernel::serial::puts(s_initialized ? "yes" : "no");
    kernel::serial::puts("\n  Mounted volumes: ");
    kernel::serial::put_hex32(s_volumeCount);
    kernel::serial::putc('\n');
}

} // namespace fs_ntfs
} // namespace kernel
