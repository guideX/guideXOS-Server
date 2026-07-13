#pragma once

#include "kernel/core/include/kernel/types.h"

namespace gxos {
namespace gui {

static constexpr uint32_t kVirtioGpuMaxOutputs = 16u;

template <typename T, uint32_t Capacity>
struct FixedList {
    T items[Capacity]{};
    uint32_t count{0};

    bool push_back(const T& value)
    {
        if (count >= Capacity) {
            return false;
        }

        items[count++] = value;
        return true;
    }

    uint32_t size() const
    {
        return count;
    }

    bool empty() const
    {
        return count == 0u;
    }

    T& operator[](uint32_t index)
    {
        return items[index];
    }

    const T& operator[](uint32_t index) const
    {
        return items[index];
    }

    T& front()
    {
        return items[0];
    }

    const T& front() const
    {
        return items[0];
    }
};

struct DisplayRect {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    int width() const
    {
        return right > left ? right - left : 0;
    }

    int height() const
    {
        return bottom > top ? bottom - top : 0;
    }

    bool isValid() const
    {
        return width() > 0 && height() > 0;
    }
};

struct DisplayVirtualDesktop {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    int mode{0};

    int width() const
    {
        return right > left ? right - left : 0;
    }

    int height() const
    {
        return bottom > top ? bottom - top : 0;
    }
};

inline void append_char(char*& cursor, char* end, char value);
inline void append_text(char*& cursor, char* end, const char* text);
inline void append_u64(char*& cursor, char* end, uint64_t value);
inline void append_u32(char*& cursor, char* end, uint32_t value);
inline void append_int(char*& cursor, char* end, int value);
inline void append_yesno(char*& cursor, char* end, bool value);
inline void append_geometry(char*& cursor, char* end, int x, int y, int width, int height);

struct VirtioGpuScanoutState {
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    bool connectorEnabled{false};
    int preferredX{0};
    int preferredY{0};
    int preferredWidth{0};
    int preferredHeight{0};
    int assignedX{0};
    int assignedY{0};
    int assignedWidth{0};
    int assignedHeight{0};
    bool resourceBound{false};
    bool backingAttached{false};
    bool transferReady{false};
    bool presentReady{false};
    bool presentationConfirmed{false};
    bool primary{false};
    bool active{false};
    uint64_t backingVirtualAddress{0};
    uint64_t backingByteCount{0};
    uint32_t backingMemEntryCount{0};
    uint64_t patternChecksum{0};
    const char* lastCommandStatus{""};
    const char* source{"virtio-gpu"};

    bool isOperational(uint32_t deviceConfigNumScanouts) const
    {
        return scanoutId < deviceConfigNumScanouts
            && resourceId != 0u
            && backingAttached
            && resourceBound
            && transferReady
            && presentReady;
    }

    DisplayRect preferredGeometry() const
    {
        return DisplayRect{
            preferredX,
            preferredY,
            preferredX + (preferredWidth > 0 ? preferredWidth : 0),
            preferredY + (preferredHeight > 0 ? preferredHeight : 0)
        };
    }

    DisplayRect assignedGeometry() const
    {
        return DisplayRect{
            assignedX,
            assignedY,
            assignedX + (assignedWidth > 0 ? assignedWidth : 0),
            assignedY + (assignedHeight > 0 ? assignedHeight : 0)
        };
    }
};

struct DisplayMonitorDescriptor {
    uint32_t id{0};
    const char* name{""};
    const char* source{"virtio-gpu"};
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    int virtualX{0};
    int virtualY{0};
    int width{0};
    int height{0};
    bool enabled{false};
    bool operational{false};
    bool primary{false};
    bool connectorEnabled{false};
    bool resourceBound{false};
    bool backingAttached{false};
    bool transferReady{false};
    bool presentReady{false};
    bool presentationConfirmed{false};
    int preferredX{0};
    int preferredY{0};
    int preferredWidth{0};
    int preferredHeight{0};
    int assignedX{0};
    int assignedY{0};
    int assignedWidth{0};
    int assignedHeight{0};
    uint64_t backingVirtualAddress{0};
    uint64_t backingByteCount{0};
    uint32_t backingMemEntryCount{0};
    uint64_t patternChecksum{0};
    const char* lastCommandStatus{""};
};

struct DisplayViewport {
    uint32_t index{0};
    int originX{0};
    int originY{0};
    int width{0};
    int height{0};
    bool syntheticHosted{false};
    const char* source{""};
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    bool connectorEnabled{false};
    bool resourceBound{false};
    bool backingAttached{false};
    bool transferReady{false};
    bool presentReady{false};
    bool presentationConfirmed{false};
    uint32_t monitorId{0};
    const char* monitorName{""};
    int preferredX{0};
    int preferredY{0};
    int preferredWidth{0};
    int preferredHeight{0};
    int assignedX{0};
    int assignedY{0};
    int assignedWidth{0};
    int assignedHeight{0};
    uint64_t backingVirtualAddress{0};
    uint64_t backingByteCount{0};
    uint32_t backingMemEntryCount{0};
    uint64_t patternChecksum{0};
    const char* lastCommandStatus{""};

