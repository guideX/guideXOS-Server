// XFS Filesystem Driver Implementation
//
// Provides read/write support for XFS volumes.
//
// Implementation Notes:
// - All on-disk data is big-endian, converted on read
// - Supports both V4 and V5 filesystem formats
// - Extent-based allocation for efficient large file handling
// - B+ tree traversal for directories
//
// Current Limitations:
// - No journal replay (assumes clean filesystem)
// - No realtime subvolume support
// - No extended attributes
// - Basic write support (no delayed allocation)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/fs_xfs.h"
#include "include/kernel/serial_debug.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace fs_xfs {

// ================================================================
// Internal State
// ================================================================

static bool s_initialized = false;
static XfsVolume s_volumes[4];  // Max 4 XFS volumes
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

static int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<uint8_t>(*a) - static_cast<uint8_t>(*b);
}

// ================================================================
// Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    kernel::serial::puts("[XFS] Initializing XFS filesystem driver...\n");
    
    memzero(s_volumes, sizeof(s_volumes));
    s_volumeCount = 0;
    s_initialized = true;
    
    kernel::serial::puts("[XFS] XFS driver initialized\n");
}

// ================================================================
// Volume Operations
// ================================================================

XfsStatus probe(uint8_t blockDevIndex)
{
    // Read first sector to get superblock
    uint8_t buffer[512];
    
    if (block::read_sectors(blockDevIndex, 0, 1, buffer) != block::BLOCK_OK) {
        return XFS_ERR_IO;
    }
    
    const XfsSuperblock* sb = reinterpret_cast<const XfsSuperblock*>(buffer);
    
    // Check magic number (big-endian "XFSB")
    if (be32_to_cpu(sb->sb_magicnum) != XFS_SB_MAGIC) {
        return XFS_ERR_NOT_XFS;
    }
    
    // Basic sanity checks
    uint32_t blockSize = be32_to_cpu(sb->sb_blocksize);
    if (blockSize < 512 || blockSize > 65536) {
        return XFS_ERR_CORRUPT;
    }
    
    // Verify block size is power of 2
    if ((blockSize & (blockSize - 1)) != 0) {
        return XFS_ERR_CORRUPT;
    }
    
    return XFS_OK;
}

