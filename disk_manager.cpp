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
#include <new>

#ifdef _WIN32
#include <fstream>
#include <io.h>
#endif

#ifndef _WIN32
#include "kernel/core/include/kernel/block_device.h"
#include "kernel/core/include/kernel/ramdisk.h"
#include "kernel/core/include/kernel/vfs.h"
#include "kernel/core/include/kernel/storage_manager.h"
#endif

namespace gxos {
namespace apps {

using namespace gxos::gui;

// Static member initialization
uint64_t DiskManager::s_windowId = 0;
std::vector<DiskManager::DiskEntry> DiskManager::s_disks;
int DiskManager::s_selectedDiskIndex = 0;
std::string DiskManager::s_status = "";
bool DiskManager::s_clickLock = false;
int DiskManager::s_mouseX = 0;
int DiskManager::s_mouseY = 0;
bool DiskManager::s_mouseDown = false;
std::vector<DiskManager::HostImageEntry> DiskManager::s_hostImages;
int DiskManager::s_selectedHostImageIndex = 0;

// Button positions
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
    return "Mode: Windows Host | Attached .img inspection only";
#else
    Logger::write(LogLevel::Info, "DiskManager running in guideXOS baremetal mode - using kernel::block API");
    std::string driver = "kernel::block";
    return "Mode: guideXOS Baremetal\nDriver: " + driver;
#endif
}

void DiskManager::refreshDisks() {
    bool hadSelectedImage = false;
    std::string selectedImagePath;
    if (s_selectedDiskIndex >= 0 && s_selectedDiskIndex < static_cast<int>(s_disks.size())) {
        const DiskEntry& oldSelected = s_disks[s_selectedDiskIndex];
        hadSelectedImage = oldSelected.isHostImage;
        if (hadSelectedImage) selectedImagePath = oldSelected.backingPath;
    }
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
        entry.bytesPerSector = dev->sectorSize;
        entry.totalSectors = dev->totalSectors;
        entry.haveInfo = kernel::storage::valid_geometry(entry.totalSectors,
            entry.bytesPerSector);
        
        if (dev->type == kernel::block::BDEV_RAMDISK) {
            entry.transportLabel = "RAM disk";
        } else if (dev->type == kernel::block::BDEV_ATA_PIO) {
            entry.transportLabel = "ATA";
        } else if (dev->type == kernel::block::BDEV_AHCI) {
            entry.transportLabel = "AHCI";
        } else if (dev->type == kernel::block::BDEV_NVME) {
            entry.transportLabel = "NVMe";
        } else if (dev->type == kernel::block::BDEV_USB_MASS) {
            entry.transportLabel = "USB";
        } else {
            entry.transportLabel = "unknown";
        }
        entry.name = "Disk " + std::to_string(i) + " (" + entry.transportLabel + ")";
        
        readMBRForEntry(entry);
        s_disks.push_back(entry);
    }
    
#else
    refreshHostImageLibrary();

    for (size_t i = 0; i < s_hostImages.size(); ++i) {
        if (!s_hostImages[i].attached) continue;

        DiskEntry imageDisk;
        if (buildHostDiskEntryFromImage(s_hostImages[i], static_cast<uint8_t>(s_disks.size()), imageDisk)) {
            s_disks.push_back(imageDisk);
        }
    }
#endif
    
    if (hadSelectedImage) {
        s_selectedDiskIndex = -1;
        for (size_t i = 0; i < s_disks.size(); ++i) {
            if (s_disks[i].isHostImage && s_disks[i].backingPath == selectedImagePath) {
                s_selectedDiskIndex = static_cast<int>(i);
                break;
            }
        }
    } else if (s_disks.empty()) {
        s_selectedDiskIndex = -1;
    } else if (s_selectedDiskIndex >= static_cast<int>(s_disks.size())) {
        s_selectedDiskIndex = static_cast<int>(s_disks.size()) - 1;
    } else if (s_selectedDiskIndex < 0) {
        s_selectedDiskIndex = 0;
    }
    
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
    const bool hadSelection = !s_hostImages.empty();
    std::vector<HostImageEntry> previous;
    previous.swap(s_hostImages);
    std::vector<bool> retained(previous.size(), false);
    std::vector<HostImageEntry> refreshed;

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
            item.ramdiskBlockIndex = 0xFF;

