#include "file_operations.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
#include <filesystem>
#endif
#include <mutex>

#if !defined(_WIN32) || defined(GXOS_BARE_METAL)
#if defined(SEEK_SET)
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
#endif
#include "kernel/core/include/kernel/vfs.h"
#endif

namespace gxos { namespace files {

    namespace {
        constexpr int kMaxUniqueNameAttempts = 1000;
        constexpr uint32_t kCopyBufferBytes = 64u * 1024u;

        enum class EntryKind {
            Missing,
            RegularFile,
            Directory,
            Unsupported
        };

        std::mutex s_clipboardMutex;
        FileClipboardEntry s_clipboard;
        bool s_clipboardValid = false;
        std::atomic<uint64_t> s_operationGeneration{0};

        std::string normalizeVirtualPath(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::string normalized = path.empty() ? "/" : path;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            std::filesystem::path virtualPath(normalized);
            if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
            std::string out = virtualPath.lexically_normal().generic_string();
            if (out.empty()) out = "/";
            if (out.front() != '/') out.insert(out.begin(), '/');
            return out;
#else
            char normalized[kernel::vfs::VFS_MAX_PATH]{};
            kernel::vfs::normalize_path(path.empty() ? "/" : path.c_str(), normalized, sizeof(normalized));
            return normalized[0] ? normalized : "/";
#endif
        }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        std::filesystem::path hostedRootPath() {
            return std::filesystem::current_path();
        }

        std::filesystem::path hostedPathForVirtual(const std::string& path) {
            const std::string normalized = normalizeVirtualPath(path);
            if (normalized == "/") return hostedRootPath();
            return hostedRootPath() / std::filesystem::path(normalized.substr(1));
        }
#endif

        std::string pathKey(const std::string& path) {
            std::string key = normalizeVirtualPath(path);
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
#endif
            return key;
        }

        bool samePath(const std::string& left, const std::string& right) {
            return pathKey(left) == pathKey(right);
        }

        std::string parentPath(const std::string& path) {
            const std::string normalized = normalizeVirtualPath(path);
            if (normalized == "/") return "/";
            const size_t slash = normalized.find_last_of('/');
            return slash == 0 ? "/" : normalized.substr(0, slash);
        }

        std::string baseName(const std::string& path) {
            const std::string normalized = normalizeVirtualPath(path);
            if (normalized == "/") return std::string();
            const size_t slash = normalized.find_last_of('/');
            return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
        }

        std::string combinePath(const std::string& directory, const std::string& name) {
            const std::string base = normalizeVirtualPath(directory);
            if (base == "/") return normalizeVirtualPath("/" + name);
            return normalizeVirtualPath(base + "/" + name);
        }

        bool exists(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            return std::filesystem::exists(hostedPathForVirtual(path), ec) && !ec;
#else
            return kernel::vfs::exists(normalizeVirtualPath(path).c_str());
#endif
        }

        bool isDirectory(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            return std::filesystem::is_directory(hostedPathForVirtual(path), ec) && !ec;
#else
            kernel::vfs::FileInfo info{};
            return kernel::vfs::stat(normalizeVirtualPath(path).c_str(), &info) == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_DIRECTORY;
#endif
        }

        bool isRegularFile(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            return std::filesystem::is_regular_file(hostedPathForVirtual(path), ec) && !ec;
#else
            kernel::vfs::FileInfo info{};
            return kernel::vfs::stat(normalizeVirtualPath(path).c_str(), &info) == kernel::vfs::VFS_OK &&
                info.type == kernel::vfs::FILE_TYPE_REGULAR;
#endif
        }

        EntryKind entryKind(const std::string& path) {
            if (isDirectory(path)) return EntryKind::Directory;
            if (isRegularFile(path)) return EntryKind::RegularFile;
            return exists(path) ? EntryKind::Unsupported : EntryKind::Missing;
        }

        bool isDirectoryWritable(const std::string& path) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            if (!isDirectory(path)) return false;
            std::error_code ec;
            const std::filesystem::perms permissions = std::filesystem::status(hostedPathForVirtual(path), ec).permissions();
            if (ec) return false;
            return (permissions & (std::filesystem::perms::owner_write |
                                   std::filesystem::perms::group_write |
                                   std::filesystem::perms::others_write)) != std::filesystem::perms::none;
#else
            if (!isDirectory(path)) return false;
            const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount(normalizeVirtualPath(path).c_str());
            return mount && mount->active && !mount->readOnly && mount->fsType == kernel::vfs::FS_TYPE_FAT32;
#endif
        }

