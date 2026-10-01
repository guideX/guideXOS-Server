#include "vfs.h"
#include <algorithm>

#ifdef _WIN32
#include <filesystem>
#include <fstream>
#include <iterator>
#endif

namespace gxos {
    Vfs::Vfs(){ _root = std::make_unique<Node>(); _root->isDir = true; }
    Vfs& Vfs::instance(){ static Vfs inst; return inst; }

    void Vfs::splitPath(const std::string& path, std::vector<std::string>& out){ out.clear(); std::string cur; for(char c: path){ if(c=='/'||c=='\\'){ if(!cur.empty()){ out.push_back(cur); cur.clear(); } } else cur.push_back(c); } if(!cur.empty()) out.push_back(cur); }

    Vfs::Node* Vfs::getOrCreateDir(const std::vector<std::string>& parts, size_t upto){ Node* n = _root.get(); for(size_t i=0;i<upto;i++){ const std::string& seg = parts[i]; auto it = n->children.find(seg); if(it==n->children.end()){ auto nn = std::make_unique<Node>(); nn->isDir=true; n->children[seg] = std::move(nn); it = n->children.find(seg); } n = it->second.get(); if(!n->isDir) return nullptr; } return n; }

    Vfs::Node* Vfs::getNode(const std::vector<std::string>& parts){ Node* n = _root.get(); for(size_t i=0;i<parts.size();i++){ auto it = n->children.find(parts[i]); if(it==n->children.end()) return nullptr; n = it->second.get(); } return n; }

    bool Vfs::mkdirs(const std::string& path){ std::lock_guard<std::mutex> lk(_mu); std::vector<std::string> parts; splitPath(path, parts); if(parts.empty()) return true; return getOrCreateDir(parts, parts.size())!=nullptr; }

    bool Vfs::writeFile(const std::string& path, const std::vector<uint8_t>& data){ std::lock_guard<std::mutex> lk(_mu); std::vector<std::string> parts; splitPath(path, parts); if(parts.empty()) return false; Node* dir = getOrCreateDir(parts, parts.size()-1); if(!dir) return false; const std::string& fname = parts.back(); auto it = dir->children.find(fname); if(it==dir->children.end()){ auto f = std::make_unique<Node>(); f->isDir=false; f->content = data; dir->children[fname] = std::move(f); } else { if(it->second->isDir) return false; it->second->content = data; } return true; }

    bool Vfs::readFile(const std::string& path, std::vector<uint8_t>& out){
        out.clear();
        {
            std::lock_guard<std::mutex> lk(_mu);
            std::vector<std::string> parts;
            splitPath(path, parts);
            if (parts.empty()) return false;
            Node* n = getNode(parts);
            if (n && !n->isDir) {
                out = n->content;
                return true;
            }
            if (n && n->isDir) return false;
        }

#ifdef _WIN32
        // Hosted File Explorer exposes the checkout through virtual paths
        // rooted at the process working directory. Let existing VFS readers
        // consume those same files after checking the in-memory VFS first.
        std::string normalized = path.empty() ? "/" : path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::filesystem::path virtualPath(normalized);
        if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
        std::string generic = virtualPath.lexically_normal().generic_string();
        std::filesystem::path hostPath = std::filesystem::current_path();
        if (!generic.empty() && generic != "/") {
            if (generic.front() == '/') generic.erase(generic.begin());
            hostPath /= std::filesystem::path(generic);
        }

        std::ifstream file(hostPath, std::ios::binary);
        if (!file) return false;
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) return false;
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> hostedBytes((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        if (!file.good() && !file.eof()) return false;
        out.swap(hostedBytes);
        return true;
#else
        return false;
#endif
    }

    std::vector<VfsEntryInfo> Vfs::list(const std::string& path){ std::lock_guard<std::mutex> lk(_mu); std::vector<std::string> parts; splitPath(path, parts); Node* n = getNode(parts); std::vector<VfsEntryInfo> v; if(!n || !n->isDir) return v; for(auto& kv: n->children){ VfsEntryInfo ei; ei.name = kv.first; ei.isDir = kv.second->isDir; ei.size = kv.second->isDir?0:(uint64_t)kv.second->content.size(); v.push_back(ei); } std::sort(v.begin(), v.end(), [](const VfsEntryInfo& a, const VfsEntryInfo& b){ return a.name < b.name; }); return v; }

    bool Vfs::exists(const std::string& path){ std::lock_guard<std::mutex> lk(_mu); std::vector<std::string> parts; splitPath(path, parts); if(parts.empty()) return true; return getNode(parts)!=nullptr; }

    void Vfs::clear(){ std::lock_guard<std::mutex> lk(_mu); _root = std::make_unique<Node>(); _root->isDir=true; }
}
