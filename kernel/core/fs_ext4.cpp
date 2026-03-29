// ext2 / ext4 Filesystem Driver — Implementation
//
// Reads the superblock at offset 1024, locates the block group
// descriptor table, resolves inodes, and supports both indirect-
// block (ext2) and extent-tree (ext4) allocation.
//
// Read-only for now.  Write support would require journal replay
// and metadata checksum updates.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/fs_ext4.h"
#include "include/kernel/block_device.h"

namespace kernel {
namespace fs_ext4 {

// ================================================================
// Internal state
// ================================================================

static Ext4Volume s_volumes[MAX_EXT4_VOLUMES];
static Ext4File   s_files[MAX_EXT4_FILES];
static uint8_t    s_volumeCount = 0;

// Sector / block buffer (up to 4096 bytes)
static uint8_t    s_blkBuf[4096];

// Directory iteration state
static struct {
    bool     active;
    uint8_t  volIdx;
    Inode    dirInode;
    uint32_t offset;          // byte offset within directory data
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

// ================================================================
// Block I/O: read one filesystem block from the volume
// ================================================================

static block::Status read_block(const Ext4Volume& vol,
                                uint64_t blockNum,
                                void* buffer)
{
    uint32_t sectorsPerBlock = vol.blockSize / 512;
    uint64_t lba = blockNum * sectorsPerBlock;
    return block::read_sectors(vol.blockDevIndex, lba, sectorsPerBlock, buffer);
}

// ================================================================
// Inode lookup
// ================================================================

static bool read_inode(const Ext4Volume& vol, uint32_t inodeNum, Inode* out)
{
    if (inodeNum == 0) return false;
    uint32_t group = (inodeNum - 1) / vol.inodesPerGroup;
    uint32_t index = (inodeNum - 1) % vol.inodesPerGroup;

    // Read the block group descriptor
    uint32_t bgdtBytesPerEntry = 32; // ext2 size; ext4 64-bit uses s_desc_size
    uint32_t bgdtOffset = group * bgdtBytesPerEntry;
    uint32_t bgdtBlock  = vol.bgdtBlock + (bgdtOffset / vol.blockSize);
    uint32_t bgdtOff    = bgdtOffset % vol.blockSize;

    if (read_block(vol, bgdtBlock, s_blkBuf) != block::BLOCK_OK)
        return false;

    const BlockGroupDesc* bgd = reinterpret_cast<const BlockGroupDesc*>(
        &s_blkBuf[bgdtOff]);

    uint64_t inodeTableBlock = bgd->bg_inode_table_lo;

    // Calculate which block of the inode table, and offset within it
    uint32_t inodeOffset = index * vol.inodeSize;
    uint64_t inodeBlock  = inodeTableBlock + (inodeOffset / vol.blockSize);
    uint32_t inodeOff    = inodeOffset % vol.blockSize;

    if (read_block(vol, inodeBlock, s_blkBuf) != block::BLOCK_OK)
        return false;

    memcopy(out, &s_blkBuf[inodeOff], sizeof(Inode));
    return true;
}

// ================================================================
// ext2 indirect-block data block resolution
//
// i_block[0..11]  ? direct blocks
// i_block[12]     ? single indirect
// i_block[13]     ? double indirect
// i_block[14]     ? triple indirect
// ================================================================

static uint64_t resolve_indirect_block(const Ext4Volume& vol,
                                       const Inode& inode,
                                       uint32_t logicalBlock)
{
    uint32_t ptrsPerBlock = vol.blockSize / 4;

    // Direct blocks (0-11)
    if (logicalBlock < 12) {
        return inode.i_block[logicalBlock];
    }

    logicalBlock -= 12;

    // Single indirect (12 .. 12 + ptrs - 1)
    if (logicalBlock < ptrsPerBlock) {
        if (read_block(vol, inode.i_block[12], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint32_t* ptrs = reinterpret_cast<const uint32_t*>(s_blkBuf);
        return ptrs[logicalBlock];
    }

    logicalBlock -= ptrsPerBlock;

    // Double indirect
    if (logicalBlock < ptrsPerBlock * ptrsPerBlock) {
        if (read_block(vol, inode.i_block[13], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint32_t* l1 = reinterpret_cast<const uint32_t*>(s_blkBuf);
        uint32_t l1idx = logicalBlock / ptrsPerBlock;
        uint32_t l2idx = logicalBlock % ptrsPerBlock;

        uint32_t l1block = l1[l1idx];
        if (read_block(vol, l1block, s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint32_t* l2 = reinterpret_cast<const uint32_t*>(s_blkBuf);
        return l2[l2idx];
    }

    logicalBlock -= ptrsPerBlock * ptrsPerBlock;

    // Triple indirect
    if (read_block(vol, inode.i_block[14], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint32_t* t1 = reinterpret_cast<const uint32_t*>(s_blkBuf);
    uint32_t t1idx = logicalBlock / (ptrsPerBlock * ptrsPerBlock);
    uint32_t rem   = logicalBlock % (ptrsPerBlock * ptrsPerBlock);

    uint32_t t1block = t1[t1idx];
    if (read_block(vol, t1block, s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint32_t* t2 = reinterpret_cast<const uint32_t*>(s_blkBuf);
    uint32_t t2idx = rem / ptrsPerBlock;
    uint32_t t3idx = rem % ptrsPerBlock;

    uint32_t t2block = t2[t2idx];
    if (read_block(vol, t2block, s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint32_t* t3 = reinterpret_cast<const uint32_t*>(s_blkBuf);
    return t3[t3idx];
}

// ================================================================
// ext4 extent-tree data block resolution
// ================================================================

static uint64_t resolve_extent_block(const Ext4Volume& vol,
                                     const Inode& inode,
                                     uint32_t logicalBlock)
{
    // The extent tree root is stored in i_block[0..14] (60 bytes)
    const ExtentHeader* hdr = reinterpret_cast<const ExtentHeader*>(inode.i_block);
    if (hdr->eh_magic != EXTENT_MAGIC) return 0;

    // Walk the tree down from the root
    const uint8_t* node = reinterpret_cast<const uint8_t*>(inode.i_block);

    uint16_t depth = hdr->eh_depth;

    while (depth > 0) {
        const ExtentHeader* h = reinterpret_cast<const ExtentHeader*>(node);
        const ExtentIdx* idx = reinterpret_cast<const ExtentIdx*>(
            node + sizeof(ExtentHeader));
        uint16_t entries = h->eh_entries;

        // Find the index entry covering logicalBlock
        uint16_t chosen = 0;
        for (uint16_t i = 1; i < entries; ++i) {
            if (idx[i].ei_block <= logicalBlock) chosen = i;
            else break;
        }

        uint64_t childBlock = (static_cast<uint64_t>(idx[chosen].ei_leaf_hi) << 32) |
                              idx[chosen].ei_leaf_lo;

        if (read_block(vol, childBlock, s_blkBuf) != block::BLOCK_OK)
            return 0;

        node = s_blkBuf;
        --depth;
    }

    // Leaf level: scan extents
    const ExtentHeader* lh = reinterpret_cast<const ExtentHeader*>(node);
    const Extent* ext = reinterpret_cast<const Extent*>(
        node + sizeof(ExtentHeader));
    uint16_t entries = lh->eh_entries;

    for (uint16_t i = 0; i < entries; ++i) {
        if (logicalBlock >= ext[i].ee_block &&
            logicalBlock < ext[i].ee_block + ext[i].ee_len) {
            uint32_t offset = logicalBlock - ext[i].ee_block;
            uint64_t phys = (static_cast<uint64_t>(ext[i].ee_start_hi) << 32) |
                            ext[i].ee_start_lo;
            return phys + offset;
        }
    }
    return 0;
}

// ================================================================
// Resolve logical block to physical block
// ================================================================

static uint64_t resolve_block(const Ext4Volume& vol,
                              const Inode& inode,
                              uint32_t logicalBlock)
{
    if (vol.hasExtents && (inode.i_flags & EXT4_EXTENTS_FL)) {
        return resolve_extent_block(vol, inode, logicalBlock);
    }
    return resolve_indirect_block(vol, inode, logicalBlock);
}

// ================================================================
// Read data from an inode at a byte offset
// ================================================================

static uint32_t read_inode_data(const Ext4Volume& vol,
                                const Inode& inode,
                                uint32_t offset,
                                void* buffer,
                                uint32_t len)
{
    uint64_t fileSize = inode.i_size_lo |
                        (static_cast<uint64_t>(inode.i_size_hi) << 32);
    if (offset >= fileSize) return 0;

    uint32_t maxRead = static_cast<uint32_t>(fileSize - offset);
    if (len > maxRead) len = maxRead;

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t bytesRead = 0;

    while (bytesRead < len) {
        uint32_t curOffset    = offset + bytesRead;
        uint32_t logicalBlock = curOffset / vol.blockSize;
        uint32_t blockOffset  = curOffset % vol.blockSize;

        uint64_t physBlock = resolve_block(vol, inode, logicalBlock);
        if (physBlock == 0) break;

        if (read_block(vol, physBlock, s_blkBuf) != block::BLOCK_OK)
            break;

        uint32_t available = vol.blockSize - blockOffset;
        uint32_t toCopy    = len - bytesRead;
        if (toCopy > available) toCopy = available;

        memcopy(dst + bytesRead, &s_blkBuf[blockOffset], toCopy);
        bytesRead += toCopy;
    }
    return bytesRead;
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
    if (s_volumeCount >= MAX_EXT4_VOLUMES) return 0xFF;

    uint8_t idx = 0xFF;
    for (uint8_t i = 0; i < MAX_EXT4_VOLUMES; ++i) {
        if (!s_volumes[i].mounted) { idx = i; break; }
    }
    if (idx == 0xFF) return 0xFF;

    // The superblock starts at byte offset 1024 (sector 2 for 512-byte sectors)
    block::Status st = block::read_sectors(blockDevIndex, 2, 2, s_blkBuf);
    if (st != block::BLOCK_OK) return 0xFF;

    const Superblock* sb = reinterpret_cast<const Superblock*>(s_blkBuf);

    if (sb->s_magic != EXT_MAGIC) return 0xFF;

    Ext4Volume& vol = s_volumes[idx];
    memzero(&vol, sizeof(vol));

    vol.mounted        = true;
    vol.blockDevIndex  = blockDevIndex;
    vol.blockSize      = 1024u << sb->s_log_block_size;
    vol.inodeSize      = (sb->s_rev_level >= 1) ? sb->s_inode_size : 128;
    vol.inodesPerGroup = sb->s_inodes_per_group;
    vol.blocksPerGroup = sb->s_blocks_per_group;
    vol.firstDataBlock = sb->s_first_data_block;

    vol.has64bit    = (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) != 0;
    vol.hasExtents  = (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS) != 0;

    // Block group descriptor table starts in the block after the superblock
    // For block size 1024: block 2.  For block size ? 2048: block 1.
    vol.bgdtBlock = (vol.blockSize == 1024) ? 2 : 1;

    // Number of block groups
    uint64_t totalBlocks = sb->s_blocks_count_lo |
                           (static_cast<uint64_t>(sb->s_blocks_count_hi) << 32);
    vol.groupCount = static_cast<uint32_t>(
        (totalBlocks + vol.blocksPerGroup - 1) / vol.blocksPerGroup);

    memcopy(vol.volumeName, sb->s_volume_name, 16);
    vol.volumeName[16] = '\0';

    ++s_volumeCount;
    return idx;
}

void unmount(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_EXT4_VOLUMES) return;
    if (!s_volumes[volumeIndex].mounted) return;

    for (uint8_t i = 0; i < MAX_EXT4_FILES; ++i) {
        if (s_files[i].open && s_files[i].volumeIndex == volumeIndex)
            s_files[i].open = false;
    }

    s_volumes[volumeIndex].mounted = false;
    if (s_volumeCount > 0) --s_volumeCount;
}

bool open_root_dir(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_EXT4_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;

    if (!read_inode(s_volumes[volumeIndex], EXT_ROOT_INO, &s_dirIter.dirInode))
        return false;

    s_dirIter.active = true;
    s_dirIter.volIdx = volumeIndex;
    s_dirIter.offset = 0;
    return true;
}

bool read_dir(uint8_t volumeIndex, Ext4DirEntry* out)
{
    if (!s_dirIter.active || s_dirIter.volIdx != volumeIndex) return false;

    Ext4Volume& vol = s_volumes[volumeIndex];
    uint64_t dirSize = s_dirIter.dirInode.i_size_lo;

    while (s_dirIter.offset < dirSize) {
        // Read a chunk of directory data
        uint8_t entBuf[264];
        uint32_t rd = read_inode_data(vol, s_dirIter.dirInode,
                                      s_dirIter.offset, entBuf, sizeof(entBuf));
        if (rd < 8) {
            s_dirIter.active = false;
            return false;
        }

        const DirEntry2* de = reinterpret_cast<const DirEntry2*>(entBuf);
        if (de->rec_len == 0) {
            s_dirIter.active = false;
            return false;
        }

        s_dirIter.offset += de->rec_len;

        if (de->inode == 0) continue; // deleted entry

        // Skip "." and ".."
        if (de->name_len == 1 && de->name[0] == '.') continue;
        if (de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.') continue;

        memzero(out, sizeof(Ext4DirEntry));
        uint8_t nameLen = de->name_len;
        if (nameLen > 255) nameLen = 255;
        memcopy(out->name, de->name, nameLen);
        out->name[nameLen] = '\0';
        out->inodeNum = de->inode;
        out->fileType = de->file_type;
        out->isDir    = (de->file_type == EXT4_FT_DIR);
        return true;
    }

    s_dirIter.active = false;
    return false;
}

uint8_t open_file(uint8_t volumeIndex, uint32_t inodeNum)
{
    if (volumeIndex >= MAX_EXT4_VOLUMES) return 0xFF;
    if (!s_volumes[volumeIndex].mounted) return 0xFF;

    for (uint8_t i = 0; i < MAX_EXT4_FILES; ++i) {
        if (!s_files[i].open) {
            s_files[i].open          = true;
            s_files[i].volumeIndex   = volumeIndex;
            s_files[i].inodeNum      = inodeNum;
            s_files[i].currentOffset = 0;

            if (!read_inode(s_volumes[volumeIndex], inodeNum, &s_files[i].inode)) {
                s_files[i].open = false;
                return 0xFF;
            }
            return i;
        }
    }
    return 0xFF;
}

uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len)
{
    if (fileHandle >= MAX_EXT4_FILES) return 0;
    Ext4File& f = s_files[fileHandle];
    if (!f.open) return 0;

    Ext4Volume& vol = s_volumes[f.volumeIndex];
    uint32_t rd = read_inode_data(vol, f.inode, f.currentOffset, buffer, len);
    f.currentOffset += rd;
    return rd;
}

void close_file(uint8_t fileHandle)
{
    if (fileHandle >= MAX_EXT4_FILES) return;
    s_files[fileHandle].open = false;
}

const Ext4Volume* get_volume(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_EXT4_VOLUMES) return nullptr;
    if (!s_volumes[volumeIndex].mounted) return nullptr;
    return &s_volumes[volumeIndex];
}

} // namespace fs_ext4
} // namespace kernel