            for (size_t oldIndex = 0; oldIndex < previous.size(); ++oldIndex) {
                HostImageEntry& old = previous[oldIndex];
                if (old.path != item.path || !old.attached || old.ramdiskIndex == 0xFF)
                    continue;
                if (!kernel::ramdisk::validate_attachment_identity(
                        old.ramdiskIndex, old.ramdiskBlockIndex,
                        old.ramdiskIdentity)) continue;

                item.attached = true;
                item.ramdiskIndex = old.ramdiskIndex;
                item.ramdiskBlockIndex = old.ramdiskBlockIndex;
                item.ramdiskIdentity = old.ramdiskIdentity;
                item.sizeBytes = old.sizeBytes;
                if (item.data) delete[] item.data;
                item.data = old.data; // Retain legacy caller-owned backing, when present.
                old.data = nullptr;
                retained[oldIndex] = true;
                break;
            }
            refreshed.push_back(item);
        }

        kernel::vfs::closedir(dir);
    }

    // An attached RAM disk remains registered even if its source file was
    // removed. Keep its path/index row so another rescan cannot attach a
    // duplicate or forget the live device identity.
    for (size_t oldIndex = 0; oldIndex < previous.size(); ++oldIndex) {
        HostImageEntry& old = previous[oldIndex];
        if (old.attached && old.ramdiskIndex != 0xFF) {
            if (kernel::ramdisk::validate_attachment_identity(
                    old.ramdiskIndex, old.ramdiskBlockIndex, old.ramdiskIdentity)) {
                if (!retained[oldIndex]) refreshed.push_back(old);
                old.data = nullptr;
                retained[oldIndex] = true;
            } else {
                const kernel::ramdisk::RamDisk* current =
                    kernel::ramdisk::get_disk(old.ramdiskIndex);
                if (current && current->instanceId == old.ramdiskIdentity)
                    kernel::ramdisk::destroy(old.ramdiskIndex);
            }
        }
        }
        if (!retained[oldIndex] && old.data) {
            delete[] old.data;
            old.data = nullptr;
        }
    }

    s_hostImages.swap(refreshed);

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
        image.ramdiskIndex = kernel::ramdisk::create_readonly_owned(image.data, image.sizeBytes, image.displayName.c_str());
        if (image.ramdiskIndex == 0xFF) {
            s_status = "Attach failed: no RAM disk slot or image is too small.";
            return;
        }
        image.data = nullptr; // RAM disk owns the image buffer until destroy().
        const kernel::ramdisk::RamDisk* attached = kernel::ramdisk::get_disk(image.ramdiskIndex);
        image.ramdiskBlockIndex = attached ? attached->blockDeviceIndex : 0xFF;
        image.ramdiskIdentity = attached ? attached->instanceId : 0;
        image.attached = attached != nullptr;
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
    entry.name.clear();
    entry.transportLabel = "Host image";
    entry.isHostImage = true;
    entry.devIndex = devIndex;
    entry.haveInfo = true;
    entry.bytesPerSector = 512;
    entry.totalSectors = static_cast<uint64_t>(fileSize) / entry.bytesPerSector;
    entry.backingPath = image.path;
    entry.mbrStatus = MBR_PREVIEW_NO_SIGNATURE;

    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        bool hasProtectiveMbr = false;
        bool validPreview = (static_cast<uint64_t>(fileSize) % entry.bytesPerSector) == 0;
        uint64_t starts[4] = {};
        uint64_t ends[4] = {};
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

            const uint64_t start = part.lbaStart;
            const uint64_t count = part.lbaCount;
            if (part.type == 0 && count == 0 && start == 0 && part.status == 0) continue;
            if (part.type == 0xEE) hasProtectiveMbr = true;
            if ((part.status != 0 && part.status != 0x80) || part.type == 0 ||
                count == 0 || start == 0 || start >= entry.totalSectors ||
                count > entry.totalSectors - start) {
                validPreview = false;
                continue;
            }
            starts[i] = start;
            ends[i] = start + count;
        }
        for (int i = 0; i < 4; ++i) {
            if (starts[i] == 0) continue;
            for (int j = i + 1; j < 4; ++j) {
                if (starts[j] != 0 && starts[i] < ends[j] && starts[j] < ends[i])
                    validPreview = false;
            }
        }
        if (validPreview) {
            for (int i = 0; i < 4; ++i) {
                PartitionEntry& part = entry.parts[i];
                if (part.lbaCount == 0) continue;
                part.fs = part.type == 0x05 || part.type == 0x0F || part.type == 0x85
                    ? "Not probed" : "Unknown";
            }
        }
        if (hasProtectiveMbr) {
            entry.mbrStatus = DISK_GPT_UNSUPPORTED;
        } else if (validPreview) {
            entry.mbrStatus = MBR_PREVIEW_VALID;
        } else {
            entry.mbrStatus = MBR_INVALID;
        }
    } else {
        entry.mbrStatus = MBR_PREVIEW_NO_SIGNATURE;
    }

    entry.name = "Image: " + image.displayName;
    return true;
