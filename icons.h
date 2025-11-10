#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

namespace gxos { namespace gui {
    class Icons {
    public:
        // Returns cached HBITMAP or nullptr if not available
        static HBITMAP StartIcon(int size);
        static HBITMAP TaskbarIcon(int size);
        static HBITMAP CloseIcon(int size);
        static HBITMAP MinimizeIcon(int size);
        static HBITMAP MaximizeIcon(int size);
        static HBITMAP RestoreIcon(int size);
        static HBITMAP TombstoneIcon(int size);
        static HBITMAP DocumentIcon(int size);
        static HBITMAP FolderIcon(int size);
        static HBITMAP ImageIcon(int size);
        static HBITMAP AudioIcon(int size);
    private:
        static HBITMAP loadBmp(const std::string& path);
        static HBITMAP getCached(const std::string& key, const std::string& relPath);
        static std::unordered_map<std::string,HBITMAP> s_cache;
    };
} }
