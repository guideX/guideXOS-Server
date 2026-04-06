// NTFS Filesystem Driver
//
// Provides support for Microsoft NTFS (New Technology File System):
//   - Superblock (Boot Sector) parsing
//   - Master File Table (MFT) access
//   - File and directory read operations
//   - Write support (with journaling awareness)
//
// NTFS is the primary filesystem for Windows NT/2000/XP/Vista/7/8/10/11.
// This driver enables read/write access to NTFS partitions from guideXOS.
//
// Reference: NTFS Documentation Project, Microsoft NTFS Specification
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FS_NTFS_H
#define KERNEL_FS_NTFS_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace fs_ntfs {

// ================================================================
// NTFS Constants
// ================================================================

static const uint64_t NTFS_SIGNATURE = 0x202020205346544EULL; // "NTFS    "
static const uint32_t NTFS_SECTOR_SIZE = 512;
static const uint32_t NTFS_MAX_PATH = 260;
static const uint32_t NTFS_MAX_FILENAME = 255;

// Master File Table (MFT) entry indices
static const uint32_t MFT_ENTRY_MFT         = 0;   // $MFT itself
static const uint32_t MFT_ENTRY_MFTMIRR     = 1;   // $MFTMirr
static const uint32_t MFT_ENTRY_LOGFILE     = 2;   // $LogFile (journal)
static const uint32_t MFT_ENTRY_VOLUME      = 3;   // $Volume
static const uint32_t MFT_ENTRY_ATTRDEF     = 4;   // $AttrDef
static const uint32_t MFT_ENTRY_ROOT        = 5;   // Root directory
static const uint32_t MFT_ENTRY_BITMAP      = 6;   // $Bitmap
static const uint32_t MFT_ENTRY_BOOT        = 7;   // $Boot
static const uint32_t MFT_ENTRY_BADCLUS     = 8;   // $BadClus
static const uint32_t MFT_ENTRY_SECURE      = 9;   // $Secure
static const uint32_t MFT_ENTRY_UPCASE      = 10;  // $UpCase
static const uint32_t MFT_ENTRY_EXTEND      = 11;  // $Extend

// Attribute types
static const uint32_t ATTR_TYPE_STANDARD_INFO    = 0x10;
static const uint32_t ATTR_TYPE_ATTRIBUTE_LIST   = 0x20;
static const uint32_t ATTR_TYPE_FILE_NAME        = 0x30;
static const uint32_t ATTR_TYPE_OBJECT_ID        = 0x40;
static const uint32_t ATTR_TYPE_SECURITY_DESC    = 0x50;
static const uint32_t ATTR_TYPE_VOLUME_NAME      = 0x60;
static const uint32_t ATTR_TYPE_VOLUME_INFO      = 0x70;
static const uint32_t ATTR_TYPE_DATA             = 0x80;
static const uint32_t ATTR_TYPE_INDEX_ROOT       = 0x90;
static const uint32_t ATTR_TYPE_INDEX_ALLOCATION = 0xA0;
static const uint32_t ATTR_TYPE_BITMAP           = 0xB0;
static const uint32_t ATTR_TYPE_REPARSE_POINT    = 0xC0;
static const uint32_t ATTR_TYPE_EA_INFO          = 0xD0;
static const uint32_t ATTR_TYPE_EA               = 0xE0;
static const uint32_t ATTR_TYPE_LOGGED_UTIL_STREAM = 0x100;
static const uint32_t ATTR_TYPE_END              = 0xFFFFFFFF;

// File attribute flags
static const uint32_t FILE_ATTR_READONLY         = 0x0001;
static const uint32_t FILE_ATTR_HIDDEN           = 0x0002;
static const uint32_t FILE_ATTR_SYSTEM           = 0x0004;
static const uint32_t FILE_ATTR_DIRECTORY        = 0x0010;
static const uint32_t FILE_ATTR_ARCHIVE          = 0x0020;
static const uint32_t FILE_ATTR_DEVICE           = 0x0040;
static const uint32_t FILE_ATTR_NORMAL           = 0x0080;
static const uint32_t FILE_ATTR_TEMPORARY        = 0x0100;
static const uint32_t FILE_ATTR_SPARSE_FILE      = 0x0200;
static const uint32_t FILE_ATTR_REPARSE_POINT    = 0x0400;
static const uint32_t FILE_ATTR_COMPRESSED       = 0x0800;
static const uint32_t FILE_ATTR_ENCRYPTED        = 0x4000;

