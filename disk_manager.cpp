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
#include "desktop_theme.h"
#include "desktop_control_theme.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <new>

#ifdef _WIN32
#include <fstream>
#include <io.h>
#endif

#ifndef _WIN32
#include "kernel/core/include/kernel/block_device.h"
#include "kernel/core/include/kernel/ramdisk.h"
#include "kernel/core/include/kernel/vfs.h"
#endif

namespace gxos {
namespace apps {

using namespace gxos::gui;

namespace {

static bool diskManagerSciFiThemeActive()
{
    return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
}

static DesktopControlTheme diskManagerControlTheme()
{
    return GetDesktopControlTheme(GetCurrentDesktopTheme());
}

static void diskManagerColorComponents(uint32_t color, int& red, int& green, int& blue)
{
    red = static_cast<int>((color >> 16) & 0xFF);
    green = static_cast<int>((color >> 8) & 0xFF);
    blue = static_cast<int>(color & 0xFF);
}

static void diskManagerDrawRect(uint64_t windowId, int x, int y, int w, int h, uint32_t color)
{
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawRect;
    int red = 0;
    int green = 0;
    int blue = 0;
    diskManagerColorComponents(color, red, green, blue);
    std::ostringstream oss;
    oss << windowId << "|" << x << "|" << y << "|" << w << "|" << h
        << "|" << red << "|" << green << "|" << blue;
    const std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
}

static void diskManagerDrawText(uint64_t windowId, int x, int y, const std::string& text, uint32_t color)
{
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    int red = 0;
    int green = 0;
    int blue = 0;
    diskManagerColorComponents(color, red, green, blue);
    std::ostringstream oss;
    oss << windowId << "|" << x << "|" << y << "|" << text
        << "|" << red << "|" << green << "|" << blue;
    const std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
}

static uint32_t diskManagerStatusTextColor(const std::string& text)
{
    if (!diskManagerSciFiThemeActive()) return 0xFFFFFFFFu;

    const DesktopControlTheme roles = diskManagerControlTheme();
    if (text.find("invalid") != std::string::npos ||
        text.find("unreadable") != std::string::npos ||
        text.find("warning") != std::string::npos) {
        return roles.statusWarning;
    }
    if (text.find("Healthy") != std::string::npos ||
        text.find("valid MBR") != std::string::npos ||
        text == "yes" || text.find("Active") != std::string::npos ||
        text.find("Boot/System") != std::string::npos) {
        return roles.controlHoverBorder;
    }
    return roles.secondaryText;
}

}

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
std::vector<DiskManager::HostImageEntry> DiskManager::s_hostImages;
int DiskManager::s_selectedHostImageIndex = 0;

// Button positions
int DiskManager::s_bxDetectX = 0, DiskManager::s_bxDetectY = 0;
int DiskManager::s_bxAutoX = 0, DiskManager::s_bxAutoY = 0;
int DiskManager::s_bxSwitchFatX = 0, DiskManager::s_bxSwitchFatY = 0;
int DiskManager::s_bxSwitchTarX = 0, DiskManager::s_bxSwitchTarY = 0;
int DiskManager::s_bxSwitchExtX = 0, DiskManager::s_bxSwitchExtY = 0;
int DiskManager::s_bxFormatExfatX = 0, DiskManager::s_bxFormatExfatY = 0;
int DiskManager::s_bxCreatePartX = 0, DiskManager::s_bxCreatePartY = 0;
int DiskManager::s_bxRefreshX = 0, DiskManager::s_bxRefreshY = 0;
int DiskManager::s_bxAttachImageX = 0, DiskManager::s_bxAttachImageY = 0;
int DiskManager::s_bxPrevImageX = 0, DiskManager::s_bxPrevImageY = 0;
int DiskManager::s_bxNextImageX = 0, DiskManager::s_bxNextImageY = 0;
int DiskManager::s_bxRescanImagesX = 0, DiskManager::s_bxRescanImagesY = 0;

uint64_t DiskManager::Launch() {
    ProcessSpec spec{"diskmanager", DiskManager::main};
    spec.appId = "gxos.builtin.diskmanager";
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
        s_disks.reserve(16);
        s_hostImages.clear();
        s_hostImages.reserve(16);
        s_selectedHostImageIndex = 0;
        s_selectedDiskIndex = 0;
        s_mouseX = 0;
        s_mouseY = 0;
        s_mouseDown = false;
        
        // Load initial disk data
#ifndef _WIN32
        refreshHostImageLibrary();
#endif
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
                    
                    case MsgType::MT_Invalidate: {
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
    Logger::write(LogLevel::Info, "DiskManager running in Windows host mode - host .img attachment enabled");
    return "Mode: Windows Host (.img attach)\nDetected media: " + s_detected;
#else
    Logger::write(LogLevel::Info, "DiskManager running in guideXOS baremetal mode - using kernel::block API");
    std::string driver = "kernel::block";
    return "Mode: guideXOS Baremetal\nDriver: " + driver + "\nDetected media: " + s_detected;
#endif
}

void DiskManager::refreshDisks() {
    s_disks.clear();
    s_disks.reserve(16);
    
#ifndef _WIN32
    uint8_t devCount = kernel::block::device_count();
    uint8_t found = 0;
    
    for (uint8_t i = 0; i < kernel::block::MAX_BLOCK_DEVICES && found < devCount; i++) {
        const kernel::block::BlockDevice* dev = kernel::block::get_device(i);
        if (!dev || !dev->active) continue;
        found++;
        
        DiskEntry entry;
        entry.devIndex = i;
        entry.bytesPerSector = dev->sectorSize == 0 ? 512 : dev->sectorSize;
        entry.totalSectors = dev->totalSectors;
        entry.haveInfo = true;
        
        if (dev->name[0] == 'r' && dev->name[1] == 'a' && dev->name[2] == 'm') {
            entry.transportLabel = "RAM disk";
            entry.isSystem = false;
        } else if (dev->type == kernel::block::BDEV_ATA_PIO) {
            entry.transportLabel = "ATA";
            entry.isSystem = true;
        } else if (dev->type == kernel::block::BDEV_AHCI) {
            entry.transportLabel = "AHCI";
            entry.isSystem = true;
        } else if (dev->type == kernel::block::BDEV_NVME) {
            entry.transportLabel = "NVMe";
            entry.isSystem = true;
        } else if (dev->type == kernel::block::BDEV_USB_MASS) {
            entry.transportLabel = "USB";
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
    refreshHostImageLibrary();

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

    for (size_t i = 0; i < s_hostImages.size(); ++i) {
        if (!s_hostImages[i].attached) continue;

        DiskEntry imageDisk;
        if (buildHostDiskEntryFromImage(s_hostImages[i], static_cast<uint8_t>(s_disks.size()), imageDisk)) {
            s_disks.push_back(imageDisk);
        }
    }
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

void DiskManager::refreshHostImageLibrary() {
#ifdef _WIN32
    std::vector<HostImageEntry> refreshed;
    intptr_t handle = 0;
    _finddata_t findData;
    handle = _findfirst("disks\\*.img", &findData);
    if (handle == -1) {
        handle = _findfirst("disks\\*.IMG", &findData);
    }
    if (handle == -1) {
        s_hostImages.clear();
        s_selectedHostImageIndex = 0;
        return;
    }

    do {
        if ((findData.attrib & _A_SUBDIR) != 0) continue;

        HostImageEntry item;
        item.path = std::string("disks\\") + findData.name;
        item.displayName = findData.name;
        item.attached = false;

        for (size_t oldIndex = 0; oldIndex < s_hostImages.size(); ++oldIndex) {
            if (s_hostImages[oldIndex].path == item.path) {
                item.attached = s_hostImages[oldIndex].attached;
                break;
            }
        }

        refreshed.push_back(item);
    } while (_findnext(handle, &findData) == 0);

    _findclose(handle);

    s_hostImages.swap(refreshed);
    if (s_selectedHostImageIndex >= static_cast<int>(s_hostImages.size())) {
        s_selectedHostImageIndex = static_cast<int>(s_hostImages.size()) - 1;
    }
    if (s_selectedHostImageIndex < 0) {
        s_selectedHostImageIndex = 0;
    }
#else
    bool hadSelection = !s_hostImages.empty();
    for (size_t i = 0; i < s_hostImages.size(); ++i) {
        if (s_hostImages[i].data) {
            delete[] s_hostImages[i].data;
        }
    }
    s_hostImages.clear();

    kernel::vfs::DirEntry entry;
    const char* searchPaths[] = { "/disks", "/" };
    for (int pathIndex = 0; pathIndex < 2; ++pathIndex) {
        uint8_t dir = kernel::vfs::opendir(searchPaths[pathIndex]);
        if (dir == 0xFF) continue;

        while (kernel::vfs::readdir(dir, &entry)) {
            if (entry.type != kernel::vfs::FILE_TYPE_REGULAR || !isImgName(entry.name) || entry.size == 0) {
                continue;
            }
            if (entry.size > 16U * 1024U * 1024U) {
                continue;
            }

            char fullPath[256];
            kernel::vfs::join_path(searchPaths[pathIndex], entry.name, fullPath, sizeof(fullPath));
            uint8_t file = kernel::vfs::open(fullPath, kernel::vfs::OPEN_READ);
            if (file == 0xFF) continue;

            uint8_t* data = new (std::nothrow) uint8_t[static_cast<size_t>(entry.size)];
            if (!data) {
                kernel::vfs::close(file);
                continue;
            }

            int32_t readBytes = kernel::vfs::read(file, data, static_cast<uint32_t>(entry.size));
            kernel::vfs::close(file);
            if (readBytes != static_cast<int32_t>(entry.size)) {
                delete[] data;
                continue;
            }

            HostImageEntry item;
            item.path = fullPath;
            item.displayName = entry.name;
            item.attached = false;
            item.data = data;
            item.sizeBytes = static_cast<uint32_t>(entry.size);
            item.ramdiskIndex = 0xFF;
            s_hostImages.push_back(item);
        }

        kernel::vfs::closedir(dir);
    }

    if (!hadSelection) {
        s_selectedHostImageIndex = 0;
    }
    if (s_selectedHostImageIndex >= static_cast<int>(s_hostImages.size())) {
        s_selectedHostImageIndex = static_cast<int>(s_hostImages.size()) - 1;
    }
    if (s_selectedHostImageIndex < 0) {
        s_selectedHostImageIndex = 0;
    }
#endif
}

void DiskManager::attachSelectedHostImage() {
#ifdef _WIN32
    if (s_hostImages.empty() || s_selectedHostImageIndex < 0 || s_selectedHostImageIndex >= static_cast<int>(s_hostImages.size())) {
        s_status = "No .img file found in /disks. Add an image there and rescan.";
        return;
    }

    s_hostImages[s_selectedHostImageIndex].attached = true;
    refreshDisks();
    s_status = "Attached image: " + s_hostImages[s_selectedHostImageIndex].displayName + " (read-only)";
#else
    if (s_hostImages.empty() || s_selectedHostImageIndex < 0 || s_selectedHostImageIndex >= static_cast<int>(s_hostImages.size())) {
        s_status = "No .img found in /disks or /. Put a small image on a mounted boot volume and rescan.";
        return;
    }

    HostImageEntry& image = s_hostImages[s_selectedHostImageIndex];
    if (!image.attached) {
        image.ramdiskIndex = kernel::ramdisk::create_readonly_at(image.data, image.sizeBytes, image.displayName.c_str());
        if (image.ramdiskIndex == 0xFF) {
            s_status = "Attach failed: no RAM disk slot or image is too small.";
            return;
        }
        image.attached = true;
    }

    refreshDisks();
    s_status = "Attached image as read-only RAM disk: " + image.displayName;
#endif
}

void DiskManager::selectPrevHostImage() {
    if (s_hostImages.empty()) return;
    if (s_selectedHostImageIndex > 0) {
        s_selectedHostImageIndex--;
    }
}

void DiskManager::selectNextHostImage() {
    if (s_hostImages.empty()) return;
    if (s_selectedHostImageIndex < static_cast<int>(s_hostImages.size()) - 1) {
        s_selectedHostImageIndex++;
    }
}

bool DiskManager::readHostSectors(const std::string& path, uint64_t lba, uint32_t count, void* buffer, uint32_t sectorSize) {
#ifdef _WIN32
    if (!buffer || sectorSize == 0 || count == 0) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    uint64_t offset = lba * sectorSize;
    uint64_t bytes = static_cast<uint64_t>(count) * sectorSize;
    file.seekg(0, std::ios::end);
    std::streamoff fileSize = file.tellg();
    if (fileSize < 0 || offset + bytes > static_cast<uint64_t>(fileSize)) {
        return false;
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
    return file.good() || file.gcount() == static_cast<std::streamsize>(bytes);
#else
    (void)path;
    (void)lba;
    (void)count;
    (void)buffer;
    (void)sectorSize;
    return false;
#endif
}

bool DiskManager::buildHostDiskEntryFromImage(const HostImageEntry& image, uint8_t devIndex, DiskEntry& entry) {
#ifdef _WIN32
    std::ifstream file(image.path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamoff fileSize = file.tellg();
    if (fileSize < 512) {
        return false;
    }

    uint8_t mbr[512];
    std::memset(mbr, 0, sizeof(mbr));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(mbr), sizeof(mbr));
    if (file.gcount() != sizeof(mbr)) {
        return false;
    }

    entry = DiskEntry();
    entry.name = "Disk " + std::to_string(devIndex) + " (USB)";
    entry.transportLabel = "USB";
    entry.isSystem = false;
    entry.isHostImage = true;
    entry.devIndex = devIndex;
    entry.haveInfo = true;
    entry.bytesPerSector = 512;
    entry.totalSectors = static_cast<uint64_t>(fileSize) / entry.bytesPerSector;
    entry.backingPath = image.path;
    entry.mbrStatus = MBR_UNREADABLE;

    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        entry.mbrStatus = MBR_VALID;
        for (int i = 0; i < 4; ++i) {
            int off = 446 + i * 16;
            PartitionEntry& part = entry.parts[i];
            part.status = mbr[off + 0];
            part.type = mbr[off + 4];
            part.lbaStart =
                static_cast<uint32_t>(mbr[off + 8]) |
                (static_cast<uint32_t>(mbr[off + 9]) << 8) |
                (static_cast<uint32_t>(mbr[off + 10]) << 16) |
                (static_cast<uint32_t>(mbr[off + 11]) << 24);
            part.lbaCount =
                static_cast<uint32_t>(mbr[off + 12]) |
                (static_cast<uint32_t>(mbr[off + 13]) << 8) |
                (static_cast<uint32_t>(mbr[off + 14]) << 16) |
                (static_cast<uint32_t>(mbr[off + 15]) << 24);

            if (part.type != 0 && part.lbaCount != 0) {
                part.fs = detectFsAtLBAFromImage(image.path, part.lbaStart);
                part.mountPoint = suggestMountPoint(entry, part, i);
                part.mounted = false;
            }
        }
    } else {
        entry.mbrStatus = MBR_INVALID;
    }

    entry.name = "Disk " + std::to_string(devIndex) + " (USB: " + image.displayName + ")";
    return true;
#else
    (void)image;
    (void)devIndex;
    (void)entry;
    return false;
#endif
}

std::string DiskManager::detectFsAtLBAFromImage(const std::string& path, uint32_t lbaStart) {
#ifdef _WIN32
    if (lbaStart == 0) return "<empty>";

    uint8_t sec[512];
    std::memset(sec, 0, sizeof(sec));
    if (!readHostSectors(path, lbaStart, 1, sec, 512)) {
        return "Unknown";
    }

    if (sec[3] == 'E' && sec[4] == 'X' && sec[5] == 'F' && sec[6] == 'A' && sec[7] == 'T') {
        return "exFAT";
    }
    if (sec[257] == 'u' && sec[258] == 's' && sec[259] == 't' && sec[260] == 'a' && sec[261] == 'r') {
        return "TarFS";
    }
    if (sec[510] == 0x55 && sec[511] == 0xAA) {
        if (sec[82] == 'F' && sec[83] == 'A' && sec[84] == 'T' && sec[85] == '3' && sec[86] == '2') {
            return "FAT32";
        }
        if (sec[54] == 'F' && sec[55] == 'A' && sec[56] == 'T') {
            return "FAT";
        }
        uint16_t bytesPerSec = sec[11] | (sec[12] << 8);
        uint8_t secPerClus = sec[13];
        if ((bytesPerSec == 512 || bytesPerSec == 1024 || bytesPerSec == 2048 || bytesPerSec == 4096) && secPerClus != 0) {
            return "FAT";
        }
    }

    uint8_t sb[1024];
    std::memset(sb, 0, sizeof(sb));
    if (readHostSectors(path, static_cast<uint64_t>(lbaStart) + 2, 2, sb, 512)) {
        uint16_t magic = sb[56] | (sb[57] << 8);
        if (magic == 0xEF53) {
            return "EXT2/EXT4";
        }
    }
#else
    (void)path;
    (void)lbaStart;
#endif

    return "Unknown";
}

bool DiskManager::isImgName(const char* name) {
    if (!name) return false;
    size_t len = 0;
    while (name[len]) ++len;
    if (len < 4) return false;
    const char* ext = name + len - 4;
    return (ext[0] == '.' && (ext[1] == 'i' || ext[1] == 'I') &&
            (ext[2] == 'm' || ext[2] == 'M') && (ext[3] == 'g' || ext[3] == 'G'));
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
                
                if (entry.parts[i].type != 0 && entry.parts[i].lbaCount != 0) {
                    entry.parts[i].fs = detectFsAtLBA(entry.devIndex, entry.parts[i].lbaStart);
                    entry.parts[i].mountPoint = suggestMountPoint(entry, entry.parts[i], i);
#ifndef _WIN32
                    const kernel::vfs::MountPoint* mount = nullptr;
                    if (entry.parts[i].mountPoint != "unmounted") {
                        mount = kernel::vfs::get_mount(entry.parts[i].mountPoint.c_str());
                    }
                    if (mount && mount->blockDevIndex == entry.devIndex && mount->fsVolumeIndex == static_cast<uint8_t>(i + 1)) {
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
    std::memset(sec, 0, sizeof(sec));
    kernel::block::Status status = kernel::block::read_sectors(devIndex, lbaStart, 1, sec);
    
    if (status == kernel::block::BLOCK_OK) {
        if (sec[3] == 'E' && sec[4] == 'X' && sec[5] == 'F' && sec[6] == 'A' && sec[7] == 'T') {
            return "exFAT";
        }

        if (sec[257] == 'u' && sec[258] == 's' && sec[259] == 't' &&
            sec[260] == 'a' && sec[261] == 'r') {
            return "TarFS";
        }
        
        if (sec[510] == 0x55 && sec[511] == 0xAA) {
            if (sec[82] == 'F' && sec[83] == 'A' && sec[84] == 'T' && sec[85] == '3' && sec[86] == '2') {
                return "FAT32";
            }
            if (sec[54] == 'F' && sec[55] == 'A' && sec[56] == 'T') {
                return "FAT";
            }
            uint16_t bytesPerSec = sec[11] | (sec[12] << 8);
            uint8_t secPerClus = sec[13];
            if ((bytesPerSec == 512 || bytesPerSec == 1024 || 
                 bytesPerSec == 2048 || bytesPerSec == 4096) && secPerClus != 0) {
                return "FAT";
            }
        }
        
        uint8_t sb[1024];
        std::memset(sb, 0, sizeof(sb));
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
    if (part.type == 0x83 || part.type == 0x82) {
        return "/users";
    }
    if (part.type == 0x0B || part.type == 0x0C || part.type == 0x07 || part.fs == "FAT" || part.fs == "FAT32" || part.fs == "exFAT") {
        return "/shared";
    }
    return "unmounted";
}

bool DiskManager::hit(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

void DiskManager::trySetFS_Auto() {
    refreshDisks();
    s_status = "Read-only scan refreshed; no filesystem changes were made.";
    Logger::write(LogLevel::Info, "DiskManager: read-only auto scan requested");
}

void DiskManager::trySetFS_FAT() {
    s_status = "Set FS is disabled: DiskManager only detects filesystems in read-only mode.";
    Logger::write(LogLevel::Warn, "DiskManager: FAT switch requested while disabled");
}

void DiskManager::trySetFS_TAR() {
    s_status = "Set FS is disabled: DiskManager only detects filesystems in read-only mode.";
    Logger::write(LogLevel::Warn, "DiskManager: TarFS switch requested while disabled");
}

void DiskManager::trySetFS_EXT2() {
    s_status = "Set FS is disabled: DiskManager only detects filesystems in read-only mode.";
    Logger::write(LogLevel::Warn, "DiskManager: EXT2 switch requested while disabled");
}

void DiskManager::tryFormatFAT() {
    s_status = "Format is disabled: write support must be intentionally enabled first.";
    Logger::write(LogLevel::Warn, "DiskManager: Format FAT requested while disabled");
}

void DiskManager::tryCreatePartitionLargestFree() {
    DiskEntry* sel = getSelected();
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
    if (hit(mx, my, s_bxAttachImageX, s_bxAttachImageY, 160, BTN_H)) {
        attachSelectedHostImage();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxPrevImageX, s_bxPrevImageY, 32, BTN_H)) {
        selectPrevHostImage();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxNextImageX, s_bxNextImageY, 32, BTN_H)) {
        selectNextHostImage();
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxRescanImagesX, s_bxRescanImagesY, 160, BTN_H)) {
        refreshHostImageLibrary();
        refreshDisks();
#ifdef _WIN32
        s_status = "Rescanned disks/ for .img files.";
#else
        s_status = "Rescanned /disks and / for .img files.";
#endif
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
    msg.type = (uint32_t)MsgType::MT_Invalidate;
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    std::ostringstream oss;
    int red = 43;
    int green = 43;
    int blue = 43;
    if (sciFi) {
        red = static_cast<int>((roles.panelBackground >> 16) & 0xFF);
        green = static_cast<int>((roles.panelBackground >> 8) & 0xFF);
        blue = static_cast<int>(roles.panelBackground & 0xFF);
    }
    oss << s_windowId << "|" << red << "|" << green << "|" << blue;
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);

    if (sciFi) {
        // Keep the existing geometry while making the two application-owned
        // work areas and their boundary read as a single technical surface.
        diskManagerDrawRect(s_windowId, LEFT_PANE_W, 0, 1, 560, roles.separator);
    }
    
    // Draw all components
    drawLeftPane(0, 0, 920, 560);
    
    int rightX = LEFT_PANE_W + PAD;
    int rightW = 920 - rightX - PAD;
    if (rightW < 100) rightW = 100;
    
    int topH = 170;
    int mountsY = PAD + topH + GAP;
    int mountsH = 110;
    int bottomY = mountsY + mountsH + GAP;
    int bottomH = 104;
    
    drawVolumesGrid(rightX, PAD, rightW, topH);
    drawMountsSection(rightX, mountsY, rightW, mountsH);
    drawPartitionMap(rightX, bottomY, rightW, bottomH);
    drawActions(rightX, 560 - (PAD + 160), rightW, 160);
    
    // Request compositor to paint
    ipc::Message paintMsg;
    paintMsg.type = (uint32_t)MsgType::MT_Invalidate;
    std::string paintPayload = std::to_string(s_windowId);
    paintMsg.data.assign(paintPayload.begin(), paintPayload.end());
    ipc::Bus::publish("gui.input", std::move(paintMsg), false);
}

void DiskManager::drawLeftPane(int winX, int winY, int winW, int winH) {
    (void)winX;
    (void)winY;
    (void)winW;
    int lx = PAD;
    int ly = PAD;
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();

    diskManagerDrawRect(s_windowId, PAD, PAD, LEFT_PANE_W - PAD, winH - PAD * 2,
                        sciFi ? roles.raisedPanel : 0xFF2B2B2Bu);
    
    // Title "Disks"
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    const uint32_t titleColor = sciFi ? roles.primaryText : 0xFFFFFFFFu;
    int titleR = static_cast<int>((titleColor >> 16) & 0xFF);
    int titleG = static_cast<int>((titleColor >> 8) & 0xFF);
    int titleB = static_cast<int>(titleColor & 0xFF);
    oss << s_windowId << "|" << lx << "|" << (ly - 2) << "|Disks|"
        << titleR << "|" << titleG << "|" << titleB;
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    int listY = ly + HEADER_H;
    int rowW = LEFT_PANE_W - PAD * 2;
    int rowX = PAD + PAD;
    
    // Draw disk list
    for (int i = 0; i < static_cast<int>(s_disks.size()); i++) {
        int ry = listY + i * (ROW_H + 4);
        bool selected = (s_selectedDiskIndex == i);
        const uint32_t rowColor = sciFi
            ? (selected ? DesktopSelectionColor(roles, true)
                        : (i % 2 == 0 ? roles.panelBackground : roles.recessedField))
            : (selected ? 0xFF3A3A3Au : 0xFF303030u);
        
        // Background rect
        diskManagerDrawRect(s_windowId, rowX, ry, rowW, ROW_H, rowColor);
        
        // Disk name text
        diskManagerDrawText(s_windowId, rowX + 6, ry + 3, s_disks[i].name,
                            sciFi ? (selected ? roles.selectionText : roles.primaryText)
                                  : 0xFFFFFFFFu);

        if (s_disks[i].haveInfo) {
            uint64_t totalBytes = s_disks[i].totalSectors * s_disks[i].bytesPerSector;
            std::string detail = "#" + std::to_string(s_disks[i].devIndex) + "  " + fmtSize(totalBytes) + "  " + mbrStatusText(s_disks[i].mbrStatus);
            if (s_disks[i].isHostImage) {
                detail += "  attached .img";
            }
            diskManagerDrawText(s_windowId, rowX + 6, ry + 15, detail,
                                sciFi ? roles.secondaryText : 0xFFB4B4B4u);
        }
    }
    
    // Status text at bottom
    int statusY = winH - (PAD + 40);
    diskManagerDrawText(s_windowId, lx, statusY, s_status,
                        sciFi ? roles.secondaryText : 0xFFFFFFFFu);
}

void DiskManager::drawVolumesGrid(int x, int y, int w, int h) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        sciFi ? roles.recessedField : 0xFF2A2A2Au);

    // Title
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    const uint32_t titleColor = sciFi ? roles.primaryText : 0xFFFFFFFFu;
    oss << s_windowId << "|" << x << "|" << y << "|Volumes|"
        << ((titleColor >> 16) & 0xFF) << "|" << ((titleColor >> 8) & 0xFF)
        << "|" << (titleColor & 0xFF);
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    DiskEntry* sel = getSelected();
    int gridY = y + HEADER_H;
    if (sel && sel->haveInfo) {
        uint64_t totalBytes = sel->totalSectors * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector);
        std::string info = "Disk " + std::to_string(sel->devIndex) + "  " + sel->transportLabel +
            "  Sector " + std::to_string(sel->bytesPerSector) + " B" +
            "  Sectors " + std::to_string(sel->totalSectors) +
            "  Size " + fmtSize(totalBytes) +
            "  " + mbrStatusText(sel->mbrStatus);
        diskManagerDrawText(s_windowId, x, y + 16, info,
                            sciFi ? roles.secondaryText : 0xFFD2D2D2u);
        gridY += 18;
    }
    
    // Column widths
    int cw[] = { 124, 42, 44, 70, 124, 52, 80, 72, 78, 74, 68 };
    int sum = 0;
    for (int i = 0; i < 11; i++) sum += cw[i];
    
    if (sum != w) {
        if (sum > w) {
            float scale = static_cast<float>(w) / static_cast<float>(sum);
            int newsum = 0;
            for (int i = 0; i < 11; i++) {
                cw[i] = static_cast<int>(cw[i] * scale);
                if (cw[i] < 40) cw[i] = 40;
                newsum += cw[i];
            }
            cw[10] += w - newsum;
        } else {
            cw[10] += w - sum;
        }
    }
    
    // Draw headers
    int cx = x;
    const char* headers[] = { "Volume", "Dev", "Part", "FS", "Status", "Type", "Capacity", "MBR", "Start", "Sectors", "Sector" };
    for (int i = 0; i < 11; i++) {
        drawHeaderCell(cx, gridY, cw[i], ROW_H, headers[i]);
        cx += cw[i];
    }
    
    // Draw partition rows
    int rowY = gridY + ROW_H;
    if (sel && sel->haveInfo) {
        for (int i = 0; i < 4; i++) {
            const PartitionEntry& p = sel->parts[i];
            if (p.lbaCount == 0) continue;
            
            cx = x;
            
            std::string vol = "Disk " + std::to_string(sel->devIndex) + " Part " + std::to_string(i + 1);
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
            drawCellWithColor(cx, rowY, cw[4], ROW_H, status.c_str(),
                              diskManagerStatusTextColor(status));
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
            drawCellWithColor(cx, rowY, cw[7], ROW_H, mbr.c_str(),
                              diskManagerStatusTextColor(mbr));
            cx += cw[7];
            
            std::string start = std::to_string(p.lbaStart);
            drawCell(cx, rowY, cw[8], ROW_H, start.c_str());
            cx += cw[8];

            std::string sectors = std::to_string(p.lbaCount);
            drawCell(cx, rowY, cw[9], ROW_H, sectors.c_str());
            cx += cw[9];

            std::string sectorSize = std::to_string(sel->bytesPerSector);
            drawCell(cx, rowY, cw[10], ROW_H, sectorSize.c_str());
            
            rowY += ROW_H;
            if (rowY > y + h - ROW_H) break;
        }
    }
}

void DiskManager::drawMountsSection(int x, int y, int w, int h) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        sciFi ? roles.panelBackground : 0xFF2A2A2Au);

    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    const uint32_t titleColor = sciFi ? roles.primaryText : 0xFFFFFFFFu;
    oss << s_windowId << "|" << x << "|" << y << "|Mounts|"
        << ((titleColor >> 16) & 0xFF) << "|" << ((titleColor >> 8) & 0xFF)
        << "|" << (titleColor & 0xFF);
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);

