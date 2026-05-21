#pragma once

#include "logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace gxos { namespace gui {

    struct DesktopFolderEntry {
        std::string name;
        std::string virtualPath;
        bool isDirectory{false};
        uint64_t size{0};
    };

    class DesktopFolderResolver {
    public:
        static std::string VirtualPath() {
            return "/Desktop";
        }

        static std::filesystem::path HostedRootPath() {
            return std::filesystem::current_path();
        }

        static std::filesystem::path HostedPathForVirtual(const std::string& path) {
            std::string normalized = path.empty() ? "/" : path;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            std::filesystem::path virtualPath(normalized);
            if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
            std::string generic = virtualPath.lexically_normal().generic_string();
            if (generic.empty() || generic == "/") return HostedRootPath();
            if (generic.front() == '/') generic.erase(generic.begin());
            return HostedRootPath() / std::filesystem::path(generic);
        }

        static bool EnsureExists(std::string& error) {
            error.clear();
            const std::string virtualPath = VirtualPath();
            const std::filesystem::path hostedPath = HostedPathForVirtual(virtualPath);
            Logger::write(LogLevel::Info, "Desktop folder resolver selected virtualPath=" + virtualPath + " hostedPath=" + hostedPath.generic_string());

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

            if (std::filesystem::create_directories(hostedPath, ec) && !ec) {
                Logger::write(LogLevel::Info, "Desktop folder created: " + hostedPath.generic_string());
                return true;
            }

            error = ec ? ec.message() : "Unable to create Desktop folder";
            Logger::write(LogLevel::Warn, "Desktop folder creation failed: " + error);
            return false;
        }

        static std::vector<DesktopFolderEntry> Enumerate() {
            std::vector<DesktopFolderEntry> entries;
            std::string ensureError;
            const bool available = EnsureExists(ensureError);
            if (!available) {
                Logger::write(LogLevel::Warn, "Desktop folder enumeration skipped: " + ensureError);
                return entries;
            }

            const std::string virtualRoot = VirtualPath();
            const std::filesystem::path hostedRoot = HostedPathForVirtual(virtualRoot);
            Logger::write(LogLevel::Info, "Desktop folder enumeration started: " + hostedRoot.generic_string());

            std::error_code ec;
            for (const auto& item : std::filesystem::directory_iterator(hostedRoot, ec)) {
                if (ec) {
                    Logger::write(LogLevel::Warn, "Desktop folder enumeration stopped: " + ec.message());
                    break;
                }

                DesktopFolderEntry entry;
                entry.name = item.path().filename().generic_string();
                entry.virtualPath = virtualRoot + "/" + entry.name;
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
        }
    };

}} // namespace gxos::gui
