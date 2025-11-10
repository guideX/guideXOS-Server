#include "fs.h"
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
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
    bool FS::readAll(const std::string& path, std::vector<uint8_t>& out){ std::ifstream f(path.c_str(), std::ios::binary); if(!f) return false; f.seekg(0, std::ios::end); std::streamoff n = f.tellg(); if(n<0) return false; f.seekg(0); out.resize((size_t)n); f.read((char*)out.data(), n); return true; }
    bool FS::writeAll(const std::string& path, const std::vector<uint8_t>& data){ std::ofstream f(path.c_str(), std::ios::binary); if(!f) return false; f.write((const char*)data.data(), data.size()); return true; }
}
