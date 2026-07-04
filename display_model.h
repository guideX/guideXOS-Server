#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace gxos {
namespace gui {

inline constexpr int kSyntheticTestMonitorWidth = 1920;
inline constexpr int kSyntheticTestMonitorHeight = 1080;

inline bool hostedSyntheticDualMonitorEnabled()
{
    const char* value = std::getenv("GXOS_SYNTHETIC_DUAL_MONITOR");
    if (!value || !*value) {
        return false;
    }

    std::string lower;
    lower.reserve(std::strlen(value));
    for (const char* p = value; *p; ++p) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return !(lower == "0" || lower == "false" || lower == "off" || lower == "no");
}

enum class DisplayModeKind {
    Mirror,
    Extend
};

inline std::string normalizeDisplayModeName(const std::string& value)
{
    std::string lower;
    lower.reserve(value.size());
    for (char c : value) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "extend") return "extend";
    return "mirror";
}

inline DisplayModeKind parseDisplayModeKind(const std::string& value)
{
    return normalizeDisplayModeName(value) == "extend" ? DisplayModeKind::Extend : DisplayModeKind::Mirror;
}

inline const char* displayModeName(DisplayModeKind mode)
{
    return mode == DisplayModeKind::Extend ? "extend" : "mirror";
}

struct DisplayMonitorDescriptor {
    std::string id;
    uint32_t* framebufferBase{nullptr};
    int width{0};
    int height{0};
    int pitch{0};
    std::string pixelFormat{"XRGB32"};
    int virtualX{0};
    int virtualY{0};
    bool enabled{true};
    bool primary{false};
    float scale{1.0f};
    int rotation{0};
    int refreshRateHz{0};
    std::string name;

    bool isActive() const
    {
        return enabled && width > 0 && height > 0;
    }

    bool containsVirtualPoint(int x, int y) const
    {
        return isActive() && x >= virtualX && x < (virtualX + width) && y >= virtualY && y < (virtualY + height);
    }
};

inline std::vector<std::string> splitDisplayTokens(const std::string& text, char delimiter)
{
    std::vector<std::string> tokens;
    std::string current;
    std::istringstream input(text);
    while (std::getline(input, current, delimiter)) {
        tokens.push_back(current);
    }
    return tokens;
}

inline DisplayMonitorDescriptor parseDisplayMonitorDescriptor(const std::string& entry)
{
    DisplayMonitorDescriptor monitor;
    const std::vector<std::string> fields = splitDisplayTokens(entry, '|');
    if (fields.empty()) {
        return monitor;
    }

    try {
        if (fields.size() > 0) monitor.id = fields[0];
        if (fields.size() > 1) monitor.name = fields[1];
        if (fields.size() > 2) monitor.virtualX = std::stoi(fields[2]);
        if (fields.size() > 3) monitor.virtualY = std::stoi(fields[3]);
        if (fields.size() > 4) monitor.width = std::max(0, std::stoi(fields[4]));
        if (fields.size() > 5) monitor.height = std::max(0, std::stoi(fields[5]));
        if (fields.size() > 6) monitor.enabled = fields[6] != "0";
        if (fields.size() > 7) monitor.primary = fields[7] != "0";
        if (fields.size() > 8) monitor.scale = static_cast<float>(std::atof(fields[8].c_str()));
        if (fields.size() > 9) monitor.rotation = std::stoi(fields[9]);
        if (fields.size() > 10) monitor.refreshRateHz = std::stoi(fields[10]);
        if (fields.size() > 11) monitor.pixelFormat = fields[11];
    } catch (...) {
        return DisplayMonitorDescriptor{};
    }
    return monitor;
}

inline std::string serializeDisplayMonitorDescriptor(const DisplayMonitorDescriptor& monitor)
{
    std::ostringstream out;
    out << monitor.id << '|'
        << monitor.name << '|'
        << monitor.virtualX << '|'
        << monitor.virtualY << '|'
        << monitor.width << '|'
        << monitor.height << '|'
        << (monitor.enabled ? 1 : 0) << '|'
        << (monitor.primary ? 1 : 0) << '|'
        << monitor.scale << '|'
        << monitor.rotation << '|'
        << monitor.refreshRateHz << '|'
        << monitor.pixelFormat;
    return out.str();
}

