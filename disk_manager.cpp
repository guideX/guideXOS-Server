//
// Disk Manager - Implementation
//
// Ported from guideXOS.Legacy/DefaultApps/DiskManager.cs
//
// Copyright (c) 2026 guideXOS Server
//

#include "disk_manager.h"
#include "logger.h"
#include "desktop_service.h"
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include "kernel/core/include/kernel/block_device.h"
#include "kernel/core/include/kernel/framebuffer.h"
#endif

namespace gxos {
namespace gui {

// Static member initialization
uint64_t DiskManager::s_windowId = 0;
bool DiskManager::s_visible = false;
std::vector<DiskManager::DiskEntry> DiskManager::s_disks;
int DiskManager::s_selectedDiskIndex = 0;
std::string DiskManager::s_status = "";
std::string DiskManager::s_detected = "Unknown";
bool DiskManager::s_clickLock = false;
std::string DiskManager::s_cachedTotalCaption = "";

// Button positions
int DiskManager::s_bxDetectX = 0, DiskManager::s_bxDetectY = 0;
int DiskManager::s_bxAutoX = 0, DiskManager::s_bxAutoY = 0;
int DiskManager::s_bxSwitchFatX = 0, DiskManager::s_bxSwitchFatY = 0;
int DiskManager::s_bxSwitchTarX = 0, DiskManager::s_bxSwitchTarY = 0;
int DiskManager::s_bxSwitchExtX = 0, DiskManager::s_bxSwitchExtY = 0;
int DiskManager::s_bxFormatExfatX = 0, DiskManager::s_bxFormatExfatY = 0;
int DiskManager::s_bxCreatePartX = 0, DiskManager::s_bxCreatePartY = 0;
int DiskManager::s_bxRefreshX = 0, DiskManager::s_bxRefreshY = 0;

void DiskManager::show(int x, int y) {
    if (s_visible) {
        Logger::write(LogLevel::Warn, "DiskManager already visible");
        return;
    }
    
    init(x, y);
    s_visible = true;
}

void DiskManager::close() {
    if (!s_visible) return;
    
    // Clean up
    s_disks.clear();
    s_windowId = 0;
    s_visible = false;
    
    Logger::write(LogLevel::Info, "DiskManager closed");
}

bool DiskManager::isVisible() {
    return s_visible;
}

void DiskManager::init(int x, int y) {
    // TODO: Create window via compositor
    // For now, this is a placeholder for the window creation
    s_windowId = 0; // Will be assigned by compositor
    
    s_status = buildStatus();
    refreshDisks();
    
    Logger::write(LogLevel::Info, "DiskManager initialized");
}

std::string DiskManager::buildStatus() {
#ifdef _WIN32
    return "Driver: <Windows Host Mode>\nDetected media: " + s_detected;
#else
    // TODO: Query current VFS driver
    std::string driver = "Unknown";
    // if (kernel::vfs::getCurrentDriver() == kernel::vfs::DriverType::FAT)
    //     driver = "FAT";
    // else if (kernel::vfs::getCurrentDriver() == kernel::vfs::DriverType::EXT4)
    //     driver = "EXT2/EXT4";
    
    return "Driver: " + driver + "\nDetected media: " + s_detected;
#endif
}

void DiskManager::refreshDisks() {
    s_disks.clear();
    
#ifndef _WIN32
    // Enumerate all block devices
    uint8_t devCount = kernel::block::device_count();
    
    for (uint8_t i = 0; i < devCount; i++) {
        const kernel::block::BlockDevice* dev = kernel::block::get_device(i);
        if (!dev || !dev->active) continue;
        
        DiskEntry entry;
        entry.bytesPerSector = dev->sectorSize;
        entry.totalSectors = dev->totalSectors;
        entry.haveInfo = true;
        
        // Determine disk type and name
        if (dev->type == kernel::block::BDEV_ATA_PIO || 
            dev->type == kernel::block::BDEV_AHCI) {
            entry.name = "Disk " + std::to_string(i) + " (System)";
            entry.isSystem = true;
            entry.usbDisk = nullptr;
        } else if (dev->type == kernel::block::BDEV_USB_MASS) {
            entry.name = "Disk " + std::to_string(i) + " (USB)";
            entry.isSystem = false;
            // Store device index for USB disk access
            entry.usbDisk = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
        } else if (dev->type == kernel::block::BDEV_NVME) {
            entry.name = "Disk " + std::to_string(i) + " (NVMe)";
            entry.isSystem = true;
            entry.usbDisk = nullptr;
        } else {
            entry.name = std::string(dev->name);
            entry.isSystem = false;
            entry.usbDisk = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
        }
        
        readMBRForEntry(entry);
        s_disks.push_back(entry);
    }
    
    // If no devices found, add a placeholder
    if (s_disks.empty()) {
        DiskEntry sysDisk;
        sysDisk.name = "No Disks Detected";
        sysDisk.isSystem = true;
        sysDisk.usbDisk = nullptr;
        sysDisk.haveInfo = false;
        s_disks.push_back(sysDisk);
    }
#else
    // Windows host mode - create dummy entries for testing
    DiskEntry sysDisk;
    sysDisk.name = "Disk 0 (System)";
    sysDisk.isSystem = true;
    sysDisk.usbDisk = nullptr;
    sysDisk.haveInfo = true;
    sysDisk.bytesPerSector = 512;
    sysDisk.totalSectors = 209715200; // 100 GB
    
    // Create dummy partitions for testing
    sysDisk.parts[0].status = 0x80; // Bootable
    sysDisk.parts[0].type = 0x07;   // NTFS
    sysDisk.parts[0].lbaStart = 2048;
    sysDisk.parts[0].lbaCount = 204800000;
    sysDisk.parts[0].fs = "NTFS";
    
    s_disks.push_back(sysDisk);
#endif
    
    // Clamp selection
    if (s_selectedDiskIndex >= static_cast<int>(s_disks.size())) {
        s_selectedDiskIndex = static_cast<int>(s_disks.size()) - 1;
    }
    if (s_selectedDiskIndex < 0) {
        s_selectedDiskIndex = 0;
    }
    
    // Update cached total caption
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
    // Read first sector and detect filesystem
    uint8_t buffer[512];
    kernel::block::Status status = kernel::block::read_sectors(0, 0, 1, buffer);
    
    if (status == kernel::block::BLOCK_OK) {
        // Check for TAR signature
        if (buffer[257] == 'u' && buffer[258] == 's' && buffer[259] == 't' &&
            buffer[260] == 'a' && buffer[261] == 'r') {
            s_detected = "TAR (initrd)";
        }
        // Check for boot sector signature
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
    // Initialize partitions
    for (int i = 0; i < 4; i++) {
        entry.parts[i] = PartitionEntry();
    }
    
#ifndef _WIN32
    // Get device index
    uint8_t devIndex = 0;
    if (entry.usbDisk != nullptr) {
        devIndex = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(entry.usbDisk));
    }
    
    // Read MBR
    uint8_t mbr[512];
    kernel::block::Status status = kernel::block::read_sectors(devIndex, 0, 1, mbr);
    
    if (status == kernel::block::BLOCK_OK) {
        // Check boot signature
        if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
            // Parse partition table (starts at offset 446)
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
                
                // Detect filesystem for this partition
                entry.parts[i].fs = detectFsAtLBA(entry.parts[i].lbaStart);
            }
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

std::string DiskManager::detectFsAtLBA(uint32_t lbaStart) {
    if (lbaStart == 0) return "<empty>";
    
#ifndef _WIN32
    uint8_t sec[512];
    kernel::block::Status status = kernel::block::read_sectors(0, lbaStart, 1, sec);
    
    if (status == kernel::block::BLOCK_OK) {
        // TAR signature
        if (sec[257] == 'u' && sec[258] == 's' && sec[259] == 't' &&
            sec[260] == 'a' && sec[261] == 'r') {
            return "TarFS";
        }
        
        // FAT boot sector
        if (sec[510] == 0x55 && sec[511] == 0xAA) {
            uint16_t bytesPerSec = sec[11] | (sec[12] << 8);
            uint8_t secPerClus = sec[13];
            if ((bytesPerSec == 512 || bytesPerSec == 1024 || 
                 bytesPerSec == 2048 || bytesPerSec == 4096) && secPerClus != 0) {
                return "FAT";
            }
        }
        
        // EXT2/EXT4 superblock (starts at byte 1024, which is LBA + 2)
        uint8_t sb[1024];
        status = kernel::block::read_sectors(0, lbaStart + 2, 2, sb);
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

bool DiskManager::hit(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

void DiskManager::trySetFS_Auto() {
    // TODO: Implement auto filesystem detection
    s_status = "Auto FS detect not yet implemented";
    Logger::write(LogLevel::Info, "DiskManager: Auto FS requested");
}

void DiskManager::trySetFS_FAT() {
    // TODO: Switch to FAT driver
    s_status = "Switch to FAT";
    Logger::write(LogLevel::Info, "DiskManager: FAT driver requested");
}

void DiskManager::trySetFS_TAR() {
    // TODO: Switch to TAR driver
    s_status = "Switch to TAR";
    Logger::write(LogLevel::Info, "DiskManager: TAR driver requested");
}

void DiskManager::trySetFS_EXT2() {
    // TODO: Switch to EXT2/EXT4 driver
    s_status = "Switch to EXT2";
    Logger::write(LogLevel::Info, "DiskManager: EXT2 driver requested");
}

void DiskManager::tryFormatFAT() {
    // TODO: Format selected partition as FAT
    s_status = "Format FAT not yet implemented";
    Logger::write(LogLevel::Warn, "DiskManager: Format FAT requested (not implemented)");
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
    
    // TODO: Implement partition creation
    s_status = "Partition creation not yet implemented";
    Logger::write(LogLevel::Warn, "DiskManager: Create partition requested (not implemented)");
}

void DiskManager::handleInput(WinInfo* win) {
    if (!win) return;
    
#ifdef _WIN32
    // Get mouse position
    POINT pt;
    GetCursorPos(&pt);
    if (s_windowId > 0) {
        // Convert screen to client coordinates
        // In a real implementation, we'd need the window handle
        // For now, use relative coordinates
    }
    int mx = pt.x;
    int my = pt.y;
    bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#else
    // On bare-metal, get from input system
    // TODO: Get actual mouse state from kernel input
    int mx = 0, my = 0;
    bool leftDown = false;
#endif
    
    // Left pane disk selection
    int listX = win->x + PAD;
    int firstY = win->y + PAD + HEADER_H;
    int rowW = LEFT_PANE_W - PAD * 2;
    int rowX = listX + PAD;
    
    if (leftDown && !s_clickLock) {
        for (int i = 0; i < static_cast<int>(s_disks.size()); i++) {
            int ry = firstY + i * (ROW_H + 4);
            if (hit(mx, my, rowX, ry, rowW, ROW_H)) {
                s_selectedDiskIndex = i;
                s_clickLock = true;
                
                // Update cached total caption
                DiskEntry* sel = getSelected();
                if (sel && sel->haveInfo) {
                    uint64_t totalBytes = sel->totalSectors * sel->bytesPerSector;
                    s_cachedTotalCaption = "Total: " + fmtSize(totalBytes);
                }
                return;
            }
        }
        
        // Button clicks
        if (hit(mx, my, s_bxDetectX, s_bxDetectY, 180, BTN_H)) {
            probeOnce();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxAutoX, s_bxAutoY, 180, BTN_H)) {
            trySetFS_Auto();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxSwitchFatX, s_bxSwitchFatY, 180, BTN_H)) {
            trySetFS_FAT();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxSwitchTarX, s_bxSwitchTarY, 180, BTN_H)) {
            trySetFS_TAR();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxSwitchExtX, s_bxSwitchExtY, 180, BTN_H)) {
            trySetFS_EXT2();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxFormatExfatX, s_bxFormatExfatY, 200, BTN_H)) {
            tryFormatFAT();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxCreatePartX, s_bxCreatePartY, 220, BTN_H)) {
            tryCreatePartitionLargestFree();
            s_clickLock = true;
            return;
        }
        if (hit(mx, my, s_bxRefreshX, s_bxRefreshY, 160, BTN_H)) {
            refreshDisks();
            s_clickLock = true;
            return;
        }
    } else if (!leftDown) {
        s_clickLock = false;
    }
}

