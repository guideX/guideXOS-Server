// XFS Filesystem Driver
//
// Provides support for SGI/Linux XFS filesystem:
//   - Superblock parsing and validation
//   - Allocation group management
//   - B+ tree inode and directory traversal
//   - File and directory read/write operations
//
// XFS is a high-performance 64-bit journaling filesystem originally
// developed by SGI for IRIX, now commonly used on Linux servers.
//
// Key XFS features:
//   - Extent-based allocation (efficient large files)
//   - B+ trees for directories and free space
//   - Delayed allocation
//   - Journal (log) for metadata integrity
//
// Reference: XFS Algorithms & Data Structures (kernel.org),
//            SGI XFS Filesystem documentation
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FS_XFS_H
#define KERNEL_FS_XFS_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace fs_xfs {

// ================================================================
// XFS Constants
// ================================================================

static const uint32_t XFS_SB_MAGIC = 0x58465342;  // "XFSB"
static const uint32_t XFS_AGF_MAGIC = 0x58414746; // "XAGF"
static const uint32_t XFS_AGI_MAGIC = 0x58414749; // "XAGI"
static const uint32_t XFS_AGFL_MAGIC = 0x5841464C; // "XAFL"

// Inode format types
static const uint8_t XFS_DINODE_FMT_DEV     = 0;  // Device (special)
static const uint8_t XFS_DINODE_FMT_LOCAL   = 1;  // Data in inode
static const uint8_t XFS_DINODE_FMT_EXTENTS = 2;  // Extent list
static const uint8_t XFS_DINODE_FMT_BTREE   = 3;  // B+tree root in inode
static const uint8_t XFS_DINODE_FMT_UUID    = 4;  // UUID

// Directory entry file types (v3+)
static const uint8_t XFS_DIR3_FT_UNKNOWN  = 0;
static const uint8_t XFS_DIR3_FT_REG_FILE = 1;
static const uint8_t XFS_DIR3_FT_DIR      = 2;
static const uint8_t XFS_DIR3_FT_CHRDEV   = 3;
static const uint8_t XFS_DIR3_FT_BLKDEV   = 4;
static const uint8_t XFS_DIR3_FT_FIFO     = 5;
static const uint8_t XFS_DIR3_FT_SOCK     = 6;
static const uint8_t XFS_DIR3_FT_SYMLINK  = 7;
static const uint8_t XFS_DIR3_FT_WHT      = 8;

// Superblock version flags
static const uint16_t XFS_SB_VERSION_NUMBITS    = 0x000f;
static const uint16_t XFS_SB_VERSION_ATTRBIT    = 0x0010;
static const uint16_t XFS_SB_VERSION_NLINKBIT   = 0x0020;
static const uint16_t XFS_SB_VERSION_QUOTABIT   = 0x0040;
static const uint16_t XFS_SB_VERSION_ALIGNBIT   = 0x0080;
static const uint16_t XFS_SB_VERSION_DALIGNBIT  = 0x0100;
static const uint16_t XFS_SB_VERSION_SHAREDBIT  = 0x0200;
static const uint16_t XFS_SB_VERSION_LOGV2BIT   = 0x0400;
static const uint16_t XFS_SB_VERSION_SECTORBIT  = 0x0800;
static const uint16_t XFS_SB_VERSION_EXTFLGBIT  = 0x1000;
static const uint16_t XFS_SB_VERSION_DIRV2BIT   = 0x2000;
static const uint16_t XFS_SB_VERSION_MOREBITSBIT = 0x8000;

// Version 5 feature flags
static const uint32_t XFS_SB_FEAT_INCOMPAT_FTYPE   = 0x01;  // Directory ftype
static const uint32_t XFS_SB_FEAT_INCOMPAT_SPINODES = 0x02; // Sparse inodes
static const uint32_t XFS_SB_FEAT_INCOMPAT_META_UUID = 0x04;

// ================================================================
// XFS Status Codes
// ================================================================