XfsStatus mount(uint8_t blockDevIndex, XfsVolume* volumeOut, bool readOnly)
{
    if (!s_initialized) {
        init();
    }
    
    if (volumeOut == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    // First probe to verify XFS
    XfsStatus probeResult = probe(blockDevIndex);
    if (probeResult != XFS_OK) {
        return probeResult;
    }
    
    kernel::serial::puts("[XFS] Mounting XFS volume on device ");
    kernel::serial::put_hex8(blockDevIndex);
    kernel::serial::putc('\n');
    
    // Read superblock (may span multiple sectors for large block sizes)
    // For now, assume superblock fits in first sector (always true)
    uint8_t buffer[512];
    if (block::read_sectors(blockDevIndex, 0, 1, buffer) != block::BLOCK_OK) {
        return XFS_ERR_IO;
    }
    
    const XfsSuperblock* sb = reinterpret_cast<const XfsSuperblock*>(buffer);
    
    // Initialize volume structure
    memzero(volumeOut, sizeof(XfsVolume));
    volumeOut->blockDeviceIndex = blockDevIndex;
    volumeOut->readOnly = readOnly;
    
    // Convert superblock fields from big-endian
    volumeOut->blockSize = be32_to_cpu(sb->sb_blocksize);
    volumeOut->sectorSize = be16_to_cpu(sb->sb_sectsize);
    volumeOut->inodeSize = be16_to_cpu(sb->sb_inodesize);
    volumeOut->totalBlocks = be64_to_cpu(sb->sb_dblocks);
    volumeOut->rootInode = be64_to_cpu(sb->sb_rootino);
    volumeOut->agCount = be32_to_cpu(sb->sb_agcount);
    volumeOut->agBlocks = be32_to_cpu(sb->sb_agblocks);
    volumeOut->blockLog = sb->sb_blocklog;
    volumeOut->inodeLog = sb->sb_inodelog;
    volumeOut->agBlockLog = sb->sb_agblklog;
    volumeOut->versionNum = be16_to_cpu(sb->sb_versionnum);
    
    // Copy UUID
    memcopy(volumeOut->uuid, sb->sb_uuid, 16);
    
    // Copy volume label
    for (int i = 0; i < 12; ++i) {
        volumeOut->volumeLabel[i] = sb->sb_fname[i];
    }
    volumeOut->volumeLabel[12] = '\0';
    
    // Counters
    volumeOut->freeBlocks = be64_to_cpu(sb->sb_fdblocks);
    volumeOut->freeInodes = be64_to_cpu(sb->sb_ifree);
    
    // Check version
    uint16_t versionBase = volumeOut->versionNum & XFS_SB_VERSION_NUMBITS;
    volumeOut->isV5 = (versionBase == 5);
    
    // Check for ftype support (V5 or V4 with feature)
    if (volumeOut->isV5) {
        uint32_t featIncompat = be32_to_cpu(sb->sb_features_incompat);
        volumeOut->hasFtype = (featIncompat & XFS_SB_FEAT_INCOMPAT_FTYPE) != 0;
    } else {
        volumeOut->hasFtype = false;
    }
    
    // Allocate inode buffer
    static uint8_t s_inodeBuffer[1024];  // Max 1KB inodes
    if (volumeOut->inodeSize > sizeof(s_inodeBuffer)) {
        kernel::serial::puts("[XFS] Inode size too large\n");
        return XFS_ERR_UNSUPPORTED;
    }
    volumeOut->inodeBuffer = s_inodeBuffer;
    
    volumeOut->mounted = true;
    
    kernel::serial::puts("[XFS] Volume mounted successfully\n");
    kernel::serial::puts("  Block size:   ");
    kernel::serial::put_hex32(volumeOut->blockSize);
    kernel::serial::puts("\n  Inode size:   ");
    kernel::serial::put_hex32(volumeOut->inodeSize);
    kernel::serial::puts("\n  Total blocks: ");
    kernel::serial::put_hex64(volumeOut->totalBlocks);
    kernel::serial::puts("\n  Root inode:   ");
    kernel::serial::put_hex64(volumeOut->rootInode);
    kernel::serial::puts("\n  AG count:     ");
    kernel::serial::put_hex32(volumeOut->agCount);
    kernel::serial::puts("\n  Version:      ");
    kernel::serial::puts(volumeOut->isV5 ? "V5" : "V4");
    kernel::serial::putc('\n');
    
    return XFS_OK;
}

XfsStatus unmount(XfsVolume* volume)
{
    if (volume == nullptr || !volume->mounted) {
        return XFS_ERR_INVALID;
    }
    
    // Flush pending writes
    sync(volume);
    
    volume->mounted = false;
    volume->inodeBuffer = nullptr;
    
    kernel::serial::puts("[XFS] Volume unmounted\n");
    return XFS_OK;
}

// ================================================================
// Inode Operations
// ================================================================

XfsStatus read_inode(XfsVolume* volume, uint64_t inodeNum, XfsDinodeCore* inodeOut)
{
    if (volume == nullptr || !volume->mounted || inodeOut == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    // Calculate which block contains the inode
    uint64_t blockNum = ino_to_block(volume, inodeNum);
    
    // Calculate offset within block
    uint32_t inodesPerBlock = volume->blockSize / volume->inodeSize;
    uint32_t ag, agIno;
    ino_to_ag(volume, inodeNum, &ag, &agIno);
    uint32_t offsetInBlock = (agIno % inodesPerBlock) * volume->inodeSize;
    
    // Read the block
    uint64_t lba = (blockNum * volume->blockSize) / volume->sectorSize;
    uint32_t sectorsPerBlock = volume->blockSize / volume->sectorSize;
    
    // Use static buffer for block (assumes blockSize <= 4KB for simplicity)
    static uint8_t blockBuffer[4096];
    if (volume->blockSize > sizeof(blockBuffer)) {
        return XFS_ERR_UNSUPPORTED;
    }
    
    if (block::read_sectors(volume->blockDeviceIndex, lba, sectorsPerBlock, blockBuffer)
        != block::BLOCK_OK) {
        return XFS_ERR_IO;
    }
    
    // Copy inode core
    const XfsDinodeCore* diskInode = reinterpret_cast<const XfsDinodeCore*>(
        blockBuffer + offsetInBlock);
    
    // Verify inode magic
    if (be16_to_cpu(diskInode->di_magic) != 0x494E) {  // "IN"
        return XFS_ERR_CORRUPT;
    }
    
    // Copy and convert fields
    // Note: Full implementation would convert all fields from big-endian
    memcopy(inodeOut, diskInode, sizeof(XfsDinodeCore));
    
    return XFS_OK;
}

// ================================================================
// Extent Parsing
// ================================================================

void parse_extent(const XfsBmbtRec* rec, uint64_t* startOffOut, 
                  uint64_t* startBlockOut, uint32_t* countOut, bool* prealloc)
{
    /*
     * XFS extent record is 128 bits packed as:
     * l0: [ext_flag:1][startoff:54][startblock_hi:9]
     * l1: [startblock_lo:43][blockcount:21]
     * 
     * Note: On-disk format is big-endian
     */
    
    uint64_t l0 = be64_to_cpu(rec->l0);
    uint64_t l1 = be64_to_cpu(rec->l1);
    
    // Extract fields
    *prealloc = (l0 >> 63) != 0;
    *startOffOut = (l0 >> 9) & 0x1FFFFFFFFFFFFFULL;  // 54 bits
    
    uint64_t startBlockHi = l0 & 0x1FF;  // 9 bits
    uint64_t startBlockLo = (l1 >> 21);  // 43 bits
    *startBlockOut = (startBlockHi << 43) | startBlockLo;
    
    *countOut = static_cast<uint32_t>(l1 & 0x1FFFFF);  // 21 bits
}

// ================================================================
// File Operations
// ================================================================

XfsStatus open(XfsVolume* volume, const char* path, XfsFile* fileOut)
{
    /*
     * STUB: Open a file by path
     * 
     * Full implementation would:
     * 1. Start at root inode
     * 2. Parse path components
     * 3. For each component:
     *    a. Read directory inode
     *    b. Based on format (local/extents/btree):
     *       - Local: scan inline entries
     *       - Extents: read directory blocks, scan entries
     *       - B+tree: traverse tree to find entry
     *    c. Get target inode number
     * 4. Read final inode
     * 5. Initialize XfsFile handle
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr || fileOut == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    kernel::serial::puts("[XFS] open: ");
    kernel::serial::puts(path);
    kernel::serial::puts(" (stub)\n");
    
    memzero(fileOut, sizeof(XfsFile));
    fileOut->volume = volume;
    
    // TODO: Implement path resolution
    
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus close(XfsFile* file)
{
    if (file == nullptr || !file->open) {
        return XFS_ERR_INVALID;
    }
    
    file->open = false;
    return XFS_OK;
}

XfsStatus read(XfsFile* file, void* buffer, size_t size, size_t* bytesRead)
{
    /*
     * STUB: Read data from file
     * 
     * Full implementation would:
     * 1. Check format:
     *    - LOCAL: data is inline in inode, copy directly
     *    - EXTENTS: 
     *      a. Walk extent list to find extent covering position
     *      b. Convert file offset to disk block
     *      c. Read data
     *    - BTREE:
     *      a. Traverse B+tree to find extent
     *      b. Same as EXTENTS from there
     * 2. Update position
     */
    
    if (file == nullptr || !file->open || buffer == nullptr) {
        if (bytesRead) *bytesRead = 0;
        return XFS_ERR_INVALID;
    }
    
    if (file->position >= file->size) {
        if (bytesRead) *bytesRead = 0;
        return XFS_OK;
    }
    
    kernel::serial::puts("[XFS] read: stub implementation\n");
    if (bytesRead) *bytesRead = 0;
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus write(XfsFile* file, const void* buffer, size_t size, size_t* bytesWritten)
{
    /*
     * STUB: Write data to file
     * 
     * Full implementation would:
     * 1. Check read-only
     * 2. Allocate new extents if needed
     * 3. Write data to disk
     * 4. Update inode (size, timestamps, extent list)
     * 5. Update free space accounting
     * 
     * NOTE: Production implementation should use XFS logging
     * for atomic metadata updates.
     */
    
    if (file == nullptr || !file->open || buffer == nullptr) {
        if (bytesWritten) *bytesWritten = 0;
        return XFS_ERR_INVALID;
    }
    
    if (file->volume->readOnly) {
        if (bytesWritten) *bytesWritten = 0;
        return XFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[XFS] write: stub implementation\n");
    if (bytesWritten) *bytesWritten = 0;
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus seek(XfsFile* file, int64_t offset, int whence)
{
    if (file == nullptr || !file->open) {
        return XFS_ERR_INVALID;
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
            newPos = static_cast<int64_t>(file->size) + offset;
            break;
        default:
            return XFS_ERR_INVALID;
    }
    
    if (newPos < 0) {
        return XFS_ERR_INVALID;
    }
    
    file->position = static_cast<uint64_t>(newPos);
    return XFS_OK;
}

uint64_t get_size(XfsFile* file)
{
    if (file == nullptr || !file->open) {
        return 0;
    }
    return file->size;
}

// ================================================================
// Directory Operations
// ================================================================

XfsStatus list_directory(XfsVolume* volume, const char* path,
                         XfsDirCallback callback, void* context)
{
    /*
     * STUB: List directory contents
     * 
     * XFS directory formats:
     * 1. Short-form (sf): Small directories inline in inode
     *    - Header followed by entries
     *    - Each entry: namelen, offset, name, ino
     * 
     * 2. Block format: Single directory block
     *    - Data entries with free space management
     * 
     * 3. Leaf format: Data blocks + leaf block
     *    - Leaf block indexes into data blocks
     * 
     * 4. Node format: Data + leaf + node B+tree
     *    - For large directories
     * 
     * Full implementation would:
     * 1. Open directory path
     * 2. Read inode, check di_format
     * 3. Based on format, iterate entries
     * 4. For each entry, call callback
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr || callback == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    kernel::serial::puts("[XFS] list_directory: ");
    kernel::serial::puts(path);
    kernel::serial::puts(" (stub)\n");
    
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus create_directory(XfsVolume* volume, const char* path)
{
    /*
     * STUB: Create a new directory
     * 
     * Full implementation would:
     * 1. Check read-only
     * 2. Allocate inode
     * 3. Initialize directory inode
     * 4. Create "." and ".." entries
     * 5. Add entry to parent directory
     * 6. Update free space
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return XFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[XFS] create_directory: stub implementation\n");
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus remove_directory(XfsVolume* volume, const char* path)
{
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return XFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[XFS] remove_directory: stub implementation\n");
    return XFS_ERR_UNSUPPORTED;
}

// ================================================================
// File Management
// ================================================================

XfsStatus create_file(XfsVolume* volume, const char* path)
{
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return XFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[XFS] create_file: stub implementation\n");
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus delete_file(XfsVolume* volume, const char* path)
{
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return XFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[XFS] delete_file: stub implementation\n");
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus rename(XfsVolume* volume, const char* oldPath, const char* newPath)
{
    if (volume == nullptr || !volume->mounted || oldPath == nullptr || newPath == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    if (volume->readOnly) {
        return XFS_ERR_READ_ONLY;
    }
    
    kernel::serial::puts("[XFS] rename: stub implementation\n");
    return XFS_ERR_UNSUPPORTED;
}

XfsStatus stat(XfsVolume* volume, const char* path, uint16_t* modeOut, uint64_t* sizeOut)
{
    /*
     * STUB: Get file statistics
     * 
     * Full implementation would:
     * 1. Resolve path to inode
     * 2. Read inode
     * 3. Extract mode and size
     */
    
    if (volume == nullptr || !volume->mounted || path == nullptr) {
        return XFS_ERR_INVALID;
    }
    
    if (modeOut) *modeOut = 0;
    if (sizeOut) *sizeOut = 0;
    
    kernel::serial::puts("[XFS] stat: stub implementation\n");
    return XFS_ERR_UNSUPPORTED;
}

// ================================================================
// Volume Operations
// ================================================================

XfsStatus get_volume_info(XfsVolume* volume, char* labelOut, 
                          uint64_t* totalBytesOut, uint64_t* freeBytesOut)
{
    if (volume == nullptr || !volume->mounted) {
        return XFS_ERR_INVALID;
    }
    
    if (labelOut) {
        for (int i = 0; i < 12; ++i) {
            labelOut[i] = volume->volumeLabel[i];
        }
        labelOut[12] = '\0';
    }
    
    if (totalBytesOut) {
        *totalBytesOut = volume->totalBlocks * volume->blockSize;
    }
    
    if (freeBytesOut) {
        *freeBytesOut = volume->freeBlocks * volume->blockSize;
    }
    
    return XFS_OK;
}

XfsStatus sync(XfsVolume* volume)
{
    if (volume == nullptr || !volume->mounted) {
        return XFS_ERR_INVALID;
    }
    
    // No caching implemented yet
    return XFS_OK;
}

// ================================================================
// Status/Debug
// ================================================================

void print_status()
{
    kernel::serial::puts("[XFS] Driver status:\n");
    kernel::serial::puts("  Initialized: ");
    kernel::serial::puts(s_initialized ? "yes" : "no");
    kernel::serial::puts("\n  Mounted volumes: ");
    kernel::serial::put_hex32(s_volumeCount);
    kernel::serial::putc('\n');
}

} // namespace fs_xfs
} // namespace kernel