    const char* summary() const
    {
        static char buffer[512];
        char* cursor = buffer;
        char* const end = buffer + sizeof(buffer) - 1;

        append_text(cursor, end, "activeViewport=");
        append_u32(cursor, end, index);
        append_text(cursor, end, " origin=");
        append_int(cursor, end, originX);
        append_char(cursor, end, ',');
        append_int(cursor, end, originY);
        append_text(cursor, end, " size=");
        append_int(cursor, end, width);
        append_char(cursor, end, 'x');
        append_int(cursor, end, height);
        append_text(cursor, end, " synthetic=");
        append_text(cursor, end, syntheticHosted ? "true" : "false");
        if (source != nullptr && source[0] != '\0') {
            append_text(cursor, end, " source=");
            append_text(cursor, end, source);
            append_text(cursor, end, " scanout=");
            append_u32(cursor, end, scanoutId);
            append_text(cursor, end, " resource=");
            append_u32(cursor, end, resourceId);
            append_text(cursor, end, " connectorEnabled=");
            append_text(cursor, end, connectorEnabled ? "true" : "false");
            append_text(cursor, end, " resourceBound=");
            append_text(cursor, end, resourceBound ? "true" : "false");
            append_text(cursor, end, " backingAttached=");
            append_text(cursor, end, backingAttached ? "true" : "false");
            append_text(cursor, end, " transferReady=");
            append_text(cursor, end, transferReady ? "true" : "false");
            append_text(cursor, end, " presentReady=");
            append_text(cursor, end, presentReady ? "true" : "false");
            append_text(cursor, end, " confirmed=");
            append_text(cursor, end, presentationConfirmed ? "true" : "false");
            append_text(cursor, end, " preferred=");
            append_geometry(cursor, end, preferredX, preferredY, preferredWidth, preferredHeight);
            append_text(cursor, end, " assigned=");
            append_geometry(cursor, end, assignedX, assignedY, assignedWidth, assignedHeight);
        }
        if (monitorId != 0u) {
            append_text(cursor, end, " monitor=");
            append_u32(cursor, end, monitorId);
            if (monitorName != nullptr && monitorName[0] != '\0') {
                append_char(cursor, end, '(');
                append_text(cursor, end, monitorName);
                append_char(cursor, end, ')');
            }
        }
        *cursor = '\0';
        return buffer;
    }
};

struct DisplayRenderTarget {
    uint32_t targetIndex{1};
    const char* targetId{""};
    const char* source{""};
    uint32_t monitorId{0};
    const char* monitorName{""};
    uint32_t scanoutId{0};
    uint32_t resourceId{0};
    int viewportOriginX{0};
    int viewportOriginY{0};
    int width{0};
    int height{0};
    DisplayRect framebufferRect{ 0, 0, 0, 0 };
    int preferredX{0};
    int preferredY{0};
    int preferredWidth{0};
    int preferredHeight{0};
    int assignedX{0};
    int assignedY{0};
    int assignedWidth{0};
    int assignedHeight{0};
    bool primary{false};
    bool active{false};
    bool backedByHostedFramebuffer{false};
    bool backedByOutputResource{false};
    bool connectorEnabled{false};
    bool resourceBound{false};
    bool backingAttached{false};
    bool transferReady{false};
    bool presentReady{false};
    bool presentationConfirmed{false};
    bool syntheticHosted{false};
    uint64_t backingVirtualAddress{0};
    uint64_t backingByteCount{0};
    uint32_t backingMemEntryCount{0};
    uint64_t patternChecksum{0};
    const char* lastCommandStatus{""};

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

