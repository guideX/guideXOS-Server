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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos {
namespace gui {

// Forward declaration
struct WinInfo;

class DiskManager {
public:
    static void show(int x, int y);
    static void close();
    static bool isVisible();
    
private:
    // Window management
    static uint64_t s_windowId;
    static bool s_visible;
    
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
        
        PartitionEntry() : status(0), type(0), lbaStart(0), lbaCount(0) {}
    };
    
    // Disk entry
    struct DiskEntry {
        std::string name;                  // "Disk 0 (System)", "Disk 1 (USB)", etc.
        bool isSystem;                     // True for IDE/SATA system disk
        void* usbDisk;                     // USBDisk pointer (null for system)
        bool haveInfo;                     // True if size info available
        uint64_t totalSectors;             // Total disk capacity in sectors
        uint32_t bytesPerSector;           // Bytes per sector (usually 512)
        PartitionEntry parts[4];           // MBR primary partitions
        
        DiskEntry() : isSystem(false), usbDisk(nullptr), haveInfo(false), 
                      totalSectors(0), bytesPerSector(512) {}
    };
    
    // State
    static std::vector<DiskEntry> s_disks;
    static int s_selectedDiskIndex;
    static std::string s_status;
    static std::string s_detected;
    static bool s_clickLock;
    static std::string s_cachedTotalCaption;
    
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
    static void init(int x, int y);
    static void refreshDisks();
    static void probeOnce();
    static void readMBRForEntry(DiskEntry& entry);
    static DiskEntry* getSelected();
    
    // Filesystem operations
    static std::string detectFsAtLBA(uint32_t lbaStart);
    static void trySetFS_Auto();
    static void trySetFS_FAT();
    static void trySetFS_TAR();
    static void trySetFS_EXT2();
    static void tryFormatFAT();
    static void tryCreatePartitionLargestFree();
    
    // Disk I/O helpers (with disk switching for USB)
    template<typename Func>
    static void withDisk(DiskEntry* entry, Func&& func);
    
    // UI rendering
    static void draw();
    static void drawLeftPane(WinInfo* win);
    static void drawVolumesGrid(int x, int y, int w, int h, WinInfo* win);
    static void drawPartitionMap(int x, int y, int w, int h, WinInfo* win);
    static void drawActions(int x, int y, int w, int h, WinInfo* win);
    static void drawHeaderCell(int x, int y, int w, int h, const char* text);
    static void drawCell(int x, int y, int w, int h, const char* text);
    static void drawButton(int x, int y, int w, int h, const char* text);
    
    // Input handling
    static void handleInput(WinInfo* win);
    static bool hit(int mx, int my, int x, int y, int w, int h);
    
    // Utilities
    static std::string fmtSize(uint64_t bytes);
    static std::string buildStatus();
};

} // namespace gui
} // namespace gxos