        bool sameOrDescendant(const std::string& path, const std::string& ancestor) {
            const std::string childKey = pathKey(path);
            const std::string ancestorKey = pathKey(ancestor);
            if (childKey == ancestorKey) return true;
            return childKey.size() > ancestorKey.size() &&
                childKey.compare(0, ancestorKey.size(), ancestorKey) == 0 &&
                ancestorKey != "/" && childKey[ancestorKey.size()] == '/';
        }

        std::string uniqueDestination(const std::string& destinationDirectory, const std::string& sourceName,
                                      bool sourceIsDirectory) {
            const std::string first = combinePath(destinationDirectory, sourceName);
            if (!exists(first)) return first;

            std::string stem = sourceName;
            std::string extension;
            const size_t dot = sourceName.find_last_of('.');
            if (!sourceIsDirectory && dot != std::string::npos && dot > 0 && dot + 1 < sourceName.size()) {
                stem = sourceName.substr(0, dot);
                extension = sourceName.substr(dot);
            }

            for (int attempt = 1; attempt < kMaxUniqueNameAttempts; ++attempt) {
                const std::string suffix = attempt == 1
                    ? " - Copy"
                    : " - Copy (" + std::to_string(attempt) + ")";
                const std::string candidate = combinePath(destinationDirectory, stem + suffix + extension);
                if (!exists(candidate)) return candidate;
            }
            return std::string();
        }

        std::string statusText(int status) {
#if !defined(_WIN32) || defined(GXOS_BARE_METAL)
            switch (status) {
                case kernel::vfs::VFS_ERR_NOT_FOUND: return "Path not found";
                case kernel::vfs::VFS_ERR_EXISTS: return "Destination already exists";
                case kernel::vfs::VFS_ERR_NOT_DIR: return "Parent is not a directory";
                case kernel::vfs::VFS_ERR_IS_DIR: return "Path is a directory";
                case kernel::vfs::VFS_ERR_READ_ONLY: return "Filesystem is read-only";
                case kernel::vfs::VFS_ERR_NOT_SUPPORTED: return "Filesystem operation is not supported";
                default: return "Filesystem operation failed";
            }
#else
            (void)status;
            return "Filesystem operation failed";
#endif
        }

        bool copyFile(const std::string& source, const std::string& destination, std::string& error) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            if (!std::filesystem::copy_file(hostedPathForVirtual(source), hostedPathForVirtual(destination),
                std::filesystem::copy_options::none, ec)) {
                error = ec ? ec.message() : "Unable to copy file";
                return false;
            }
            return true;
#else
            const std::string sourcePath = normalizeVirtualPath(source);
            const std::string destinationPath = normalizeVirtualPath(destination);
            const uint8_t sourceHandle = kernel::vfs::open(sourcePath.c_str(), kernel::vfs::OPEN_READ);
            if (sourceHandle == 0xFF) {
                error = "Unable to open source file";
                return false;
            }
            const uint8_t destinationHandle = kernel::vfs::open(destinationPath.c_str(),
                kernel::vfs::OPEN_WRITE | kernel::vfs::OPEN_CREATE | kernel::vfs::OPEN_TRUNCATE | kernel::vfs::OPEN_EXCL);
            if (destinationHandle == 0xFF) {
                kernel::vfs::close(sourceHandle);
                error = "Unable to create destination file";
                return false;
            }

