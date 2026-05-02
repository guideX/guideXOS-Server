// FAT32 / exFAT Filesystem Driver
//
// Supports:
//   - FAT32: mount, read directory, open/read/write files
//   - exFAT: mount, read directory, open/read files
//   - Long File Name (LFN) support for FAT32
//   - Cluster chain traversal
//
// Operates on top of the block device abstraction layer.
// Architecture-independent — works on all platforms.
//
// Reference: Microsoft FAT32 File System Specification (Dec 2000),
//            Microsoft exFAT Revision 1.00
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FS_FAT_H
#define KERNEL_FS_FAT_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace fs_fat {

// ================================================================
// FAT type enumeration
// ================================================================

enum FATType : uint8_t {
    FAT_TYPE_NONE  = 0,
    FAT_TYPE_FAT32 = 1,
    FAT_TYPE_EXFAT = 2,
};

// ================================================================
// FAT32 BIOS Parameter Block (BPB) — first 90 bytes of sector 0
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define FAT_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define FAT_PACKED
#endif

struct FAT32_BPB {
    uint8_t  jmpBoot[3];
    char     oemName[8];
    uint16_t bytesPerSector;
    uint8_t  sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t  numFATs;
    uint16_t rootEntryCount;      // 0 for FAT32
    uint16_t totalSectors16;      // 0 for FAT32
    uint8_t  mediaType;
    uint16_t fatSize16;           // 0 for FAT32
    uint16_t sectorsPerTrack;
    uint16_t numHeads;
    uint32_t hiddenSectors;
    uint32_t totalSectors32;
    // FAT32-specific extension
    uint32_t fatSize32;
    uint16_t extFlags;
    uint16_t fsVersion;
    uint32_t rootCluster;
    uint16_t fsInfoSector;
    uint16_t backupBootSector;
    uint8_t  reserved[12];
    uint8_t  driveNumber;
    uint8_t  reserved1;
    uint8_t  bootSig;
    uint32_t volumeID;
    char     volumeLabel[11];
    char     fsType[8];
} FAT_PACKED;

// ================================================================
// exFAT Boot Sector — first 120 bytes of sector 0
// ================================================================

struct ExFAT_BootSector {
    uint8_t  jmpBoot[3];
    char     fsName[8];           // "EXFAT   "
    uint8_t  zeroes[53];
    uint64_t partitionOffset;
    uint64_t volumeLength;
    uint32_t fatOffset;
    uint32_t fatLength;
    uint32_t clusterHeapOffset;
    uint32_t clusterCount;
    uint32_t rootDirCluster;
    uint32_t volumeSerial;
    uint16_t fsRevision;
    uint16_t volumeFlags;
    uint8_t  bytesPerSectorShift;
    uint8_t  sectorsPerClusterShift;
    uint8_t  numFATs;
    uint8_t  driveSelect;
    uint8_t  percentInUse;
    uint8_t  reserved[7];
    uint8_t  bootCode[390];
    uint16_t bootSignature;
} FAT_PACKED;

// ================================================================
// FAT32 Directory Entry (32 bytes)
// ================================================================

struct FAT32_DirEntry {
    char     name[11];            // 8.3 short name
    uint8_t  attr;
    uint8_t  ntRes;
    uint8_t  crtTimeTenth;
    uint16_t crtTime;
    uint16_t crtDate;
    uint16_t lstAccDate;
    uint16_t firstClusterHi;
    uint16_t wrtTime;
    uint16_t wrtDate;
    uint16_t firstClusterLo;
    uint32_t fileSize;
} FAT_PACKED;

// ================================================================
// FAT32 Long File Name (LFN) Entry (32 bytes)
// ================================================================

struct FAT32_LFNEntry {
    uint8_t  order;
    uint16_t name1[5];            // UCS-2 chars 1-5
    uint8_t  attr;                // always 0x0F
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];            // UCS-2 chars 6-11
    uint16_t firstClusterLo;      // always 0
    uint16_t name3[2];            // UCS-2 chars 12-13
} FAT_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef FAT_PACKED

// Directory entry attribute flags
static const uint8_t ATTR_READ_ONLY  = 0x01;
static const uint8_t ATTR_HIDDEN     = 0x02;
static const uint8_t ATTR_SYSTEM     = 0x04;
static const uint8_t ATTR_VOLUME_ID  = 0x08;
static const uint8_t ATTR_DIRECTORY  = 0x10;
static const uint8_t ATTR_ARCHIVE    = 0x20;
static const uint8_t ATTR_LFN       = 0x0F;