enum XfsStatus : int8_t {
    XFS_OK              =  0,
    XFS_ERR_NOT_FOUND   = -1,
    XFS_ERR_NOT_XFS     = -2,
    XFS_ERR_IO          = -3,
    XFS_ERR_CORRUPT     = -4,
    XFS_ERR_NO_SPACE    = -5,
    XFS_ERR_READ_ONLY   = -6,
    XFS_ERR_NOT_DIR     = -7,
    XFS_ERR_IS_DIR      = -8,
    XFS_ERR_INVALID     = -9,
    XFS_ERR_NOT_EMPTY   = -10,
    XFS_ERR_UNSUPPORTED = -11,
    XFS_ERR_NAME_TOO_LONG = -12,
};

// ================================================================
// XFS On-Disk Structures
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define XFS_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define XFS_PACKED
#endif

// XFS Superblock (sector 0 of AG 0)
// All multi-byte values are big-endian on disk
struct XfsSuperblock {
    uint32_t sb_magicnum;          // Magic number (XFS_SB_MAGIC)
    uint32_t sb_blocksize;         // Block size in bytes
    uint64_t sb_dblocks;           // Total data blocks
    uint64_t sb_rblocks;           // Realtime blocks
    uint64_t sb_rextents;          // Realtime extents
    uint8_t  sb_uuid[16];          // Filesystem UUID
    uint64_t sb_logstart;          // Log start block (internal log)
    uint64_t sb_rootino;           // Root inode number
    uint64_t sb_rbmino;            // Realtime bitmap inode
    uint64_t sb_rsumino;           // Realtime summary inode
    uint32_t sb_rextsize;          // Realtime extent size (blocks)
    uint32_t sb_agblocks;          // Blocks per AG
    uint32_t sb_agcount;           // Number of AGs
    uint32_t sb_rbmblocks;         // Realtime bitmap blocks
    uint32_t sb_logblocks;         // Log blocks
    uint16_t sb_versionnum;        // Version/feature flags
    uint16_t sb_sectsize;          // Sector size (bytes)
    uint16_t sb_inodesize;         // Inode size (bytes)
    uint16_t sb_inopblock;         // Inodes per block
    char     sb_fname[12];         // Volume name
    uint8_t  sb_blocklog;          // log2(blocksize)
    uint8_t  sb_sectlog;           // log2(sectsize)
    uint8_t  sb_inodelog;          // log2(inodesize)
    uint8_t  sb_inopblog;          // log2(inopblock)
    uint8_t  sb_agblklog;          // log2(agblocks)
    uint8_t  sb_rextslog;          // log2(rextsize)
    uint8_t  sb_inprogress;        // mkfs in progress flag
    uint8_t  sb_imax_pct;          // Max inode percentage
    
    // Counters (updated at unmount)
    uint64_t sb_icount;            // Allocated inodes
    uint64_t sb_ifree;             // Free inodes
    uint64_t sb_fdblocks;          // Free data blocks
    uint64_t sb_frextents;         // Free realtime extents
    
    // Quota inodes
    uint64_t sb_uquotino;          // User quota inode
    uint64_t sb_gquotino;          // Group quota inode
    uint16_t sb_qflags;            // Quota flags
    uint8_t  sb_flags;             // Misc flags
    uint8_t  sb_shared_vn;         // Shared version number
    uint32_t sb_inoalignmt;        // Inode alignment
    uint32_t sb_unit;              // Stripe unit
    uint32_t sb_width;             // Stripe width
    uint8_t  sb_dirblklog;         // log2(dirblocksize)
    uint8_t  sb_logsectlog;        // log2(log sector size)
    uint16_t sb_logsectsize;       // Log sector size
    uint32_t sb_logsunit;          // Log stripe unit
    uint32_t sb_features2;         // Additional feature flags
    uint32_t sb_bad_features2;     // Duplicate (for recovery)
    
