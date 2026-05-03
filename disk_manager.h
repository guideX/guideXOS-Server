//
// Disk Manager - guideXOS Server Port
//
// A Windows-like Disk Management UI with multi-disk support (System + USB MSC),
// left disk list, right volumes grid, and partition map. Buttons to switch filesystem drivers.
//
// Ported from guideXOS.Legacy/DefaultApps/DiskManager.cs
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gxos {
namespace apps {

class DiskManager {
public:
    static uint64_t Launch();
    static int main(int argc, char** argv);
    
private:
    // Layout constants
    static const int PAD = 12;
    static const int LEFT_PANE_W = 200;
    static const int HEADER_H = 26;
    static const int ROW_H = 24;
    static const int BTN_H = 26;
    static const int GAP = 8;
    
    // Partition table entry (MBR)
    struct PartitionEntry {
        uint32_t status;       // Boot flag (0x80 = bootable)
        uint8_t type;          // Partition type
        uint32_t lbaStart;     // Starting LBA
        uint32_t lbaCount;     // Size in sectors
        std::string fs;        // Detected filesystem ("FAT", "EXT2", "TarFS", "Unknown")
        std::string mountPoint; // Suggested mount point, or "unmounted"
        bool mounted;
        
        PartitionEntry() : status(0), type(0), lbaStart(0), lbaCount(0), mounted(false) {}
    };

    enum MbrStatus : uint8_t {
        MBR_UNREADABLE = 0,
        MBR_INVALID = 1,
        MBR_VALID = 2,
    };
    
    // Disk entry
    struct DiskEntry {
        std::string name;                  // "Disk 0 (System)", "Disk 1 (USB)", etc.
        std::string transportLabel;        // ATA, AHCI, NVMe, USB, RAM disk, unknown
        bool isSystem;                     // True for IDE/SATA system disk
        uint8_t devIndex;                  // Device index in block layer
        bool haveInfo;                     // True if size info available
        uint64_t totalSectors;             // Total disk capacity in sectors
        uint32_t bytesPerSector;           // Bytes per sector (usually 512)
        MbrStatus mbrStatus;               // MBR signature/read status
        PartitionEntry parts[4];           // MBR primary partitions
        
        DiskEntry() : isSystem(false), devIndex(0), haveInfo(false), 
                      totalSectors(0), bytesPerSector(512), mbrStatus(MBR_UNREADABLE) {}
    };
    
    // State
    static uint64_t s_windowId;
    static std::vector<DiskEntry> s_disks;
    static int s_selectedDiskIndex;
    static std::string s_status;
    static std::string s_detected;
    static bool s_clickLock;
    static std::string s_cachedTotalCaption;
    static int s_mouseX, s_mouseY;
    static bool s_mouseDown;
    
    // Button positions (for hit testing)
    static int s_bxDetectX, s_bxDetectY;
    static int s_bxAutoX, s_bxAutoY;
    static int s_bxSwitchFatX, s_bxSwitchFatY;
    static int s_bxSwitchTarX, s_bxSwitchTarY;
    static int s_bxSwitchExtX, s_bxSwitchExtY;
    static int s_bxFormatExfatX, s_bxFormatExfatY;
    static int s_bxCreatePartX, s_bxCreatePartY;
    static int s_bxRefreshX, s_bxRefreshY;
    
    // Core operations
    static void refreshDisks();
    static void probeOnce();
    static void readMBRForEntry(DiskEntry& entry);
    static DiskEntry* getSelected();
    
    // Filesystem operations
    static std::string detectFsAtLBA(uint8_t devIndex, uint32_t lbaStart);
    static void trySetFS_Auto();
    static void trySetFS_FAT();
    static void trySetFS_TAR();
    static void trySetFS_EXT2();
    static void tryFormatFAT();
    static void tryCreatePartitionLargestFree();
    
    // UI rendering
    static void render();
    static void drawLeftPane(int winX, int winY, int winW, int winH);
    static void drawVolumesGrid(int x, int y, int w, int h);
    static void drawMountsSection(int x, int y, int w, int h);
    static void drawPartitionMap(int x, int y, int w, int h);
    static void drawActions(int x, int y, int w, int h);
    static void drawHeaderCell(int x, int y, int w, int h, const char* text);
    static void drawCell(int x, int y, int w, int h, const char* text);
    static void drawButton(int x, int y, int w, int h, const char* text, bool hover);
    static void drawDisabledButton(int x, int y, int w, int h, const char* text);
    
    // Input handling
    static void handleMouseMove(int mx, int my);
    static void handleMouseDown(int mx, int my);
    static void handleMouseUp(int mx, int my);
    static void handleKey(int keyCode, bool down);
    static bool hit(int mx, int my, int x, int y, int w, int h);
    
    // Utilities
    static std::string fmtSize(uint64_t bytes);
    static std::string fmtHexByte(uint8_t value);
    static std::string mbrStatusText(MbrStatus status);
    static std::string partitionStatusText(const PartitionEntry& part, int partIndex);
    static std::string suggestMountPoint(const DiskEntry& disk, const PartitionEntry& part, int partIndex);
    static std::string buildStatus();
};

} // namespace apps
} // namespace gxos
