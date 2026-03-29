// UFS (Unix File System) Driver — Implementation
//
// Reads the superblock at byte offset 8192, locates cylinder
// group descriptors, resolves inodes, and traverses direct /
// indirect block pointers for file I/O.
//
// Supports both UFS1 (32-bit block pointers) and UFS2 (64-bit).
// Read-only.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/fs_ufs.h"
#include "include/kernel/block_device.h"

namespace kernel {
namespace fs_ufs {

// ================================================================
// Internal state
// ================================================================

static UFSVolume s_volumes[MAX_UFS_VOLUMES];
static UFSFile   s_files[MAX_UFS_FILES];
static uint8_t   s_volumeCount = 0;

// Block buffer (up to 8192 bytes for large UFS blocks)
static uint8_t   s_blkBuf[8192];

// Directory iteration state
static struct {
    bool     active;
    uint8_t  volIdx;
    UFSType  inodeType;
    UFS1_Inode dirInode1;
    UFS2_Inode dirInode2;
    uint32_t offset;
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
// Fragment-based I/O
//
// UFS addresses storage in fragments (fs_fsize, typically 1024).
// A block = fs_frag fragments.
// ================================================================

static block::Status read_frag(const UFSVolume& vol,
                               uint64_t fragNum,
                               void* buffer)
{
    // Convert fragment number to LBA (512-byte sectors)
    uint32_t sectorsPerFrag = vol.fragSize / 512;
    uint64_t lba = fragNum * sectorsPerFrag;
    return block::read_sectors(vol.blockDevIndex, lba, sectorsPerFrag, buffer);
}

static block::Status read_block_full(const UFSVolume& vol,
                                     uint64_t blockNum,
                                     void* buffer)
{
    // A UFS block = fragsPerBlock consecutive fragments
    uint64_t fragNum = blockNum * vol.fragsPerBlock;
    uint32_t sectorsPerBlock = vol.blockSize / 512;
    uint64_t lba = fragNum * (vol.fragSize / 512);
    return block::read_sectors(vol.blockDevIndex, lba, sectorsPerBlock, buffer);
}

// ================================================================
// Inode lookup — UFS1
// ================================================================

static bool read_inode_ufs1(const UFSVolume& vol, uint32_t ino, UFS1_Inode* out)
{
    if (ino == 0) return false;

    uint32_t cg    = (ino - 1) / vol.inodesPerGroup;  // which cylinder group
    uint32_t index = (ino - 1) % vol.inodesPerGroup;

    // Inode table starts at fragment (cg * fragsPerGroup + inodeBlockOffset)
    uint64_t inodeTableFrag = static_cast<uint64_t>(cg) * vol.fragsPerGroup +
                              vol.inodeBlockOffset;

    // Each UFS1 inode is 128 bytes
    uint32_t inodeOffset = index * 128;
    uint32_t fragInTable = inodeOffset / vol.fragSize;
    uint32_t offsetInFrag = inodeOffset % vol.fragSize;

    if (read_frag(vol, inodeTableFrag + fragInTable, s_blkBuf) != block::BLOCK_OK)
        return false;

    memcopy(out, &s_blkBuf[offsetInFrag], sizeof(UFS1_Inode));
    return true;
}

// ================================================================
// Inode lookup — UFS2
// ================================================================

static bool read_inode_ufs2(const UFSVolume& vol, uint32_t ino, UFS2_Inode* out)
{
    if (ino == 0) return false;

    uint32_t cg    = (ino - 1) / vol.inodesPerGroup;
    uint32_t index = (ino - 1) % vol.inodesPerGroup;

    uint64_t inodeTableFrag = static_cast<uint64_t>(cg) * vol.fragsPerGroup +
                              vol.inodeBlockOffset;

    // Each UFS2 inode is 256 bytes
    uint32_t inodeOffset = index * 256;
    uint32_t fragInTable = inodeOffset / vol.fragSize;
    uint32_t offsetInFrag = inodeOffset % vol.fragSize;

    if (read_frag(vol, inodeTableFrag + fragInTable, s_blkBuf) != block::BLOCK_OK)
        return false;

    memcopy(out, &s_blkBuf[offsetInFrag], sizeof(UFS2_Inode));
    return true;
}

// ================================================================
// Block resolution — UFS1 (32-bit pointers)
// ================================================================

static uint64_t resolve_block_ufs1(const UFSVolume& vol,
                                   const UFS1_Inode& inode,
                                   uint32_t logicalBlock)
{
    uint32_t ptrsPerBlock = vol.blockSize / 4;

    if (logicalBlock < 12) {
        return inode.di_db[logicalBlock];
    }
    logicalBlock -= 12;

    // Single indirect
    if (logicalBlock < ptrsPerBlock) {
        if (inode.di_ib[0] == 0) return 0;
        if (read_block_full(vol, inode.di_ib[0], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint32_t* ptrs = reinterpret_cast<const uint32_t*>(s_blkBuf);
        return ptrs[logicalBlock];
    }
    logicalBlock -= ptrsPerBlock;

    // Double indirect
    if (logicalBlock < ptrsPerBlock * ptrsPerBlock) {
        if (inode.di_ib[1] == 0) return 0;
        if (read_block_full(vol, inode.di_ib[1], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint32_t* l1 = reinterpret_cast<const uint32_t*>(s_blkBuf);
        uint32_t l1idx = logicalBlock / ptrsPerBlock;
        uint32_t l2idx = logicalBlock % ptrsPerBlock;
        if (l1[l1idx] == 0) return 0;
        if (read_block_full(vol, l1[l1idx], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint32_t* l2 = reinterpret_cast<const uint32_t*>(s_blkBuf);
        return l2[l2idx];
    }
    logicalBlock -= ptrsPerBlock * ptrsPerBlock;

    // Triple indirect
    if (inode.di_ib[2] == 0) return 0;
    if (read_block_full(vol, inode.di_ib[2], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint32_t* t1 = reinterpret_cast<const uint32_t*>(s_blkBuf);
    uint32_t t1idx = logicalBlock / (ptrsPerBlock * ptrsPerBlock);
    uint32_t rem   = logicalBlock % (ptrsPerBlock * ptrsPerBlock);
    if (t1[t1idx] == 0) return 0;
    if (read_block_full(vol, t1[t1idx], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint32_t* t2 = reinterpret_cast<const uint32_t*>(s_blkBuf);
    uint32_t t2idx = rem / ptrsPerBlock;
    uint32_t t3idx = rem % ptrsPerBlock;
    if (t2[t2idx] == 0) return 0;
    if (read_block_full(vol, t2[t2idx], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint32_t* t3 = reinterpret_cast<const uint32_t*>(s_blkBuf);
    return t3[t3idx];
}

// ================================================================
// Block resolution — UFS2 (64-bit pointers)
// ================================================================

static uint64_t resolve_block_ufs2(const UFSVolume& vol,
                                   const UFS2_Inode& inode,
                                   uint32_t logicalBlock)
{
    uint32_t ptrsPerBlock = vol.blockSize / 8;

    if (logicalBlock < 12) {
        return inode.di_db[logicalBlock];
    }
    logicalBlock -= 12;

    if (logicalBlock < ptrsPerBlock) {
        if (inode.di_ib[0] == 0) return 0;
        if (read_block_full(vol, inode.di_ib[0], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint64_t* ptrs = reinterpret_cast<const uint64_t*>(s_blkBuf);
        return ptrs[logicalBlock];
    }
    logicalBlock -= ptrsPerBlock;

    if (logicalBlock < ptrsPerBlock * ptrsPerBlock) {
        if (inode.di_ib[1] == 0) return 0;
        if (read_block_full(vol, inode.di_ib[1], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint64_t* l1 = reinterpret_cast<const uint64_t*>(s_blkBuf);
        uint32_t l1idx = logicalBlock / ptrsPerBlock;
        uint32_t l2idx = logicalBlock % ptrsPerBlock;
        if (l1[l1idx] == 0) return 0;
        if (read_block_full(vol, l1[l1idx], s_blkBuf) != block::BLOCK_OK)
            return 0;
        const uint64_t* l2 = reinterpret_cast<const uint64_t*>(s_blkBuf);
        return l2[l2idx];
    }
    logicalBlock -= ptrsPerBlock * ptrsPerBlock;

    if (inode.di_ib[2] == 0) return 0;
    if (read_block_full(vol, inode.di_ib[2], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint64_t* t1 = reinterpret_cast<const uint64_t*>(s_blkBuf);
    uint32_t t1idx = logicalBlock / (ptrsPerBlock * ptrsPerBlock);
    uint32_t rem   = logicalBlock % (ptrsPerBlock * ptrsPerBlock);
    if (t1[t1idx] == 0) return 0;
    if (read_block_full(vol, t1[t1idx], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint64_t* t2 = reinterpret_cast<const uint64_t*>(s_blkBuf);
    uint32_t t2idx = rem / ptrsPerBlock;
    uint32_t t3idx = rem % ptrsPerBlock;
    if (t2[t2idx] == 0) return 0;
    if (read_block_full(vol, t2[t2idx], s_blkBuf) != block::BLOCK_OK)
        return 0;
    const uint64_t* t3 = reinterpret_cast<const uint64_t*>(s_blkBuf);
    return t3[t3idx];
}

// ================================================================
// Read data from an inode — UFS1
// ================================================================

static uint32_t read_inode_data_ufs1(const UFSVolume& vol,
                                     const UFS1_Inode& inode,
                                     uint32_t offset,
                                     void* buffer,
                                     uint32_t len)
{
    uint64_t fileSize = inode.di_size;
    if (offset >= fileSize) return 0;

    uint32_t maxRead = static_cast<uint32_t>(fileSize - offset);
    if (len > maxRead) len = maxRead;

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t bytesRead = 0;

    while (bytesRead < len) {
        uint32_t curOff       = offset + bytesRead;
        uint32_t logicalBlock = curOff / vol.blockSize;
        uint32_t blockOffset  = curOff % vol.blockSize;

        uint64_t physBlock = resolve_block_ufs1(vol, inode, logicalBlock);
        if (physBlock == 0) break;

        if (read_block_full(vol, physBlock, s_blkBuf) != block::BLOCK_OK) break;

        uint32_t avail  = vol.blockSize - blockOffset;
        uint32_t toCopy = len - bytesRead;
        if (toCopy > avail) toCopy = avail;

        memcopy(dst + bytesRead, &s_blkBuf[blockOffset], toCopy);
        bytesRead += toCopy;
    }
    return bytesRead;
}

// ================================================================
// Read data from an inode — UFS2
// ================================================================

static uint32_t read_inode_data_ufs2(const UFSVolume& vol,
                                     const UFS2_Inode& inode,
                                     uint32_t offset,
                                     void* buffer,
                                     uint32_t len)
{
    uint64_t fileSize = inode.di_size;
    if (offset >= fileSize) return 0;

    uint32_t maxRead = static_cast<uint32_t>(fileSize - offset);
    if (len > maxRead) len = maxRead;

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t bytesRead = 0;

    while (bytesRead < len) {
        uint32_t curOff       = offset + bytesRead;
        uint32_t logicalBlock = curOff / vol.blockSize;
        uint32_t blockOffset  = curOff % vol.blockSize;

        uint64_t physBlock = resolve_block_ufs2(vol, inode, logicalBlock);
        if (physBlock == 0) break;

        if (read_block_full(vol, physBlock, s_blkBuf) != block::BLOCK_OK) break;

        uint32_t avail  = vol.blockSize - blockOffset;
        uint32_t toCopy = len - bytesRead;
        if (toCopy > avail) toCopy = avail;

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
    if (s_volumeCount >= MAX_UFS_VOLUMES) return 0xFF;

    uint8_t idx = 0xFF;
    for (uint8_t i = 0; i < MAX_UFS_VOLUMES; ++i) {
        if (!s_volumes[i].mounted) { idx = i; break; }
    }
    if (idx == 0xFF) return 0xFF;

    // UFS superblock is at byte offset 8192 = sector 16
    block::Status st = block::read_sectors(blockDevIndex, 16, 4, s_blkBuf);
    if (st != block::BLOCK_OK) return 0xFF;

    // Try UFS1 first — magic is at a well-known offset within the superblock
    // For the simplified structure, check the fs_magic field
    const UFS1_Superblock* sb1 = reinterpret_cast<const UFS1_Superblock*>(s_blkBuf);

    UFSVolume& vol = s_volumes[idx];
    memzero(&vol, sizeof(vol));

    if (sb1->fs_magic == UFS1_MAGIC) {
        vol.type             = UFS_TYPE_UFS1;
        vol.mounted          = true;
        vol.blockDevIndex    = blockDevIndex;
        vol.blockSize        = sb1->fs_bsize;
        vol.fragSize         = sb1->fs_fsize;
        vol.fragsPerBlock    = sb1->fs_frag;
        vol.inodesPerGroup   = sb1->fs_ipg;
        vol.fragsPerGroup    = sb1->fs_fpg;
        vol.numGroups        = sb1->fs_ncg;
        vol.inodeBlockOffset = sb1->fs_iblkno;
        vol.dataBlockOffset  = sb1->fs_dblkno;
        vol.inopb            = sb1->fs_inopb;
        vol.cgOffset         = sb1->fs_cgoffset;
        ++s_volumeCount;
        return idx;
    }

    // Try UFS2 — the magic number is at offset 1372 in the superblock
    // (UFS2 places magic at the same field but with a different value)
    // For simplicity, scan first 2048 bytes for UFS2_MAGIC
    bool foundUFS2 = false;
    for (uint32_t off = 0; off < 2048 - 4; off += 4) {
        uint32_t val = *reinterpret_cast<uint32_t*>(&s_blkBuf[off]);
        if (val == UFS2_MAGIC) { foundUFS2 = true; break; }
    }

    if (foundUFS2) {
        // Use UFS1 superblock structure to read common fields
        // (UFS2 shares layout for these fields)
        vol.type             = UFS_TYPE_UFS2;
        vol.mounted          = true;
        vol.blockDevIndex    = blockDevIndex;
        vol.blockSize        = sb1->fs_bsize;
        vol.fragSize         = sb1->fs_fsize;
        vol.fragsPerBlock    = sb1->fs_frag;
        vol.inodesPerGroup   = sb1->fs_ipg;
        vol.fragsPerGroup    = sb1->fs_fpg;
        vol.numGroups        = sb1->fs_ncg;
        vol.inodeBlockOffset = sb1->fs_iblkno;
        vol.dataBlockOffset  = sb1->fs_dblkno;
        vol.inopb            = sb1->fs_inopb;
        vol.cgOffset         = sb1->fs_cgoffset;
        ++s_volumeCount;
        return idx;
    }

    return 0xFF;
}

void unmount(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_UFS_VOLUMES) return;
    if (!s_volumes[volumeIndex].mounted) return;

    for (uint8_t i = 0; i < MAX_UFS_FILES; ++i) {
        if (s_files[i].open && s_files[i].volumeIndex == volumeIndex)
            s_files[i].open = false;
    }

    s_volumes[volumeIndex].mounted = false;
    if (s_volumeCount > 0) --s_volumeCount;
}

bool open_root_dir(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_UFS_VOLUMES) return false;
    if (!s_volumes[volumeIndex].mounted) return false;

    UFSVolume& vol = s_volumes[volumeIndex];
    s_dirIter.active    = true;
    s_dirIter.volIdx    = volumeIndex;
    s_dirIter.offset    = 0;
    s_dirIter.inodeType = vol.type;

    if (vol.type == UFS_TYPE_UFS1) {
        if (!read_inode_ufs1(vol, UFS_ROOT_INO, &s_dirIter.dirInode1))
            return false;
    } else {
        if (!read_inode_ufs2(vol, UFS_ROOT_INO, &s_dirIter.dirInode2))
            return false;
    }
    return true;
}

bool read_dir(uint8_t volumeIndex, DirEntry* out)
{
    if (!s_dirIter.active || s_dirIter.volIdx != volumeIndex) return false;

    UFSVolume& vol = s_volumes[volumeIndex];
    uint64_t dirSize = (s_dirIter.inodeType == UFS_TYPE_UFS1)
        ? s_dirIter.dirInode1.di_size
        : s_dirIter.dirInode2.di_size;

    while (s_dirIter.offset < dirSize) {
        uint8_t entBuf[264];
        uint32_t rd;
        if (s_dirIter.inodeType == UFS_TYPE_UFS1) {
            rd = read_inode_data_ufs1(vol, s_dirIter.dirInode1,
                                      s_dirIter.offset, entBuf, sizeof(entBuf));
        } else {
            rd = read_inode_data_ufs2(vol, s_dirIter.dirInode2,
                                      s_dirIter.offset, entBuf, sizeof(entBuf));
        }
        if (rd < 8) {
            s_dirIter.active = false;
            return false;
        }

        const UFSDirEntry* de = reinterpret_cast<const UFSDirEntry*>(entBuf);
        if (de->d_reclen == 0) {
            s_dirIter.active = false;
            return false;
        }

        s_dirIter.offset += de->d_reclen;

        if (de->d_ino == 0) continue;

        // Skip "." and ".."
        if (de->d_namlen == 1 && de->d_name[0] == '.') continue;
        if (de->d_namlen == 2 && de->d_name[0] == '.' && de->d_name[1] == '.') continue;

        memzero(out, sizeof(DirEntry));
        uint8_t nameLen = de->d_namlen;
        if (nameLen > 255) nameLen = 255;
        memcopy(out->name, de->d_name, nameLen);
        out->name[nameLen] = '\0';
        out->inodeNum = de->d_ino;
        out->fileType = de->d_type;
        out->isDir    = (de->d_type == UFS_DT_DIR);
        return true;
    }

    s_dirIter.active = false;
    return false;
}

uint8_t open_file(uint8_t volumeIndex, uint32_t inodeNum)
{
    if (volumeIndex >= MAX_UFS_VOLUMES) return 0xFF;
    if (!s_volumes[volumeIndex].mounted) return 0xFF;

    UFSVolume& vol = s_volumes[volumeIndex];

    for (uint8_t i = 0; i < MAX_UFS_FILES; ++i) {
        if (!s_files[i].open) {
            s_files[i].open          = true;
            s_files[i].volumeIndex   = volumeIndex;
            s_files[i].inodeNum      = inodeNum;
            s_files[i].currentOffset = 0;
            s_files[i].inodeType     = vol.type;

            if (vol.type == UFS_TYPE_UFS1) {
                if (!read_inode_ufs1(vol, inodeNum, &s_files[i].inode1)) {
                    s_files[i].open = false;
                    return 0xFF;
                }
            } else {
                if (!read_inode_ufs2(vol, inodeNum, &s_files[i].inode2)) {
                    s_files[i].open = false;
                    return 0xFF;
                }
            }
            return i;
        }
    }
    return 0xFF;
}

uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len)
{
    if (fileHandle >= MAX_UFS_FILES) return 0;
    UFSFile& f = s_files[fileHandle];
    if (!f.open) return 0;

    UFSVolume& vol = s_volumes[f.volumeIndex];
    uint32_t rd;
    if (f.inodeType == UFS_TYPE_UFS1) {
        rd = read_inode_data_ufs1(vol, f.inode1, f.currentOffset, buffer, len);
    } else {
        rd = read_inode_data_ufs2(vol, f.inode2, f.currentOffset, buffer, len);
    }
    f.currentOffset += rd;
    return rd;
}

void close_file(uint8_t fileHandle)
{
    if (fileHandle >= MAX_UFS_FILES) return;
    s_files[fileHandle].open = false;
}

const UFSVolume* get_volume(uint8_t volumeIndex)
{
    if (volumeIndex >= MAX_UFS_VOLUMES) return nullptr;
    if (!s_volumes[volumeIndex].mounted) return nullptr;
    return &s_volumes[volumeIndex];
}

} // namespace fs_ufs
} // namespace kernel
