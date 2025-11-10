#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace gxos {
    struct FileInfo { std::string name; uint64_t size; bool isDir; };
    class FS {
    public:
        static std::vector<FileInfo> list(const std::string& path);
        static bool readAll(const std::string& path, std::vector<uint8_t>& out);
        static bool writeAll(const std::string& path, const std::vector<uint8_t>& data);
    };
}
