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

struct DisplayViewport;

struct DisplayRect {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    int width() const { return std::max(0, right - left); }
    int height() const { return std::max(0, bottom - top); }
    bool isValid() const { return width() > 0 && height() > 0; }

    std::string summary() const
    {
        std::ostringstream out;
        out << left << ',' << top << '-' << right << ',' << bottom;
        return out.str();
    }
};

inline DisplayRect makeDisplayRect(int left, int top, int right, int bottom)
{
    return DisplayRect{ left, top, right, bottom };
}

inline DisplayRect offsetDisplayRect(const DisplayRect& rect, int dx, int dy)
{
    return DisplayRect{ rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy };
}

inline int displayRectIntersectionArea(const DisplayRect& a, const DisplayRect& b)
{
    const int left = std::max(a.left, b.left);
    const int top = std::max(a.top, b.top);
    const int right = std::min(a.right, b.right);
    const int bottom = std::min(a.bottom, b.bottom);
    if (right <= left || bottom <= top) {
        return 0;
    }
    return (right - left) * (bottom - top);
}

inline DisplayRect insetDisplayRectForTaskbar(const DisplayRect& bounds, const DisplayRect& taskbarRect)
{
    if (!bounds.isValid() || !taskbarRect.isValid()) {
        return bounds;
    }

    const int overlapLeft = std::max(bounds.left, taskbarRect.left);
    const int overlapTop = std::max(bounds.top, taskbarRect.top);
    const int overlapRight = std::min(bounds.right, taskbarRect.right);
    const int overlapBottom = std::min(bounds.bottom, taskbarRect.bottom);
    if (overlapRight <= overlapLeft || overlapBottom <= overlapTop) {
        return bounds;
    }

    DisplayRect work = bounds;
    if (taskbarRect.top <= bounds.top && taskbarRect.bottom < bounds.bottom) {
        work.top = std::min(bounds.bottom, taskbarRect.bottom);
    } else if (taskbarRect.bottom >= bounds.bottom && taskbarRect.top > bounds.top) {
        work.bottom = std::max(bounds.top, taskbarRect.top);
    } else if (taskbarRect.left <= bounds.left && taskbarRect.right < bounds.right) {
        work.left = std::min(bounds.right, taskbarRect.right);
    } else if (taskbarRect.right >= bounds.right && taskbarRect.left > bounds.left) {
        work.right = std::max(bounds.left, taskbarRect.left);
    }
    return work;
}

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

    DisplayRect virtualBounds() const
    {
        return DisplayRect{ virtualX, virtualY, virtualX + width, virtualY + height };
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

    const DisplayMonitorDescriptor* findMonitorByVirtualPoint(int x, int y) const
    {
        return monitorAt(x, y);
    }

    DisplayMonitorDescriptor* findMonitorByVirtualPoint(int x, int y)
    {
        return monitorAt(x, y);
    }

    const DisplayMonitorDescriptor* findMonitorByVirtualRectLargestIntersection(const DisplayRect& rect) const
    {
        const DisplayMonitorDescriptor* bestMonitor = nullptr;
        int bestArea = 0;
        for (const auto& monitor : monitors) {
            if (!monitor.isActive()) {
                continue;
            }
            const int area = displayRectIntersectionArea(rect, monitor.virtualBounds());
            if (area > bestArea) {
                bestArea = area;
                bestMonitor = &monitor;
            }
        }

        if (bestMonitor && bestArea > 0) {
            return bestMonitor;
        }

        if (rect.isValid()) {
            const int centerX = rect.left + rect.width() / 2;
            const int centerY = rect.top + rect.height() / 2;
            if (const DisplayMonitorDescriptor* centerMonitor = monitorAt(centerX, centerY)) {
                return centerMonitor;
            }
        }

        return primaryMonitor();
    }

    DisplayRect monitorBounds(const DisplayMonitorDescriptor& monitor) const
    {
        return monitor.virtualBounds();
    }

    DisplayRect monitorWorkArea(
        const DisplayMonitorDescriptor& monitor,
        const DisplayRect& primaryTaskbarRect,
        bool syntheticExtendMode,
        bool taskbarPrimaryOnly) const
    {
        const DisplayRect bounds = monitorBounds(monitor);
        if (!taskbarPrimaryOnly) {
            return insetDisplayRectForTaskbar(bounds, primaryTaskbarRect);
        }

        if (syntheticExtendMode) {
            if (monitor.primary) {
                return insetDisplayRectForTaskbar(bounds, primaryTaskbarRect);
            }
            // TODO(v0.2): per-monitor taskbars should shrink secondary work areas too.
            return bounds;
        }

        return insetDisplayRectForTaskbar(bounds, primaryTaskbarRect);
    }

    DisplayRect primaryMonitorBounds() const
    {
        if (const DisplayMonitorDescriptor* primary = primaryMonitor()) {
            return monitorBounds(*primary);
        }
        return DisplayRect{ left, top, right, bottom };
    }

    DisplayRect primaryMonitorWorkArea(
        const DisplayRect& primaryTaskbarRect,
        bool syntheticExtendMode,
        bool taskbarPrimaryOnly) const
    {
        if (const DisplayMonitorDescriptor* primary = primaryMonitor()) {
            return monitorWorkArea(*primary, primaryTaskbarRect, syntheticExtendMode, taskbarPrimaryOnly);
        }
        return DisplayRect{ left, top, right, bottom };
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

    const DisplayMonitorDescriptor* activeViewportMonitor(const DisplayViewport& viewport) const;
};

struct DisplayViewport {
    int index{1};
    int originX{0};
    int originY{0};
    int width{0};
    int height{0};
    bool syntheticHosted{false};
    std::string monitorId;
    std::string monitorName;

    bool isValid() const
    {
        return width > 0 && height > 0;
    }

    bool containsLocalPoint(int x, int y) const
    {
        return x >= 0 && x < width && y >= 0 && y < height;
    }

    bool containsVirtualPoint(int x, int y) const
    {
        return x >= originX && x < (originX + width) && y >= originY && y < (originY + height);
    }

    int localXFromVirtual(int x) const
    {
        return x - originX;
    }

    int localYFromVirtual(int y) const
    {
        return y - originY;
    }

    int virtualXFromLocal(int x) const
    {
        return originX + x;
    }

    int virtualYFromLocal(int y) const
    {
        return originY + y;
    }

    DisplayRect localRectFromVirtual(const DisplayRect& rect) const
    {
        return DisplayRect{
            rect.left - originX,
            rect.top - originY,
            rect.right - originX,
            rect.bottom - originY
        };
    }

    DisplayRect virtualRectFromLocal(const DisplayRect& rect) const
    {
        return DisplayRect{
            originX + rect.left,
            originY + rect.top,
            originX + rect.right,
            originY + rect.bottom
        };
    }

    std::string summary() const
    {
        std::ostringstream out;
        out << "activeViewport=" << index
            << " origin=" << originX << ',' << originY
            << " size=" << width << 'x' << height
            << " synthetic=" << (syntheticHosted ? "true" : "false");
        if (!monitorId.empty()) {
            out << " monitor=" << monitorId;
            if (!monitorName.empty()) {
                out << "(" << monitorName << ")";
            }
        }
        return out.str();
    }
};

struct DisplayRenderTarget {
    int targetIndex{1};
    std::string targetId;
    std::string monitorId;
    std::string monitorName;
    int viewportOriginX{0};
    int viewportOriginY{0};
    int width{0};
    int height{0};
    DisplayRect framebufferRect{ 0, 0, 0, 0 };
    bool primary{false};
    bool active{false};
    bool backedByHostedFramebuffer{false};
    bool syntheticHosted{false};

    bool isValid() const
    {
        return width > 0 && height > 0;
    }

    DisplayRect virtualBounds() const
    {
        return DisplayRect{
            viewportOriginX,
            viewportOriginY,
            viewportOriginX + width,
            viewportOriginY + height
        };
    }

    DisplayViewport viewportDescriptor() const
    {
        DisplayViewport viewport;
        viewport.index = targetIndex;
        viewport.originX = viewportOriginX;
        viewport.originY = viewportOriginY;
        viewport.width = width;
        viewport.height = height;
        viewport.syntheticHosted = syntheticHosted;
        viewport.monitorId = monitorId;
        viewport.monitorName = monitorName;
        return viewport;
    }

    std::string summary() const
    {
        std::ostringstream out;
        out << targetId
            << " monitor=" << (monitorId.empty() ? "(none)" : monitorId);
        if (!monitorName.empty()) {
            out << "(" << monitorName << ")";
        }
        out << " origin=" << viewportOriginX << ',' << viewportOriginY
            << " size=" << width << 'x' << height
            << " framebuffer=" << framebufferRect.left << ',' << framebufferRect.top << '-' << framebufferRect.right << ',' << framebufferRect.bottom
            << " primary=" << (primary ? "true" : "false")
            << " active=" << (active ? "true" : "false")
            << " backed=" << (backedByHostedFramebuffer ? "true" : "false");
        return out.str();
    }
};

inline DisplayRenderTarget makeDisplayRenderTarget(
    int targetIndex,
    const DisplayMonitorDescriptor& monitor,
    bool active,
    bool backedByHostedFramebuffer,
    bool syntheticHosted)
{
    const int width = std::max(1, monitor.width);
    const int height = std::max(1, monitor.height);
    DisplayRenderTarget target;
    target.targetIndex = std::max(1, targetIndex);
    target.targetId = std::string("display-target-") + std::to_string(target.targetIndex);
    target.monitorId = monitor.id;
    target.monitorName = monitor.name;
    target.viewportOriginX = monitor.virtualX;
    target.viewportOriginY = monitor.virtualY;
    target.width = width;
    target.height = height;
    target.framebufferRect = DisplayRect{ 0, 0, width, height };
    target.primary = monitor.primary;
    target.active = active;
    target.backedByHostedFramebuffer = backedByHostedFramebuffer;
    target.syntheticHosted = syntheticHosted;
    return target;
}

inline DisplayRenderTarget makeHostedFallbackRenderTarget(
    int fallbackWidth,
    int fallbackHeight,
    const DisplayMonitorDescriptor* monitor,
    bool syntheticHosted)
{
    const int width = std::max(1, fallbackWidth);
    const int height = std::max(1, fallbackHeight);
    DisplayRenderTarget target;
    target.targetIndex = 1;
    target.targetId = "display-target-1";
    if (monitor) {
        target.monitorId = monitor->id;
        target.monitorName = monitor->name;
        target.primary = monitor->primary;
    }
    target.viewportOriginX = 0;
    target.viewportOriginY = 0;
    target.width = width;
    target.height = height;
    target.framebufferRect = DisplayRect{ 0, 0, width, height };
    target.active = true;
    target.backedByHostedFramebuffer = true;
    target.syntheticHosted = syntheticHosted;
    return target;
}

inline std::vector<DisplayRenderTarget> buildDisplayRenderTargets(
    const DisplayVirtualDesktop& desktop,
    const DisplayViewport& viewport,
    int fallbackWidth,
    int fallbackHeight)
{
    std::vector<DisplayRenderTarget> targets;
    const bool syntheticHosted = viewport.syntheticHosted
        && hostedSyntheticDualMonitorEnabled()
        && desktop.mode == DisplayModeKind::Extend
        && desktop.activeMonitorCount() > 1;

    if (!syntheticHosted) {
        const DisplayMonitorDescriptor* activeMonitor = desktop.activeViewportMonitor(viewport);
        if (!activeMonitor) {
            activeMonitor = desktop.primaryMonitor();
        }
        if (activeMonitor) {
            DisplayRenderTarget target;
            target.targetIndex = 1;
            target.targetId = "display-target-1";
            target.monitorId = activeMonitor->id;
            target.monitorName = activeMonitor->name;
            target.viewportOriginX = viewport.originX;
            target.viewportOriginY = viewport.originY;
            target.width = std::max(1, viewport.width);
            target.height = std::max(1, viewport.height);
            target.framebufferRect = DisplayRect{ 0, 0, target.width, target.height };
            target.primary = true;
            target.active = true;
            target.backedByHostedFramebuffer = true;
            target.syntheticHosted = false;
            targets.push_back(target);
            return targets;
        }

        targets.push_back(makeHostedFallbackRenderTarget(fallbackWidth, fallbackHeight, nullptr, false));
        return targets;
    }

    std::vector<const DisplayMonitorDescriptor*> activeMonitors;
    activeMonitors.reserve(desktop.monitors.size());
    for (const auto& monitor : desktop.monitors) {
        if (monitor.isActive()) {
            activeMonitors.push_back(&monitor);
        }
    }

    if (activeMonitors.empty()) {
        targets.push_back(makeHostedFallbackRenderTarget(fallbackWidth, fallbackHeight, desktop.primaryMonitor(), true));
        return targets;
    }

    const DisplayMonitorDescriptor* activeMonitor = desktop.activeViewportMonitor(viewport);
    if (!activeMonitor) {
        activeMonitor = desktop.primaryMonitor();
    }

    int targetIndex = 1;
    for (const DisplayMonitorDescriptor* monitor : activeMonitors) {
        const bool isActive = activeMonitor && monitor->id == activeMonitor->id;
        targets.push_back(makeDisplayRenderTarget(targetIndex++, *monitor, isActive, isActive, true));
    }
    return targets;
}

inline const DisplayRenderTarget* activeDisplayRenderTarget(const std::vector<DisplayRenderTarget>& targets)
{
    for (const auto& target : targets) {
        if (target.active) {
            return &target;
        }
    }
    for (const auto& target : targets) {
        if (target.backedByHostedFramebuffer) {
            return &target;
        }
    }
    return targets.empty() ? nullptr : &targets.front();
}

inline std::string displayRenderTargetsSummary(const std::vector<DisplayRenderTarget>& targets)
{
    std::ostringstream out;
    out << "renderTargetCount=" << targets.size();
    if (!targets.empty()) {
        const DisplayRenderTarget* active = activeDisplayRenderTarget(targets);
        out << " activeHostedTarget=" << (active ? active->targetId : std::string("(none)"))
            << " targets=[";
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i != 0) {
                out << "; ";
            }
            out << targets[i].summary();
        }
        out << "]";
    }
    return out.str();
}

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

