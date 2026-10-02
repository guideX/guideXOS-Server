#include "vfs.h"
#include <algorithm>
#include <limits>

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
        return readFileBounded(path, out, std::numeric_limits<uint64_t>::max()) == VfsReadFileStatus::Success;
    }

    VfsReadFileStatus Vfs::readFileBounded(const std::string& path, std::vector<uint8_t>& out, uint64_t maxBytes){
        out.clear();
        {
            std::lock_guard<std::mutex> lk(_mu);
            std::vector<std::string> parts;
            splitPath(path, parts);
            if (parts.empty()) return VfsReadFileStatus::NotFound;
            Node* n = getNode(parts);
            if (n && !n->isDir) {
                if (n->content.size() > maxBytes) return VfsReadFileStatus::TooLarge;
                out = n->content;
                return VfsReadFileStatus::Success;
            }
            if (n && n->isDir) return VfsReadFileStatus::NotFound;
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
        if (!file) return VfsReadFileStatus::NotFound;
        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0) return VfsReadFileStatus::ReadFailed;
        if (static_cast<uint64_t>(size) > maxBytes) return VfsReadFileStatus::TooLarge;
        if (static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max() ||
            static_cast<uint64_t>(size) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            return VfsReadFileStatus::TooLarge;
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> hostedBytes(static_cast<size_t>(size));
        const std::streamsize expectedSize = static_cast<std::streamsize>(hostedBytes.size());
        if (expectedSize > 0) {
            file.read(reinterpret_cast<char*>(hostedBytes.data()), expectedSize);
            if (file.gcount() != expectedSize) return VfsReadFileStatus::ReadFailed;
        }
        if (file.peek() != std::char_traits<char>::eof()) return VfsReadFileStatus::TooLarge;
        if (file.bad()) return VfsReadFileStatus::ReadFailed;
        out.swap(hostedBytes);
        return VfsReadFileStatus::Success;
#else
        return VfsReadFileStatus::NotFound;
#endif
    }

    std::vector<VfsEntryInfo> Vfs::list(const std::string& path){ std::lock_guard<std::mutex> lk(_mu); std::vector<std::string> parts; splitPath(path, parts); Node* n = getNode(parts); std::vector<VfsEntryInfo> v; if(!n || !n->isDir) return v; for(auto& kv: n->children){ VfsEntryInfo ei; ei.name = kv.first; ei.isDir = kv.second->isDir; ei.size = kv.second->isDir?0:(uint64_t)kv.second->content.size(); v.push_back(ei); } std::sort(v.begin(), v.end(), [](const VfsEntryInfo& a, const VfsEntryInfo& b){ return a.name < b.name; }); return v; }

    bool Vfs::exists(const std::string& path){ std::lock_guard<std::mutex> lk(_mu); std::vector<std::string> parts; splitPath(path, parts); if(parts.empty()) return true; return getNode(parts)!=nullptr; }

    void Vfs::clear(){ std::lock_guard<std::mutex> lk(_mu); _root = std::make_unique<Node>(); _root->isDir=true; }
}
