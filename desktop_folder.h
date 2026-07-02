#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(GXOS_BARE_METAL)
#include "logger.h"
#endif

namespace gxos { namespace gui {

    struct DesktopFolderEntry {
        std::string name;
        std::string virtualPath;
        bool isDirectory{false};
        uint64_t size{0};
    };

    class DesktopFolderResolver {
    public:
        static std::string TrimAsciiWhitespace(const std::string& value) {
            size_t begin = 0;
            size_t end = value.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
            return value.substr(begin, end - begin);
        }

        static std::string VirtualPath() {
            return "/Desktop";
        }

        static std::string NormalizeVirtualPath(const std::string& path) {
            std::string normalized = path.empty() ? "/" : path;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            std::filesystem::path virtualPath(normalized);
            if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
            std::string generic = virtualPath.lexically_normal().generic_string();
            if (generic.empty()) return "/";
            if (generic.front() != '/') generic.insert(generic.begin(), '/');
            return generic;
        }

        static std::filesystem::path HostedRootPath() {
            return std::filesystem::current_path();
        }

        static std::filesystem::path HostedPathForVirtual(const std::string& path) {
            std::string generic = NormalizeVirtualPath(path);
            if (generic.empty() || generic == "/") return HostedRootPath();
            if (generic.front() == '/') generic.erase(generic.begin());
            return HostedRootPath() / std::filesystem::path(generic);
        }

        static std::string ParentVirtualPath(const std::string& path) {
            const std::string normalized = NormalizeVirtualPath(path);
            std::filesystem::path virtualPath(normalized);
            std::string parent = virtualPath.parent_path().generic_string();
            if (parent.empty()) return "/";
            if (parent.front() != '/') parent.insert(parent.begin(), '/');
            return parent;
        }

        static std::string JoinVirtualPath(const std::string& parent, const std::string& childName) {
            const std::string normalizedParent = NormalizeVirtualPath(parent.empty() ? "/" : parent);
            if (normalizedParent == "/") return NormalizeVirtualPath("/" + childName);
            return NormalizeVirtualPath(normalizedParent + "/" + childName);
        }

        static bool EqualsAsciiIgnoreCase(const std::string& value, const char* other) {
            if (!other) return false;
            size_t i = 0;
            for (; i < value.size() && other[i]; ++i) {
                const unsigned char a = static_cast<unsigned char>(value[i]);
                const unsigned char b = static_cast<unsigned char>(other[i]);
                if (std::tolower(a) != std::tolower(b)) return false;
            }
            return i == value.size() && other[i] == '\0';
        }

        static bool IsReservedDesktopName(const std::string& name) {
            return EqualsAsciiIgnoreCase(name, "Trash") ||
                EqualsAsciiIgnoreCase(name, "File Explorer") ||
                EqualsAsciiIgnoreCase(name, "FileExplorer") ||
                EqualsAsciiIgnoreCase(name, "Files") ||
                EqualsAsciiIgnoreCase(name, "FileManager") ||
                EqualsAsciiIgnoreCase(name, "File Manager") ||
                EqualsAsciiIgnoreCase(name, "Computer") ||
                EqualsAsciiIgnoreCase(name, "This System") ||
                EqualsAsciiIgnoreCase(name, "Computer Files") ||
                EqualsAsciiIgnoreCase(name, "ComputerFiles") ||
                EqualsAsciiIgnoreCase(name, "System Settings") ||
                EqualsAsciiIgnoreCase(name, "SystemSettings") ||
                EqualsAsciiIgnoreCase(name, "Settings") ||
                EqualsAsciiIgnoreCase(name, "Display Options") ||
                EqualsAsciiIgnoreCase(name, "DisplayOptions") ||
                EqualsAsciiIgnoreCase(name, "Display Settings") ||
                EqualsAsciiIgnoreCase(name, "Desktop Background") ||
                EqualsAsciiIgnoreCase(name, "Wallpaper") ||
                EqualsAsciiIgnoreCase(name, "Back") ||
                EqualsAsciiIgnoreCase(name, "Go to Desktop");
        }

        static bool ValidateRenameName(const std::string& rawName, std::string& trimmedName, std::string& error) {
            trimmedName = TrimAsciiWhitespace(rawName);
            if (trimmedName.empty()) {
                error = "Name cannot be empty";
                return false;
            }
            if (trimmedName == "." || trimmedName == "..") {
                error = "Name cannot be '.' or '..'";
                return false;
            }
            if (trimmedName.find('/') != std::string::npos || trimmedName.find('\\') != std::string::npos) {
                error = "Name cannot contain path separators";
                return false;
            }
            return true;
        }

