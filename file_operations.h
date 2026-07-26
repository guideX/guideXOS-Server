#pragma once

#include <cstdint>
#include <string>

namespace gxos { namespace files {

    enum class FileClipboardOperation {
        Copy,
        Move
    };

    struct FileClipboardEntry {
        std::string sourcePath;
        FileClipboardOperation operation{FileClipboardOperation::Copy};
    };

    struct FilePasteResult {
        bool success{false};
        std::string destinationPath;
        std::string error;
    };

    struct FileCreateResult {
        bool success{false};
        std::string path;
        std::string error;
    };

    /// Process-local guideXOS file clipboard. It intentionally contains only
    /// a canonical virtual path and operation metadata; file bytes stay in the
    /// guideXOS VFS and never enter the host clipboard.
    class FileClipboard {
    public:
        static bool Set(const std::string& sourcePath, FileClipboardOperation operation, std::string& error);
        static bool Get(FileClipboardEntry& out);
        static void Clear();
        static bool IsCutSource(const std::string& sourcePath);
    };

    /// Filesystem operations shared by File Explorer and desktop shell menus.
    /// Paths are guideXOS virtual paths (for example /Desktop/photo.png).
    class FileOperations {
    public:
        static std::string NormalizePath(const std::string& path);
        static std::string ParentPath(const std::string& path);
        static std::string BaseName(const std::string& path);
        static std::string CombinePath(const std::string& directory, const std::string& name);
        static bool Exists(const std::string& path);
        static bool IsDirectory(const std::string& path);
        static bool IsRegularFile(const std::string& path);
        static bool CanPasteFile(const std::string& destinationDirectory, std::string& error);
        static FilePasteResult PasteFile(const std::string& destinationDirectory);
        static FileCreateResult CreateUniqueFolder(const std::string& destinationDirectory);
        static uint64_t OperationGeneration();
    };

}} // namespace gxos::files