inline std::vector<DisplayMonitorDescriptor> parseDisplayArrangement(const std::string& text)
{
    std::vector<DisplayMonitorDescriptor> monitors;
    if (text.empty()) {
        return monitors;
    }

    for (const std::string& entry : splitDisplayTokens(text, ';')) {
        if (entry.empty()) continue;
        monitors.push_back(parseDisplayMonitorDescriptor(entry));
    }
    return monitors;
}

inline std::string serializeDisplayArrangement(const std::vector<DisplayMonitorDescriptor>& monitors)
{
    std::ostringstream out;
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (i != 0) {
            out << ';';
        }
        out << serializeDisplayMonitorDescriptor(monitors[i]);
    }
    return out.str();
}

struct DisplayVirtualDesktop {
    DisplayModeKind mode{DisplayModeKind::Mirror};
    std::vector<DisplayMonitorDescriptor> monitors;
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    int width() const { return std::max(0, right - left); }
    int height() const { return std::max(0, bottom - top); }

    bool containsPoint(int x, int y) const
    {
        return x >= left && x < right && y >= top && y < bottom;
    }

    int activeMonitorCount() const
    {
        int count = 0;
        for (const auto& monitor : monitors) {
            if (monitor.isActive()) {
                ++count;
            }
        }
        return count;
    }

    void recomputeBounds()
    {
        bool haveBounds = false;
        left = top = right = bottom = 0;
        for (const auto& monitor : monitors) {
            if (!monitor.isActive()) {
                continue;
            }
            const int monitorRight = monitor.virtualX + monitor.width;
            const int monitorBottom = monitor.virtualY + monitor.height;
            if (!haveBounds) {
                left = monitor.virtualX;
                top = monitor.virtualY;
                right = monitorRight;
                bottom = monitorBottom;
                haveBounds = true;
            } else {
                left = std::min(left, monitor.virtualX);
                top = std::min(top, monitor.virtualY);
                right = std::max(right, monitorRight);
                bottom = std::max(bottom, monitorBottom);
            }
        }
        if (!haveBounds) {
            left = top = 0;
            right = bottom = 0;
        }
    }

    const DisplayMonitorDescriptor* monitorAt(int x, int y) const
    {
        for (const auto& monitor : monitors) {
            if (monitor.containsVirtualPoint(x, y)) {
                return &monitor;
            }
        }
        return nullptr;
    }

    DisplayMonitorDescriptor* monitorAt(int x, int y)
    {
        for (auto& monitor : monitors) {
            if (monitor.containsVirtualPoint(x, y)) {
                return &monitor;
            }
        }
        return nullptr;
    }

    void clampPointToBounds(int& x, int& y) const
    {
        if (activeMonitorCount() == 0) {
            return;
        }

        const int maxX = std::max(left, right - 1);
        const int maxY = std::max(top, bottom - 1);
        x = std::max(left, std::min(x, maxX));
        y = std::max(top, std::min(y, maxY));
    }

    int primaryViewportLeft() const
    {
        const DisplayMonitorDescriptor* primary = primaryMonitor();
        return primary ? primary->virtualX : left;
    }

    int primaryViewportTop() const
    {
        const DisplayMonitorDescriptor* primary = primaryMonitor();
        return primary ? primary->virtualY : top;
    }

    int primaryViewportWidth() const
    {
        const DisplayMonitorDescriptor* primary = primaryMonitor();
        return primary ? std::max(0, primary->width) : width();
    }

    int primaryViewportHeight() const
    {
        const DisplayMonitorDescriptor* primary = primaryMonitor();
        return primary ? std::max(0, primary->height) : height();
    }

    std::string monitorRectString() const
    {
        std::ostringstream out;
        bool first = true;
        for (const auto& monitor : monitors) {
            if (!monitor.isActive()) {
                continue;
            }
            if (!first) {
                out << "; ";
            }
            first = false;
            out << monitor.id;
            if (!monitor.name.empty()) {
                out << "(" << monitor.name << ")";
            }
            out << "@" << monitor.virtualX << "," << monitor.virtualY
                << " " << monitor.width << "x" << monitor.height;
            if (monitor.primary) {
                out << " primary";
            }
        }
        if (first) {
            out << "(none)";
        }
        return out.str();
    }