    int gridY = y + HEADER_H;
    int cw[] = { 72, 78, 130, 170, 92 };
    int sum = 0;
    for (int i = 0; i < 5; i++) sum += cw[i];
    if (sum > w) {
        int over = sum - w;
        if (cw[3] > over + 80) {
            cw[3] -= over;
        } else {
            cw[4] -= over;
        }
    } else {
        cw[4] += w - sum;
    }

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
        std::string mp = p.mountPoint.empty() ? "unmounted" : p.mountPoint;

        drawCell(cx, rowY, cw[0], ROW_H, dev.c_str());
        cx += cw[0];
        drawCell(cx, rowY, cw[1], ROW_H, part.c_str());
        cx += cw[1];
        drawCell(cx, rowY, cw[2], ROW_H, p.fs.c_str());
        cx += cw[2];
        drawCell(cx, rowY, cw[3], ROW_H, mp.c_str());
        cx += cw[3];
        drawCellWithColor(cx, rowY, cw[4], ROW_H, mounted.c_str(),
                          diskManagerStatusTextColor(mounted));

        rowY += ROW_H;
        if (rowY > y + h - ROW_H) break;
    }
}

void DiskManager::drawPartitionMap(int x, int y, int w, int h) {
    DiskEntry* sel = getSelected();
    if (!sel) return;
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        sciFi ? roles.panelBackground : 0xFF2A2A2Au);
    
    // Title
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    const uint32_t titleColor = sciFi ? roles.primaryText : 0xFFFFFFFFu;
    oss << s_windowId << "|" << x << "|" << y << "|" << sel->name << "|"
        << ((titleColor >> 16) & 0xFF) << "|" << ((titleColor >> 8) & 0xFF)
        << "|" << (titleColor & 0xFF);
    std::string payload = oss.str();
    msg.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(msg), false);
    
    int barY = y + HEADER_H;
    int barH = 34;
    
    // Background bar
    diskManagerDrawRect(s_windowId, x, barY, w, barH,
                        sciFi ? roles.recessedField : 0xFF1E1E1Eu);
    if (sciFi) {
        diskManagerDrawRect(s_windowId, x, barY, w, 1, roles.separator);
        diskManagerDrawRect(s_windowId, x, barY + barH - 1, w, 1, roles.separator);
    }
    
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
            diskManagerDrawRect(s_windowId, freeX, barY, freeW, barH,
                                sciFi ? roles.raisedPanel : 0xFF3A3A3Au);
            if (freeW > 70) {
                std::string freeLbl = "Unallocated " + fmtSize(freeCount * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
                diskManagerDrawText(s_windowId, freeX + 4, barY + 10, freeLbl,
                                    sciFi ? roles.secondaryText : 0xFFD2D2D2u);
            }
        }

        int segX = x + static_cast<int>((start * w) / total);
        int segW = static_cast<int>((count * w) / total);
        if (segW <= 0) segW = 1;

        const bool systemPartition = p.status == 0x80 || (next == 0 && sel->isSystem);
        const uint32_t segmentColor = sciFi
            ? (systemPartition ? GetCurrentDesktopTheme().accent
                               : (next % 2 == 0 ? roles.controlHoverBorder : roles.selectionInactive))
            : 0xFF4C8BF5u;
        diskManagerDrawRect(s_windowId, segX, barY, segW, barH, segmentColor);

        std::string lbl = "P" + std::to_string(next + 1) + " " + p.fs + " " + fmtHexByte(p.type);
        if (p.status == 0x80) lbl += " Active";
        if (p.status == 0x80 || (next == 0 && sel->isSystem)) lbl += " Boot/System";
        lbl += " " + fmtSize(p.lbaCount * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
        if (segW > 40) {
            diskManagerDrawText(s_windowId, segX + 4, barY + 10, lbl,
                                sciFi ? roles.selectionText : 0xFFFFFFFFu);
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
        diskManagerDrawRect(s_windowId, freeX, barY, freeW, barH,
                            sciFi ? roles.raisedPanel : 0xFF3A3A3Au);
        if (freeW > 70) {
            std::string freeLbl = "Unallocated " + fmtSize(freeCount * (sel->bytesPerSector == 0 ? 512UL : sel->bytesPerSector));
            diskManagerDrawText(s_windowId, freeX + 4, barY + 10, freeLbl,
                                sciFi ? roles.secondaryText : 0xFFD2D2D2u);
        }
    }
    
    // Total capacity
    if (!s_cachedTotalCaption.empty()) {
        ipc::Message capMsg;
        capMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream capOss;
        std::string info = "Device " + std::to_string(sel->devIndex) + ", " + s_cachedTotalCaption + ", sector " + std::to_string(sel->bytesPerSector) + " B, sectors " + std::to_string(sel->totalSectors) + ", " + mbrStatusText(sel->mbrStatus);
        const uint32_t infoColor = sciFi ? roles.primaryText : 0xFFFFFFFFu;
        capOss << s_windowId << "|" << x << "|" << (barY + barH + 6) << "|" << info
               << "|" << ((infoColor >> 16) & 0xFF) << "|" << ((infoColor >> 8) & 0xFF)
               << "|" << (infoColor & 0xFF);
        std::string capPayload = capOss.str();
        capMsg.data.assign(capPayload.begin(), capPayload.end());
        ipc::Bus::publish("gui.input", std::move(capMsg), false);
    }
}

