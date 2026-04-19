// HD Installer - Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "hd_installer.h"
#include "gui_protocol.h"
#include "logger.h"
#include "process.h"
#include "ipc_bus.h"
#include <sstream>
#include <algorithm>
#include <cstring>

namespace gxos {
namespace apps {

using namespace gxos::gui;

// Static member initialization
uint64_t HDInstaller::s_windowId = 0;
int HDInstaller::s_currentStep = 0;
bool HDInstaller::s_clickLock = false;

std::vector<HDInstaller::DiskInfo> HDInstaller::s_availableDisks;
int HDInstaller::s_selectedDiskIndex = -1;

int HDInstaller::s_bootPartitionMB = 100;  // Default 100MB boot partition
std::string HDInstaller::s_systemFs = "FAT32";  // Default filesystem

bool HDInstaller::s_installInProgress = false;
int HDInstaller::s_installProgress = 0;
std::string HDInstaller::s_statusMessage = "";

bool HDInstaller::s_didPartition = false;
bool HDInstaller::s_didFormat = false;
bool HDInstaller::s_didCopyFiles = false;
bool HDInstaller::s_didBootloader = false;

int HDInstaller::s_btnNextX = 0;
int HDInstaller::s_btnNextY = 0;
int HDInstaller::s_btnBackX = 0;
int HDInstaller::s_btnBackY = 0;
int HDInstaller::s_btnCancelX = 0;
int HDInstaller::s_btnCancelY = 0;

int HDInstaller::s_mouseX = 0;
int HDInstaller::s_mouseY = 0;
bool HDInstaller::s_mouseDown = false;

uint64_t HDInstaller::Launch() {
    ProcessSpec spec{"hdinstaller", HDInstaller::main};
    return ProcessTable::spawn(spec, {"hdinstaller"});
}

int HDInstaller::main(int argc, char** argv) {
    try {
        Logger::write(LogLevel::Info, "HDInstaller starting...");

        // Initialize window
        initWindow();
        
        // Scan for available disks
        scanDisks();
        
        // Message loop
        while (true) {
            IPCMessage msg;
            if (!IPCBus::receive(msg, 16)) {
                continue;
            }
            
            if (msg.type == IPCMessageType::WindowEvent) {
                uint32_t eventType = msg.data[0];
                uint32_t params = msg.data[1];
                
                switch (static_cast<WindowEventType>(eventType)) {
                    case WindowEventType::MouseDown:
                        handleMouseDown(params);
                        break;
                    case WindowEventType::MouseUp:
                        handleMouseUp(params);
                        break;
                    case WindowEventType::MouseMove:
                        handleMouseMove(params);
                        break;
                    case WindowEventType::KeyPress:
                        handleKeyPress(params);
                        break;
                    case WindowEventType::Paint:
                        handlePaint(params);
                        break;
                    case WindowEventType::Close:
                        handleClose(params);
                        return 0;
                    default:
                        break;
                }
            }
        }
        
        return 0;
    }
    catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("HDInstaller exception: ") + e.what());
        return 1;
    }
}

void HDInstaller::initWindow() {
    // Create window
    WindowCreateRequest req;
    req.x = 100;
    req.y = 100;
    req.width = 700;
    req.height = 500;
    req.flags = WindowFlags::Resizable | WindowFlags::ShowInTaskbar;
    std::strncpy(req.title, "Install guideXOS to Hard Drive", sizeof(req.title) - 1);
    
    s_windowId = createWindow(req);
    if (s_windowId == 0) {
        throw std::runtime_error("Failed to create window");
    }
    
    Logger::write(LogLevel::Info, "HDInstaller window created");
}

void HDInstaller::scanDisks() {
    s_availableDisks.clear();
    
    // TODO: Implement disk scanning via block layer
    // For now, add a placeholder system disk
    DiskInfo sysInfo;
    sysInfo.name = "System Disk 0 (IDE/SATA)";
    sysInfo.isUSB = false;
    sysInfo.totalSectors = 20971520;  // ~10GB
    sysInfo.bytesPerSector = 512;
    sysInfo.devIndex = 0;
    s_availableDisks.push_back(sysInfo);
    
    // TODO: Scan for USB mass storage devices
    // This would query the USB subsystem for MSC devices
    
    Logger::write(LogLevel::Info, std::string("Found ") + std::to_string(s_availableDisks.size()) + " disk(s)");
}

