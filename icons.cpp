#include "icons.h"
#include <windows.h>
#include <shlwapi.h>

namespace gxos { namespace gui {
    std::unordered_map<std::string,HBITMAP> Icons::s_cache;

    HBITMAP Icons::loadBmp(const std::string& path){
        return (HBITMAP)LoadImageA(nullptr, path.c_str(), IMAGE_BITMAP, 0,0, LR_LOADFROMFILE|LR_CREATEDIBSECTION);
    }
    HBITMAP Icons::getCached(const std::string& key, const std::string& rel){
        auto it=s_cache.find(key); if(it!=s_cache.end()) return it->second;
        std::string full = std::string("assets/") + rel; HBITMAP hb = loadBmp(full); s_cache[key]=hb; return hb;
    }
    HBITMAP Icons::StartIcon(int size){ return getCached("start"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/start.bmp"); }
    HBITMAP Icons::TaskbarIcon(int size){ return getCached("taskbar"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/taskbar.bmp"); }
    HBITMAP Icons::CloseIcon(int size){ return getCached("close"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/close.bmp"); }
    HBITMAP Icons::MinimizeIcon(int size){ return getCached("min"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/down.bmp"); }
    HBITMAP Icons::MaximizeIcon(int size){ return getCached("max"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/image.bmp"); }
    HBITMAP Icons::RestoreIcon(int size){ return getCached("restore"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/restore.bmp"); }
    HBITMAP Icons::TombstoneIcon(int size){ return getCached("tomb"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/tombstone.bmp"); }
    HBITMAP Icons::DocumentIcon(int size){ return getCached("doc"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/documents.bmp"); }
    HBITMAP Icons::FolderIcon(int size){ return getCached("folder"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/folder.bmp"); }
    HBITMAP Icons::ImageIcon(int size){ return getCached("img"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/imagefile.bmp"); }
    HBITMAP Icons::AudioIcon(int size){ return getCached("audio"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/music.bmp"); }
    HBITMAP Icons::HardDiskIcon(int size){ return getCached("harddisk"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/harddisk.bmp"); }
    HBITMAP Icons::SettingsIcon(int size){ return getCached("settings"+std::to_string(size), "BlueVelvet/"+std::to_string(size)+"/settings.bmp"); }
} }
