//
// Disk Manager - Complete Implementation
//
// Ported from guideXOS.Legacy/DefaultApps/DiskManager.cs
//
// Copyright (c) 2026 guideXOS Server
//

#include "disk_manager.h"
#include "gui_protocol.h"
#include "logger.h"
#include "process.h"
#include "ipc_bus.h"
#include "bitmap_font.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>

#ifndef _WIN32
#include "kernel/core/include/kernel/block_device.h"
#include "kernel/core/include/kernel/vfs.h"
#endif

namespace gxos {
namespace apps {

using namespace gxos::gui;

// Static member initialization
uint64_t DiskManager::s_windowId = 0;
std::vector<DiskManager::DiskEntry> DiskManager::s_disks;
int DiskManager::s_selectedDiskIndex = 0;
std::string DiskManager::s_status = "";
std::string DiskManager::s_detected = "Unknown";
bool DiskManager::s_clickLock = false;
std::string DiskManager::s_cachedTotalCaption = "";
int DiskManager::s_mouseX = 0;
int DiskManager::s_mouseY = 0;
bool DiskManager::s_mouseDown = false;

// Button positions
int DiskManager::s_bxDetectX = 0, DiskManager::s_bxDetectY = 0;
int DiskManager::s_bxAutoX = 0, DiskManager::s_bxAutoY = 0;
int DiskManager::s_bxSwitchFatX = 0, DiskManager::s_bxSwitchFatY = 0;
int DiskManager::s_bxSwitchTarX = 0, DiskManager::s_bxSwitchTarY = 0;
int DiskManager::s_bxSwitchExtX = 0, DiskManager::s_bxSwitchExtY = 0;
int DiskManager::s_bxFormatExfatX = 0, DiskManager::s_bxFormatExfatY = 0;
int DiskManager::s_bxCreatePartX = 0, DiskManager::s_bxCreatePartY = 0;
int DiskManager::s_bxRefreshX = 0, DiskManager::s_bxRefreshY = 0;

uint64_t DiskManager::Launch() {
    ProcessSpec spec{"diskmanager", DiskManager::main};
    return ProcessTable::spawn(spec, {"diskmanager"});
}

int DiskManager::main(int argc, char** argv) {
    try {
#ifdef _WIN32
        Logger::write(LogLevel::Info, "DiskManager starting [Windows host mode]");
#else
        Logger::write(LogLevel::Info, "DiskManager starting [guideXOS baremetal mode]");
#endif
        
        // Initialize state
        s_windowId = 0;
        s_disks.clear();
        s_selectedDiskIndex = 0;
        s_mouseX = 0;
        s_mouseY = 0;
        s_mouseDown = false;
        
        // Load initial disk data
        refreshDisks();
        s_status = buildStatus();
        
        // Subscribe to IPC channels
        const char* kGuiChanIn = "gui.input";
        const char* kGuiChanOut = "gui.output";
        ipc::Bus::ensure(kGuiChanIn);
        ipc::Bus::ensure(kGuiChanOut);
        
        // Create window (920x560)
        ipc::Message createMsg;
        createMsg.type = (uint32_t)MsgType::MT_Create;
        std::ostringstream oss;
        oss << "Disk Management|920|560";
        std::string payload = oss.str();
        createMsg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
        
        // Main event loop
        bool running = true;
        while (running) {
            ipc::Message msg;
            if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
                MsgType msgType = (MsgType)msg.type;
                std::string payload(msg.data.begin(), msg.data.end());
                
                switch (msgType) {
                    case MsgType::MT_Create: {
                        size_t sep = payload.find('|');
                        if (sep != std::string::npos && sep > 0) {
                            try {
                                std::string idStr = payload.substr(0, sep);
                                s_windowId = std::stoull(idStr);
                                Logger::write(LogLevel::Info, std::string("DiskManager window created: ") + std::to_string(s_windowId));
                                render();
                            } catch (...) {
                                Logger::write(LogLevel::Error, "Failed to parse window ID");
                            }
                        }
                        break;
                    }
                    
                    case MsgType::MT_Paint: {
                        render();
                        break;
                    }
                    
                    case MsgType::MT_InputMouse: {
                        std::istringstream iss(payload);
                        std::string xs, ys, btns;
                        std::getline(iss, xs, '|');
                        std::getline(iss, ys, '|');
                        std::getline(iss, btns, '|');
                        
                        try {
                            int x = std::stoi(xs);
                            int y = std::stoi(ys);
                            int buttons = std::stoi(btns);
                            
                            s_mouseX = x;
                            s_mouseY = y;
                            bool wasDown = s_mouseDown;
                            s_mouseDown = (buttons & 1) != 0;
                            
                            handleMouseMove(x, y);
                            
                            if (s_mouseDown && !wasDown) {
                                handleMouseDown(x, y);
                            } else if (!s_mouseDown && wasDown) {
                                handleMouseUp(x, y);
                            }
                        } catch (...) {}
                        break;
                    }
                    
                    case MsgType::MT_InputKey: {
                        size_t sep = payload.find('|');
                        if (sep != std::string::npos) {
                            try {
                                int key = std::stoi(payload.substr(0, sep));
                                bool down = (payload.substr(sep + 1) == "down");
                                handleKey(key, down);
                            } catch (...) {}
                        }
                        break;
                    }
                    
                    case MsgType::MT_Close: {
                        Logger::write(LogLevel::Info, "DiskManager closing");
                        running = false;
                        break;
                    }
                    
                    default:
                        break;
                }
            }
        }
        
        Logger::write(LogLevel::Info, "DiskManager terminated");
        return 0;
        
    } catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("DiskManager exception: ") + e.what());
        return 1;
    }
}