inline DisplayViewport makeHostedDisplayViewport(
    const DisplayVirtualDesktop& desktop,
    int requestedIndex,
    int fallbackWidth,
    int fallbackHeight)
{
    DisplayViewport viewport;
    viewport.syntheticHosted = hostedSyntheticDualMonitorEnabled()
        && desktop.mode == DisplayModeKind::Extend
        && desktop.activeMonitorCount() > 1;

    if (!viewport.syntheticHosted) {
        viewport.index = 1;
        viewport.originX = 0;
        viewport.originY = 0;
        viewport.width = std::max(1, fallbackWidth);
        viewport.height = std::max(1, fallbackHeight);
        if (const DisplayMonitorDescriptor* primary = desktop.primaryMonitor()) {
            viewport.monitorId = primary->id;
            viewport.monitorName = primary->name;
        }
        return viewport;
    }

    std::vector<const DisplayMonitorDescriptor*> activeMonitors;
    activeMonitors.reserve(desktop.monitors.size());
    for (const auto& monitor : desktop.monitors) {
        if (monitor.isActive()) {
            activeMonitors.push_back(&monitor);
        }
    }

    if (activeMonitors.empty()) {
        viewport.index = 1;
        viewport.originX = 0;
        viewport.originY = 0;
        viewport.width = std::max(1, fallbackWidth);
        viewport.height = std::max(1, fallbackHeight);
        return viewport;
    }

    const int clampedIndex = requestedIndex <= 1 ? 1 : 2;
    size_t activeIndex = static_cast<size_t>(clampedIndex - 1);
    if (activeIndex >= activeMonitors.size()) {
        activeIndex = activeMonitors.size() - 1;
    }

    const DisplayMonitorDescriptor* selected = activeMonitors[activeIndex];
    viewport.index = static_cast<int>(activeIndex) + 1;
    viewport.originX = selected->virtualX;
    viewport.originY = selected->virtualY;
    viewport.width = std::max(1, selected->width);
    viewport.height = std::max(1, selected->height);
    viewport.monitorId = selected->id;
    viewport.monitorName = selected->name;
    return viewport;
}

inline const DisplayMonitorDescriptor* DisplayVirtualDesktop::activeViewportMonitor(const DisplayViewport& viewport) const
{
    if (!viewport.monitorId.empty()) {
        for (const auto& monitor : monitors) {
            if (monitor.isActive() && monitor.id == viewport.monitorId) {
                return &monitor;
            }
        }
    }

    if (viewport.index > 0) {
        size_t activeIndex = 0;
        const size_t targetIndex = static_cast<size_t>(viewport.index - 1);
        for (const auto& monitor : monitors) {
            if (!monitor.isActive()) {
                continue;
            }
            if (activeIndex == targetIndex) {
                return &monitor;
            }
            ++activeIndex;
        }
    }

    return primaryMonitor();
}

} // namespace gui
} // namespace gxos
