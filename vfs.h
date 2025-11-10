#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace gxos {
    struct VfsEntryInfo { std::string name; uint64_t size; bool isDir; };

    class Vfs {
    public:
        // Singleton-style access
        static Vfs& instance();

        // Ensure directory exists (create intermediate directories)
        bool mkdirs(const std::string& path);
        // Add or overwrite a file with given bytes
        bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
        // Read file; returns false if not found or is directory
        bool readFile(const std::string& path, std::vector<uint8_t>& out);
        // List directory entries; empty vector if no such dir
        std::vector<VfsEntryInfo> list(const std::string& path);
        // Test existence
        bool exists(const std::string& path);
        // Remove all contents
        void clear();
    private:
        struct Node {
            bool isDir{true};
            std::unordered_map<std::string, std::unique_ptr<Node>> children; // for dir
            std::vector<uint8_t> content; // for file
        };
        std::unique_ptr<Node> _root;
        std::mutex _mu;
        Vfs();
        static void splitPath(const std::string& path, std::vector<std::string>& out);
        Node* getOrCreateDir(const std::vector<std::string>& parts, size_t upto);
        Node* getNode(const std::vector<std::string>& parts);
    };
}