std::string DiskManager::buildStatus() {
#ifdef _WIN32
    Logger::write(LogLevel::Info, "DiskManager running in Windows host mode - disk data is simulated");
    return "Mode: Windows Host (simulated)\nDetected media: " + s_detected;
#else
    Logger::write(LogLevel::Info, "DiskManager running in guideXOS baremetal mode - using kernel::block API");
    std::string driver = "kernel::block";
    return "Mode: guideXOS Baremetal\nDriver: " + driver + "\nDetected media: " + s_detected;
#endif
}

void DiskManager::refreshDisks() {
    s_disks.clear();
    
#ifndef _WIN32
    uint8_t devCount = kernel::block::device_count();
    
    for (uint8_t i = 0; i < devCount; i++) {
        const kernel::block::BlockDevice* dev = kernel::block::get_device(i);
        if (!dev || !dev->active) continue;
        
        DiskEntry entry;
        entry.devIndex = i;
        entry.bytesPerSector = dev->sectorSize;
        entry.totalSectors = dev->totalSectors;
        entry.haveInfo = true;
        
        if (dev->type == kernel::block::BDEV_ATA_PIO) {
            entry.transportLabel = "ATA";
            entry.isSystem = true;
        } else if (dev->type == kernel::block::BDEV_AHCI) {
            entry.transportLabel = "AHCI";
            entry.isSystem = true;
        } else if (dev->type == kernel::block::BDEV_NVME) {
            entry.transportLabel = "NVMe";
            entry.isSystem = true;
        } else if (dev->type == kernel::block::BDEV_USB_MASS) {
            if (dev->name[0] == 'r' && dev->name[1] == 'a' && dev->name[2] == 'm') {
                entry.transportLabel = "RAM disk";
            } else {
                entry.transportLabel = "USB";
            }
            entry.isSystem = false;
        } else {
            entry.transportLabel = "unknown";
            entry.isSystem = false;
        }
        entry.name = "Disk " + std::to_string(i) + " (" + entry.transportLabel + ")";
        
        readMBRForEntry(entry);
        s_disks.push_back(entry);
    }
    
    if (s_disks.empty()) {
        DiskEntry sysDisk;
        sysDisk.name = "No Disks Detected";
        sysDisk.transportLabel = "unknown";
        sysDisk.isSystem = true;
        sysDisk.devIndex = 0;
        sysDisk.haveInfo = false;
        s_disks.push_back(sysDisk);
    }
#else
    DiskEntry sysDisk;
    sysDisk.name = "Disk 0 (ATA)";
    sysDisk.transportLabel = "ATA";
    sysDisk.isSystem = true;
    sysDisk.devIndex = 0;
    sysDisk.haveInfo = true;
    sysDisk.bytesPerSector = 512;
    sysDisk.totalSectors = 209715200;
    sysDisk.parts[0].status = 0x80;
    sysDisk.parts[0].type = 0x07;
    sysDisk.parts[0].lbaStart = 2048;
    sysDisk.parts[0].lbaCount = 204800000;
    sysDisk.parts[0].fs = "NTFS";
    sysDisk.parts[0].mountPoint = "/";
    sysDisk.parts[0].mounted = false;
    sysDisk.mbrStatus = MBR_VALID;
    s_disks.push_back(sysDisk);
#endif
    
    if (s_selectedDiskIndex >= static_cast<int>(s_disks.size())) {
        s_selectedDiskIndex = static_cast<int>(s_disks.size()) - 1;
    }
    if (s_selectedDiskIndex < 0) {
        s_selectedDiskIndex = 0;
    }
    
    DiskEntry* sel = getSelected();
    if (sel && sel->haveInfo) {
        uint64_t totalBytes = sel->totalSectors * sel->bytesPerSector;
        s_cachedTotalCaption = "Total: " + fmtSize(totalBytes);
    } else {
        s_cachedTotalCaption = "";
    }
}

void DiskManager::probeOnce() {
#ifndef _WIN32
    uint8_t buffer[512];
    kernel::block::Status status = kernel::block::read_sectors(0, 0, 1, buffer);
    
    if (status == kernel::block::BLOCK_OK) {
        if (buffer[257] == 'u' && buffer[258] == 's' && buffer[259] == 't' &&
            buffer[260] == 'a' && buffer[261] == 'r') {
            s_detected = "TAR (initrd)";
        }
        else if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
            s_detected = "FAT (boot sector)";
        }
        else {
            s_detected = "Unknown";
        }
    } else {
        s_detected = "Unknown (read error)";
    }
#else
    s_detected = "Unknown (Windows host)";
#endif
    
    s_status = buildStatus();
}