void DiskManager::draw() {
#ifdef _WIN32
    // On Windows, get window DC and render
    // TODO: Integrate with compositor window system
    Logger::write(LogLevel::Info, "DiskManager::draw() called (Windows mode)");
#else
    // On bare-metal, render directly to framebuffer
    if (!s_visible || s_windowId == 0) return;
    
    // Get framebuffer
    if (!kernel::framebuffer::is_available()) return;
    
    uint32_t screenW = kernel::framebuffer::get_width();
    uint32_t screenH = kernel::framebuffer::get_height();
    
    // Window dimensions (fixed for now)
    int winX = 100;
    int winY = 100;
    int winW = 920;
    int winH = 560;
    
    // Background
    kernel::framebuffer::fill_rect(winX + 1, winY + 1, winW - 2, winH - 2, 0xFF2B2B2B);
    
    // Create a mock WinInfo for drawing
    WinInfo mockWin;
    mockWin.x = winX;
    mockWin.y = winY;
    mockWin.w = winW;
    mockWin.h = winH;
    
    // Draw components
    drawLeftPane(&mockWin);
    
    int rightX = winX + LEFT_PANE_W + PAD;
    int rightW = winW - (rightX - winX) - PAD;
    if (rightW < 100) rightW = 100;
    
    int topH = 200;
    int bottomY = winY + PAD + topH + GAP;
    int bottomH = winH - (bottomY - winY) - (PAD + 180);
    if (bottomH < 80) bottomH = 80;
    
    drawVolumesGrid(rightX, winY + PAD, rightW, topH, &mockWin);
    drawPartitionMap(rightX, bottomY, rightW, bottomH, &mockWin);
    drawActions(rightX, winY + winH - (PAD + 160), rightW, 160, &mockWin);
#endif
}