    DisplayRect preferredGeometry() const
    {
        return DisplayRect{
            preferredX,
            preferredY,
            preferredX + (preferredWidth > 0 ? preferredWidth : 0),
            preferredY + (preferredHeight > 0 ? preferredHeight : 0)
        };
    }

    DisplayRect assignedGeometry() const
    {
        return DisplayRect{
            assignedX,
            assignedY,
            assignedX + (assignedWidth > 0 ? assignedWidth : 0),
            assignedY + (assignedHeight > 0 ? assignedHeight : 0)
        };
    }
};

struct VirtioGpuOutputInventory {
    bool qemuOnly{true};
    uint32_t deviceConfigNumScanouts{0};
    uint32_t outputCount{0};
    uint32_t operationalOutputCount{0};
    uint32_t protocolConnectorEnabledCount{0};
    uint32_t presentationConfirmedCount{0};
    uint32_t primaryOutput{0};
    uint32_t targetCount{0};
    uint32_t backedTargetCount{0};
    DisplayVirtualDesktop virtualDesktop{};
    FixedList<VirtioGpuScanoutState, kVirtioGpuMaxOutputs> outputs;
    FixedList<DisplayMonitorDescriptor, kVirtioGpuMaxOutputs> monitors;
    FixedList<DisplayViewport, kVirtioGpuMaxOutputs> viewports;
    FixedList<DisplayRenderTarget, kVirtioGpuMaxOutputs> renderTargets;
};

inline void append_char(char*& cursor, char* end, char value)
{
    if (cursor < end) {
        *cursor++ = value;
    }
}

inline void append_text(char*& cursor, char* end, const char* text)
{
    if (text == nullptr) {
        return;
    }

    while (*text != '\0' && cursor < end) {
        *cursor++ = *text++;
    }
}

inline void append_u64(char*& cursor, char* end, uint64_t value)
{
    char digits[32];
    uint32_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    while (count > 0u) {
        append_char(cursor, end, digits[--count]);
    }
}

inline void append_u32(char*& cursor, char* end, uint32_t value)
{
    append_u64(cursor, end, value);
}

inline void append_int(char*& cursor, char* end, int value)
{
    if (value < 0) {
        append_char(cursor, end, '-');
        append_u64(cursor, end, static_cast<uint64_t>(-(static_cast<int64_t>(value) + 1)) + 1u);
        return;
    }

    append_u64(cursor, end, static_cast<uint64_t>(value));
}

inline void append_yesno(char*& cursor, char* end, bool value)
{
    append_text(cursor, end, value ? "yes" : "no");
}

inline void append_geometry(char*& cursor, char* end, int x, int y, int width, int height)
{
    append_int(cursor, end, x);
    append_char(cursor, end, ',');
    append_int(cursor, end, y);
    append_char(cursor, end, ' ');
    append_int(cursor, end, width);
    append_char(cursor, end, 'x');
    append_int(cursor, end, height);
}

inline const char* virtioGpuGeometrySummary(int x, int y, int width, int height)
{
    static char buffer[64];
    char* cursor = buffer;
    char* const end = buffer + sizeof(buffer) - 1;
    append_geometry(cursor, end, x, y, width, height);
    *cursor = '\0';
    return buffer;
}

inline const char* virtioGpuMonitorSummaryLine(const DisplayMonitorDescriptor& monitor)
{
    static char buffer[512];
    char* cursor = buffer;
    char* const end = buffer + sizeof(buffer) - 1;

    append_text(cursor, end, "monitor[");
    append_u32(cursor, end, monitor.id);
    append_text(cursor, end, "]: source=");
    append_text(cursor, end, monitor.source != nullptr && monitor.source[0] != '\0' ? monitor.source : "virtio-gpu");
    append_text(cursor, end, " scanout=");
    append_u32(cursor, end, monitor.scanoutId);
    append_text(cursor, end, " resource=");
    append_u32(cursor, end, monitor.resourceId);
    append_text(cursor, end, " primary=");
    append_yesno(cursor, end, monitor.primary);
    append_text(cursor, end, " enabled=");
    append_yesno(cursor, end, monitor.enabled);
    append_text(cursor, end, " operational=");
    append_yesno(cursor, end, monitor.operational || monitor.enabled);
    append_text(cursor, end, " connectorEnabled=");
    append_yesno(cursor, end, monitor.connectorEnabled);
    append_text(cursor, end, " resourceBound=");
    append_yesno(cursor, end, monitor.resourceBound);
    append_text(cursor, end, " backingAttached=");
    append_yesno(cursor, end, monitor.backingAttached);
    append_text(cursor, end, " transferReady=");
    append_yesno(cursor, end, monitor.transferReady);
    append_text(cursor, end, " presentReady=");
    append_yesno(cursor, end, monitor.presentReady);
    append_text(cursor, end, " confirmed=");
    append_yesno(cursor, end, monitor.presentationConfirmed);
    append_text(cursor, end, " preferred=");
    append_geometry(cursor, end, monitor.preferredX, monitor.preferredY, monitor.preferredWidth, monitor.preferredHeight);
    append_text(cursor, end, " assigned=");
    append_geometry(cursor, end, monitor.assignedX, monitor.assignedY, monitor.assignedWidth, monitor.assignedHeight);
    append_text(cursor, end, " virtual=");
    append_int(cursor, end, monitor.virtualX);
    append_char(cursor, end, ',');
    append_int(cursor, end, monitor.virtualY);
    *cursor = '\0';
    return buffer;
}

inline const char* virtioGpuRenderTargetSummaryLine(const DisplayRenderTarget& target)
{
    static char buffer[768];
    char* cursor = buffer;
    char* const end = buffer + sizeof(buffer) - 1;

    append_text(cursor, end, "target[");
    append_u32(cursor, end, target.targetIndex);
    append_text(cursor, end, "]: source=");
    append_text(cursor, end, target.source != nullptr && target.source[0] != '\0' ? target.source : "virtio-gpu");
    append_text(cursor, end, " monitor=");
    if (target.monitorId == 0u) {
        append_text(cursor, end, "(none)");
    } else {
        append_u32(cursor, end, target.monitorId);
        if (target.monitorName != nullptr && target.monitorName[0] != '\0') {
            append_char(cursor, end, '(');
            append_text(cursor, end, target.monitorName);
            append_char(cursor, end, ')');
        }
    }
    append_text(cursor, end, " scanout=");
    append_u32(cursor, end, target.scanoutId);
    append_text(cursor, end, " resource=");
    append_u32(cursor, end, target.resourceId);
    append_text(cursor, end, " primary=");
    append_yesno(cursor, end, target.primary);
    append_text(cursor, end, " active=");
    append_yesno(cursor, end, target.active);
    append_text(cursor, end, " backed=");
    append_yesno(cursor, end, target.backedByOutputResource || target.backedByHostedFramebuffer);
    append_text(cursor, end, " connectorEnabled=");
    append_yesno(cursor, end, target.connectorEnabled);
    append_text(cursor, end, " resourceBound=");
    append_yesno(cursor, end, target.resourceBound);
    append_text(cursor, end, " backingAttached=");
    append_yesno(cursor, end, target.backingAttached);
    append_text(cursor, end, " transferReady=");
    append_yesno(cursor, end, target.transferReady);
    append_text(cursor, end, " presentReady=");
    append_yesno(cursor, end, target.presentReady);
    append_text(cursor, end, " confirmed=");
    append_yesno(cursor, end, target.presentationConfirmed);
    append_text(cursor, end, " viewport=");
    append_geometry(cursor, end, target.viewportOriginX, target.viewportOriginY, target.width, target.height);
    append_text(cursor, end, " preferred=");
    append_geometry(cursor, end, target.preferredX, target.preferredY, target.preferredWidth, target.preferredHeight);
    append_text(cursor, end, " assigned=");
    append_geometry(cursor, end, target.assignedX, target.assignedY, target.assignedWidth, target.assignedHeight);
    *cursor = '\0';
    return buffer;
}

inline const char* virtioGpuOutputSummaryLine(
    const VirtioGpuScanoutState& output,
    const DisplayMonitorDescriptor& monitor)
{
    static char buffer[768];
    char* cursor = buffer;
    char* const end = buffer + sizeof(buffer) - 1;

    append_text(cursor, end, "output[");
    append_u32(cursor, end, output.scanoutId);
    append_text(cursor, end, "]: source=");
    append_text(cursor, end, output.source != nullptr && output.source[0] != '\0' ? output.source : "virtio-gpu");
    append_text(cursor, end, " scanout=");
    append_u32(cursor, end, output.scanoutId);
    append_text(cursor, end, " resource=");
    append_u32(cursor, end, output.resourceId);
    append_text(cursor, end, " connectorEnabled=");
    append_yesno(cursor, end, output.connectorEnabled);
    append_text(cursor, end, " resourceBound=");
    append_yesno(cursor, end, output.resourceBound);
    append_text(cursor, end, " backingAttached=");
    append_yesno(cursor, end, output.backingAttached);
    append_text(cursor, end, " transferReady=");
    append_yesno(cursor, end, output.transferReady);
    append_text(cursor, end, " presentReady=");
    append_yesno(cursor, end, output.presentReady);
    append_text(cursor, end, " confirmed=");
    append_yesno(cursor, end, output.presentationConfirmed);
    append_text(cursor, end, " operational=");
    append_yesno(cursor, end, monitor.operational);
    append_text(cursor, end, " preferred=");
    append_geometry(cursor, end, output.preferredX, output.preferredY, output.preferredWidth, output.preferredHeight);
    append_text(cursor, end, " assigned=");
    append_geometry(cursor, end, output.assignedX, output.assignedY, output.assignedWidth, output.assignedHeight);
    append_text(cursor, end, " virtual=");
    append_int(cursor, end, monitor.virtualX);
    append_char(cursor, end, ',');
    append_int(cursor, end, monitor.virtualY);
    append_text(cursor, end, " primary=");
    append_yesno(cursor, end, monitor.primary);
    append_text(cursor, end, " active=");
    append_yesno(cursor, end, monitor.enabled && monitor.width > 0 && monitor.height > 0);
    *cursor = '\0';
    return buffer;
}

inline const char* virtioGpuOutputInventorySummary(const VirtioGpuOutputInventory& inventory)
{
    static char buffer[256];
    char* cursor = buffer;
    char* const end = buffer + sizeof(buffer) - 1;

    append_text(cursor, end, "VirtioGPU outputs: configured=");
    append_u32(cursor, end, inventory.outputCount);
    append_text(cursor, end, " operational=");
    append_u32(cursor, end, inventory.operationalOutputCount);
    append_text(cursor, end, " connectorEnabled=");
    append_u32(cursor, end, inventory.protocolConnectorEnabledCount);
    append_text(cursor, end, " presentationConfirmed=");
    append_u32(cursor, end, inventory.presentationConfirmedCount);
    append_text(cursor, end, " virtualDesktop=");
    append_u32(cursor, end, static_cast<uint32_t>(inventory.virtualDesktop.width()));
    append_char(cursor, end, 'x');
    append_u32(cursor, end, static_cast<uint32_t>(inventory.virtualDesktop.height()));
    append_text(cursor, end, " targets=");
    append_u32(cursor, end, inventory.targetCount);
    append_text(cursor, end, " backed=");
    append_u32(cursor, end, inventory.backedTargetCount);
    append_text(cursor, end, " primaryOutput=");
    append_u32(cursor, end, inventory.primaryOutput);
    append_text(cursor, end, " protocolConnectorEnabledCount=");
    append_u32(cursor, end, inventory.protocolConnectorEnabledCount);
    append_text(cursor, end, " operationalOutputCount=");
    append_u32(cursor, end, inventory.operationalOutputCount);
    append_text(cursor, end, " presentationConfirmedCount=");
    append_u32(cursor, end, inventory.presentationConfirmedCount);
    *cursor = '\0';
    return buffer;
}

class VirtioGpuDisplayBackend {
public:
    static bool isQemuOnly()
    {
        return true;
    }