void DiskManager::readMBRForEntry(DiskEntry& entry) {
    for (int i = 0; i < 4; i++) {
        entry.parts[i] = PartitionEntry();
    }
    entry.mbrStatus = MBR_UNREADABLE;
    
#ifndef _WIN32
    uint8_t mbr[512];
    kernel::block::Status status = kernel::block::read_sectors(entry.devIndex, 0, 1, mbr);
    
    if (status == kernel::block::BLOCK_OK) {
        if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
            entry.mbrStatus = MBR_VALID;
            for (int i = 0; i < 4; i++) {
                int off = 446 + i * 16;
                entry.parts[i].status = mbr[off + 0];
                entry.parts[i].type = mbr[off + 4];
                entry.parts[i].lbaStart = 
                    static_cast<uint32_t>(mbr[off + 8]) |
                    (static_cast<uint32_t>(mbr[off + 9]) << 8) |
                    (static_cast<uint32_t>(mbr[off + 10]) << 16) |
                    (static_cast<uint32_t>(mbr[off + 11]) << 24);
                entry.parts[i].lbaCount = 
                    static_cast<uint32_t>(mbr[off + 12]) |
                    (static_cast<uint32_t>(mbr[off + 13]) << 8) |
                    (static_cast<uint32_t>(mbr[off + 14]) << 16) |
                    (static_cast<uint32_t>(mbr[off + 15]) << 24);
                
                if (entry.parts[i].lbaCount != 0) {
                    entry.parts[i].fs = detectFsAtLBA(entry.devIndex, entry.parts[i].lbaStart);
                    entry.parts[i].mountPoint = suggestMountPoint(entry, entry.parts[i], i);
#ifndef _WIN32
                    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount(entry.parts[i].mountPoint.c_str());
                    if (mount && mount->blockDevIndex == entry.devIndex) {
                        entry.parts[i].mounted = true;
                    } else {
                        entry.parts[i].mounted = false;
                    }
#endif
                }
            }
        } else {
            entry.mbrStatus = MBR_INVALID;
        }
    }
#endif
}

DiskManager::DiskEntry* DiskManager::getSelected() {
    if (s_disks.empty() || s_selectedDiskIndex < 0 || 
        s_selectedDiskIndex >= static_cast<int>(s_disks.size())) {
        return nullptr;
    }
    return &s_disks[s_selectedDiskIndex];
}

std::string DiskManager::detectFsAtLBA(uint8_t devIndex, uint32_t lbaStart) {
    if (lbaStart == 0) return "<empty>";
    
#ifndef _WIN32
    uint8_t sec[512];
    kernel::block::Status status = kernel::block::read_sectors(devIndex, lbaStart, 1, sec);
    
    if (status == kernel::block::BLOCK_OK) {
        if (sec[257] == 'u' && sec[258] == 's' && sec[259] == 't' &&
            sec[260] == 'a' && sec[261] == 'r') {
            return "TarFS";
        }
        
        if (sec[510] == 0x55 && sec[511] == 0xAA) {
            uint16_t bytesPerSec = sec[11] | (sec[12] << 8);
            uint8_t secPerClus = sec[13];
            if ((bytesPerSec == 512 || bytesPerSec == 1024 || 
                 bytesPerSec == 2048 || bytesPerSec == 4096) && secPerClus != 0) {
                return "FAT";
            }
        }
        
        uint8_t sb[1024];
        status = kernel::block::read_sectors(devIndex, lbaStart + 2, 2, sb);
        if (status == kernel::block::BLOCK_OK) {
            uint16_t magic = sb[56] | (sb[57] << 8);
            if (magic == 0xEF53) {
                return "EXT2/EXT4";
            }
        }
    }
#endif
    
    return "Unknown";
}

std::string DiskManager::fmtSize(uint64_t bytes) {
    const uint64_t KB = 1024;
    const uint64_t MB = 1024 * 1024;
    const uint64_t GB = 1024 * 1024 * 1024;
    
    if (bytes >= GB) {
        return std::to_string((bytes + GB / 10) / GB) + " GB";
    }
    if (bytes >= MB) {
        return std::to_string((bytes + MB / 10) / MB) + " MB";
    }
    if (bytes >= KB) {
        return std::to_string((bytes + KB / 10) / KB) + " KB";
    }
    return std::to_string(bytes) + " B";
}

std::string DiskManager::fmtHexByte(uint8_t value) {
    char buf[5];
    std::snprintf(buf, sizeof(buf), "0x%02X", value);
    return std::string(buf);
}

std::string DiskManager::mbrStatusText(MbrStatus status) {
    switch (status) {
        case MBR_VALID: return "valid MBR";
        case MBR_INVALID: return "invalid MBR";
        default: return "unreadable";
    }
}

std::string DiskManager::partitionStatusText(const PartitionEntry& part, int partIndex) {
    std::string text = "Healthy";
    if (part.status == 0x80) {
        text += " (Active";
        if (partIndex == 0) text += ", Boot/System";
        text += ")";
    } else if (partIndex == 0) {
        text += " (System candidate)";
    }
    return text;
}

std::string DiskManager::suggestMountPoint(const DiskEntry& disk, const PartitionEntry& part, int partIndex) {
    if (part.lbaCount == 0 || part.fs == "Unknown" || part.fs == "<empty>") {
        return "unmounted";
    }
    if (part.status == 0x80 || (disk.isSystem && partIndex == 0)) {
        return "/";
    }
    if (part.type == 0x0B || part.type == 0x0C || part.type == 0x07) {
        return "/shared";
    }
    if (part.type == 0x83 || part.type == 0x82) {
        return "/users";
    }
    return "unmounted";
}

