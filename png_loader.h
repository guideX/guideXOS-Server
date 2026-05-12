#pragma once

#include "image.h"
#include <string>
#include <vector>

namespace gxos {
namespace gui {

class PngLoader {
public:
    static ImagePtr LoadFromMemory(const uint8_t* bytes, size_t byteCount, const std::string& sourceName = "<memory>");
    static ImagePtr LoadFromMemory(const std::vector<uint8_t>& bytes, const std::string& sourceName = "<memory>");
    static ImagePtr LoadFromFile(const std::string& path);
};

} // namespace gui
} // namespace gxos