    static VirtioGpuOutputInventory getVirtioGpuOutputInventory(
        const FixedList<VirtioGpuScanoutState, kVirtioGpuMaxOutputs>& scanouts,
        uint32_t deviceConfigNumScanouts)
    {
        VirtioGpuOutputInventory inventory;
        inventory.deviceConfigNumScanouts = deviceConfigNumScanouts;

        FixedList<VirtioGpuScanoutState, kVirtioGpuMaxOutputs> activeScanouts;
        for (uint32_t i = 0; i < scanouts.size(); ++i) {
            const VirtioGpuScanoutState& scanout = scanouts[i];
            if (scanout.isOperational(deviceConfigNumScanouts)) {
                activeScanouts.push_back(scanout);
            }
        }

        for (uint32_t i = 1; i < activeScanouts.size(); ++i) {
            const VirtioGpuScanoutState key = activeScanouts[i];
            int32_t j = static_cast<int32_t>(i) - 1;
            while (j >= 0 && activeScanouts[static_cast<uint32_t>(j)].scanoutId > key.scanoutId) {
                activeScanouts[static_cast<uint32_t>(j + 1)] = activeScanouts[static_cast<uint32_t>(j)];
                --j;
            }
            activeScanouts[static_cast<uint32_t>(j + 1)] = key;
        }

        inventory.outputCount = activeScanouts.size();

        int virtualX = 0;
        bool havePrimary = false;
        int maxBottom = 0;

        for (uint32_t index = 0; index < activeScanouts.size(); ++index) {
            VirtioGpuScanoutState output = activeScanouts[index];
            output.active = true;
            if (!havePrimary && output.primary) {
                havePrimary = true;
                inventory.primaryOutput = output.scanoutId;
            }
            if (!havePrimary && index == 0u) {
                output.primary = true;
                havePrimary = true;
                inventory.primaryOutput = output.scanoutId;
            }

            const int assignedWidth = output.assignedWidth > 0 ? output.assignedWidth : 1;
            const int assignedHeight = output.assignedHeight > 0 ? output.assignedHeight : 1;

            DisplayMonitorDescriptor monitor{};
            monitor.id = index + 1u;
            monitor.name = output.scanoutId == 0u ? "Virtio GPU Output 0" : "Virtio GPU Output 1";
            monitor.source = output.source != nullptr ? output.source : "virtio-gpu";
            monitor.scanoutId = output.scanoutId;
            monitor.resourceId = output.resourceId;
            monitor.preferredX = output.preferredX;
            monitor.preferredY = output.preferredY;
            monitor.preferredWidth = output.preferredWidth;
            monitor.preferredHeight = output.preferredHeight;
            monitor.assignedX = output.assignedX;
            monitor.assignedY = output.assignedY;
            monitor.assignedWidth = output.assignedWidth;
            monitor.assignedHeight = output.assignedHeight;
            monitor.virtualX = virtualX;
            monitor.virtualY = 0;
            monitor.width = assignedWidth;
            monitor.height = assignedHeight;
            monitor.enabled = true;
            monitor.operational = true;
            monitor.connectorEnabled = output.connectorEnabled;
            monitor.resourceBound = output.resourceBound;
            monitor.backingAttached = output.backingAttached;
            monitor.transferReady = output.transferReady;
            monitor.presentReady = output.presentReady;
            monitor.presentationConfirmed = output.presentationConfirmed;
            monitor.primary = output.primary;
            monitor.backingVirtualAddress = output.backingVirtualAddress;
            monitor.backingByteCount = output.backingByteCount;
            monitor.backingMemEntryCount = output.backingMemEntryCount;
            monitor.patternChecksum = output.patternChecksum;
            monitor.lastCommandStatus = output.lastCommandStatus;
            inventory.monitors.push_back(monitor);

            DisplayViewport viewport{};
            viewport.index = index + 1u;
            viewport.originX = virtualX;
            viewport.originY = 0;
            viewport.width = assignedWidth;
            viewport.height = assignedHeight;
            viewport.syntheticHosted = false;
            viewport.source = monitor.source;
            viewport.scanoutId = output.scanoutId;
            viewport.resourceId = output.resourceId;
            viewport.connectorEnabled = output.connectorEnabled;
            viewport.resourceBound = output.resourceBound;
            viewport.backingAttached = output.backingAttached;
            viewport.transferReady = output.transferReady;
            viewport.presentReady = output.presentReady;
            viewport.presentationConfirmed = output.presentationConfirmed;
            viewport.monitorId = monitor.id;
            viewport.monitorName = monitor.name;
            viewport.preferredX = output.preferredX;
            viewport.preferredY = output.preferredY;
            viewport.preferredWidth = output.preferredWidth;
            viewport.preferredHeight = output.preferredHeight;
            viewport.assignedX = output.assignedX;
            viewport.assignedY = output.assignedY;
            viewport.assignedWidth = output.assignedWidth;
            viewport.assignedHeight = output.assignedHeight;
            viewport.backingVirtualAddress = output.backingVirtualAddress;
            viewport.backingByteCount = output.backingByteCount;
            viewport.backingMemEntryCount = output.backingMemEntryCount;
            viewport.patternChecksum = output.patternChecksum;
            viewport.lastCommandStatus = output.lastCommandStatus;
            inventory.viewports.push_back(viewport);

            DisplayRenderTarget target{};
            target.targetIndex = index + 1u;
            target.targetId = index == 0u ? "virtio-gpu-target-1" : "virtio-gpu-target-2";
            target.source = monitor.source;
            target.monitorId = monitor.id;
            target.monitorName = monitor.name;
            target.scanoutId = output.scanoutId;
            target.resourceId = output.resourceId;
            target.viewportOriginX = virtualX;
            target.viewportOriginY = 0;
            target.width = assignedWidth;
            target.height = assignedHeight;
            target.framebufferRect = DisplayRect{ 0, 0, assignedWidth, assignedHeight };
            target.preferredX = output.preferredX;
            target.preferredY = output.preferredY;
            target.preferredWidth = output.preferredWidth;
            target.preferredHeight = output.preferredHeight;
            target.assignedX = output.assignedX;
            target.assignedY = output.assignedY;
            target.assignedWidth = output.assignedWidth;
            target.assignedHeight = output.assignedHeight;
            target.primary = output.primary;
            target.active = true;
            target.backedByHostedFramebuffer = false;
            target.backedByOutputResource = true;
            target.connectorEnabled = output.connectorEnabled;
            target.resourceBound = output.resourceBound;
            target.backingAttached = output.backingAttached;
            target.transferReady = output.transferReady;
            target.presentReady = output.presentReady;
            target.presentationConfirmed = output.presentationConfirmed;
            target.syntheticHosted = false;
            target.backingVirtualAddress = output.backingVirtualAddress;
            target.backingByteCount = output.backingByteCount;
            target.backingMemEntryCount = output.backingMemEntryCount;
            target.patternChecksum = output.patternChecksum;
            target.lastCommandStatus = output.lastCommandStatus;
            inventory.renderTargets.push_back(target);

            if (output.connectorEnabled) {
                ++inventory.protocolConnectorEnabledCount;
            }
            if (output.presentationConfirmed) {
                ++inventory.presentationConfirmedCount;
            }

            virtualX += assignedWidth;
            if (assignedHeight > maxBottom) {
                maxBottom = assignedHeight;
            }

            inventory.outputs.push_back(output);
        }

        inventory.operationalOutputCount = inventory.monitors.size();
        inventory.targetCount = inventory.renderTargets.size();
        inventory.backedTargetCount = inventory.renderTargets.size();

        if (!havePrimary && !inventory.monitors.empty()) {
            inventory.primaryOutput = inventory.monitors.front().scanoutId;
            inventory.monitors.front().primary = true;
            inventory.renderTargets.front().primary = true;
        }

        inventory.virtualDesktop.left = 0;
        inventory.virtualDesktop.top = 0;
        inventory.virtualDesktop.right = virtualX;
        inventory.virtualDesktop.bottom = maxBottom;
        inventory.virtualDesktop.mode = inventory.monitors.size() > 1u ? 1 : 0;

        return inventory;
    }
};

} // namespace gui
} // namespace gxos