    // V5 superblock fields
    uint32_t sb_features_compat;
    uint32_t sb_features_ro_compat;
    uint32_t sb_features_incompat;
    uint32_t sb_features_log_incompat;
    uint32_t sb_crc;               // Superblock CRC
    uint32_t sb_spino_align;       // Sparse inode alignment
    uint64_t sb_pquotino;          // Project quota inode
    uint64_t sb_lsn;               // Last write LSN
    uint8_t  sb_meta_uuid[16];     // Metadata UUID
} XFS_PACKED;

// Allocation Group Free Space Header (AGF)
struct XfsAgf {
    uint32_t agf_magicnum;         // Magic (XFS_AGF_MAGIC)
    uint32_t agf_versionnum;       // Version
    uint32_t agf_seqno;            // AG sequence number
    uint32_t agf_length;           // AG length (blocks)
    uint32_t agf_roots[2];         // Free space B+tree roots
    uint32_t agf_spare0;
    uint32_t agf_levels[2];        // B+tree levels
    uint32_t agf_spare1;
    uint32_t agf_flfirst;          // First freelist block index
    uint32_t agf_fllast;           // Last freelist block index
    uint32_t agf_flcount;          // Freelist block count
    uint32_t agf_freeblks;         // Free blocks in AG
    uint32_t agf_longest;          // Longest free extent
    uint32_t agf_btreeblks;        // B+tree blocks used
    uint8_t  agf_uuid[16];         // UUID (v5)
    // Additional v5 fields...
} XFS_PACKED;

// Allocation Group Inode Header (AGI)
struct XfsAgi {
    uint32_t agi_magicnum;         // Magic (XFS_AGI_MAGIC)
    uint32_t agi_versionnum;       // Version
    uint32_t agi_seqno;            // AG sequence number
    uint32_t agi_length;           // AG length (blocks)
    uint32_t agi_count;            // Allocated inodes
    uint32_t agi_root;             // Inode B+tree root
    uint32_t agi_level;            // B+tree levels
    uint32_t agi_freecount;        // Free inodes
    uint32_t agi_newino;           // Last allocated inode
    uint32_t agi_dirino;           // Last directory inode (deprecated)
    uint32_t agi_unlinked[64];     // Unlinked inode hash buckets
    uint8_t  agi_uuid[16];         // UUID (v5)
    // Additional v5 fields...
} XFS_PACKED;

// Inode Core (on-disk inode)
struct XfsDinodeCore {
    uint16_t di_magic;             // Inode magic (0x494E "IN")
    uint16_t di_mode;              // File mode (Unix permissions + type)
    int8_t   di_version;           // Inode version (1, 2, or 3)
    int8_t   di_format;            // Data fork format (XFS_DINODE_FMT_*)
    uint16_t di_onlink;            // Old link count (v1)
    uint32_t di_uid;               // Owner UID
    uint32_t di_gid;               // Owner GID
    uint32_t di_nlink;             // Link count (v2+)
    uint16_t di_projid_lo;         // Project ID (low)
    uint16_t di_projid_hi;         // Project ID (high)
    uint8_t  di_pad[6];            // Padding
    uint16_t di_flushiter;         // Flush iteration (deprecated)
    int64_t  di_atime;             // Access time (seconds)
    int64_t  di_mtime;             // Modification time
    int64_t  di_ctime;             // Change time
    int64_t  di_size;              // File size (bytes)
    int64_t  di_nblocks;           // Blocks used (all forks)
    int32_t  di_extsize;           // Extent size hint
    int32_t  di_nextents;          // Data fork extents
    int16_t  di_anextents;         // Attr fork extents
    uint8_t  di_forkoff;           // Attr fork offset (*8)
    int8_t   di_aformat;           // Attr fork format
    uint32_t di_dmevmask;          // DMAPI event mask
    uint16_t di_dmstate;           // DMAPI state
    uint16_t di_flags;             // Inode flags
    uint32_t di_gen;               // Generation number
    
    // V3 inode fields
    uint32_t di_next_unlinked;     // Next unlinked inode
    uint32_t di_crc;               // Inode CRC
    uint64_t di_changecount;       // Change counter
    uint64_t di_lsn;               // Last write LSN
    uint64_t di_flags2;            // Extended flags
    uint32_t di_cowextsize;        // CoW extent size hint
    uint8_t  di_pad2[12];          // Padding
    int64_t  di_crtime;            // Creation time (ns)
    uint64_t di_ino;               // Inode number
    uint8_t  di_uuid[16];          // Filesystem UUID
} XFS_PACKED;

