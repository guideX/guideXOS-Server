#pragma once
#include <string>

namespace gxos { namespace gui {
    class GxmLoader {
    public:
        // Execute a GUI script from a file (supports binary GXM with GUI preface or plain text script)
        static bool ExecuteFile(const std::string& path, std::string& error);
        // Execute a GUI script from a plain text string
        static bool ExecuteText(const std::string& text, std::string& error);
    };
} }
