//
// Disk Manager - guideXOS Server Port
//
// Hosted Disk Manager: read-only .img preview. Bare-metal authoritative block
// device management is implemented by kernel::apps::DiskManagerApp.
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
    
    // Four primary entries for the hosted .img MBR viewer. Bare-metal disks
    // use kernel::storage's normalized MBR/GPT representation.
    struct PartitionEntry {
        uint32_t status;       // Boot flag (0x80 = bootable)
        uint8_t type;          // Partition type
        uint32_t lbaStart;     // Starting LBA
        uint32_t lbaCount;     // Size in sectors
        std::string fs;        // Read-only signature probe result.
        
        PartitionEntry() : status(0), type(0), lbaStart(0), lbaCount(0) {}
    };

    enum MbrStatus : uint8_t {
        MBR_UNREADABLE = 0,
        MBR_INVALID = 1,
        MBR_PREVIEW_VALID = 2,
        DISK_NOT_INITIALIZED = 3,
        DISK_GPT_UNSUPPORTED = 4,
        DISK_UNSUPPORTED = 5,
        MBR_PREVIEW_NO_SIGNATURE = 6,
        MBR_VALID = 7,
        DISK_GPT_VALID = 8,
        DISK_GPT_DEGRADED = 9,
    };
    
    // Disk entry
    struct DiskEntry {
        std::string name;                  // Display name for a disk or host image.
        std::string transportLabel;        // ATA, AHCI, NVMe, USB, RAM disk, unknown
        bool isHostImage;                  // Windows host mode: backed by a .img file
        uint8_t devIndex;                  // Device index in block layer
        bool haveInfo;                     // True if size info available
        uint64_t totalSectors;             // Total disk capacity in sectors
        uint32_t bytesPerSector;           // Bytes per sector (usually 512)
        MbrStatus mbrStatus;               // Partition table parse state
        std::string backingPath;           // Optional source path in host mode
        PartitionEntry parts[4];           // MBR primary preview entries only
        
        DiskEntry() : isHostImage(false), devIndex(0), haveInfo(false),
                      totalSectors(0), bytesPerSector(512), mbrStatus(MBR_UNREADABLE) {}
    };

    struct HostImageEntry {
        std::string path;
        std::string displayName;
        bool attached;
#ifndef _WIN32
        uint8_t* data;
        uint32_t sizeBytes;
        uint8_t ramdiskIndex;
        uint8_t ramdiskBlockIndex;
        uint64_t ramdiskIdentity;
#endif

        HostImageEntry() : attached(false)
#ifndef _WIN32
            , data(nullptr), sizeBytes(0), ramdiskIndex(0xFF), ramdiskBlockIndex(0xFF), ramdiskIdentity(0)
#endif
        {}
    };
    
    // State
    static uint64_t s_windowId;
    static std::vector<DiskEntry> s_disks;
    static int s_selectedDiskIndex;
    static std::string s_status;
    static bool s_clickLock;
    static int s_mouseX, s_mouseY;
    static bool s_mouseDown;
    static std::vector<HostImageEntry> s_hostImages;
    static int s_selectedHostImageIndex;
    
    // Button positions (for hit testing)
    static int s_bxRefreshX, s_bxRefreshY;
    static int s_bxAttachImageX, s_bxAttachImageY;
    static int s_bxPrevImageX, s_bxPrevImageY;
    static int s_bxNextImageX, s_bxNextImageY;
    static int s_bxRescanImagesX, s_bxRescanImagesY;
    
    // Core operations
    static void refreshDisks();
    static void readMBRForEntry(DiskEntry& entry);
    static DiskEntry* getSelected();
    static void refreshHostImageLibrary();
    static void attachSelectedHostImage();
    static void selectPrevHostImage();
    static void selectNextHostImage();
    static bool buildHostDiskEntryFromImage(const HostImageEntry& image, uint8_t devIndex, DiskEntry& entry);
    static bool isImgName(const char* name);
    
    // UI rendering
    static void render();
    static void drawLeftPane(int winX, int winY, int winW, int winH);
    static void drawVolumesGrid(int x, int y, int w, int h);
    static void drawPartitionMap(int x, int y, int w, int h);
    static void drawActions(int x, int y, int w, int h);
    static void drawHeaderCell(int x, int y, int w, int h, const char* text);
    static void drawCell(int x, int y, int w, int h, const char* text);
    static void drawButton(int x, int y, int w, int h, const char* text, bool hover);
    
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
    static std::string buildStatus();
};

} // namespace apps
} // namespace gxos
