#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace gxos {
    struct FileInfo { std::string name; uint64_t size; bool isDir; };
    struct FSResult { bool success; std::string message; };
    class FS {
    public:
        static std::vector<FileInfo> list(const std::string& path);
        static bool readAll(const std::string& path, std::vector<uint8_t>& out);
        static FSResult readAll(const std::string& path, std::vector<uint8_t>& out, uint64_t maxBytes);
        static bool writeAll(const std::string& path, const std::vector<uint8_t>& data);
        static bool exists(const std::string& path);
        static bool createDirectories(const std::string& path);
        static bool renameFile(const std::string& from, const std::string& to, bool replaceExisting);
        static bool removeFile(const std::string& path);
    };
}
