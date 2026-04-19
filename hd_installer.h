// HD Installer - guideXOS Server Port
//
// A comprehensive OS installer with multi-step wizard UI that guides users
// through installing guideXOS to their hard drive when booting from USB.
//
// Features:
// - 6-step wizard (Welcome, Disk Selection, Partition Setup, Format Warning, Installing, Complete)
// - Support for IDE/SATA system disks and USB storage devices
// - Automatic partitioning (Boot FAT32 + System partition)
// - Progress tracking with visual feedback
// - USB removal detection before reboot
//
// Ported from guideXOS.Legacy/DefaultApps/HDInstaller.cs
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gxos {
namespace apps {

class HDInstaller {
public:
    static uint64_t Launch();
    static int main(int argc, char** argv);
    
private:
    // Installation wizard steps
    enum class InstallStep {
        Welcome = 0,
        DiskSelection = 1,
        PartitionSetup = 2,
        FormatWarning = 3,
        Installing = 4,
        Complete = 5
    };
    
    // Disk information for selection
    struct DiskInfo {
        std::string name;          // Display name (e.g., "System Disk 0 (IDE)")
        bool isUSB;                // True if USB storage device
        uint64_t totalSectors;     // Total disk capacity in sectors
        uint32_t bytesPerSector;   // Sector size (typically 512)
        uint8_t devIndex;          // Device index for block layer access
        
        DiskInfo() : isUSB(false), totalSectors(0), bytesPerSector(512), devIndex(0) {}
    };
    
    // Layout constants
    static const int PAD = 20;
    static const int BTN_W = 120;
    static const int BTN_H = 32;
    
    // Window state
    static uint64_t s_windowId;
    static int s_currentStep;
    static bool s_clickLock;
    
    // Disk selection
    static std::vector<DiskInfo> s_availableDisks;
    static int s_selectedDiskIndex;
    
    // Partition configuration (editable in step 2)
    static int s_bootPartitionMB;    // Boot partition size in MB (64-1024)
    static std::string s_systemFs;   // System filesystem type ("FAT32", "EXT2", etc.)
    
    // Installation progress
    static bool s_installInProgress;
    static int s_installProgress;    // 0-100%
    static std::string s_statusMessage;
    
    // Installation phase flags
    static bool s_didPartition;
    static bool s_didFormat;
    static bool s_didCopyFiles;
    static bool s_didBootloader;
    
    // Button coordinates (calculated during draw)
    static int s_btnNextX, s_btnNextY;
    static int s_btnBackX, s_btnBackY;
    static int s_btnCancelX, s_btnCancelY;
    
    // Mouse state
    static int s_mouseX, s_mouseY;
    static bool s_mouseDown;
    
    // Event handlers
    static void handleMouseDown(uint32_t params);
    static void handleMouseUp(uint32_t params);
    static void handleMouseMove(uint32_t params);
    static void handleKeyPress(uint32_t params);
    static void handlePaint(uint32_t params);
    static void handleClose(uint32_t params);
    
    // Initialization and scanning
    static void initWindow();
    static void scanDisks();
    
    // UI drawing functions
    static void drawStepIndicator();
    static void drawWelcomeStep();
    static void drawDiskSelectionStep();
    static void drawPartitionSetupStep();
    static void drawFormatWarningStep();
    static void drawInstallingStep();
    static void drawCompleteStep();
    static void drawNavigationButtons();
    static void drawButton(int x, int y, int w, int h, const char* label, bool enabled);
    
    // Input handling
    static void handleInput();
    static bool handleNextButton();
    static bool hitTest(int mx, int my, int x, int y, int w, int h);
    
    // Installation operations
    static void startInstallation();
    static bool partitionDisk();
    static bool formatPartitions();
    static bool copySystemFiles();
    static bool installBootloader();
    static bool checkUSBInstallMediaPresent();
    
    // Helper functions
    static std::string formatSize(uint64_t bytes);
    static void createBootConfiguration();
    static std::vector<uint8_t> getGRUBStage1();
    
    // Filesystem helpers
    static bool formatFAT32Partition(uint8_t devIndex, uint64_t startSector, 
                                     uint64_t sectorCount, uint32_t bytesPerSector, 
                                     const char* label);
    static std::vector<std::string> gatherAllFiles(const std::string& path);
    static void ensureDirectoryExists(const std::string& path);
    static std::string getDirectoryPath(const std::string& filePath);
};

} // namespace apps
} // namespace gxos