void HDInstaller::handleMouseDown(uint32_t params) {
    s_mouseDown = true;
    if (!s_clickLock) {
        handleInput();
        s_clickLock = true;
    }
}

void HDInstaller::handleMouseUp(uint32_t params) {
    s_mouseDown = false;
    s_clickLock = false;
}

void HDInstaller::handleMouseMove(uint32_t params) {
    s_mouseX = static_cast<int16_t>(params & 0xFFFF);
    s_mouseY = static_cast<int16_t>((params >> 16) & 0xFFFF);
}

void HDInstaller::handleKeyPress(uint32_t params) {
    // Handle keyboard input for partition setup step
    if (s_currentStep == static_cast<int>(InstallStep::PartitionSetup)) {
        char key = static_cast<char>(params & 0xFF);
        
        if (key == '+' || key == '=') {
            // Increase boot partition size
            s_bootPartitionMB += 10;
            if (s_bootPartitionMB > 1024) s_bootPartitionMB = 1024;
            invalidateWindow(s_windowId);
        }
        else if (key == '-' || key == '_') {
            // Decrease boot partition size
            s_bootPartitionMB -= 10;
            if (s_bootPartitionMB < 64) s_bootPartitionMB = 64;
            invalidateWindow(s_windowId);
        }
        else if (key == ' ') {
            // Toggle filesystem type
            if (s_systemFs == "FAT32") s_systemFs = "EXT2";
            else if (s_systemFs == "EXT2") s_systemFs = "EXT3";
            else if (s_systemFs == "EXT3") s_systemFs = "EXT4";
            else s_systemFs = "FAT32";
            invalidateWindow(s_windowId);
        }
    }
}

void HDInstaller::handlePaint(uint32_t params) {
    // Get window dimensions
    WindowInfo info;
    if (!getWindowInfo(s_windowId, info)) {
        return;
    }
    
    // Background
    fillRect(s_windowId, 0, 0, info.width, info.height, 0xFF2B2B2B);
    
    // Draw step indicator at top
    drawStepIndicator();
    
    // Draw content based on current step
    switch (static_cast<InstallStep>(s_currentStep)) {
        case InstallStep::Welcome:
            drawWelcomeStep();
            break;
        case InstallStep::DiskSelection:
            drawDiskSelectionStep();
            break;
        case InstallStep::PartitionSetup:
            drawPartitionSetupStep();
            break;
        case InstallStep::FormatWarning:
            drawFormatWarningStep();
            break;
        case InstallStep::Installing:
            drawInstallingStep();
            break;
        case InstallStep::Complete:
            drawCompleteStep();
            break;
    }
    
    // Draw navigation buttons
    drawNavigationButtons();
    
    // Draw status message if any
    if (!s_statusMessage.empty()) {
        WindowInfo winfo;
        if (getWindowInfo(s_windowId, winfo)) {
            drawText(s_windowId, PAD, winfo.height - 80, s_statusMessage.c_str(), 0xFFFFFFFF);
        }
    }
}

void HDInstaller::handleClose(uint32_t params) {
    if (s_currentStep != static_cast<int>(InstallStep::Installing)) {
        destroyWindow(s_windowId);
    }
}

void HDInstaller::handleInput() {
    WindowInfo info;
    if (!getWindowInfo(s_windowId, info)) {
        return;
    }
    
    int mx = s_mouseX;
    int my = s_mouseY;
    
    // Navigation buttons
    if (s_currentStep > 0 && s_currentStep < static_cast<int>(InstallStep::Installing)) {
        if (hitTest(mx, my, s_btnBackX, s_btnBackY, BTN_W, BTN_H)) {
            s_currentStep--;
            invalidateWindow(s_windowId);
            return;
        }
    }
    
    if (s_currentStep < static_cast<int>(InstallStep::Installing)) {
        if (hitTest(mx, my, s_btnNextX, s_btnNextY, BTN_W, BTN_H)) {
            if (handleNextButton()) {
                s_currentStep++;
            }
            invalidateWindow(s_windowId);
            return;
        }
    }
    
    if (hitTest(mx, my, s_btnCancelX, s_btnCancelY, BTN_W, BTN_H)) {
        if (s_currentStep != static_cast<int>(InstallStep::Installing)) {
            destroyWindow(s_windowId);
        }
        return;
    }
    
    // Disk selection in step 1
    if (s_currentStep == static_cast<int>(InstallStep::DiskSelection)) {
        int listY = 120;
        for (size_t i = 0; i < s_availableDisks.size(); i++) {
            int diskY = listY + i * 60;
            if (hitTest(mx, my, PAD, diskY, info.width - PAD * 2, 50)) {
                s_selectedDiskIndex = static_cast<int>(i);
                invalidateWindow(s_windowId);
                return;
            }
        }
    }
}

