// UFS (Unix File System) Driver
//
// Supports:
//   - UFS1 (BSD / SunOS 4.x style, 32-bit block addresses)
//   - UFS2 (Solaris / FreeBSD style, 64-bit block addresses)
//   - Read-only: mount, directory listing, file read
//   - Direct, single-, double-, triple-indirect block allocation
//
// UFS is the native filesystem for SPARC-based Solaris and SunOS
// systems, making it essential for SPARC v8 and SPARC v9 targets.
// Also common on FreeBSD (all architectures).
//
// Operates on top of the block device abstraction layer.
// Architecture-independent.
//
// Reference: The Design and Implementation of the 4.4BSD
//            Operating System (McKusick et al.),
//            Solaris Internals (Jim Mauro, Richard McDougall)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FS_UFS_H
#define KERNEL_FS_UFS_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace fs_ufs {

// ================================================================
// UFS magic numbers
// ================================================================

static const uint32_t UFS1_MAGIC = 0x00011954;  // UFS1 superblock magic
static const uint32_t UFS2_MAGIC = 0x19540119;  // UFS2 superblock magic

// ================================================================
// UFS type
// ================================================================

enum UFSType : uint8_t {
    UFS_TYPE_NONE = 0,
    UFS_TYPE_UFS1 = 1,
    UFS_TYPE_UFS2 = 2,
};

// ================================================================
// UFS1 Superblock (selected fields)
// The superblock is at byte offset 8192 (sector 16 for 512-byte sectors).
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define UFS_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define UFS_PACKED
#endif

struct UFS1_Superblock {
    uint8_t  padding1[8];
    uint32_t fs_sblkno;         // offset of superblock in cylinder group
    uint32_t fs_cblkno;         // offset of cg block in cylinder group
    uint32_t fs_iblkno;         // offset of inode blocks in cg
    uint32_t fs_dblkno;         // offset of first data block in cg
    uint32_t fs_cgoffset;       // cg offset within block
    uint32_t fs_cgmask;
    uint32_t fs_time;
    uint32_t fs_size;           // total data blocks in fs
    uint32_t fs_dsize;          // total data blocks available
    uint32_t fs_ncg;            // number of cylinder groups
    uint32_t fs_bsize;          // block size (typically 8192)
    uint32_t fs_fsize;          // fragment size (typically 1024)
    uint32_t fs_frag;           // fragments per block
    uint8_t  padding2[48];      // min free, rotational delay, etc.
    uint32_t fs_sbsize;         // actual size of superblock
    uint8_t  padding3[44];
    uint32_t fs_nindir;         // number of indirect pointers per block
    uint32_t fs_inopb;          // inodes per block
    uint8_t  padding4[8];
    uint32_t fs_csaddr;         // blk addr of cyl grp summary area
    uint32_t fs_cssize;
    uint32_t fs_cgsize;
    uint8_t  padding5[12];
    uint32_t fs_ipg;            // inodes per cylinder group
    uint32_t fs_fpg;            // data blocks (frags) per group
    uint8_t  padding6[48];
    uint32_t fs_magic;          // UFS1_MAGIC
    uint8_t  padding7[476];     // rest of superblock to ~1380 bytes
} UFS_PACKED;

// ================================================================
// UFS1 Inode (128 bytes)
// ================================================================

struct UFS1_Inode {
    uint16_t di_mode;
    uint16_t di_nlink;
    uint32_t di_uid;
    uint32_t di_gid;
    uint64_t di_size;
    uint32_t di_atime;
    uint32_t di_atimensec;
    uint32_t di_mtime;
    uint32_t di_mtimensec;
    uint32_t di_ctime;
    uint32_t di_ctimensec;
    uint32_t di_db[12];         // direct block pointers
    uint32_t di_ib[3];          // indirect block pointers
    uint32_t di_flags;
    uint32_t di_blocks;
    uint32_t di_gen;
    uint8_t  di_padding[12];
} UFS_PACKED;

// ================================================================
// UFS2 Inode (256 bytes)
// ================================================================