void DiskManager::drawActions(int x, int y, int w, int h) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        sciFi ? roles.raisedPanel : 0xFF2B2B2Bu);

    // Title
    ipc::Message msg;
    msg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream oss;
    const uint32_t titleColor = sciFi ? roles.primaryText : 0xFFFFFFFFu;
    oss << s_windowId << "|" << x << "|" << y << "|Actions|"
        << ((titleColor >> 16) & 0xFF) << "|" << ((titleColor >> 8) & 0xFF)
        << "|" << (titleColor & 0xFF);
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
    drawDisabledButton(s_bxSwitchFatX, s_bxSwitchFatY, btnWLeft, BTN_H, "Set FS: FAT");
    byL += BTN_H + GAP;
    
    s_bxSwitchTarX = leftX; s_bxSwitchTarY = byL;
    drawDisabledButton(s_bxSwitchTarX, s_bxSwitchTarY, btnWLeft, BTN_H, "Set FS: TarFS");
    byL += BTN_H + GAP;
    
    s_bxSwitchExtX = leftX; s_bxSwitchExtY = byL;
    drawDisabledButton(s_bxSwitchExtX, s_bxSwitchExtY, btnWLeft, BTN_H, "Set FS: EXT2");
    
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
    diskManagerDrawText(s_windowId, rightX, noteY,
                        "Dangerous actions disabled: read-only disk inspection mode.",
                        sciFi ? roles.statusWarning : 0xFFB4B4B4u);

    int hostY = noteY + 24;
    std::string hostText;
