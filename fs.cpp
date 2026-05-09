#include "fs.h"
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <limits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <cstdio>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gxos {
    std::vector<FileInfo> FS::list(const std::string& path){
        std::vector<FileInfo> v;
#ifdef _WIN32
        std::string pattern = path;
        if (!pattern.empty() && pattern.back()!='\\' && pattern.back()!='/') pattern += "\\";
        pattern += "*";
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE){
            do {
                if (std::strcmp(fd.cFileName, ".") == 0 || std::strcmp(fd.cFileName, "..") == 0) continue;
                FileInfo fi; fi.name = fd.cFileName; bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)!=0; fi.isDir = isDir;
                if (!isDir){ ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh; fi.size = (uint64_t)sz.QuadPart; } else fi.size = 0; v.push_back(fi);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#else
        DIR* dir = opendir(path.c_str()); if (!dir) return v; struct dirent* ent;
        while ((ent = readdir(dir))){ if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) continue; FileInfo fi; fi.name = ent->d_name; std::string full = path; if(!full.empty() && full.back()!='/') full += "/"; full += fi.name; struct stat st; if (stat(full.c_str(), &st)==0){ fi.isDir = S_ISDIR(st.st_mode); fi.size = fi.isDir?0:(uint64_t)st.st_size; } else { fi.isDir=false; fi.size=0; } v.push_back(fi); }
        closedir(dir);
#endif
        return v;
    }
    bool FS::readAll(const std::string& path, std::vector<uint8_t>& out){ return readAll(path, out, static_cast<uint64_t>(std::numeric_limits<size_t>::max())).success; }
    FSResult FS::readAll(const std::string& path, std::vector<uint8_t>& out, uint64_t maxBytes){ out.clear(); std::ifstream f(path.c_str(), std::ios::binary); if(!f) return {false, "open failed"}; f.seekg(0, std::ios::end); std::streamoff n = f.tellg(); if(n<0) return {false, "size query failed"}; uint64_t size = static_cast<uint64_t>(n); if(size > maxBytes) return {false, "file exceeds maximum size"}; if(size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) return {false, "file too large for address space"}; f.seekg(0); if(!f) return {false, "seek failed"}; out.resize(static_cast<size_t>(size)); if(size != 0){ f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size)); if(!f || static_cast<uint64_t>(f.gcount()) != size){ out.clear(); return {false, "short read"}; } } return {true, std::string()}; }
    bool FS::writeAll(const std::string& path, const std::vector<uint8_t>& data){ std::ofstream f(path.c_str(), std::ios::binary); if(!f) return false; if(!data.empty()) f.write((const char*)data.data(), data.size()); return f.good(); }
    bool FS::exists(const std::string& path){
#ifdef _WIN32
        return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
        struct stat st; return stat(path.c_str(), &st) == 0;
#endif
    }
    bool FS::createDirectories(const std::string& path){
        if(path.empty()) return false;
        std::string current;
        for(size_t i=0; i<path.size(); ++i){
            char c = path[i];
            current.push_back(c);
            if(c != '/' && c != '\' && i + 1 != path.size()) continue;
            while(!current.empty() && (current.back() == '/' || current.back() == '\')) current.pop_back();
            if(current.empty()) continue;
#ifdef _WIN32
            DWORD attrs = GetFileAttributesA(current.c_str());
            if(attrs == INVALID_FILE_ATTRIBUTES){ if(!CreateDirectoryA(current.c_str(), nullptr)) return false; }
            else if((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
#else
            struct stat st;
            if(stat(current.c_str(), &st) != 0){ if(mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) return false; }
            else if(!S_ISDIR(st.st_mode)) return false;
#endif
            current.push_back(c);
        }
        return true;
    }
    bool FS::renameFile(const std::string& from, const std::string& to, bool replaceExisting){
#ifdef _WIN32
        DWORD flags = replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0;
        return MoveFileExA(from.c_str(), to.c_str(), flags) != 0;
#else
        if(!replaceExisting && exists(to)) return false;
        return std::rename(from.c_str(), to.c_str()) == 0;
#endif
    }
    bool FS::removeFile(const std::string& path){
#ifdef _WIN32
        return DeleteFileA(path.c_str()) != 0 || !exists(path);
#else
        return std::remove(path.c_str()) == 0 || !exists(path);
#endif
    }
}
