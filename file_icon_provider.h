#pragma once
#include <string>

namespace gxos { namespace apps {

    struct ExplorerFileEntry; // Forward declaration

    /// Logical file type used to select an icon from the shared icon theme.
    enum class FileIconType {
        Folder,
        SystemFolder,
        Drive,
        MountedDrive,
        TextFile,
        ImageFile,
        BinaryFile,
        Application,
        UnknownFile,
    };

    /// Maps ExplorerFileEntry metadata to shared IconThemeManager logical icon names.
    /// All other sub-systems (desktop, start menu, shortcuts) use the same logical
    /// names, so this provider is the single mapping table for file types.
    class FileIconProvider {
    public:
        /// Derive the icon type from an entry's kind and filename extension.
        static FileIconType iconTypeForEntry(const ExplorerFileEntry& entry);

        /// Convert a FileIconType to the shared logical icon name used by IconThemeManager.
        static const char* logicalIconName(FileIconType type);

        /// Convenience: logical icon name directly from entry.
        static const char* logicalIconNameForEntry(const ExplorerFileEntry& entry);

    private:
        static FileIconType iconTypeForExtension(const std::string& name);
    };

}} // namespace gxos::apps