bool DiskManager::hit(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

void DiskManager::trySetFS_Auto() {
    s_status = "Auto FS detect not yet implemented";
    Logger::write(LogLevel::Info, "DiskManager: Auto FS requested");
}

void DiskManager::trySetFS_FAT() {
    s_status = "Switch to FAT";
    Logger::write(LogLevel::Info, "DiskManager: FAT driver requested");
}

void DiskManager::trySetFS_TAR() {
    s_status = "Switch to TAR";
    Logger::write(LogLevel::Info, "DiskManager: TAR driver requested");
}

void DiskManager::trySetFS_EXT2() {
    s_status = "Switch to EXT2";
    Logger::write(LogLevel::Info, "DiskManager: EXT2 driver requested");
}

void DiskManager::tryFormatFAT() {
    s_status = "Format is disabled: write support must be intentionally enabled first.";
    Logger::write(LogLevel::Warn, "DiskManager: Format FAT requested while disabled");
}

void DiskManager::tryCreatePartitionLargestFree() {
    DiskEntry* sel = getSelected();
    if (!sel || !sel->isSystem) {
        s_status = "Partitioning supported only on System disk";
        return;
    }
    
    if (!sel->haveInfo) {
        s_status = "Disk info unavailable";
        return;
    }
    
    s_status = "Create Partition is disabled: MBR writes are read-only in this build.";
    Logger::write(LogLevel::Warn, "DiskManager: Create partition requested while disabled");
}

void DiskManager::handleMouseMove(int mx, int my) {
    // Trigger redraw if mouse position affects hover states
    // For now, we'll redraw on any mouse move to update button hover states
}

void DiskManager::handleMouseDown(int mx, int my) {
    if (s_clickLock) return;
    
    // Check disk list (relative to window client area - no titlebar offset needed for clicks)
    int listX = PAD;
    int firstY = PAD + HEADER_H;
    int rowW = LEFT_PANE_W - PAD * 2;
    int rowX = listX + PAD;
    
    for (int i = 0; i < static_cast<int>(s_disks.size()); i++) {
        int ry = firstY + i * (ROW_H + 4);
        if (hit(mx, my, rowX, ry, rowW, ROW_H)) {
            s_selectedDiskIndex = i;
            s_clickLock = true;
            
            DiskEntry* sel = getSelected();
            if (sel && sel->haveInfo) {
                uint64_t totalBytes = sel->totalSectors * sel->bytesPerSector;
                s_cachedTotalCaption = "Total: " + fmtSize(totalBytes);
            }
            render();
            return;
        }
    }
    
    // Check buttons
    if (hit(mx, my, s_bxDetectX, s_bxDetectY, 180, BTN_H)) {
        probeOnce();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxAutoX, s_bxAutoY, 180, BTN_H)) {
        trySetFS_Auto();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxSwitchFatX, s_bxSwitchFatY, 180, BTN_H)) {
        trySetFS_FAT();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxSwitchTarX, s_bxSwitchTarY, 180, BTN_H)) {
        trySetFS_TAR();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxSwitchExtX, s_bxSwitchExtY, 180, BTN_H)) {
        trySetFS_EXT2();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxFormatExfatX, s_bxFormatExfatY, 200, BTN_H)) {
        tryFormatFAT();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxCreatePartX, s_bxCreatePartY, 220, BTN_H)) {
        tryCreatePartitionLargestFree();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxRefreshX, s_bxRefreshY, 160, BTN_H)) {
        refreshDisks();
        s_clickLock = true;
        render();
        return;
    }
}

void DiskManager::handleMouseUp(int mx, int my) {
    s_clickLock = false;
}

void DiskManager::handleKey(int keyCode, bool down) {
    if (!down) return;
    
    // F5 = Refresh
    if (keyCode == 116) {  // VK_F5
        refreshDisks();
        render();
    }
    
    // Up/Down arrows for disk selection
    if (keyCode == 38) {  // VK_UP
        if (s_selectedDiskIndex > 0) {
            s_selectedDiskIndex--;
            DiskEntry* sel = getSelected();
            if (sel && sel->haveInfo) {
                uint64_t totalBytes = sel->totalSectors * sel->bytesPerSector;
                s_cachedTotalCaption = "Total: " + fmtSize(totalBytes);
            }
            render();
        }
    } else if (keyCode == 40) {  // VK_DOWN
        if (s_selectedDiskIndex < static_cast<int>(s_disks.size()) - 1) {
            s_selectedDiskIndex++;
            DiskEntry* sel = getSelected();
            if (sel && sel->haveInfo) {
                uint64_t totalBytes = sel->totalSectors * sel->bytesPerSector;
                s_cachedTotalCaption = "Total: " + fmtSize(totalBytes);
            }
            render();
        }
    }
}

// Rendering implementation continues in next part...
void DiskManager::render() {
    if (s_windowId == 0) return;
    
    // Send draw commands via IPC
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawClear;
    std::ostringstream oss;
    oss << s_windowId << "|43|43|43";  // Dark gray background (RGB: 43,43,43)
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    // Draw all components
    drawLeftPane(0, 0, 920, 560);
    
    int rightX = LEFT_PANE_W + PAD;
    int rightW = 920 - rightX - PAD;
    if (rightW < 100) rightW = 100;
    
    int topH = 152;
    int mountsY = PAD + topH + GAP;
    int mountsH = 112;
    int bottomY = mountsY + mountsH + GAP;
    int bottomH = 112;
    
    drawVolumesGrid(rightX, PAD, rightW, topH);
    drawMountsSection(rightX, mountsY, rightW, mountsH);
    drawPartitionMap(rightX, bottomY, rightW, bottomH);
    drawActions(rightX, 560 - (PAD + 160), rightW, 160);
    
    // Request compositor to paint
    ipc::Message paintMsg;
    paintMsg.type = (uint32_t)MsgType::MT_Paint;
    std::string paintPayload = std::to_string(s_windowId);
    paintMsg.data.assign(paintPayload.begin(), paintPayload.end());
    ipc::Bus::publish("gui.input", std::move(paintMsg), false);
}