// MFT record flags
static const uint16_t MFT_RECORD_IN_USE          = 0x0001;
static const uint16_t MFT_RECORD_IS_DIRECTORY    = 0x0002;
static const uint16_t MFT_RECORD_IS_EXTENSION    = 0x0004;
static const uint16_t MFT_RECORD_HAS_SPECIAL_INDEX = 0x0008;

// ================================================================
// NTFS Status Codes
// ================================================================

enum NtfsStatus : int8_t {
    NTFS_OK               =  0,
    NTFS_ERR_NOT_FOUND    = -1,
    NTFS_ERR_NOT_NTFS     = -2,
    NTFS_ERR_IO           = -3,
    NTFS_ERR_CORRUPT      = -4,
    NTFS_ERR_NO_SPACE     = -5,
    NTFS_ERR_READ_ONLY    = -6,
    NTFS_ERR_NOT_DIR      = -7,
    NTFS_ERR_IS_DIR       = -8,
    NTFS_ERR_INVALID      = -9,
    NTFS_ERR_NOT_EMPTY    = -10,
    NTFS_ERR_UNSUPPORTED  = -11,
};

// ================================================================
// NTFS Boot Sector (BPB - BIOS Parameter Block)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define NTFS_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define NTFS_PACKED
#endif

struct NtfsBootSector {
    uint8_t  jump[3];              // Jump instruction (0xEB 0x52 0x90)
    uint8_t  oemId[8];             // "NTFS    "
    
    // BIOS Parameter Block
    uint16_t bytesPerSector;       // Usually 512
    uint8_t  sectorsPerCluster;    // Power of 2: 1, 2, 4, 8, ...
    uint16_t reservedSectors;      // Always 0 for NTFS
    uint8_t  unused1[3];           // Always 0
    uint16_t unused2;              // Always 0
    uint8_t  mediaDescriptor;      // 0xF8 for hard disks
    uint16_t unused3;              // Always 0
    uint16_t sectorsPerTrack;      // Geometry
    uint16_t numberOfHeads;        // Geometry
    uint32_t hiddenSectors;        // Partition offset
    uint32_t unused4;              // Always 0
    
    // Extended BPB (NTFS-specific)
    uint32_t unused5;              // Always 0x80008000
    uint64_t totalSectors;         // Volume size in sectors
    uint64_t mftLcn;               // LCN of $MFT
    uint64_t mftMirrLcn;           // LCN of $MFTMirr
    int8_t   clustersPerMftRecord; // Negative = 2^|value| bytes
    uint8_t  unused6[3];
    int8_t   clustersPerIndexRecord; // Negative = 2^|value| bytes
    uint8_t  unused7[3];
    uint64_t volumeSerialNumber;
    uint32_t checksum;
    
    uint8_t  bootCode[426];        // Bootstrap code
    uint16_t bootSignature;        // 0xAA55
} NTFS_PACKED;

// ================================================================
// MFT Record Header (FILE record)
// ================================================================

struct MftRecordHeader {
    uint8_t  signature[4];         // "FILE" (0x454C4946)
    uint16_t fixupOffset;          // Offset to fixup array
    uint16_t fixupCount;           // Number of fixup entries
    uint64_t logSequenceNumber;    // $LogFile sequence number
    uint16_t sequenceNumber;       // Reuse counter
    uint16_t linkCount;            // Hard link count
    uint16_t attributeOffset;      // Offset to first attribute
    uint16_t flags;                // MFT_RECORD_* flags
    uint32_t usedSize;             // Bytes used in this record
    uint32_t allocatedSize;        // Allocated record size
    uint64_t baseRecordRef;        // Base MFT record (for extensions)
    uint16_t nextAttributeId;      // Next attribute ID
    uint16_t padding;              // Alignment (XP+)
    uint32_t mftRecordNumber;      // MFT record number (XP+)
} NTFS_PACKED;

// ================================================================
// Attribute Header (common to all attributes)
// ================================================================

struct AttributeHeader {
    uint32_t type;                 // Attribute type (ATTR_TYPE_*)
    uint32_t length;               // Total length including header
    uint8_t  nonResident;          // 0 = resident, 1 = non-resident
    uint8_t  nameLength;           // Name length in Unicode chars
    uint16_t nameOffset;           // Offset to name (from start)
    uint16_t flags;                // Compression, sparse, encrypted
    uint16_t attributeId;          // Unique ID within record
} NTFS_PACKED;