void DiskManager::drawLeftPane(WinInfo* win) {
    if (!win) return;
    
    int lx = win->x + PAD;
    int ly = win->y + PAD;
    int lw = LEFT_PANE_W;
    int lh = win->h - PAD * 2;
    
#ifndef _WIN32
    // Title
    const char* title = "Disks";
    // TODO: Draw text using bitmap font
    // BitmapFont::DrawString(lx, ly - 2, title, 0xFFFFFFFF);
    
    int listX = lx;
    int listY = ly + HEADER_H;
    int rowW = lw - PAD * 2;
    int rowX = listX + PAD;
    
    uint32_t bg = 0xFF303030;
    uint32_t bgSel = 0xFF3A3A3A;
    
    // Draw disk list
    for (int i = 0; i < static_cast<int>(s_disks.size()); i++) {
        int ry = listY + i * (ROW_H + 4);
        uint32_t rowBg = (s_selectedDiskIndex == i) ? bgSel : bg;
        kernel::framebuffer::fill_rect(rowX, ry, rowW, ROW_H, rowBg);
        
        // TODO: Draw disk name text
        // BitmapFont::DrawString(rowX + 6, ry + (ROW_H / 2 - 7 / 2), 
        //                        s_disks[i].name.c_str(), 0xFFFFFFFF);
    }
    
    // Status text at bottom left
    int statusY = win->y + win->h - (PAD + 30);
    int statusMaxW = lw - PAD * 2;
    // TODO: Draw status text
    // BitmapFont::DrawString(lx, statusY, s_status.c_str(), 0xFFFFFFFF);
#endif
}