void DiskManager::drawLeftPane(int winX, int winY, int winW, int winH) {
    int lx = PAD;
    int ly = PAD;
    
    // Title "Disks"
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    oss << s_windowId << "|" << lx << "|" << (ly - 2) << "|Disks|255|255|255";
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    int listY = ly + HEADER_H;
    int rowW = LEFT_PANE_W - PAD * 2;
    int rowX = PAD + PAD;
    
    uint32_t bgR = 48, bgG = 48, bgB = 48;
    uint32_t bgSelR = 58, bgSelG = 58, bgSelB = 58;
    
    // Draw disk list
    for (int i = 0; i < static_cast<int>(s_disks.size()); i++) {
        int ry = listY + i * (ROW_H + 4);
        bool selected = (s_selectedDiskIndex == i);
        
        // Background rect
        ipc::Message rectMsg;
        rectMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream rectOss;
        rectOss << s_windowId << "|" << rowX << "|" << ry << "|" << rowW << "|" << ROW_H << "|";
        if (selected) {
            rectOss << bgSelR << "|" << bgSelG << "|" << bgSelB;
        } else {
            rectOss << bgR << "|" << bgG << "|" << bgB;
        }
        std::string rectPayload = rectOss.str();
        rectMsg.data.assign(rectPayload.begin(), rectPayload.end());
        ipc::Bus::publish("gui.input", std::move(rectMsg), false);
        
        // Disk name text
        ipc::Message textMsg;
        textMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream textOss;
        textOss << s_windowId << "|" << (rowX + 6) << "|" << (ry + 3) << "|" 
                << s_disks[i].name << "|255|255|255";
        std::string textPayload = textOss.str();
        textMsg.data.assign(textPayload.begin(), textPayload.end());
        ipc::Bus::publish("gui.input", std::move(textMsg), false);

        if (s_disks[i].haveInfo) {
            uint64_t totalBytes = s_disks[i].totalSectors * s_disks[i].bytesPerSector;
            std::string detail = "#" + std::to_string(s_disks[i].devIndex) + "  " + fmtSize(totalBytes);
            ipc::Message detailMsg;
            detailMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream detailOss;
            detailOss << s_windowId << "|" << (rowX + 6) << "|" << (ry + 15) << "|" 
                      << detail << "|180|180|180";
            std::string detailPayload = detailOss.str();
            detailMsg.data.assign(detailPayload.begin(), detailPayload.end());
            ipc::Bus::publish("gui.input", std::move(detailMsg), false);
        }
    }
    
    // Status text at bottom
    int statusY = winH - (PAD + 40);
    ipc::Message statusMsg;
    statusMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream statusOss;
    statusOss << s_windowId << "|" << lx << "|" << statusY << "|" << s_status << "|255|255|255";
    std::string statusPayload = statusOss.str();
    statusMsg.data.assign(statusPayload.begin(), statusPayload.end());
    ipc::Bus::publish("gui.input", std::move(statusMsg), false);
}

void DiskManager::drawVolumesGrid(int x, int y, int w, int h) {
    // Title
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    oss << s_windowId << "|" << x << "|" << y << "|Volumes|255|255|255";
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    int gridY = y + HEADER_H;
    
    // Column widths
    int cw[] = { 150, 58, 58, 74, 132, 80, 90, 72, 96 };
    int sum = 0;
    for (int i = 0; i < 9; i++) sum += cw[i];
    
    if (sum != w) {
        if (sum > w) {
            float scale = static_cast<float>(w) / static_cast<float>(sum);
            int newsum = 0;
            for (int i = 0; i < 9; i++) {
                cw[i] = static_cast<int>(cw[i] * scale);
                if (cw[i] < 60) cw[i] = 60;
                newsum += cw[i];
            }
            cw[8] += w - newsum;
        } else {
            cw[8] += w - sum;
        }
    }
    
    // Draw headers
    int cx = x;
    const char* headers[] = { "Volume", "Dev", "Part", "FS", "Status", "Type", "Capacity", "MBR", "Start LBA" };
    for (int i = 0; i < 9; i++) {
        drawHeaderCell(cx, gridY, cw[i], ROW_H, headers[i]);
        cx += cw[i];
    }
    
    // Draw partition rows
    int rowY = gridY + ROW_H;
    DiskEntry* sel = getSelected();
    if (sel && sel->haveInfo) {
        for (int i = 0; i < 4; i++) {
            const PartitionEntry& p = sel->parts[i];
            if (p.lbaCount == 0) continue;
            
            cx = x;
            
            std::string vol = sel->name + ", Partition " + std::to_string(i + 1);
            drawCell(cx, rowY, cw[0], ROW_H, vol.c_str());
            cx += cw[0];
            
            std::string dev = std::to_string(sel->devIndex);
            drawCell(cx, rowY, cw[1], ROW_H, dev.c_str());
            cx += cw[1];
            
            std::string partNo = std::to_string(i + 1);
            drawCell(cx, rowY, cw[2], ROW_H, partNo.c_str());
            cx += cw[2];
            
            drawCell(cx, rowY, cw[3], ROW_H, p.fs.c_str());
            cx += cw[3];

            std::string status = partitionStatusText(p, i);
            drawCell(cx, rowY, cw[4], ROW_H, status.c_str());
            cx += cw[4];

            std::string type = fmtHexByte(p.type);
            drawCell(cx, rowY, cw[5], ROW_H, type.c_str());
            cx += cw[5];
            
            uint64_t capB = static_cast<uint64_t>(p.lbaCount) * 
                           (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector);
            std::string cap = fmtSize(capB);
            drawCell(cx, rowY, cw[6], ROW_H, cap.c_str());
            cx += cw[6];
            
            std::string mbr = mbrStatusText(sel->mbrStatus);
            drawCell(cx, rowY, cw[7], ROW_H, mbr.c_str());
            cx += cw[7];
            
            std::string start = std::to_string(p.lbaStart);
            drawCell(cx, rowY, cw[8], ROW_H, start.c_str());
            
            rowY += ROW_H;
        }
    }
}