            uint8_t buffer[kCopyBufferBytes];
            bool success = true;
            while (true) {
                const int32_t readBytes = kernel::vfs::read(sourceHandle, buffer, sizeof(buffer));
                if (readBytes < 0) {
                    error = "Unable to read source file";
                    success = false;
                    break;
                }
                if (readBytes == 0) break;
                const int32_t written = kernel::vfs::write(destinationHandle, buffer, static_cast<uint32_t>(readBytes));
                if (written != readBytes) {
                    error = "Unable to write destination file";
                    success = false;
                    break;
                }
            }
            kernel::vfs::close(sourceHandle);
            kernel::vfs::close(destinationHandle);
            if (!success) kernel::vfs::unlink(destinationPath.c_str());
            return success;
#endif
        }

#if !defined(_WIN32) || defined(GXOS_BARE_METAL)
        bool readDirectoryEntryAt(const std::string& path, size_t wantedIndex,
                                  kernel::vfs::DirEntry& out, std::string& error) {
            const uint8_t iterator = kernel::vfs::opendir(normalizeVirtualPath(path).c_str());
            if (iterator == 0xFF) {
                error = "Unable to enumerate source folder";
                return false;
            }

            size_t index = 0;
            kernel::vfs::DirEntry entry{};
            while (kernel::vfs::readdir(iterator, &entry)) {
                if (entry.name[0] == '.' && (entry.name[1] == '\0' ||
                    (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
                if (index == wantedIndex) {
                    out = entry;
                    kernel::vfs::closedir(iterator);
                    return true;
                }
                ++index;
            }
            kernel::vfs::closedir(iterator);
            return false;
        }
#endif

        bool removeEntry(const std::string& path, std::string& error);

        bool copyEntry(const std::string& source, const std::string& destination, EntryKind kind, std::string& error) {
            if (kind == EntryKind::RegularFile) return copyFile(source, destination, error);
            if (kind != EntryKind::Directory) {
                error = "Clipboard source is not a supported file or folder";
                return false;
            }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            std::filesystem::copy(hostedPathForVirtual(source), hostedPathForVirtual(destination),
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::none, ec);
            if (ec) {
                error = ec.message();
                std::string cleanupError;
                removeEntry(destination, cleanupError);
                return false;
            }
            return true;
#else
            if (kernel::vfs::mkdir(normalizeVirtualPath(destination).c_str()) != kernel::vfs::VFS_OK) {
                error = "Unable to create destination folder";
                return false;
            }

            for (size_t childIndex = 0;; ++childIndex) {
                kernel::vfs::DirEntry child{};
                std::string enumerationError;
                if (!readDirectoryEntryAt(source, childIndex, child, enumerationError)) {
                    if (!enumerationError.empty()) {
                        error = enumerationError;
                        std::string cleanupError;
                        removeEntry(destination, cleanupError);
                        return false;
                    }
                    break;
                }

                const std::string childSource = combinePath(source, child.name);
                const std::string childDestination = combinePath(destination, child.name);
                const EntryKind childKind = child.type == kernel::vfs::FILE_TYPE_DIRECTORY
                    ? EntryKind::Directory : child.type == kernel::vfs::FILE_TYPE_REGULAR
                    ? EntryKind::RegularFile : EntryKind::Unsupported;
                if (!copyEntry(childSource, childDestination, childKind, error)) {
                    std::string cleanupError;
                    removeEntry(destination, cleanupError);
                    return false;
                }
            }
            return true;
#endif
        }

        bool moveFile(const std::string& source, const std::string& destination, std::string& error) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            std::filesystem::rename(hostedPathForVirtual(source), hostedPathForVirtual(destination), ec);
            if (ec) {
                error = ec.message();
                return false;
            }
            return true;
#else
            const kernel::vfs::Status status = kernel::vfs::rename(
                normalizeVirtualPath(source).c_str(), normalizeVirtualPath(destination).c_str());
            if (status != kernel::vfs::VFS_OK) {
                error = statusText(status);
                return false;
            }
            return true;
#endif
        }

        bool removeFile(const std::string& path, std::string& error) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            if (!std::filesystem::remove(hostedPathForVirtual(path), ec) || ec) {
                error = ec ? ec.message() : "Unable to remove source file after copy";
                return false;
            }
            return true;
#else
            const kernel::vfs::Status status = kernel::vfs::unlink(normalizeVirtualPath(path).c_str());
            if (status != kernel::vfs::VFS_OK) {
                error = statusText(status);
                return false;
            }
            return true;
#endif
        }

        bool removeEntry(const std::string& path, std::string& error) {
            const EntryKind kind = entryKind(path);
            if (kind == EntryKind::Missing) return true;
            if (kind == EntryKind::RegularFile) return removeFile(path, error);
            if (kind != EntryKind::Directory) {
                error = "Unable to remove unsupported filesystem entry";
                return false;
            }

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            std::filesystem::remove_all(hostedPathForVirtual(path), ec);
            if (ec) {
                error = ec.message();
                return false;
            }
            return true;
#else
            // The VFS directory iterator is index-based and entries shift
            // after a child is removed. Always remove the first remaining
            // child so recursive cleanup cannot skip entries.
            for (;;) {
                kernel::vfs::DirEntry child{};
                std::string enumerationError;
                if (!readDirectoryEntryAt(path, 0, child, enumerationError)) {
                    if (!enumerationError.empty()) {
                        error = enumerationError;
                        return false;
                    }
                    break;
                }
                const std::string childPath = combinePath(path, child.name);
                if (!removeEntry(childPath, error)) return false;
            }
            const kernel::vfs::Status status = kernel::vfs::rmdir(normalizeVirtualPath(path).c_str());
            if (status != kernel::vfs::VFS_OK) {
                error = statusText(status);
                return false;
            }
            return true;
#endif
        }

        bool validatePaste(const FileClipboardEntry& entry, const std::string& destinationDirectory, std::string& error) {
            const std::string source = normalizeVirtualPath(entry.sourcePath);
            const std::string destination = normalizeVirtualPath(destinationDirectory);
            const EntryKind sourceKind = entryKind(source);
            if (sourceKind == EntryKind::Missing) {
                error = "Clipboard source is no longer available";
                return false;
            }
            if (sourceKind == EntryKind::Unsupported) {
                error = "Clipboard source is not a supported file or folder";
                return false;
            }
            if (!isDirectory(destination) || !isDirectoryWritable(destination)) {
                error = "Paste destination is not an available folder";
                return false;
            }
            if (sourceKind == EntryKind::Directory && sameOrDescendant(destination, source)) {
                error = "Cannot paste a folder into itself or one of its descendants";
                return false;
            }
            return true;
        }
    }