    DisplayMonitorDescriptor* primaryMonitor()
    {
        for (auto& monitor : monitors) {
            if (monitor.isActive() && monitor.primary) {
                return &monitor;
            }
        }
        for (auto& monitor : monitors) {
            if (monitor.isActive()) {
                return &monitor;
            }
        }
        return nullptr;
    }

    const DisplayMonitorDescriptor* primaryMonitor() const
    {
        for (const auto& monitor : monitors) {
            if (monitor.isActive() && monitor.primary) {
                return &monitor;
            }
        }
        for (const auto& monitor : monitors) {
            if (monitor.isActive()) {
                return &monitor;
            }
        }
        return nullptr;
    }

    std::string resolutionString() const
    {
        std::ostringstream out;
        out << width() << 'x' << height();
        return out.str();
    }

    std::string summary() const
    {
        std::ostringstream out;
        out << "mode=" << displayModeName(mode)
            << " monitors=" << activeMonitorCount()
            << " bounds=" << width() << 'x' << height();
        if (const DisplayMonitorDescriptor* primary = primaryMonitor()) {
            out << " primary=" << primary->id;
            if (!primary->name.empty()) {
                out << "(" << primary->name << ")";
            }
        }
        return out.str();
    }

    std::string detailedSummary() const
    {
        std::ostringstream out;
        out << "mode=" << displayModeName(mode)
            << " monitorCount=" << activeMonitorCount()
            << " virtualDesktop=" << width() << 'x' << height()
            << " bounds=" << left << "," << top << "-" << right << "," << bottom;
        if (const DisplayMonitorDescriptor* primary = primaryMonitor()) {
            out << " primary=" << primary->id;
            if (!primary->name.empty()) {
                out << "(" << primary->name << ")";
            }
            out << " primaryViewport=" << primaryViewportLeft() << "," << primaryViewportTop()
                << " " << primaryViewportWidth() << "x" << primaryViewportHeight();
        }
        out << " monitors=[" << monitorRectString() << "]";
        return out.str();
    }

    std::string arrangementString() const
    {
        return serializeDisplayArrangement(monitors);
    }
};

inline DisplayMonitorDescriptor makeDisplayMonitor(
    const std::string& id,
    const std::string& name,
    int x,
    int y,
    int width,
    int height,
    uint32_t* framebufferBase = nullptr,
    int pitch = 0,
    bool enabled = true,
    bool primary = false)
{
    DisplayMonitorDescriptor monitor;
    monitor.id = id;
    monitor.name = name;
    monitor.virtualX = x;
    monitor.virtualY = y;
    monitor.width = width;
    monitor.height = height;
    monitor.framebufferBase = framebufferBase;
    monitor.pitch = pitch;
    monitor.enabled = enabled;
    monitor.primary = primary;
    return monitor;
}

inline DisplayVirtualDesktop makeSingleMonitorDesktop(
    uint32_t* framebufferBase,
    int width,
    int height,
    int pitch,
    const std::string& id = "display-1",
    const std::string& name = "Display 1")
{
    DisplayVirtualDesktop desktop;
    desktop.mode = DisplayModeKind::Mirror;
    desktop.monitors.push_back(makeDisplayMonitor(id, name, 0, 0, width, height, framebufferBase, pitch, true, true));
    desktop.recomputeBounds();
    return desktop;
}

inline DisplayVirtualDesktop makeSyntheticDualMonitorDesktop(
    uint32_t* framebufferBase,
    int monitorWidth = kSyntheticTestMonitorWidth,
    int monitorHeight = kSyntheticTestMonitorHeight,
    int pitch = 0)
{
    DisplayVirtualDesktop desktop;
    desktop.mode = DisplayModeKind::Extend;
    monitorWidth = std::max(1, monitorWidth);
    monitorHeight = std::max(1, monitorHeight);
    desktop.monitors.push_back(makeDisplayMonitor("display-1", "Synthetic Display 1", 0, 0, monitorWidth, monitorHeight, framebufferBase, pitch, true, true));
    desktop.monitors.push_back(makeDisplayMonitor("display-2", "Synthetic Display 2", monitorWidth, 0, monitorWidth, monitorHeight, framebufferBase, pitch, true, false));
    desktop.recomputeBounds();
    return desktop;
}

} // namespace gui
} // namespace gxos