void DiskManager::drawMountsSection(int x, int y, int w, int h) {
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    oss << s_windowId << "|" << x << "|" << y << "|Mounts|255|255|255";
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);

    int gridY = y + HEADER_H;
    int cw[] = { 72, 78, 130, 150, 92 };
    int sum = 0;
    for (int i = 0; i < 5; i++) sum += cw[i];
    cw[4] += w - sum;

    const char* headers[] = { "Device", "Partition", "Filesystem", "Suggested mount", "Mounted" };
    int cx = x;
    for (int i = 0; i < 5; i++) {
        drawHeaderCell(cx, gridY, cw[i], ROW_H, headers[i]);
        cx += cw[i];
    }

    DiskEntry* sel = getSelected();
    int rowY = gridY + ROW_H;
    if (!sel || !sel->haveInfo) return;

    for (int i = 0; i < 4; i++) {
        const PartitionEntry& p = sel->parts[i];
        if (p.lbaCount == 0) continue;

        cx = x;
        std::string dev = std::to_string(sel->devIndex);
        std::string part = std::to_string(i + 1);
        std::string mounted = p.mounted ? "yes" : "no";
        const std::string& mp = p.mountPoint.empty() ? std::string("unmounted") : p.mountPoint;

        drawCell(cx, rowY, cw[0], ROW_H, dev.c_str());
        cx += cw[0];
        drawCell(cx, rowY, cw[1], ROW_H, part.c_str());
        cx += cw[1];
        drawCell(cx, rowY, cw[2], ROW_H, p.fs.c_str());
        cx += cw[2];
        drawCell(cx, rowY, cw[3], ROW_H, mp.c_str());
        cx += cw[3];
        drawCell(cx, rowY, cw[4], ROW_H, mounted.c_str());

        rowY += ROW_H;
        if (rowY > y + h - ROW_H) break;
    }
}