    bool FileClipboard::Set(const std::string& sourcePath, FileClipboardOperation operation, std::string& error) {
        error.clear();
        const std::string normalized = normalizeVirtualPath(sourcePath);
        const EntryKind kind = entryKind(normalized);
        if (kind != EntryKind::RegularFile && kind != EntryKind::Directory) {
            error = "Clipboard source is not an available file or folder";
            return false;
        }
        std::lock_guard<std::mutex> lock(s_clipboardMutex);
        s_clipboard.sourcePath = normalized;
        s_clipboard.operation = operation;
        s_clipboardValid = true;
        return true;
    }

    bool FileClipboard::Get(FileClipboardEntry& out) {
        std::lock_guard<std::mutex> lock(s_clipboardMutex);
        if (!s_clipboardValid) return false;
        out = s_clipboard;
        return true;
    }

    void FileClipboard::Clear() {
        std::lock_guard<std::mutex> lock(s_clipboardMutex);
        s_clipboard = FileClipboardEntry{};
        s_clipboardValid = false;
    }

    bool FileClipboard::IsCutSource(const std::string& sourcePath) {
        std::lock_guard<std::mutex> lock(s_clipboardMutex);
        return s_clipboardValid && s_clipboard.operation == FileClipboardOperation::Move &&
            samePath(s_clipboard.sourcePath, sourcePath);
    }

    std::string FileOperations::NormalizePath(const std::string& path) { return normalizeVirtualPath(path); }
    std::string FileOperations::ParentPath(const std::string& path) { return parentPath(path); }
    std::string FileOperations::BaseName(const std::string& path) { return baseName(path); }
    std::string FileOperations::CombinePath(const std::string& directory, const std::string& name) { return combinePath(directory, name); }
    bool FileOperations::Exists(const std::string& path) { return exists(path); }
    bool FileOperations::IsDirectory(const std::string& path) { return isDirectory(path); }
    bool FileOperations::IsRegularFile(const std::string& path) { return isRegularFile(path); }

    bool FileOperations::CanPasteFile(const std::string& destinationDirectory, std::string& error) {
        FileClipboardEntry entry;
        if (!FileClipboard::Get(entry)) {
            error = "Nothing to paste";
            return false;
        }
        return validatePaste(entry, destinationDirectory, error);
    }