        static bool EnsureExists(const std::string& virtualPath, std::string& error, bool createIfMissing = true) {
#if defined(GXOS_BARE_METAL)
            (void)virtualPath;
            (void)error;
            (void)createIfMissing;
            return false;
#else
            error.clear();
            const std::string normalized = NormalizeVirtualPath(virtualPath.empty() ? VirtualPath() : virtualPath);
            const std::filesystem::path hostedPath = HostedPathForVirtual(normalized);
            Logger::write(LogLevel::Info, "Desktop folder resolver selected virtualPath=" + normalized + " hostedPath=" + hostedPath.generic_string());

            std::error_code ec;
            if (std::filesystem::exists(hostedPath, ec)) {
                if (!ec && std::filesystem::is_directory(hostedPath, ec)) {
                    Logger::write(LogLevel::Info, "Desktop folder already exists: " + hostedPath.generic_string());
                    return true;
                }
                error = ec ? ec.message() : "Desktop path exists but is not a directory";
                Logger::write(LogLevel::Warn, "Desktop folder unavailable: " + error);
                return false;
            }

            if (!createIfMissing) {
                error = "Desktop path missing: " + normalized;
                Logger::write(LogLevel::Warn, "Desktop folder unavailable: " + error);
                return false;
            }

            if (std::filesystem::create_directories(hostedPath, ec) && !ec) {
                Logger::write(LogLevel::Info, "Desktop folder created: " + hostedPath.generic_string());
                return true;
            }

            error = ec ? ec.message() : "Unable to create Desktop folder";
            Logger::write(LogLevel::Warn, "Desktop folder creation failed: " + error);
            return false;
#endif
        }

        static std::vector<DesktopFolderEntry> Enumerate(const std::string& virtualPath = VirtualPath()) {
            std::vector<DesktopFolderEntry> entries;
#if defined(GXOS_BARE_METAL)
            (void)virtualPath;
            return entries;
#else
            std::string ensureError;
            const std::string normalized = NormalizeVirtualPath(virtualPath.empty() ? VirtualPath() : virtualPath);
            const bool available = EnsureExists(normalized, ensureError, normalized == VirtualPath());
            if (!available) {
                Logger::write(LogLevel::Warn, "Desktop folder enumeration skipped: " + ensureError);
                return entries;
            }

            const std::filesystem::path hostedRoot = HostedPathForVirtual(normalized);
            Logger::write(LogLevel::Info, "Desktop folder enumeration started: " + hostedRoot.generic_string());

            std::error_code ec;
            for (const auto& item : std::filesystem::directory_iterator(hostedRoot, ec)) {
                if (ec) {
                    Logger::write(LogLevel::Warn, "Desktop folder enumeration stopped: " + ec.message());
                    break;
                }

                DesktopFolderEntry entry;
                entry.name = item.path().filename().generic_string();
                entry.virtualPath = normalized + "/" + entry.name;
                if (IsReservedDesktopName(entry.name)) {
                    Logger::write(LogLevel::Info, "Desktop filesystem item skipped (reserved desktop name): " + entry.virtualPath);
                    continue;
                }
                entry.isDirectory = item.is_directory(ec);
                entry.size = entry.isDirectory ? 0 : static_cast<uint64_t>(item.file_size(ec));
                if (ec) {
                    Logger::write(LogLevel::Warn, "Desktop folder item metadata unavailable for " + entry.virtualPath + ": " + ec.message());
                    ec.clear();
                }
                entries.push_back(entry);
                Logger::write(LogLevel::Info, std::string("Desktop filesystem item discovered: ") + entry.virtualPath + (entry.isDirectory ? " [folder]" : " [file]"));
            }

            std::sort(entries.begin(), entries.end(), [](const DesktopFolderEntry& a, const DesktopFolderEntry& b) {
                if (a.isDirectory != b.isDirectory) return a.isDirectory;
                std::string an = a.name;
                std::string bn = b.name;
                std::transform(an.begin(), an.end(), an.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(bn.begin(), bn.end(), bn.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return an < bn;
            });

            Logger::write(LogLevel::Info, "Desktop folder enumeration completed count=" + std::to_string(entries.size()));
            return entries;
#endif
        }
    };

}} // namespace gxos::gui