#ifdef _WIN32
    hostText = "Attach image from disks/ (.img, read-only)";
#else
    hostText = "Attach .img from VFS /disks or / as read-only RAM disk";
#endif
    diskManagerDrawText(s_windowId, rightX, hostY, hostText,
                        sciFi ? roles.secondaryText : 0xFFD2D2D2u);

    int navY = hostY + 18;
    s_bxPrevImageX = rightX;
    s_bxPrevImageY = navY;
    drawButton(s_bxPrevImageX, s_bxPrevImageY, 32, BTN_H, "<",
               hit(s_mouseX, s_mouseY, s_bxPrevImageX, s_bxPrevImageY, 32, BTN_H));

    std::string currentImage = "No .img files found";
    if (!s_hostImages.empty() && s_selectedHostImageIndex >= 0 && s_selectedHostImageIndex < static_cast<int>(s_hostImages.size())) {
        currentImage = s_hostImages[s_selectedHostImageIndex].displayName;
        if (s_hostImages[s_selectedHostImageIndex].attached) {
            currentImage += " [attached]";
        }
    }
    diskManagerDrawText(s_windowId, rightX + 40, navY + 8, currentImage,
                        sciFi ? roles.primaryText : 0xFFFFFFFFu);

    s_bxNextImageX = rightX + 220;
    s_bxNextImageY = navY;
    drawButton(s_bxNextImageX, s_bxNextImageY, 32, BTN_H, ">",
               hit(s_mouseX, s_mouseY, s_bxNextImageX, s_bxNextImageY, 32, BTN_H));

    s_bxAttachImageX = rightX + 264;
    s_bxAttachImageY = navY;
    drawButton(s_bxAttachImageX, s_bxAttachImageY, 160, BTN_H, "Attach image",
               hit(s_mouseX, s_mouseY, s_bxAttachImageX, s_bxAttachImageY, 160, BTN_H));

    s_bxRescanImagesX = rightX + 436;
    s_bxRescanImagesY = navY;
    drawButton(s_bxRescanImagesX, s_bxRescanImagesY, 160, BTN_H, "Rescan images",
               hit(s_mouseX, s_mouseY, s_bxRescanImagesX, s_bxRescanImagesY, 160, BTN_H));
}