// Extent record (packed, 128 bits)
// Layout: [ext_flag:1][startoff:54][startblock:52][blockcount:21]
struct XfsBmbtRec {
    uint64_t l0;
    uint64_t l1;
} XFS_PACKED;

// Directory entry (short form, v2)
struct XfsDir2SfEntry {
    uint8_t  namelen;              // Name length
    uint16_t offset;               // Offset tag (for iteration)
    // Followed by: name[namelen], then inode number (4 or 8 bytes)
} XFS_PACKED;

// Directory block header (v3)
struct XfsDir3DataHdr {
    uint32_t magic;                // XFS_DIR3_DATA_MAGIC
    uint32_t crc;
    uint64_t blkno;                // Block number
    uint64_t lsn;                  // Last write LSN
    uint8_t  uuid[16];
    uint64_t owner;                // Inode number
} XFS_PACKED;

// Directory data entry
struct XfsDir2DataEntry {
    uint64_t inumber;              // Inode number
    uint8_t  namelen;              // Name length
    // Followed by: name[namelen], then ftype (v5), then tag
} XFS_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef XFS_PACKED

// ================================================================
// XFS Volume Context
// ================================================================

struct XfsVolume {
    uint8_t  blockDeviceIndex;     // Block device index
    bool     mounted;              // Is volume mounted
    bool     readOnly;             // Mounted read-only
    
    // Superblock info (converted to native endian)
    uint32_t blockSize;            // Block size
    uint16_t sectorSize;           // Sector size
    uint16_t inodeSize;            // Inode size
    uint64_t totalBlocks;          // Total data blocks
    uint64_t rootInode;            // Root inode number
    uint32_t agCount;              // Number of AGs
    uint32_t agBlocks;             // Blocks per AG
    uint8_t  blockLog;             // log2(blocksize)
    uint8_t  inodeLog;             // log2(inodesize)
    uint8_t  agBlockLog;           // log2(agblocks)
    uint16_t versionNum;           // Version/features
    
    // V5 features
    bool     isV5;                 // Is V5 filesystem
    bool     hasFtype;             // Directory ftype support
    
    // Volume info
    uint8_t  uuid[16];
    char     volumeLabel[13];      // Volume name (12 + null)
    
    // Counters
    uint64_t freeBlocks;
    uint64_t freeInodes;
    
    // Buffer for inode reading
    uint8_t* inodeBuffer;
};

// ================================================================
// XFS File Handle
// ================================================================

struct XfsFile {
    XfsVolume* volume;
    uint64_t   inodeNum;           // Inode number
    uint64_t   size;               // File size
    uint64_t   position;           // Current position
    uint16_t   mode;               // File mode
    uint8_t    format;             // Data fork format
    bool       isDirectory;
    bool       open;
    
    // For extent-based files
    int32_t    extentCount;
    // Extent list would be cached here
};

// ================================================================
// Directory Entry (for listing)
// ================================================================

struct XfsDirEntry {
    char     name[256];            // Filename
    uint64_t inodeNum;             // Inode number
    uint64_t size;                 // File size (0 for dirs until stated)
    uint16_t mode;                 // Unix mode
    uint8_t  fileType;             // XFS_DIR3_FT_*
    bool     isDirectory;
};

// ================================================================
// Byte-swap Helpers (XFS is big-endian on disk)
// ================================================================

static inline uint16_t be16_to_cpu(uint16_t x) {
    return (x >> 8) | (x << 8);
}

static inline uint32_t be32_to_cpu(uint32_t x) {
    return ((x >> 24) & 0xFF) |
           ((x >> 8)  & 0xFF00) |
           ((x << 8)  & 0xFF0000) |
           ((x << 24) & 0xFF000000);
}