#else
    (void)image;
    (void)devIndex;
    (void)entry;
    return false;
#endif
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
    for (int i = 0; i < 4; ++i) entry.parts[i] = PartitionEntry();
    entry.mbrStatus = MBR_UNREADABLE;

#ifndef _WIN32
    kernel::storage::PartitionTableModel table;
    if (!kernel::storage::parse_partition_table(entry.devIndex, table)) {
        entry.mbrStatus = MBR_UNREADABLE;
        return;
    }
    switch (table.state) {
        case kernel::storage::DISK_STATE_NOT_INITIALIZED:
            entry.mbrStatus = DISK_NOT_INITIALIZED;
            break;
        case kernel::storage::DISK_STATE_VALID_MBR:
            entry.mbrStatus = MBR_VALID;
            break;
        case kernel::storage::DISK_STATE_VALID_GPT:
            entry.mbrStatus = DISK_GPT_VALID;
            break;
        case kernel::storage::DISK_STATE_GPT_DEGRADED:
            entry.mbrStatus = DISK_GPT_DEGRADED;
            break;
        case kernel::storage::DISK_STATE_UNSUPPORTED_PARTITION_SCHEME:
            entry.mbrStatus = DISK_UNSUPPORTED;
            break;
        case kernel::storage::DISK_STATE_INVALID_PARTITION_TABLE:
            entry.mbrStatus = MBR_INVALID;
            break;
        default:
            entry.mbrStatus = MBR_UNREADABLE;
            return;
    }

    const bool parsedEntriesAvailable =
        table.state == kernel::storage::DISK_STATE_VALID_MBR ||
        table.state == kernel::storage::DISK_STATE_VALID_GPT ||
        table.state == kernel::storage::DISK_STATE_GPT_DEGRADED ||
        table.extendedPartitionsPresent;
    if (!parsedEntriesAvailable) return;

    for (uint16_t i = 0; i < table.partitionCount && i < 4; ++i) {
        const kernel::storage::PartitionEntry& source = table.partitions[i];
        if (source.startLba > UINT32_MAX || source.sectorCount > UINT32_MAX) continue;
        PartitionEntry& part = entry.parts[i];
        part.status = source.bootable ? 0x80 : 0;
        part.type = source.isGpt ? 0xEE : source.mbrType;
        part.lbaStart = static_cast<uint32_t>(source.startLba);
        part.lbaCount = static_cast<uint32_t>(source.sectorCount);
        if (part.lbaCount != 0) {
            if (!source.isGpt &&
                (source.mbrType == 0x05 || source.mbrType == 0x0F ||
                 source.mbrType == 0x85)) {
                part.fs = "Extended (unsupported)";
            } else {
                part.fs = "Unknown";
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

std::string DiskManager::fmtSize(uint64_t bytes) {
    const uint64_t KB = 1024;
    const uint64_t MB = 1024 * 1024;
    const uint64_t GB = 1024 * 1024 * 1024;
    if (bytes >= GB) return std::to_string(bytes / GB) + " GB";
    if (bytes >= MB) return std::to_string(bytes / MB) + " MB";
    if (bytes >= KB) return std::to_string(bytes / KB) + " KB";
    return std::to_string(bytes) + " B";
}

std::string DiskManager::fmtHexByte(uint8_t value) {
    char buf[5];
    std::snprintf(buf, sizeof(buf), "0x%02X", value);
    return std::string(buf);
}

std::string DiskManager::mbrStatusText(MbrStatus status) {
    switch (status) {
        case MBR_PREVIEW_VALID: return "MBR preview (validated)";
        case MBR_VALID: return "Valid MBR";
        case DISK_NOT_INITIALIZED: return "Not Initialized";
        case DISK_GPT_UNSUPPORTED: return "GPT image preview unsupported";
        case DISK_GPT_VALID: return "Valid GPT";
        case DISK_GPT_DEGRADED: return "GPT Degraded";
        case DISK_UNSUPPORTED: return "Unsupported Partition Scheme";
        case MBR_INVALID: return "Invalid Partition Table";
        case MBR_PREVIEW_NO_SIGNATURE: return "No MBR signature (raw state unknown)";
        default: return "Unreadable";
    }
}

bool DiskManager::hit(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
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
            
            render();
            return;
        }
    }
    
    // Hosted mode only attaches read-only images and refreshes its preview.
    if (hit(mx, my, s_bxRefreshX, s_bxRefreshY, 108, BTN_H)) {
        refreshDisks();
        s_status = "Disk list refreshed.";
        s_clickLock = true;
        render();
        return;
    }
    if (hit(mx, my, s_bxAttachImageX, s_bxAttachImageY, 112, BTN_H)) {
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
    if (hit(mx, my, s_bxRescanImagesX, s_bxRescanImagesY, 112, BTN_H)) {
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
            render();
        }
    } else if (keyCode == 40) {  // VK_DOWN
        if (s_selectedDiskIndex < static_cast<int>(s_disks.size()) - 1) {
            s_selectedDiskIndex++;
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
    
    drawVolumesGrid(rightX, PAD, rightW, 252);
    drawPartitionMap(rightX, 276, rightW, 112);
    drawActions(rightX, 408, rightW, 132);
    
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
            std::string detail = "#" + std::to_string(s_disks[i].devIndex) + "  " + fmtSize(totalBytes) + "  " + mbrStatusText(s_disks[i].mbrStatus);
            if (s_disks[i].isHostImage) {
                detail += "  attached .img";
            }
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
    
    if (s_disks.empty()) {
        ipc::Message emptyMsg;
        emptyMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream emptyOss;
#ifdef _WIN32
        emptyOss << s_windowId << "|" << rowX << "|" << listY
                 << "|No attached images. Host disks are not enumerated.|190|190|190";
#else
        emptyOss << s_windowId << "|" << rowX << "|" << listY
                 << "|No block devices detected.|190|190|190";
#endif
        std::string emptyPayload = emptyOss.str();
        emptyMsg.data.assign(emptyPayload.begin(), emptyPayload.end());
        ipc::Bus::publish("gui.input", std::move(emptyMsg), false);
    }
    (void)winH;
}

void DiskManager::drawVolumesGrid(int x, int y, int w, int h) {
    ipc::Message title;
    title.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream titleText;
    titleText << s_windowId << "|" << x << "|" << y << "|Partitions|255|255|255";
    std::string titlePayload = titleText.str();
    title.data.assign(titlePayload.begin(), titlePayload.end());
    ipc::Bus::publish("gui.input", std::move(title), false);

    DiskEntry* sel = getSelected();
    int infoY = y + HEADER_H;
    if (!sel || !sel->haveInfo) {
        ipc::Message empty;
        empty.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream emptyText;
        emptyText << s_windowId << "|" << x << "|" << infoY
                  << "|No attached disk image. Select an image below and attach it read-only.|190|190|190";
        std::string emptyPayload = emptyText.str();
        empty.data.assign(emptyPayload.begin(), emptyPayload.end());
        ipc::Bus::publish("gui.input", std::move(empty), false);
        return;
    }

    const uint64_t capacity = sel->totalSectors * sel->bytesPerSector;
    std::string info = sel->name + " | " + sel->transportLabel + " | " +
        fmtSize(capacity) + " | " + std::to_string(sel->bytesPerSector) + " B sectors | " +
        mbrStatusText(sel->mbrStatus);
    if (sel->isHostImage) info += " | read-only image preview";
    ipc::Message infoMsg;
    infoMsg.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream infoText;
    infoText << s_windowId << "|" << x << "|" << infoY << "|" << info << "|210|210|210";
    std::string infoPayload = infoText.str();
    infoMsg.data.assign(infoPayload.begin(), infoPayload.end());
    ipc::Bus::publish("gui.input", std::move(infoMsg), false);

    if (sel->isHostImage && sel->mbrStatus != MBR_PREVIEW_VALID) {
        const char* notes[] = {
            "No validated MBR partition list is available.",
            "GPT image parsing is unsupported in hosted mode.",
            "Unallocated ranges are not inferred."
        };
        for (int i = 0; i < 3; ++i) {
            ipc::Message note;
            note.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream noteText;
            noteText << s_windowId << "|" << x << "|" << (infoY + 24 + i * 18)
                     << "|" << notes[i] << "|190|190|190";
            std::string notePayload = noteText.str();
            note.data.assign(notePayload.begin(), notePayload.end());
            ipc::Bus::publish("gui.input", std::move(note), false);
        }
        return;
    }

    int widths[7] = { 64, 70, 102, 102, 108, 100, 0 };
    int used = 0;
    for (int i = 0; i < 6; ++i) used += widths[i];
    widths[6] = w - used;
    if (widths[6] < 80) widths[6] = 80;
    int headerY = infoY + 20;
    const char* headers[7] = { "Part", "Type", "Start LBA", "Sectors", "Capacity", "FS", "Status" };
    int cx = x;
    for (int i = 0; i < 7; ++i) {
        drawHeaderCell(cx, headerY, widths[i], ROW_H, headers[i]);
        cx += widths[i];
    }

    int rowY = headerY + ROW_H;
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        const PartitionEntry& part = sel->parts[i];
        if (part.lbaCount == 0) continue;
        any = true;
        cx = x;
        const std::string values[7] = {
            std::to_string(i + 1), fmtHexByte(part.type),
            std::to_string(part.lbaStart), std::to_string(part.lbaCount),
            fmtSize(static_cast<uint64_t>(part.lbaCount) * sel->bytesPerSector),
            part.fs.empty() ? "Unknown" : part.fs,
            (part.type == 0x05 || part.type == 0x0F || part.type == 0x85)
                ? "Extended; logical omitted"
                : (part.status == 0x80 ? "Active MBR flag" : "Primary entry")
        };
        for (int col = 0; col < 7; ++col) {
            drawCell(cx, rowY, widths[col], ROW_H, values[col].c_str());
            cx += widths[col];
        }
        rowY += ROW_H;
        if (rowY + ROW_H > y + h) break;
    }
    if (!any) {
        ipc::Message none;
        none.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream noneText;
        noneText << s_windowId << "|" << x << "|" << rowY
                 << "|No primary MBR partitions were previewed.|190|190|190";
        std::string nonePayload = noneText.str();
        none.data.assign(nonePayload.begin(), nonePayload.end());
        ipc::Bus::publish("gui.input", std::move(none), false);
    }
}
void DiskManager::drawPartitionMap(int x, int y, int w, int h) {
    DiskEntry* sel = getSelected();
    if (!sel) return;

    ipc::Message title;
    title.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream titleText;
    titleText << s_windowId << "|" << x << "|" << y << "|Disk map — validated MBR ranges only|255|255|255";
    std::string titlePayload = titleText.str();
    title.data.assign(titlePayload.begin(), titlePayload.end());
    ipc::Bus::publish("gui.input", std::move(title), false);

    const int barY = y + HEADER_H;
    const int barH = 30;
    ipc::Message bar;
    bar.type = (uint32_t)MsgType::MT_DrawRect;
    std::ostringstream barText;
    barText << s_windowId << "|" << x << "|" << barY << "|" << w << "|" << barH << "|30|30|30";
    std::string barPayload = barText.str();
    bar.data.assign(barPayload.begin(), barPayload.end());
    ipc::Bus::publish("gui.input", std::move(bar), false);

    const bool hasValidatedMbr = sel->mbrStatus == MBR_PREVIEW_VALID || sel->mbrStatus == MBR_VALID;
    if (!sel->haveInfo || sel->totalSectors == 0 || !hasValidatedMbr) {
        ipc::Message note;
        note.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream noteText;
        noteText << s_windowId << "|" << x << "|" << (barY + barH + 6)
                 << "|No validated MBR layout is available; unallocated space is not inferred.|190|190|190";
        std::string notePayload = noteText.str();
        note.data.assign(notePayload.begin(), notePayload.end());
        ipc::Bus::publish("gui.input", std::move(note), false);
        return;
    }

    bool drawn[4] = { false, false, false, false };
    for (int pass = 0; pass < 4; ++pass) {
        int next = -1;
        uint64_t nextStart = UINT64_MAX;
        for (int i = 0; i < 4; ++i) {
            const PartitionEntry& part = sel->parts[i];
            if (drawn[i] || part.lbaCount == 0 || part.lbaStart >= sel->totalSectors ||
                static_cast<uint64_t>(part.lbaCount) > sel->totalSectors - part.lbaStart)
                continue;
            if (part.lbaStart < nextStart) { nextStart = part.lbaStart; next = i; }
        }
        if (next < 0) break;

        const PartitionEntry& part = sel->parts[next];
        const uint64_t start = part.lbaStart;
        const uint64_t count = part.lbaCount;
        const int left = x + static_cast<int>((start * static_cast<uint64_t>(w)) / sel->totalSectors);
        const int right = x + static_cast<int>(((start + count) * static_cast<uint64_t>(w)) / sel->totalSectors);
        const int segmentW = right > left ? right - left : 1;

        ipc::Message segment;
        segment.type = (uint32_t)MsgType::MT_DrawRect;
        std::ostringstream segmentText;
        segmentText << s_windowId << "|" << left << "|" << barY << "|" << segmentW
                    << "|" << barH << "|76|139|245";
        std::string segmentPayload = segmentText.str();
        segment.data.assign(segmentPayload.begin(), segmentPayload.end());
        ipc::Bus::publish("gui.input", std::move(segment), false);

        if (segmentW > 64) {
            std::string label = "P" + std::to_string(next + 1) + " " + fmtSize(count * sel->bytesPerSector);
            ipc::Message labelMsg;
            labelMsg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream labelText;
            labelText << s_windowId << "|" << (left + 4) << "|" << (barY + 8) << "|" << label << "|255|255|255";
            std::string labelPayload = labelText.str();
            labelMsg.data.assign(labelPayload.begin(), labelPayload.end());
            ipc::Bus::publish("gui.input", std::move(labelMsg), false);
        }
        drawn[next] = true;
    }

    ipc::Message note;
    note.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream noteText;
    noteText << s_windowId << "|" << x << "|" << (barY + barH + 6)
             << "|Blue ranges are MBR partitions. Remaining disk area is not classified as free.|190|190|190";
    std::string notePayload = noteText.str();
    note.data.assign(notePayload.begin(), notePayload.end());
    ipc::Bus::publish("gui.input", std::move(note), false);
}
void DiskManager::drawActions(int x, int y, int w, int h) {
    ipc::Message title;
    title.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream titleText;
    titleText << s_windowId << "|" << x << "|" << y << "|Image and refresh controls|255|255|255";
    std::string titlePayload = titleText.str();
    title.data.assign(titlePayload.begin(), titlePayload.end());
    ipc::Bus::publish("gui.input", std::move(title), false);

    const int controlsY = y + HEADER_H;
    s_bxRefreshX = x;
    s_bxRefreshY = controlsY;
    drawButton(s_bxRefreshX, s_bxRefreshY, 108, BTN_H, "Refresh", hit(s_mouseX, s_mouseY, s_bxRefreshX, s_bxRefreshY, 108, BTN_H));

    s_bxPrevImageX = x + 120;
    s_bxPrevImageY = controlsY;
    drawButton(s_bxPrevImageX, s_bxPrevImageY, 32, BTN_H, "<", hit(s_mouseX, s_mouseY, s_bxPrevImageX, s_bxPrevImageY, 32, BTN_H));
    s_bxNextImageX = x + 160;
    s_bxNextImageY = controlsY;
    drawButton(s_bxNextImageX, s_bxNextImageY, 32, BTN_H, ">", hit(s_mouseX, s_mouseY, s_bxNextImageX, s_bxNextImageY, 32, BTN_H));

    std::string currentImage = "No .img files found";
    if (!s_hostImages.empty() && s_selectedHostImageIndex >= 0 &&
        s_selectedHostImageIndex < static_cast<int>(s_hostImages.size())) {
        currentImage = s_hostImages[s_selectedHostImageIndex].displayName;
        if (s_hostImages[s_selectedHostImageIndex].attached) currentImage += " [attached]";
    }
    if (currentImage.size() > 25) currentImage = currentImage.substr(0, 22) + "...";
    ipc::Message image;
    image.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream imageText;
    imageText << s_windowId << "|" << (x + 202) << "|" << (controlsY + 8) << "|" << currentImage << "|220|220|220";
    std::string imagePayload = imageText.str();
    image.data.assign(imagePayload.begin(), imagePayload.end());
    ipc::Bus::publish("gui.input", std::move(image), false);

    s_bxAttachImageX = x + 364;
    s_bxAttachImageY = controlsY;
    drawButton(s_bxAttachImageX, s_bxAttachImageY, 112, BTN_H, "Attach image", hit(s_mouseX, s_mouseY, s_bxAttachImageX, s_bxAttachImageY, 112, BTN_H));
    s_bxRescanImagesX = x + 488;
    s_bxRescanImagesY = controlsY;
    drawButton(s_bxRescanImagesX, s_bxRescanImagesY, 112, BTN_H, "Rescan images", hit(s_mouseX, s_mouseY, s_bxRescanImagesX, s_bxRescanImagesY, 112, BTN_H));

    ipc::Message note;
    note.type = (uint32_t)MsgType::MT_DrawText;
    std::ostringstream noteText;
#ifdef _WIN32
    noteText << s_windowId << "|" << x << "|" << (controlsY + BTN_H + 8)
             << "|Hosted mode previews attached .img files only; no host disks are enumerated.|190|190|190";
#else
    noteText << s_windowId << "|" << x << "|" << (controlsY + BTN_H + 8)
             << "|Images attach as read-only RAM disks; only supported image data is shown.|190|190|190";
#endif
    std::string notePayload = noteText.str();
    note.data.assign(notePayload.begin(), notePayload.end());
    ipc::Bus::publish("gui.input", std::move(note), false);

    if (!s_status.empty()) {
        ipc::Message status;
        status.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream statusText;
        statusText << s_windowId << "|" << x << "|" << (controlsY + BTN_H + 27)
                   << "|" << s_status << "|220|220|220";
        std::string statusPayload = statusText.str();
        status.data.assign(statusPayload.begin(), statusPayload.end());
        ipc::Bus::publish("gui.input", std::move(status), false);
    }
    (void)w;
    (void)h;
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

} // namespace apps
} // namespace gxos