    FilePasteResult FileOperations::PasteFile(const std::string& destinationDirectory) {
        FilePasteResult result;
        FileClipboardEntry entry;
        if (!FileClipboard::Get(entry)) {
            result.error = "Nothing to paste";
            return result;
        }

        const std::string source = normalizeVirtualPath(entry.sourcePath);
        const std::string destinationDirectoryNormalized = normalizeVirtualPath(destinationDirectory);
        if (!validatePaste(entry, destinationDirectoryNormalized, result.error)) return result;

        const EntryKind sourceKind = entryKind(source);
        const std::string sameNameDestination = combinePath(destinationDirectoryNormalized, baseName(source));
        if (entry.operation == FileClipboardOperation::Move && samePath(source, sameNameDestination)) {
            // A cut/paste into the source's current parent is a completed no-op.
            FileClipboard::Clear();
            result.success = true;
            result.destinationPath = source;
            s_operationGeneration.fetch_add(1, std::memory_order_release);
            return result;
        }

        const std::string destination = uniqueDestination(destinationDirectoryNormalized, baseName(source),
                                                           sourceKind == EntryKind::Directory);
        if (destination.empty()) {
            result.error = "Unable to choose a safe destination name";
            return result;
        }

        std::string operationError;
        if (entry.operation == FileClipboardOperation::Copy) {
            if (!copyEntry(source, destination, sourceKind, operationError)) {
                result.error = operationError;
                return result;
            }
        } else {
            if (!moveFile(source, destination, operationError)) {
                // A rename can be unsupported across mounted filesystems. In
                // that case, copy first and delete only after the copy exists.
                if (!copyEntry(source, destination, sourceKind, operationError)) {
                    result.error = operationError;
                    return result;
                }
                if (entryKind(destination) != sourceKind) {
                    std::string cleanupError;
                    removeEntry(destination, cleanupError);
                    result.error = "Move verification failed; source was preserved";
                    return result;
                }
                if (!removeEntry(source, operationError)) {
                    std::string cleanupError;
                    removeEntry(destination, cleanupError);
                    result.error = "Move cleanup failed; source was preserved";
                    return result;
                }
            }
            FileClipboard::Clear();
        }

        result.success = true;
        result.destinationPath = destination;
        s_operationGeneration.fetch_add(1, std::memory_order_release);
        return result;
    }

    FileCreateResult FileOperations::CreateUniqueFolder(const std::string& destinationDirectory) {
        FileCreateResult result;
        const std::string destination = normalizeVirtualPath(destinationDirectory);
        if (!isDirectory(destination)) {
            result.error = "Folder destination is not available";
            return result;
        }
        if (!isDirectoryWritable(destination)) {
            result.error = "Folder destination is read-only or unavailable";
            return result;
        }

        for (int suffixIndex = 1; suffixIndex <= kMaxUniqueNameAttempts; ++suffixIndex) {
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            const std::string name = suffixIndex == 1
                ? "New Folder"
                : "New Folder (" + std::to_string(suffixIndex) + ")";
#else
            // The current bare-metal FAT writer accepts only 8.3 names. Keep
            // the same collision policy while using the largest representable
            // equivalent name on that backend.
            const std::string suffix = suffixIndex == 1 ? std::string() : std::to_string(suffixIndex);
            const std::string base = "NewFolder";
            const size_t baseLength = suffix.empty() ? base.size() : 8u - std::min<size_t>(suffix.size(), 7u);
            const std::string name = base.substr(0, baseLength) + suffix;
#endif
            const std::string candidate = combinePath(destination, name);
            if (exists(candidate)) continue;

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
            std::error_code ec;
            if (std::filesystem::create_directory(hostedPathForVirtual(candidate), ec) && !ec) {
                result.success = true;
                result.path = candidate;
                return result;
            }
            if (!ec && exists(candidate)) continue;
            result.error = ec ? ec.message() : "Unable to create folder";
            return result;
#else
            const kernel::vfs::Status status = kernel::vfs::mkdir(candidate.c_str());
            if (status == kernel::vfs::VFS_OK) {
                result.success = true;
                result.path = candidate;
                return result;
            }
            if (status == kernel::vfs::VFS_ERR_EXISTS) continue;
            result.error = statusText(status);
            return result;
#endif
        }

        result.error = "Unable to choose a safe folder name";
        return result;
    }

    uint64_t FileOperations::OperationGeneration() {
        return s_operationGeneration.load(std::memory_order_acquire);
    }

}} // namespace gxos::files