// Resident attribute data (follows AttributeHeader)
struct ResidentAttribute {
    uint32_t valueLength;          // Length of attribute value
    uint16_t valueOffset;          // Offset to value (from start)
    uint8_t  indexedFlag;          // Indexed in directory
    uint8_t  padding;
} NTFS_PACKED;

// Non-resident attribute data (follows AttributeHeader)
struct NonResidentAttribute {
    uint64_t lowestVcn;            // First VCN of this extent
    uint64_t highestVcn;           // Last VCN of this extent
    uint16_t dataRunsOffset;       // Offset to data runs
    uint16_t compressionUnit;      // Compression unit size
    uint32_t padding;
    uint64_t allocatedSize;        // Allocated size (rounded to cluster)
    uint64_t dataSize;             // Actual data size
    uint64_t initializedSize;      // Initialized data size
    // Followed by optional compressed size if compressed
} NTFS_PACKED;

// ================================================================
// Standard Information Attribute (0x10)
// ================================================================

struct StandardInformation {
    uint64_t creationTime;         // File creation time
    uint64_t modificationTime;     // Last modification time
    uint64_t mftModificationTime;  // MFT record modification time
    uint64_t accessTime;           // Last access time
    uint32_t fileAttributes;       // FILE_ATTR_* flags
    uint32_t maximumVersions;      // Max allowed versions
    uint32_t versionNumber;        // Current version number
    uint32_t classId;              // Class ID
    uint32_t ownerId;              // Owner ID
    uint32_t securityId;           // Security descriptor ID
    uint64_t quotaCharged;         // Quota charged
    uint64_t updateSequenceNumber; // $UsnJrnl sequence number
} NTFS_PACKED;

// ================================================================
// File Name Attribute (0x30)
// ================================================================

struct FileNameAttribute {
    uint64_t parentDirectory;      // MFT reference of parent
    uint64_t creationTime;
    uint64_t modificationTime;
    uint64_t mftModificationTime;
    uint64_t accessTime;
    uint64_t allocatedSize;        // Allocated size of file
    uint64_t dataSize;             // Real size of file
    uint32_t fileAttributes;       // FILE_ATTR_* flags
    uint32_t extendedAttributes;   // EA and reparse
    uint8_t  filenameLength;       // Length in Unicode chars
    uint8_t  filenameNamespace;    // 0=POSIX, 1=Win32, 2=DOS, 3=Win32+DOS
    // Followed by filename in UTF-16LE
} NTFS_PACKED;

// ================================================================
// Index Root Attribute (0x90) - Directory root
// ================================================================

struct IndexRoot {
    uint32_t attributeType;        // Type of indexed attribute (0x30)
    uint32_t collationRule;        // Collation rule
    uint32_t indexBlockSize;       // Size of index allocation blocks
    uint8_t  clustersPerIndexBlock;
    uint8_t  padding[3];
    // Followed by IndexHeader
} NTFS_PACKED;

struct IndexHeader {
    uint32_t entriesOffset;        // Offset to first entry
    uint32_t indexLength;          // Size of index entries
    uint32_t allocatedLength;      // Allocated size
    uint8_t  flags;                // 0x01 = has large index
    uint8_t  padding[3];
} NTFS_PACKED;

struct IndexEntry {
    uint64_t mftReference;         // MFT reference (if not last entry)
    uint16_t entryLength;          // Length of this entry
    uint16_t keyLength;            // Length of filename attribute
    uint8_t  flags;                // 0x01 = has sub-node, 0x02 = last entry
    uint8_t  padding[3];
    // Followed by FileNameAttribute (key)
    // Followed by VCN of sub-node (if flags & 0x01)
} NTFS_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef NTFS_PACKED

// ================================================================
// NTFS Volume Context
// ================================================================

struct NtfsVolume {
    uint8_t  blockDeviceIndex;     // Block device index
    bool     mounted;              // Is volume mounted
    bool     readOnly;             // Mounted read-only
    
    // Boot sector info
    uint16_t bytesPerSector;
    uint32_t bytesPerCluster;
    uint32_t mftRecordSize;
    uint32_t indexRecordSize;
    
    // MFT location
    uint64_t mftLcn;               // MFT first cluster
    uint64_t mftMirrLcn;           // MFTMirr first cluster
    uint64_t totalSectors;
    uint64_t totalClusters;
    
    // Volume info
    uint64_t volumeSerial;
    char     volumeLabel[32];
    
    // Cache for MFT record reading
    uint8_t* mftRecordBuffer;      // Buffer for one MFT record
};

// ================================================================
// Directory Entry (for listing)
// ================================================================

