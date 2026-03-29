// ext2 / ext4 Filesystem Driver
//
// Supports:
//   - ext2 and ext4 (read-only for ext4 extent trees)
//   - Superblock parsing
//   - Inode lookup, directory listing, file read
//   - ext4 extent-based and ext2 indirect-block allocation
//
// Operates on top of the block device abstraction layer.
// Architecture-independent — the native Linux filesystem makes
// this especially useful on SPARC, IA-64, and ARM targets where
// Linux is the common host OS.
//
// Reference: ext4 Disk Layout (kernel.org documentation),
//            The Second Extended File System (Rémy Card, 1994)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FS_EXT4_H
#define KERNEL_FS_EXT4_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace fs_ext4 {

// ================================================================
// ext2/ext4 superblock (1024 bytes, at offset 1024 on disk)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define EXT_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define EXT_PACKED
#endif

struct Superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;         // block size = 1024 << s_log_block_size
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;                  // 0xEF53
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;

    // ext4-specific (rev >= 1)
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;

    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;

    // Journalling
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;

    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];

    // 64-bit support
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;

    uint8_t  s_padding[148];          // pad to 1024 bytes
} EXT_PACKED;

// ================================================================
// Block Group Descriptor (32 or 64 bytes)
// ================================================================

struct BlockGroupDesc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
} EXT_PACKED;

// ================================================================
// Inode (128 bytes ext2, 256+ bytes ext4)
// ================================================================

struct Inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];             // direct/indirect or ext4 extent tree
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_hi;               // upper 32 bits of file size (ext4)
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[12];
} EXT_PACKED;

// ================================================================
// Directory Entry (variable length)
// ================================================================

struct DirEntry2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;               // EXT4_FT_* values
    char     name[255];
} EXT_PACKED;

// ================================================================
// ext4 Extent structures (stored in i_block when EXTENTS flag set)
// ================================================================

struct ExtentHeader {
    uint16_t eh_magic;                // 0xF30A
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} EXT_PACKED;

struct ExtentIdx {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} EXT_PACKED;

struct Extent {
    uint32_t ee_block;                // first logical block
    uint16_t ee_len;                  // number of blocks
    uint16_t ee_start_hi;             // upper 16 bits of physical block
    uint32_t ee_start_lo;             // lower 32 bits of physical block
} EXT_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef EXT_PACKED

// ================================================================
// Constants
// ================================================================

static const uint16_t EXT_MAGIC    = 0xEF53;
static const uint16_t EXTENT_MAGIC = 0xF30A;

// Inode flag: uses extents (ext4)
static const uint32_t EXT4_EXTENTS_FL = 0x00080000;

// File type values in directory entries
static const uint8_t EXT4_FT_UNKNOWN  = 0;
static const uint8_t EXT4_FT_REG_FILE = 1;
static const uint8_t EXT4_FT_DIR      = 2;
static const uint8_t EXT4_FT_CHRDEV   = 3;
static const uint8_t EXT4_FT_BLKDEV   = 4;
static const uint8_t EXT4_FT_FIFO     = 5;
static const uint8_t EXT4_FT_SOCK     = 6;
static const uint8_t EXT4_FT_SYMLINK  = 7;

// Inode mode bits (S_IFMT)
static const uint16_t S_IFMT   = 0xF000;
static const uint16_t S_IFDIR  = 0x4000;
static const uint16_t S_IFREG  = 0x8000;

// Well-known inode numbers
static const uint32_t EXT_ROOT_INO = 2;

// Feature flags (incompat)
static const uint32_t EXT4_FEATURE_INCOMPAT_EXTENTS = 0x0040;
static const uint32_t EXT4_FEATURE_INCOMPAT_64BIT   = 0x0080;

// ================================================================
// Mounted volume descriptor
// ================================================================

struct Ext4Volume {
    bool     mounted;
    uint8_t  blockDevIndex;
    uint32_t blockSize;
    uint32_t inodeSize;
    uint32_t inodesPerGroup;
    uint32_t blocksPerGroup;
    uint32_t firstDataBlock;
    uint32_t groupCount;
    uint32_t bgdtBlock;              // block group descriptor table block
    bool     has64bit;
    bool     hasExtents;
    char     volumeName[17];
};

static const uint8_t MAX_EXT4_VOLUMES = 4;

// ================================================================
// File handle
// ================================================================

struct Ext4File {
    bool     open;
    uint8_t  volumeIndex;
    uint32_t inodeNum;
    Inode    inode;
    uint32_t currentOffset;
};

static const uint8_t MAX_EXT4_FILES = 8;

// ================================================================
// Directory iteration output
// ================================================================

struct Ext4DirEntry {
    char     name[256];
    uint32_t inodeNum;
    uint8_t  fileType;
    bool     isDir;
};

// ================================================================
// Public API
// ================================================================

// Initialise the ext4 filesystem driver.
void init();

// Mount an ext2/ext4 volume on a block device.
// Returns volume index, or 0xFF on failure.
uint8_t mount(uint8_t blockDevIndex);

// Unmount a volume.
void unmount(uint8_t volumeIndex);

// Open root directory for iteration.
bool open_root_dir(uint8_t volumeIndex);

// Read next directory entry.  Returns false when exhausted.
bool read_dir(uint8_t volumeIndex, Ext4DirEntry* out);

// Open a file by inode number.
// Returns file handle index, or 0xFF on failure.
uint8_t open_file(uint8_t volumeIndex, uint32_t inodeNum);

// Read up to 'len' bytes from an open file.
uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len);

// Close a file.
void close_file(uint8_t fileHandle);

// Get volume info.
const Ext4Volume* get_volume(uint8_t volumeIndex);

} // namespace fs_ext4
} // namespace kernel

#endif // KERNEL_FS_EXT4_H