void DiskManager::drawVolumesGrid(int x, int y, int w, int h, WinInfo* win) {
    if (!win) return;
    
#ifndef _WIN32
    // Header title
    const char* title = "Volumes";
    // TODO: Draw title text
    // BitmapFont::DrawString(x, y, title, 0xFFFFFFFF);
    
    int gridY = y + HEADER_H;
    
    // Column widths
    int cw[] = { 180, 100, 90, 120, 120, 120, 70 };
    int sum = 0;
    for (int i = 0; i < 7; i++) sum += cw[i];
    
    // Scale to fit width
    if (sum != w) {
        if (sum > w) {
            float scale = static_cast<float>(w) / static_cast<float>(sum);
            int newsum = 0;
            for (int i = 0; i < 7; i++) {
                cw[i] = static_cast<int>(cw[i] * scale);
                if (cw[i] < 60) cw[i] = 60;
                newsum += cw[i];
            }
            cw[6] += w - newsum; // adjust last column
        } else {
            cw[6] += w - sum;
        }
    }
    
    // Draw column headers
    int cx = x;
    const char* headers[] = { "Volume", "Layout", "Type", "Status", "Capacity", "Free Space", "% Free" };
    for (int i = 0; i < 7; i++) {
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
            
            // Volume name
            std::string vol = sel->name + ", Partition " + std::to_string(i + 1);
            drawCell(cx, rowY, cw[0], ROW_H, vol.c_str());
            cx += cw[0];
            
            // Layout
            drawCell(cx, rowY, cw[1], ROW_H, "Simple");
            cx += cw[1];
            
            // Type
            drawCell(cx, rowY, cw[2], ROW_H, p.fs.c_str());
            cx += cw[2];
            
            // Status
            const char* status = (p.status == 0x80) ? "Healthy (Active)" : "Healthy";
            drawCell(cx, rowY, cw[3], ROW_H, status);
            cx += cw[3];
            
            // Capacity
            uint64_t capB = static_cast<uint64_t>(p.lbaCount) * 
                           (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector);
            std::string cap = fmtSize(capB);
            drawCell(cx, rowY, cw[4], ROW_H, cap.c_str());
            cx += cw[4];
            
            // Free space (N/A for now)
            drawCell(cx, rowY, cw[5], ROW_H, "N/A");
            cx += cw[5];
            
            // % Free
            drawCell(cx, rowY, cw[6], ROW_H, "N/A");
            
            rowY += ROW_H;
        }
    }