// Special cluster values (FAT32)
static const uint32_t FAT32_CLUSTER_FREE = 0x00000000;
static const uint32_t FAT32_CLUSTER_BAD  = 0x0FFFFFF7;
static const uint32_t FAT32_CLUSTER_END  = 0x0FFFFFF8;
static const uint32_t FAT32_CLUSTER_MASK = 0x0FFFFFFF;

// ================================================================
// Mounted volume descriptor
// ================================================================

struct FATVolume {
    bool     mounted;
    FATType  type;
    uint8_t  blockDevIndex;       // index in block::get_device()

    // FAT32 geometry
    uint32_t bytesPerSector;
    uint32_t sectorsPerCluster;
    uint32_t reservedSectors;
    uint32_t numFATs;
    uint32_t fatSizeSectors;
    uint32_t rootCluster;
    uint32_t totalSectors;
    uint32_t firstDataSector;
    uint32_t totalDataClusters;

    // exFAT geometry
    uint64_t exfatVolumeLength;
    uint32_t exfatFatOffset;
    uint32_t exfatFatLength;
    uint32_t exfatClusterHeapOffset;
    uint32_t exfatClusterCount;
    uint8_t  exfatBytesPerSectorShift;
    uint8_t  exfatSectorsPerClusterShift;

    char     volumeLabel[12];
};

static const uint8_t MAX_FAT_VOLUMES = 4;

// ================================================================
// File handle for open files
// ================================================================

struct FATFile {
    bool     open;
    uint8_t  volumeIndex;
    uint32_t firstCluster;
    uint32_t fileSize;
    uint32_t currentCluster;
    uint32_t currentOffset;       // byte offset within the file
    uint8_t  attr;
};

static const uint8_t MAX_OPEN_FILES = 8;

// ================================================================
// Directory iteration
// ================================================================

struct DirEntry {
    char     name[256];           // long or short filename
    uint32_t fileSize;
    uint32_t firstCluster;
    uint8_t  attr;
    uint16_t crtDate;
    uint16_t crtTime;
    uint16_t wrtDate;
    uint16_t wrtTime;
    bool     isDir;
};

// ================================================================
// Public API
// ================================================================

// Initialise the FAT filesystem driver.
void init();

// Attempt to mount a FAT32 or exFAT volume on a block device.
// Returns the volume index, or 0xFF on failure.
uint8_t mount(uint8_t blockDevIndex);

// Unmount a volume.
void unmount(uint8_t volumeIndex);

// Open root directory for iteration.  Caller repeatedly calls
// read_dir() until it returns false.
bool open_root_dir(uint8_t volumeIndex);

// Read the next directory entry.  Returns false when exhausted.
bool read_dir(uint8_t volumeIndex, DirEntry* out);

// Open a file by cluster (obtained from DirEntry).
// Returns a file handle index, or 0xFF on failure.
uint8_t open_file(uint8_t volumeIndex, uint32_t firstCluster,
                  uint32_t fileSize, uint8_t attr);

// Read up to 'len' bytes from an open file into 'buffer'.
// Returns the number of bytes actually read.
uint32_t read_file(uint8_t fileHandle, void* buffer, uint32_t len);

// Overwrite bytes in an existing FAT32 file without extending its cluster chain.
// Returns the number of bytes written.
uint32_t write_file(uint8_t fileHandle, const void* buffer, uint32_t len);

// Overwrite an existing file by path and update its directory file size.
// Does not allocate additional clusters.
bool overwrite_path(uint8_t volumeIndex, const char* path, const void* buffer, uint32_t len);

// Create a new 8.3 file in an existing directory and write its contents.
bool create_file_path(uint8_t volumeIndex, const char* path, const void* buffer, uint32_t len);

// Close an open file.
void close_file(uint8_t fileHandle);

// Return volume info.
const FATVolume* get_volume(uint8_t volumeIndex);

// ================================================================
// Directory traversal by path (new functions for VFS integration)
// ================================================================

// Open a directory by cluster number for iteration.
// For root directory, use rootCluster from FATVolume.
bool open_dir(uint8_t volumeIndex, uint32_t dirCluster);

// Find a file or directory by name in the currently open directory.
// Returns true if found, fills out the DirEntry.
// Name comparison is case-insensitive for FAT32.
bool find_in_dir(uint8_t volumeIndex, const char* name, DirEntry* out);

// Lookup a file by full path relative to volume root.
// Traverses directories as needed.
// Returns true if found, fills out the DirEntry.
bool lookup_path(uint8_t volumeIndex, const char* path, DirEntry* out);

} // namespace fs_fat
} // namespace kernel

#endif // KERNEL_FS_FAT_H