bool HDInstaller::handleNextButton() {
    switch (static_cast<InstallStep>(s_currentStep)) {
        case InstallStep::Welcome:
            return true;
            
        case InstallStep::DiskSelection:
            if (s_selectedDiskIndex < 0) {
                s_statusMessage = "Please select a disk to install to.";
                return false;
            }
            return true;
            
        case InstallStep::PartitionSetup:
            return true;
            
        case InstallStep::FormatWarning:
            // Start installation
            s_installInProgress = true;
            s_installProgress = 0;
            startInstallation();
            return true;
            
        case InstallStep::Complete:
            destroyWindow(s_windowId);
            return false;
            
        default:
            return true;
    }
}

bool HDInstaller::hitTest(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

void HDInstaller::drawStepIndicator() {
    WindowInfo info;
    if (!getWindowInfo(s_windowId, info)) {
        return;
    }
    
    int indicatorY = 40;
    int stepCount = 5;
    int stepWidth = (info.width - PAD * 2) / stepCount;
    
    for (int i = 0; i < stepCount; i++) {
        int stepX = PAD + i * stepWidth;
        uint32_t color = i <= s_currentStep ? 0xFF4C8BF5 : 0xFF555555;
        
        // Draw circle
        fillRect(s_windowId, stepX + stepWidth / 2 - 15, indicatorY - 15, 30, 30, color);
        
        // Draw line to next step
        if (i < stepCount - 1) {
            uint32_t lineColor = i < s_currentStep ? 0xFF4C8BF5 : 0xFF555555;
            fillRect(s_windowId, stepX + stepWidth / 2 + 15, indicatorY - 2, stepWidth - 30, 4, lineColor);
        }
        
        // Draw step number
        std::string stepNum = std::to_string(i + 1);
        drawText(s_windowId, stepX + stepWidth / 2 - 8, indicatorY - 8, stepNum.c_str(), 0xFFFFFFFF);
    }
}

void HDInstaller::drawWelcomeStep() {
    int contentY = 100;
    
    drawText(s_windowId, PAD, contentY, "Welcome to guideXOS Installer", 0xFFFFFFFF);
    contentY += 40;
    
    const char* lines[] = {
        "This wizard will guide you through installing guideXOS",
        "to your computer's hard drive.",
        "",
        "You are currently running guideXOS from a USB flash drive.",
        "Installing to your hard drive will provide:",
        "",
        "  - Faster boot times",
        "  - Persistent storage for files and settings",
        "  - Better performance",
        "  - No need for the USB drive after installation",
        "",
        "WARNING: This will erase all data on the selected disk!",
        "Make sure you have backed up any important data.",
        "",
        "Click Next to continue."
    };
    
    for (const char* line : lines) {
        drawText(s_windowId, PAD, contentY, line, 0xFFFFFFFF);
        contentY += 22;
    }
}

void HDInstaller::drawDiskSelectionStep() {
    int contentY = 100;
    
    drawText(s_windowId, PAD, contentY, "Select Installation Disk", 0xFFFFFFFF);
    contentY += 30;
    
    drawText(s_windowId, PAD, contentY, "Choose the disk where guideXOS will be installed:", 0xFFFFFFFF);
    contentY += 30;
    
    if (s_availableDisks.empty()) {
        drawText(s_windowId, PAD, contentY, "No suitable disks found.", 0xFFFFFFFF);
        return;
    }
    
    WindowInfo info;
    if (!getWindowInfo(s_windowId, info)) {
        return;
    }
    
    // Draw disk list
    for (size_t i = 0; i < s_availableDisks.size(); i++) {
        const auto& disk = s_availableDisks[i];
        bool selected = (static_cast<int>(i) == s_selectedDiskIndex);
        
        uint32_t bgColor = selected ? 0xFF3A3A3A : 0xFF2A2A2A;
        uint32_t borderColor = selected ? 0xFF4C8BF5 : 0xFF555555;
        
        fillRect(s_windowId, PAD, contentY, info.width - PAD * 2, 50, bgColor);
        drawRect(s_windowId, PAD, contentY, info.width - PAD * 2, 50, borderColor);
        
        std::string sizeStr = formatSize(disk.totalSectors * disk.bytesPerSector);
        drawText(s_windowId, PAD + 10, contentY + 8, disk.name.c_str(), 0xFFFFFFFF);
        
        std::string sizeText = "Size: " + sizeStr;
        drawText(s_windowId, PAD + 10, contentY + 28, sizeText.c_str(), 0xFFAAAAAA);
        
        contentY += 60;
    }
}

void HDInstaller::drawPartitionSetupStep() {
    int contentY = 100;
    
    drawText(s_windowId, PAD, contentY, "Partition Configuration", 0xFFFFFFFF);
    contentY += 40;
    
    if (s_selectedDiskIndex >= 0 && s_selectedDiskIndex < static_cast<int>(s_availableDisks.size())) {
        const auto& disk = s_availableDisks[s_selectedDiskIndex];
        
        std::string diskInfo = "Disk: " + disk.name;
        drawText(s_windowId, PAD, contentY, diskInfo.c_str(), 0xFFFFFFFF);
        contentY += 22;
        
        std::string sizeInfo = "Total Size: " + formatSize(disk.totalSectors * disk.bytesPerSector);
        drawText(s_windowId, PAD, contentY, sizeInfo.c_str(), 0xFFFFFFFF);
        contentY += 30;
        
        drawText(s_windowId, PAD, contentY, "Partition Layout (editable):", 0xFFFFFFFF);
        contentY += 22;
        
        std::string bootPart = "  - Boot Partition (FAT32) - " + std::to_string(s_bootPartitionMB) + " MB";
        drawText(s_windowId, PAD, contentY, bootPart.c_str(), 0xFFFFFFFF);
        contentY += 22;
        
        std::string sysPart = "  - System Partition (" + s_systemFs + ") - Remaining space";
        drawText(s_windowId, PAD, contentY, sysPart.c_str(), 0xFFFFFFFF);
        contentY += 30;
        
        drawText(s_windowId, PAD, contentY, "Adjustments:", 0xFFFFFFFF);
        contentY += 22;
        drawText(s_windowId, PAD, contentY, "  [+/-] Increase/Decrease boot size (64 MB - 1024 MB)", 0xFFAAAAAA);
        contentY += 22;
        drawText(s_windowId, PAD, contentY, "  [Space] Toggle system filesystem (FAT32/EXT2/EXT3/EXT4)", 0xFFAAAAAA);
        contentY += 30;
        
        drawText(s_windowId, PAD, contentY, "Click Next to continue.", 0xFFFFFFFF);
    }
}

void HDInstaller::drawFormatWarningStep() {
    int contentY = 100;
    
    drawText(s_windowId, PAD, contentY, "WARNING: Data Will Be Erased", 0xFFFF4444);
    contentY += 50;
    
    if (s_selectedDiskIndex >= 0 && s_selectedDiskIndex < static_cast<int>(s_availableDisks.size())) {
        const auto& disk = s_availableDisks[s_selectedDiskIndex];
        
        const char* lines[] = {
            "The following disk will be formatted:",
            "",
        };
        
        for (const char* line : lines) {
            drawText(s_windowId, PAD, contentY, line, 0xFFFFFFFF);
            contentY += 22;
        }
        
        std::string diskName = "  " + disk.name;
        drawText(s_windowId, PAD, contentY, diskName.c_str(), 0xFFFFFFFF);
        contentY += 22;
        
        std::string sizeInfo = "  Size: " + formatSize(disk.totalSectors * disk.bytesPerSector);
        drawText(s_windowId, PAD, contentY, sizeInfo.c_str(), 0xFFFFFFFF);
        contentY += 30;
        
        const char* warnings[] = {
            "ALL DATA ON THIS DISK WILL BE PERMANENTLY DELETED!",
            "",
            "This includes:",
            "  - All files and folders",
            "  - Any existing operating systems",
            "  - All personal data",
            "  - All applications and programs",
            "",
            "This action CANNOT be undone!",
            "",
            "Make sure you have:",
            "  * Backed up all important data",
            "  * Selected the correct disk",
            "  * Saved any work in progress",
            "",
            "Click Install to begin installation, or Back to change settings."
        };
        
        for (const char* line : warnings) {
            drawText(s_windowId, PAD, contentY, line, 0xFFFFFFFF);
            contentY += 22;
        }
    }
}

void HDInstaller::drawInstallingStep() {
    WindowInfo info;
    if (!getWindowInfo(s_windowId, info)) {
        return;
    }
    
    int contentY = 100;
    
    drawText(s_windowId, PAD, contentY, "Installing guideXOS", 0xFFFFFFFF);
    contentY += 50;
    
    // Simulate installation progress
    if (s_installInProgress) {
        if (!s_didPartition) {
            s_statusMessage = "Partitioning disk...";
            if (partitionDisk()) {
                s_didPartition = true;
                s_installProgress = 15;
            } else {
                s_statusMessage = "Error: Partitioning failed";
                s_installInProgress = false;
            }
        } else if (!s_didFormat) {
            s_statusMessage = "Formatting partitions...";
            if (formatPartitions()) {
                s_didFormat = true;
                s_installProgress = 30;
            } else {
                s_statusMessage = "Error: Formatting failed";
                s_installInProgress = false;
            }
        } else if (!s_didCopyFiles) {
            s_statusMessage = "Copying system files... (this may take a while)";
            if (copySystemFiles()) {
                s_didCopyFiles = true;
                s_installProgress = 85;
                createBootConfiguration();
            } else {
                s_statusMessage = "Error: File copying failed";
                s_installInProgress = false;
            }
        } else if (!s_didBootloader) {
            s_statusMessage = "Installing bootloader...";
            if (installBootloader()) {
                s_didBootloader = true;
                s_installProgress = 95;
            } else {
                s_statusMessage = "Error: Bootloader installation failed";
                s_installInProgress = false;
            }
        } else {
            s_statusMessage = "Finalizing installation...";
            s_installProgress++;
            if (s_installProgress >= 100) {
                s_installProgress = 100;
                s_statusMessage = "Installation complete!";
                s_installInProgress = false;
                s_currentStep = static_cast<int>(InstallStep::Complete);
            }
        }
    }
    
    // Progress bar
    int progressBarW = info.width - PAD * 4;
    int progressBarH = 30;
    fillRect(s_windowId, PAD * 2, contentY, progressBarW, progressBarH, 0xFF1E1E1E);
    
    int fillWidth = (progressBarW * s_installProgress) / 100;
    fillRect(s_windowId, PAD * 2, contentY, fillWidth, progressBarH, 0xFF4C8BF5);
    
    std::string progressText = std::to_string(s_installProgress) + "%";
    drawText(s_windowId, info.width / 2 - 20, contentY + 8, progressText.c_str(), 0xFFFFFFFF);
    
    contentY += progressBarH + 30;
    
    // Installation steps
    const char* steps[] = {
        s_didPartition ? "* Partitioning disk..." : "  Partitioning disk...",
        s_didFormat ? "* Formatting partitions..." : "  Formatting partitions...",
        s_didCopyFiles ? "* Copying system files..." : "  Copying system files...",
        s_didBootloader ? "* Installing bootloader..." : "  Installing bootloader...",
        s_installProgress > 95 ? "* Configuring system..." : "  Configuring system...",
        s_installProgress >= 100 ? "* Installation complete!" : "  Finalizing installation..."
    };
    
    for (const char* step : steps) {
        drawText(s_windowId, PAD, contentY, step, 0xFFFFFFFF);
        contentY += 26;
    }
    
    contentY += 20;
    drawText(s_windowId, PAD, contentY, "Please wait, do not power off your computer...", 0xFFFFFFFF);
}

void HDInstaller::drawCompleteStep() {
    int contentY = 100;
    
    drawText(s_windowId, PAD, contentY, "Installation Complete!", 0xFF00FF00);
    contentY += 40;
    
    bool usbStillPresent = checkUSBInstallMediaPresent();
    
    if (usbStillPresent) {
        // Warning box
        WindowInfo info;
        if (getWindowInfo(s_windowId, info)) {
            int warningH = 140;
            fillRect(s_windowId, PAD, contentY, info.width - PAD * 2, warningH, 0xFFDD4400);
            drawRect(s_windowId, PAD, contentY, info.width - PAD * 2, warningH, 0xFFFF6600);
            
            int warnY = contentY + 10;
            drawText(s_windowId, PAD + 10, warnY, "WARNING: REMOVE INSTALLATION MEDIA", 0xFFFFFFFF);
            warnY += 30;
            
            const char* warnLines[] = {
                "Before rebooting, you MUST remove the USB flash drive",
                "or installation media from your computer.",
                "",
                "If you reboot without removing it, your computer will",
                "boot from the USB drive again instead of the newly",
                "installed system on your hard drive.",
                "",
                "Please remove the USB drive now."
            };
            
            for (const char* line : warnLines) {
                drawText(s_windowId, PAD + 10, warnY, line, 0xFFFFFFFF);
                warnY += 18;
            }
            
            contentY += warningH + 20;
        }
    } else {
        // Success box
        WindowInfo info;
        if (getWindowInfo(s_windowId, info)) {
            int successH = 80;
            fillRect(s_windowId, PAD, contentY, info.width - PAD * 2, successH, 0xFF228B22);
            drawRect(s_windowId, PAD, contentY, info.width - PAD * 2, successH, 0xFF32CD32);
            
            int successY = contentY + 10;
            drawText(s_windowId, PAD + 10, successY, "Installation media removed - Ready to reboot!", 0xFFFFFFFF);
            successY += 30;
            
            const char* successLines[] = {
                "Your computer is now ready to boot from the hard drive.",
                "Click the Reboot button below to restart and enjoy",
                "your newly installed guideXOS system!"
            };
            
            for (const char* line : successLines) {
                drawText(s_windowId, PAD + 10, successY, line, 0xFFFFFFFF);
                successY += 18;
            }
            
            contentY += successH + 20;
        }
    }
    
    // Installation summary
    const char* summaryLines[] = {
        "guideXOS has been successfully installed!",
        "",
        "You can now enjoy:",
        "  * Faster boot times",
        "  * Persistent file storage",
        "  * Better performance",
        "  * Full system access",
        "",
        "Thank you for choosing guideXOS!"
    };
    
    for (const char* line : summaryLines) {
        drawText(s_windowId, PAD, contentY, line, 0xFFFFFFFF);
        contentY += 22;
    }
}

void HDInstaller::drawNavigationButtons() {
    WindowInfo info;
    if (!getWindowInfo(s_windowId, info)) {
        return;
    }
    
    int btnY = info.height - 60;
    
    // Next/Finish button
    s_btnNextX = info.width - PAD - BTN_W;
    s_btnNextY = btnY;
    const char* nextLabel = s_currentStep == static_cast<int>(InstallStep::Complete) ? "Finish" :
                           s_currentStep == static_cast<int>(InstallStep::FormatWarning) ? "Install" : "Next";
    bool nextEnabled = s_currentStep != static_cast<int>(InstallStep::Installing);
    drawButton(s_btnNextX, s_btnNextY, BTN_W, BTN_H, nextLabel, nextEnabled);
    
    // Back button
    s_btnBackX = info.width - PAD - BTN_W * 2 - 10;
    s_btnBackY = btnY;
    bool backEnabled = s_currentStep > 0 && s_currentStep < static_cast<int>(InstallStep::Installing);
    if (backEnabled) {
        drawButton(s_btnBackX, s_btnBackY, BTN_W, BTN_H, "Back", true);
    }
    
    // Cancel button
    s_btnCancelX = PAD;
    s_btnCancelY = btnY;
    bool cancelEnabled = s_currentStep != static_cast<int>(InstallStep::Installing);
    drawButton(s_btnCancelX, s_btnCancelY, BTN_W, BTN_H, "Cancel", cancelEnabled);
}

void HDInstaller::drawButton(int x, int y, int w, int h, const char* label, bool enabled) {
    uint32_t bgColor = enabled ? 0xFF3A3A3A : 0xFF2A2A2A;
    uint32_t textColor = enabled ? 0xFFFFFFFF : 0xFF666666;
    
    if (enabled && hitTest(s_mouseX, s_mouseY, x, y, w, h)) {
        bgColor = 0xFF4C8BF5;
    }
    
    fillRect(s_windowId, x, y, w, h, bgColor);
    drawRect(s_windowId, x, y, w, h, 0xFF555555);
    
    // Center text
    int textX = x + w / 2 - (std::strlen(label) * 4);  // Approximate centering
    int textY = y + h / 2 - 8;
    drawText(s_windowId, textX, textY, label, textColor);
}

void HDInstaller::startInstallation() {
    s_statusMessage = "Installing guideXOS...";
    s_didPartition = false;
    s_didFormat = false;
    s_didCopyFiles = false;
    s_didBootloader = false;
}

bool HDInstaller::partitionDisk() {
    // TODO: Implement actual disk partitioning via block layer
    // For now, simulate success
    Logger::write(LogLevel::Info, "HDInstaller: Partitioning disk (simulated)");
    return true;
}

bool HDInstaller::formatPartitions() {
    // TODO: Implement actual partition formatting
    // For now, simulate success
    Logger::write(LogLevel::Info, "HDInstaller: Formatting partitions (simulated)");
    return true;
}

bool HDInstaller::copySystemFiles() {
    // TODO: Implement actual file copying from ramdisk to target partition
    // For now, simulate success
    Logger::write(LogLevel::Info, "HDInstaller: Copying system files (simulated)");
    return true;
}

bool HDInstaller::installBootloader() {
    // TODO: Implement actual bootloader installation
    // For now, simulate success
    Logger::write(LogLevel::Info, "HDInstaller: Installing bootloader (simulated)");
    return true;
}

bool HDInstaller::checkUSBInstallMediaPresent() {
    // TODO: Query USB subsystem for mass storage devices
    // For now, return false (assume USB removed)
    return false;
}

std::string HDInstaller::formatSize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream oss;
    oss.precision(2);
    oss << std::fixed << size << " " << units[unitIndex];
    return oss.str();
}