void DiskManager::drawPartitionMap(int x, int y, int w, int h) {
    DiskEntry* sel = getSelected();
    if (!sel) return;
    
    // Title
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    oss << s_windowId << "|" << x << "|" << y << "|" << sel->name << "|255|255|255";
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    int barY = y + HEADER_H;
    int barH = 36;
    
    // Background bar
    ipc::Message barMsg;
    barMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream barOss;
    barOss << s_windowId << "|" << x << "|" << barY << "|" << w << "|" << barH << "|30|30|30";
    std::string barPayload = barOss.str();
    barMsg.data.assign(barPayload.begin(), barPayload.end());
    ipc::Bus::publish("gui.input", std::move(barMsg), false);
    
    if (!sel->haveInfo) return;
    
    uint64_t total = sel->totalSectors;
    if (total == 0) return;
    
    bool drawn[4] = { false, false, false, false };
    uint64_t cursor = 0;
    for (int pass = 0; pass < 4; pass++) {
        int next = -1;
        uint64_t nextStart = UINT64_MAX;
        for (int i = 0; i < 4; i++) {
            const PartitionEntry& p = sel->parts[i];
            if (drawn[i] || p.lbaCount == 0 || p.lbaStart >= total) continue;
            if (p.lbaStart < nextStart) {
                nextStart = p.lbaStart;
                next = i;
            }
        }
        if (next < 0) break;

        const PartitionEntry& p = sel->parts[next];
        uint64_t start = p.lbaStart;
        uint64_t count = p.lbaCount;
        if (start + count > total) count = total - start;

        if (start > cursor) {
            uint64_t freeCount = start - cursor;
            int freeX = x + static_cast<int>((cursor * w) / total);
            int freeW = static_cast<int>((freeCount * w) / total);
            if (freeW <= 0) freeW = 1;
            ipc::Message freeMsg;
            freeMsg.type = (uint32_t)MsgType::MT_DrawRect;
            std::ostringstream freeOss;
            freeOss << s_windowId << "|" << freeX << "|" << barY << "|" << freeW << "|" << barH << "|58|58|58";
            std::string freePayload = freeOss.str();
            freeMsg.data.assign(freePayload.begin(), freePayload.end());
            ipc::Bus::publish("gui.input", std::move(freeMsg), false);
            if (freeW > 70) {
                std::string freeLbl = "Unallocated " + fmtSize(freeCount * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
                ipc::Message lblMsg;
                lblMsg.type = (uint32_t)MsgType::MT_DrawText;
                std::ostringstream lblOss;
                lblOss << s_windowId << "|" << (freeX + 4) << "|" << (barY + 10) << "|" << freeLbl << "|210|210|210";
                std::string lblPayload = lblOss.str();
                lblMsg.data.assign(lblPayload.begin(), lblPayload.end());
                ipc::Bus::publish("gui.input", std::move(lblMsg), false);
            }
        }

        int segX = x + static_cast<int>((start * w) / total);
        int segW = static_cast<int>((count * w) / total);
        if (segW <= 0) segW = 1;

        ipc::Message segMsg;
        segMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream segOss;
        segOss << s_windowId << "|" << segX << "|" << barY << "|" << segW << "|" << barH << "|76|139|245";
        std::string segPayload = segOss.str();
        segMsg.data.assign(segPayload.begin(), segPayload.end());
        ipc::Bus::publish("gui.input", std::move(segMsg), false);

        std::string lbl = "P" + std::to_string(next + 1) + " " + p.fs + " " + fmtSize(p.lbaCount * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
        if (p.status == 0x80) lbl += " Active";
        if (next == 0 && sel->isSystem) lbl += " System";
        if (segW > 40) {
            ipc::Message lblMsg;
            lblMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream lblOss;
            lblOss << s_windowId << "|" << (segX + 4) << "|" << (barY + 10) << "|" << lbl << "|255|255|255";
            std::string lblPayload = lblOss.str();
            lblMsg.data.assign(lblPayload.begin(), lblPayload.end());
            ipc::Bus::publish("gui.input", std::move(lblMsg), false);
        }

        uint64_t end = start + count;
        if (end > cursor) cursor = end;
        drawn[next] = true;
    }

    if (cursor < total) {
        uint64_t freeCount = total - cursor;
        int freeX = x + static_cast<int>((cursor * w) / total);
        int freeW = w - (freeX - x);
        if (freeW <= 0) freeW = 1;
        ipc::Message freeMsg;
        freeMsg.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream freeOss;
        freeOss << s_windowId << "|" << freeX << "|" << barY << "|" << freeW << "|" << barH << "|58|58|58";
        std::string freePayload = freeOss.str();
        freeMsg.data.assign(freePayload.begin(), freePayload.end());
        ipc::Bus::publish("gui.input", std::move(freeMsg), false);
        if (freeW > 70) {
            std::string freeLbl = "Unallocated " + fmtSize(freeCount * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
            ipc::Message lblMsg;
            lblMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream lblOss;
            lblOss << s_windowId << "|" << (freeX + 4) << "|" << (barY + 10) << "|" << freeLbl << "|210|210|210";
            std::string lblPayload = lblOss.str();
            lblMsg.data.assign(lblPayload.begin(), lblPayload.end());
            ipc::Bus::publish("gui.input", std::move(lblMsg), false);
        }
    }
    
    // Total capacity
    if (!s_cachedTotalCaption.empty()) {
        ipc::Message capMsg;
        capMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream capOss;
            std::string info = s_cachedTotalCaption + ", sector " + std::to_string(sel->bytesPerSector) + " B, sectors " + std::to_string(sel->totalSectors) + ", " + mbrStatusText(sel->mbrStatus);
            capOss << s_windowId << "|" << x << "|" << (barY + barH + 6) << "|" << info << "|255|255|255";
        std::string capPayload = capOss.str();
        capMsg.data.assign(capPayload.begin(), capPayload.end());
        ipc::Bus::publish("gui.input", std::move(capMsg), false);
    }
}

void DiskManager::drawActions(int x, int y, int w, int h) {
    // Title
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    oss << s_windowId << "|" << x << "|" << y << "|Actions|255|255|255";
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    int colGap = 16;
    int half = (w - colGap) / 2;
    if (half < 100) half = 100;
    
    int leftX = x;
    int rightX = x + half + colGap;
    int btnWLeft = half - 20;
    int btnWRight = half - 20;
    if (btnWLeft < 120) btnWLeft = 120;
    if (btnWRight < 120) btnWRight = 120;
    
    int byL = y + HEADER_H;
    int byR = y + HEADER_H;
    
    // Left column
    s_bxDetectX = leftX; s_bxDetectY = byL;
    drawButton(s_bxDetectX, s_bxDetectY, btnWLeft, BTN_H, "Detect media", 
               hit(s_mouseX, s_mouseY, s_bxDetectX, s_bxDetectY, btnWLeft, BTN_H));
    byL += BTN_H + GAP;
    
    s_bxAutoX = leftX; s_bxAutoY = byL;
    drawButton(s_bxAutoX, s_bxAutoY, btnWLeft, BTN_H, "Set FS: Auto",
               hit(s_mouseX, s_mouseY, s_bxAutoX, s_bxAutoY, btnWLeft, BTN_H));
    byL += BTN_H + GAP;
    
    s_bxSwitchFatX = leftX; s_bxSwitchFatY = byL;
    drawButton(s_bxSwitchFatX, s_bxSwitchFatY, btnWLeft, BTN_H, "Set FS: FAT",
               hit(s_mouseX, s_mouseY, s_bxSwitchFatX, s_bxSwitchFatY, btnWLeft, BTN_H));
    byL += BTN_H + GAP;
    
    s_bxSwitchTarX = leftX; s_bxSwitchTarY = byL;
    drawButton(s_bxSwitchTarX, s_bxSwitchTarY, btnWLeft, BTN_H, "Set FS: TarFS",
               hit(s_mouseX, s_mouseY, s_bxSwitchTarX, s_bxSwitchTarY, btnWLeft, BTN_H));
    byL += BTN_H + GAP;
    
    s_bxSwitchExtX = leftX; s_bxSwitchExtY = byL;
    drawButton(s_bxSwitchExtX, s_bxSwitchExtY, btnWLeft, BTN_H, "Set FS: EXT2",
               hit(s_mouseX, s_mouseY, s_bxSwitchExtX, s_bxSwitchExtY, btnWLeft, BTN_H));
    
    // Right column
    s_bxFormatExfatX = rightX; s_bxFormatExfatY = byR;
    drawDisabledButton(s_bxFormatExfatX, s_bxFormatExfatY, btnWRight, BTN_H, "Format as FAT");
    byR += BTN_H + GAP;
    
    s_bxCreatePartX = rightX; s_bxCreatePartY = byR;
    drawDisabledButton(s_bxCreatePartX, s_bxCreatePartY, btnWRight, BTN_H, "Create partition");
    byR += BTN_H + GAP;
    
    s_bxRefreshX = rightX; s_bxRefreshY = byR;
    drawButton(s_bxRefreshX, s_bxRefreshY, btnWRight, BTN_H, "Refresh",
               hit(s_mouseX, s_mouseY, s_bxRefreshX, s_bxRefreshY, btnWRight, BTN_H));

    int noteY = byR + BTN_H + GAP;
    ipc::Message noteMsg;
    noteMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream noteOss;
    noteOss << s_windowId << "|" << rightX << "|" << noteY << "|Dangerous actions disabled: read-only disk inspection mode.|180|180|180";
    std::string notePayload = noteOss.str();
    noteMsg.data.assign(notePayload.begin(), notePayload.end());
    ipc::Bus::publish("gui.input", std::move(noteMsg), false);
}

void DiskManager::drawHeaderCell(int x, int y, int w, int h, const char* text) {
    // Background
    ipc::Message bgMsg;
    bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream bgOss;
    bgOss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|37|37|37";
    std::string bgPayload = bgOss.str();
    bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
    ipc::Bus::publish("gui.input", std::move(bgMsg), false);
    
    // Text
    ipc::Message textMsg;
    textMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream textOss;
    textOss << s_windowId << "|" << (x + 6) << "|" << (y + 6) << "|" << text << "|255|255|255";
    std::string textPayload = textOss.str();
    textMsg.data.assign(textPayload.begin(), textPayload.end());
    ipc::Bus::publish("gui.input", std::move(textMsg), false);
}

void DiskManager::drawCell(int x, int y, int w, int h, const char* text) {
    // Background
    ipc::Message bgMsg;
    bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream bgOss;
    bgOss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|42|42|42";
    std::string bgPayload = bgOss.str();
    bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
    ipc::Bus::publish("gui.input", std::move(bgMsg), false);
    
    // Text
    ipc::Message textMsg;
    textMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream textOss;
    textOss << s_windowId << "|" << (x + 6) << "|" << (y + 6) << "|" << text << "|255|255|255";
    std::string textPayload = textOss.str();
    textMsg.data.assign(textPayload.begin(), textPayload.end());
    ipc::Bus::publish("gui.input", std::move(textMsg), false);
}

void DiskManager::drawButton(int x, int y, int w, int h, const char* text, bool hover) {
    // Background (lighter if hovered)
    uint8_t r = hover ? 58 : 50;
    uint8_t g = hover ? 58 : 50;
    uint8_t b = hover ? 58 : 50;
    
    ipc::Message bgMsg;
    bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream bgOss;
    bgOss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" 
          << static_cast<int>(r) << "|" << static_cast<int>(g) << "|" << static_cast<int>(b);
    std::string bgPayload = bgOss.str();
    bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
    ipc::Bus::publish("gui.input", std::move(bgMsg), false);
    
    // Text
    ipc::Message textMsg;
    textMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream textOss;
    textOss << s_windowId << "|" << (x + 10) << "|" << (y + 8) << "|" << text << "|255|255|255";
    std::string textPayload = textOss.str();
    textMsg.data.assign(textPayload.begin(), textPayload.end());
    ipc::Bus::publish("gui.input", std::move(textMsg), false);
}

void DiskManager::drawDisabledButton(int x, int y, int w, int h, const char* text) {
    ipc::Message bgMsg;
    bgMsg.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream bgOss;
    bgOss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|38|38|38";
    std::string bgPayload = bgOss.str();
    bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
    ipc::Bus::publish("gui.input", std::move(bgMsg), false);

    ipc::Message textMsg;
    textMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream textOss;
    textOss << s_windowId << "|" << (x + 10) << "|" << (y + 8) << "|" << text << "|120|120|120";
    std::string textPayload = textOss.str();
    textMsg.data.assign(textPayload.begin(), textPayload.end());
    ipc::Bus::publish("gui.input", std::move(textMsg), false);
}

} // namespace apps
} // namespace gxos