struct UFS2_Inode {
    uint16_t di_mode;
    uint16_t di_nlink;
    uint32_t di_uid;
    uint32_t di_gid;
    uint32_t di_blksize;
    uint64_t di_size;
    uint64_t di_blocks;
    uint64_t di_atime;
    uint64_t di_mtime;
    uint64_t di_ctime;
    uint64_t di_createtime;
    uint32_t di_mtimensec;
    uint32_t di_atimensec;
    uint32_t di_ctimensec;
    uint32_t di_createtimensec;
    uint32_t di_gen;
    uint32_t di_kernflags;
    uint32_t di_flags;
    uint32_t di_extsize;
    uint64_t di_extb[2];
    uint64_t di_db[12];         // direct block pointers (64-bit)
    uint64_t di_ib[3];          // indirect block pointers (64-bit)
    uint64_t di_spare[3];
} UFS_PACKED;

// ================================================================
// UFS directory entry
// ================================================================

struct UFSDirEntry {
    uint32_t d_ino;
    uint16_t d_reclen;
    uint8_t  d_type;
    uint8_t  d_namlen;
    char     d_name[256];       // variable length, up to 255
} UFS_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef UFS_PACKED

// UFS directory file types
static const uint8_t UFS_DT_UNKNOWN = 0;
static const uint8_t UFS_DT_REG     = 8;
static const uint8_t UFS_DT_DIR     = 4;
static const uint8_t UFS_DT_LNK     = 10;

// Inode mode bits
static const uint16_t UFS_IFMT  = 0xF000;
static const uint16_t UFS_IFDIR = 0x4000;
static const uint16_t UFS_IFREG = 0x8000;

// Well-known inode numbers
static const uint32_t UFS_ROOT_INO = 2;

// ================================================================
// Mounted volume descriptor
// ================================================================

struct UFSVolume {
    bool     mounted;
    UFSType  type;
    uint8_t  blockDevIndex;
    uint32_t blockSize;           // fs_bsize
    uint32_t fragSize;            // fs_fsize
    uint32_t fragsPerBlock;       // fs_frag
    uint32_t inodesPerGroup;      // fs_ipg
    uint32_t fragsPerGroup;       // fs_fpg
    uint32_t numGroups;           // fs_ncg
    uint32_t inodeBlockOffset;    // fs_iblkno (in frags)
    uint32_t dataBlockOffset;     // fs_dblkno (in frags)
    uint32_t inopb;               // inodes per block
    uint32_t cgOffset;            // fs_cgoffset
};

static const uint8_t MAX_UFS_VOLUMES = 4;

// ================================================================
// File handle
// ================================================================

struct UFSFile {
    bool     open;
    uint8_t  volumeIndex;
    uint32_t inodeNum;
    // Cached inode data (union for both UFS1 and UFS2)
    UFSType  inodeType;
    UFS1_Inode inode1;
    UFS2_Inode inode2;
    uint32_t currentOffset;
};

static const uint8_t MAX_UFS_FILES = 8;

// ================================================================
// Directory output entry
// ================================================================

struct DirEntry {
    char     name[256];
    uint32_t inodeNum;
    uint8_t  fileType;
    bool     isDir;
};

// ================================================================
// Public API
// ================================================================

// Initialise the UFS filesystem driver.
void init();

// Mount a UFS1 or UFS2 volume on a block device.
// Returns volume index, or 0xFF on failure.
uint8_t mount(uint8_t blockDevIndex);

// Unmount a volume.
void unmount(uint8_t volumeIndex);

// Open root directory for iteration.
bool open_root_dir(uint8_t volumeIndex);

// Read next directory entry.
bool read_dir(uint8_t volumeIndex, DirEntry* out);

// Open a file by inode number.
uint8_t open_file(uint8_t volumeIndex, uint32_t inodeNum);

// Read up to 'len' bytes from a file.
uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len);

// Close a file.
void close_file(uint8_t fileHandle);

// Volume info.
const UFSVolume* get_volume(uint8_t volumeIndex);

} // namespace fs_ufs
} // namespace kernel

#endif // KERNEL_FS_UFS_H