struct NtfsDirEntry {
    char     name[NTFS_MAX_FILENAME];
    uint64_t mftRef;               // MFT reference
    uint64_t size;                 // File size
    uint32_t attributes;           // FILE_ATTR_* flags
    bool     isDirectory;
};

// ================================================================
// File Handle
// ================================================================

struct NtfsFile {
    NtfsVolume* volume;
    uint64_t    mftRef;            // MFT reference number
    uint64_t    dataSize;          // File size
    uint64_t    position;          // Current read position
    bool        isDirectory;
    bool        open;
    
    // Data attribute info (for non-resident)
    bool        isResident;
    uint64_t    dataRunsOffset;    // Offset within MFT record
    uint64_t    allocatedSize;
};

// ================================================================
// Public API
// ================================================================

// Initialize NTFS filesystem driver
// Call once at kernel boot
void init();

// Check if a block device contains an NTFS filesystem
// Returns NTFS_OK if valid NTFS, error code otherwise
NtfsStatus probe(uint8_t blockDevIndex);

// Mount an NTFS volume
// volumeOut: receives initialized NtfsVolume on success
// readOnly: if true, mount read-only (safer)
NtfsStatus mount(uint8_t blockDevIndex, NtfsVolume* volumeOut, bool readOnly = false);

// Unmount an NTFS volume
NtfsStatus unmount(NtfsVolume* volume);

// ================================================================
// File Operations
// ================================================================

// Open a file by path
// path: absolute path from root (e.g., "/Windows/System32/config")
// fileOut: receives file handle on success
NtfsStatus open(NtfsVolume* volume, const char* path, NtfsFile* fileOut);

// Close a file handle
NtfsStatus close(NtfsFile* file);

// Read data from file
// Returns actual bytes read in bytesRead
NtfsStatus read(NtfsFile* file, void* buffer, size_t size, size_t* bytesRead);

// Write data to file (if not read-only)
NtfsStatus write(NtfsFile* file, const void* buffer, size_t size, size_t* bytesWritten);

// Seek within file
NtfsStatus seek(NtfsFile* file, int64_t offset, int whence);

// Get file size
uint64_t get_size(NtfsFile* file);

// ================================================================
// Directory Operations
// ================================================================

// List directory contents
// callback is called for each entry; return false to stop enumeration
typedef bool (*NtfsDirCallback)(const NtfsDirEntry* entry, void* context);
NtfsStatus list_directory(NtfsVolume* volume, const char* path,
                          NtfsDirCallback callback, void* context);

// Create a directory (if not read-only)
NtfsStatus create_directory(NtfsVolume* volume, const char* path);

// Remove a directory (must be empty)
NtfsStatus remove_directory(NtfsVolume* volume, const char* path);

// ================================================================
// File Management
// ================================================================

// Create a new file
NtfsStatus create_file(NtfsVolume* volume, const char* path);

// Delete a file
NtfsStatus delete_file(NtfsVolume* volume, const char* path);

// Rename/move a file or directory
NtfsStatus rename(NtfsVolume* volume, const char* oldPath, const char* newPath);

// Get file/directory attributes
NtfsStatus get_attributes(NtfsVolume* volume, const char* path, uint32_t* attrsOut);

// ================================================================
// Volume Operations
// ================================================================

// Get volume information
NtfsStatus get_volume_info(NtfsVolume* volume, char* labelOut, uint64_t* totalBytesOut,
                           uint64_t* freeBytesOut);

// Flush all cached data to disk
NtfsStatus sync(NtfsVolume* volume);

// ================================================================
// Internal/Testing
// ================================================================

// Read a specific MFT record
NtfsStatus read_mft_record(NtfsVolume* volume, uint64_t mftIndex, void* buffer);

// Parse data runs from non-resident attribute
// Returns number of runs parsed
int parse_data_runs(const uint8_t* runList, uint64_t* lcnOut, uint64_t* lengthOut, int maxRuns);

// Convert LCN (Logical Cluster Number) to byte offset
static inline uint64_t lcn_to_offset(NtfsVolume* volume, uint64_t lcn) {
    return lcn * volume->bytesPerCluster;
}

// Convert VCN (Virtual Cluster Number) to LCN using data runs
NtfsStatus vcn_to_lcn(NtfsVolume* volume, NtfsFile* file, uint64_t vcn, uint64_t* lcnOut);

// Print NTFS driver status to serial debug
void print_status();

} // namespace fs_ntfs
} // namespace kernel

#endif // KERNEL_FS_NTFS_H