void DiskManager::drawHeaderCell(int x, int y, int w, int h, const char* text) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        sciFi ? roles.tableHeaderBackground : 0xFF252525u);
    if (sciFi) {
        diskManagerDrawRect(s_windowId, x, y + h - 1, w, 1, roles.separator);
    }
    diskManagerDrawText(s_windowId, x + 6, y + 6, text,
                        sciFi ? roles.tableHeaderText : 0xFFFFFFFFu);
}

void DiskManager::drawCell(int x, int y, int w, int h, const char* text) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    drawCellWithColor(x, y, w, h, text,
                      sciFi ? roles.primaryText : 0xFFFFFFFFu);
}

void DiskManager::drawCellWithColor(int x, int y, int w, int h, const char* text, uint32_t textColor) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        sciFi ? roles.recessedField : 0xFF2A2A2Au);
    if (sciFi) {
        diskManagerDrawRect(s_windowId, x, y + h - 1, w, 1, roles.separator);
    }
    diskManagerDrawText(s_windowId, x + 6, y + 6, text, textColor);
}

void DiskManager::drawButton(int x, int y, int w, int h, const char* text, bool hover) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    if (!sciFi) {
        const uint32_t classicColor = hover ? 0xFF3A3A3Au : 0xFF323232u;
        diskManagerDrawRect(s_windowId, x, y, w, h, classicColor);
        diskManagerDrawText(s_windowId, x + 10, y + 8, text, 0xFFFFFFFFu);
        return;
    }

    const DesktopControlState state = s_mouseDown && hover
        ? DesktopControlState::Pressed
        : (hover ? DesktopControlState::Hover : DesktopControlState::Normal);
    diskManagerDrawRect(s_windowId, x, y, w, h,
                        DesktopControlBorderColor(roles, state));
    if (w > 2 && h > 2) {
        diskManagerDrawRect(s_windowId, x + 1, y + 1, w - 2, h - 2,
                            DesktopControlFillColor(roles, state));
    }
    diskManagerDrawText(s_windowId, x + 10, y + 8, text,
                        DesktopControlTextColor(roles, state));
}

void DiskManager::drawDisabledButton(int x, int y, int w, int h, const char* text) {
    const bool sciFi = diskManagerSciFiThemeActive();
    const DesktopControlTheme roles = diskManagerControlTheme();
    if (!sciFi) {
        diskManagerDrawRect(s_windowId, x, y, w, h, 0xFF262626u);
        diskManagerDrawText(s_windowId, x + 10, y + 8, text, 0xFF787878u);
        return;
    }

    diskManagerDrawRect(s_windowId, x, y, w, h,
                        DesktopControlBorderColor(roles, DesktopControlState::Disabled));
    if (w > 2 && h > 2) {
        diskManagerDrawRect(s_windowId, x + 1, y + 1, w - 2, h - 2,
                            DesktopControlFillColor(roles, DesktopControlState::Disabled));
    }
    diskManagerDrawText(s_windowId, x + 10, y + 8, text,
                        DesktopControlTextColor(roles, DesktopControlState::Disabled));
}

} // namespace apps
} // namespace gxos
