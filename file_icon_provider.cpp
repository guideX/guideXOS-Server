#include "file_icon_provider.h"
#include "file_explorer.h"
#include <string>
#include <algorithm>
#include <cctype>

namespace gxos { namespace apps {

    FileIconType FileIconProvider::iconTypeForExtension(const std::string& name) {
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot + 1 >= name.size()) return FileIconType::UnknownFile;

        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (ext == "txt" || ext == "log" || ext == "cfg" || ext == "ini" || ext == "md") return FileIconType::TextFile;
        if (ext == "bmp" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif") return FileIconType::ImageFile;
        if (ext == "elf" || ext == "gxapp" || ext == "gxq" || ext == "exe") return FileIconType::Application;
        if (ext == "img") return FileIconType::MountedDrive;
        if (ext == "bin" || ext == "dat" || ext == "so" || ext == "dll" || ext == "o") return FileIconType::BinaryFile;
        return FileIconType::UnknownFile;
    }

    FileIconType FileIconProvider::iconTypeForEntry(const ExplorerFileEntry& entry) {
        using Kind = ExplorerEntryKind;
        switch (entry.kind) {
            case Kind::Drive:
                // .img disk images inside a drive listing get a special drive icon
                if (!entry.name.empty()) {
                    std::string n = entry.name;
                    std::transform(n.begin(), n.end(), n.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (n.size() >= 4 && n.substr(n.size() - 4) == ".img") return FileIconType::MountedDrive;
                }
                return FileIconType::Drive;
            case Kind::Directory:
                return FileIconType::Folder;
            case Kind::CommonFolder:
                return FileIconType::SystemFolder;
            case Kind::File:
                return iconTypeForExtension(entry.name);
        }
        return FileIconType::UnknownFile;
    }

    const char* FileIconProvider::logicalIconName(FileIconType type) {
        switch (type) {
            case FileIconType::Folder:       return "file.folder";
            case FileIconType::SystemFolder: return "file.sysfolder";
            case FileIconType::Drive:        return "drive.fixed";
            case FileIconType::MountedDrive: return "drive.mounted";
            case FileIconType::TextFile:     return "file.text";
            case FileIconType::ImageFile:    return "file.image";
            case FileIconType::BinaryFile:   return "file.binary";
            case FileIconType::Application:  return "app.generic";
            case FileIconType::UnknownFile:  return "file.unknown";
        }
        return "file.unknown";
    }

    const char* FileIconProvider::logicalIconNameForEntry(const ExplorerFileEntry& entry) {
        return logicalIconName(iconTypeForEntry(entry));
    }

}} // namespace gxos::apps