void HDInstaller::createBootConfiguration() {
    // TODO: Create boot configuration file
    Logger::write(LogLevel::Info, "HDInstaller: Creating boot configuration");
}

std::vector<uint8_t> HDInstaller::getGRUBStage1() {
    // TODO: Return actual GRUB stage1 bootloader code
    // For now, return minimal MBR boot code
    std::vector<uint8_t> bootCode(446, 0);
    
    // Minimal x86 boot code placeholder
    bootCode[0] = 0xFA;  // CLI
    bootCode[1] = 0x31;  // XOR AX, AX
    bootCode[2] = 0xC0;
    bootCode[3] = 0x8E;  // MOV SS, AX
    bootCode[4] = 0xD0;
    bootCode[5] = 0xBC;  // MOV SP, 0x7C00
    bootCode[6] = 0x00;
    bootCode[7] = 0x7C;
    bootCode[8] = 0xFB;  // STI
    
    return bootCode;
}

bool HDInstaller::formatFAT32Partition(uint8_t devIndex, uint64_t startSector, 
                                       uint64_t sectorCount, uint32_t bytesPerSector, 
                                       const char* label) {
    // TODO: Implement FAT32 formatting
    Logger::write(LogLevel::Info, std::string("HDInstaller: Formatting FAT32 partition: ") + label);
    return true;
}

std::vector<std::string> HDInstaller::gatherAllFiles(const std::string& path) {
    // TODO: Recursively gather all files from filesystem
    std::vector<std::string> files;
    return files;
}

void HDInstaller::ensureDirectoryExists(const std::string& path) {
    // TODO: Create directory if it doesn't exist
}

std::string HDInstaller::getDirectoryPath(const std::string& filePath) {
    size_t lastSlash = filePath.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        return filePath.substr(0, lastSlash);
    }
    return "/";
}

} // namespace apps
} // namespace gxos
