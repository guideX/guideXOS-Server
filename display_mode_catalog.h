#pragma once

// Backend-neutral logical display modes. These are QEMU logical scanout
// modes, not physical timings or host display modes.

#include "display_model.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace gxos {
namespace gui {

struct DisplayMode {
    std::string id;
    int width{0};
    int height{0};
    uint8_t bitsPerPixel{32};
    std::string pixelFormat{"XRGB32"};
    bool preferred{false};
    bool current{false};
    bool backendSupported{false};
    uint64_t estimatedBackingBytes{0};

    bool valid() const
    {
        return !id.empty() && width > 0 && height > 0 && backendSupported &&
            estimatedBackingBytes != 0;
    }

    std::string label() const
    {
        return std::to_string(width) + " x " + std::to_string(height);
    }
};

static constexpr int kQemuLogicalModeMinWidth = 800;
static constexpr int kQemuLogicalModeMinHeight = 600;
static constexpr int kQemuLogicalModeMaxWidth = 1280;
static constexpr int kQemuLogicalModeMaxHeight = 800;
static constexpr uint64_t kQemuLogicalModeBytesPerPixel = 4u;
static constexpr uint64_t kQemuLogicalModePerOutputBackingLimit = 16u * 1024u * 1024u;
static constexpr uint64_t kQemuLogicalModeTotalBackingLimit = 32u * 1024u * 1024u;

inline bool checkedDisplayModeBackingBytes(int width, int height, uint64_t bytesPerPixel, uint64_t& bytes)
{
    bytes = 0;
    if (width < kQemuLogicalModeMinWidth || height < kQemuLogicalModeMinHeight ||
        width > kQemuLogicalModeMaxWidth || height > kQemuLogicalModeMaxHeight ||
        bytesPerPixel == 0u) {
        return false;
    }
    const uint64_t w = static_cast<uint64_t>(width);
    const uint64_t h = static_cast<uint64_t>(height);
    if (w > std::numeric_limits<uint64_t>::max() / h) return false;
    const uint64_t pixels = w * h;
    if (pixels > std::numeric_limits<uint64_t>::max() / bytesPerPixel) return false;
    bytes = pixels * bytesPerPixel;
    return bytes <= kQemuLogicalModePerOutputBackingLimit;
}

inline std::vector<DisplayMode> qemuVirtioGpuLogicalModeCatalog()
{
    struct Candidate { const char* id; int width; int height; bool preferred; };
    static constexpr Candidate candidates[] = {
        { "qemu-1280x800", 1280, 800, true },
        { "qemu-1024x768", 1024, 768, false },
        { "qemu-800x600", 800, 600, false }
    };

    std::vector<DisplayMode> catalog;
    for (const Candidate& candidate : candidates) {
        uint64_t bytes = 0;
        if (!checkedDisplayModeBackingBytes(candidate.width, candidate.height,
                                             kQemuLogicalModeBytesPerPixel, bytes)) {
            continue;
        }
        bool duplicate = false;
        for (const auto& existing : catalog) {
            if (existing.width == candidate.width && existing.height == candidate.height) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        DisplayMode mode;
        mode.id = candidate.id;
        mode.width = candidate.width;
        mode.height = candidate.height;
        mode.bitsPerPixel = 32;
        mode.pixelFormat = "XRGB32";
        mode.preferred = candidate.preferred;
        mode.backendSupported = true;
        mode.estimatedBackingBytes = bytes;
        catalog.push_back(mode);
    }
    return catalog;
}

inline const DisplayMode* findDisplayModeById(const std::vector<DisplayMode>& catalog,
                                               const std::string& id)
{
    for (const auto& mode : catalog) {
        if (mode.id == id) return &mode;
    }
    return nullptr;
}

inline const DisplayMode* findDisplayModeByDimensions(const std::vector<DisplayMode>& catalog,
                                                       int width, int height)
{
    for (const auto& mode : catalog) {
        if (mode.width == width && mode.height == height) return &mode;
    }
    return nullptr;
}

} // namespace gui
} // namespace gxos