static inline uint64_t be64_to_cpu(uint64_t x) {
    return ((x >> 56) & 0xFFULL) |
           ((x >> 40) & 0xFF00ULL) |
           ((x >> 24) & 0xFF0000ULL) |
           ((x >> 8)  & 0xFF000000ULL) |
           ((x << 8)  & 0xFF00000000ULL) |
           ((x << 24) & 0xFF0000000000ULL) |
           ((x << 40) & 0xFF000000000000ULL) |
           ((x << 56) & 0xFF00000000000000ULL);
}

// ================================================================
// Public API
// ================================================================

// Initialize XFS filesystem driver
void init();

// Check if a block device contains an XFS filesystem
XfsStatus probe(uint8_t blockDevIndex);

// Mount an XFS volume
XfsStatus mount(uint8_t blockDevIndex, XfsVolume* volumeOut, bool readOnly = false);

// Unmount an XFS volume
XfsStatus unmount(XfsVolume* volume);

// ================================================================
// File Operations
// ================================================================

// Open a file by path
XfsStatus open(XfsVolume* volume, const char* path, XfsFile* fileOut);

// Close a file handle
XfsStatus close(XfsFile* file);

// Read data from file
XfsStatus read(XfsFile* file, void* buffer, size_t size, size_t* bytesRead);

// Write data to file
XfsStatus write(XfsFile* file, const void* buffer, size_t size, size_t* bytesWritten);

// Seek within file
XfsStatus seek(XfsFile* file, int64_t offset, int whence);

// Get file size
uint64_t get_size(XfsFile* file);

// ================================================================
// Directory Operations
// ================================================================

typedef bool (*XfsDirCallback)(const XfsDirEntry* entry, void* context);
XfsStatus list_directory(XfsVolume* volume, const char* path,
                         XfsDirCallback callback, void* context);

XfsStatus create_directory(XfsVolume* volume, const char* path);
XfsStatus remove_directory(XfsVolume* volume, const char* path);

// ================================================================
// File Management
// ================================================================

XfsStatus create_file(XfsVolume* volume, const char* path);
XfsStatus delete_file(XfsVolume* volume, const char* path);
XfsStatus rename(XfsVolume* volume, const char* oldPath, const char* newPath);
XfsStatus stat(XfsVolume* volume, const char* path, uint16_t* modeOut, uint64_t* sizeOut);

// ================================================================
// Volume Operations
// ================================================================

XfsStatus get_volume_info(XfsVolume* volume, char* labelOut, 
                          uint64_t* totalBytesOut, uint64_t* freeBytesOut);
XfsStatus sync(XfsVolume* volume);

// ================================================================
// Internal/Low-level
// ================================================================

// Read an inode by number
XfsStatus read_inode(XfsVolume* volume, uint64_t inodeNum, XfsDinodeCore* inodeOut);

// Convert inode number to AG and AG-relative inode
static inline void ino_to_ag(XfsVolume* volume, uint64_t ino, 
                             uint32_t* agOut, uint32_t* agInoOut) {
    uint32_t agInoBits = volume->inodeLog - volume->blockLog + volume->agBlockLog;
    *agOut = static_cast<uint32_t>(ino >> agInoBits);
    *agInoOut = static_cast<uint32_t>(ino & ((1ULL << agInoBits) - 1));
}

// Calculate block number containing an inode
static inline uint64_t ino_to_block(XfsVolume* volume, uint64_t ino) {
    uint32_t ag, agIno;
    ino_to_ag(volume, ino, &ag, &agIno);
    uint32_t inodesPerBlock = volume->blockSize / volume->inodeSize;
    uint64_t blockInAg = agIno / inodesPerBlock;
    return (static_cast<uint64_t>(ag) * volume->agBlocks) + blockInAg;
}

// Parse extent record
void parse_extent(const XfsBmbtRec* rec, uint64_t* startOffOut, 
                  uint64_t* startBlockOut, uint32_t* countOut, bool* prealloc);

// Print XFS driver status
void print_status();

} // namespace fs_xfs
} // namespace kernel

#endif // KERNEL_FS_XFS_H