#endif
}

void DiskManager::drawPartitionMap(int x, int y, int w, int h, WinInfo* win) {
    if (!win) return;
    
    DiskEntry* sel = getSelected();
    if (!sel) return;
    
#ifndef _WIN32
    // Title
    const char* title = sel->name.c_str();
    // TODO: Draw title
    // BitmapFont::DrawString(x, y, title, 0xFFFFFFFF);
    
    int barY = y + HEADER_H;
    int barH = 36;
    
    // Background
    kernel::framebuffer::fill_rect(x, barY, w, barH, 0xFF1E1E1E);
    
    if (!sel->haveInfo) {
        // TODO: Draw "Disk size unavailable" text
        return;
    }
    
    uint64_t total = sel->totalSectors;
    if (total == 0) return;
    
    // Draw partition segments
    for (int i = 0; i < 4; i++) {
        const PartitionEntry& p = sel->parts[i];
        if (p.lbaCount == 0) continue;
        
        uint64_t start = p.lbaStart;
        uint64_t count = p.lbaCount;
        if (start > total) continue;
        if (start + count > total) count = total - start;
        
        int segX = x + static_cast<int>((start * w) / total);
        int segW = static_cast<int>((count * w) / total);
        if (segW <= 0) segW = 1;
        
        uint32_t color = 0xFF4C8BF5; // Blue for partitions
        kernel::framebuffer::fill_rect(segX, barY, segW, barH, color);
        
        // Label (filesystem + size)
        std::string lbl = p.fs + ", " + fmtSize(p.lbaCount * 
            (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
        // TODO: Draw label clipped to segment width
    }
    
    // Total capacity label
    if (!s_cachedTotalCaption.empty()) {
        // TODO: Draw cached total caption
        // BitmapFont::DrawString(x, barY + barH + 6, s_cachedTotalCaption.c_str(), 0xFFFFFFFF);
    }
#endif
}

void DiskManager::drawActions(int x, int y, int w, int h, WinInfo* win) {
    if (!win) return;
    
#ifndef _WIN32
    // Title
    const char* title = "Actions";
    // TODO: Draw title
    // BitmapFont::DrawString(x, y, title, 0xFFFFFFFF);
    
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
    
    // Left column buttons
    s_bxDetectX = leftX;
    s_bxDetectY = byL;
    drawButton(s_bxDetectX, s_bxDetectY, btnWLeft, BTN_H, "Detect media");
    byL += BTN_H + GAP;
    
    s_bxAutoX = leftX;
    s_bxAutoY = byL;
    drawButton(s_bxAutoX, s_bxAutoY, btnWLeft, BTN_H, "Set FS: Auto");
    byL += BTN_H + GAP;
    
    s_bxSwitchFatX = leftX;
    s_bxSwitchFatY = byL;
    drawButton(s_bxSwitchFatX, s_bxSwitchFatY, btnWLeft, BTN_H, "Set FS: FAT");
    byL += BTN_H + GAP;
    
    s_bxSwitchTarX = leftX;
    s_bxSwitchTarY = byL;
    drawButton(s_bxSwitchTarX, s_bxSwitchTarY, btnWLeft, BTN_H, "Set FS: TarFS");
    byL += BTN_H + GAP;
    
    s_bxSwitchExtX = leftX;
    s_bxSwitchExtY = byL;
    drawButton(s_bxSwitchExtX, s_bxSwitchExtY, btnWLeft, BTN_H, "Set FS: EXT2");
    
    // Right column buttons
    s_bxFormatExfatX = rightX;
    s_bxFormatExfatY = byR;
    drawButton(s_bxFormatExfatX, s_bxFormatExfatY, btnWRight, BTN_H, "Format as FAT");
    byR += BTN_H + GAP;
    
    s_bxCreatePartX = rightX;
    s_bxCreatePartY = byR;
    drawButton(s_bxCreatePartX, s_bxCreatePartY, btnWRight, BTN_H, "Create partition (largest free)");
    byR += BTN_H + GAP;
    
    s_bxRefreshX = rightX;
    s_bxRefreshY = byR;
    drawButton(s_bxRefreshX, s_bxRefreshY, btnWRight, BTN_H, "Refresh");
#endif
}

void DiskManager::drawHeaderCell(int x, int y, int w, int h, const char* text) {
#ifndef _WIN32
    kernel::framebuffer::fill_rect(x, y, w, h, 0xFF252525);
    
    // Draw border
    kernel::framebuffer::fill_rect(x, y, w, 1, 0xFF333333); // top
    kernel::framebuffer::fill_rect(x, y + h - 1, w, 1, 0xFF333333); // bottom
    kernel::framebuffer::fill_rect(x, y, 1, h, 0xFF333333); // left
    kernel::framebuffer::fill_rect(x + w - 1, y, 1, h, 0xFF333333); // right
    
    // TODO: Draw text
    // BitmapFont::DrawString(x + 6, y + (h / 2 - 7 / 2), text, 0xFFFFFFFF);
#endif
}

void DiskManager::drawCell(int x, int y, int w, int h, const char* text) {
#ifndef _WIN32
    kernel::framebuffer::fill_rect(x, y, w, h, 0xFF2A2A2A);
    
    // TODO: Draw text with clipping
    // BitmapFont::DrawString(x + 6, y + (h / 2 - 7 / 2), text, 0xFFFFFFFF);
#endif
}

void DiskManager::drawButton(int x, int y, int w, int h, const char* text) {
#ifndef _WIN32
    // Check if mouse is over button (simplified - would need real mouse coords)
    // For now, just draw normal state
    bool hover = false;
    
    uint32_t bg = hover ? 0xFF3A3A3A : 0xFF323232;
    kernel::framebuffer::fill_rect(x, y, w, h, bg);
    
    // TODO: Draw button text
    // BitmapFont::DrawString(x + 10, y + (h / 2 - 7 / 2), text, 0xFFFFFFFF);
#endif
}

} // namespace gui
} // namespace gxos
