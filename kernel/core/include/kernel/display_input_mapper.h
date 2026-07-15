//
// Backend-independent virtual-desktop pointer mapping.
//
// This layer knows only about display geometry and pointer event semantics.
// It deliberately has no dependency on virtio-gpu command submission, QMP,
// cursor queues, or a particular input transport.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DISPLAY_INPUT_MAPPER_H
#define KERNEL_DISPLAY_INPUT_MAPPER_H

#include "kernel/types.h"

namespace kernel {
namespace display_input {

static const uint8_t kDisplayInputMaxMonitors = 16;

enum class PointerSourceType : uint8_t {
    Unknown = 0,
    Ps2Relative,
    UsbRelative,
    UsbTabletAbsolute,
    VirtioInputRelative,
    VirtioInputAbsolute,
    PlatformRelative,
    PlatformAbsolute,
};

enum class PointerCoordinateMode : uint8_t {
    Unknown = 0,
    Relative,
    HeadLocalAbsolute,
    NormalizedAbsolute,
};

struct DisplayInputMonitor {
    int32_t id;
    int32_t virtualX;
    int32_t virtualY;
    int32_t width;
    int32_t height;
    int32_t assignedX;
    int32_t assignedY;
    int32_t assignedWidth;
    int32_t assignedHeight;
    bool primary;
    bool enabled;

    DisplayInputMonitor()
        : id(0), virtualX(0), virtualY(0), width(0), height(0),
          assignedX(0), assignedY(0), assignedWidth(0), assignedHeight(0),
          primary(false), enabled(false) {}

    bool isActive() const
    {
        return enabled && width > 0 && height > 0;
    }

    bool containsVirtualPoint(int32_t x, int32_t y) const
    {
        return isActive() && x >= virtualX && x < virtualX + width &&
               y >= virtualY && y < virtualY + height;
    }
};

struct DisplayInputMapperCounters {
    uint32_t eventsSeen;
    uint32_t validEvents;
    uint32_t invalidEvents;
    uint32_t relativeEvents;
    uint32_t headAbsoluteEvents;
    uint32_t normalizedAbsoluteEvents;
    uint32_t unknownHeadFallbacks;
    uint32_t clampedEvents;

    DisplayInputMapperCounters()
        : eventsSeen(0), validEvents(0), invalidEvents(0), relativeEvents(0),
          headAbsoluteEvents(0), normalizedAbsoluteEvents(0),
          unknownHeadFallbacks(0), clampedEvents(0) {}
};

struct DisplayPointerEvent {
    uint32_t sequence;
    PointerSourceType sourceType;
    int32_t sourceHead;
    int32_t sourceMonitor;
    PointerCoordinateMode coordinateMode;

    int32_t rawX;
    int32_t rawY;
    int32_t rawMinX;
    int32_t rawMaxX;
    int32_t rawMinY;
    int32_t rawMaxY;
    int32_t localX;
    int32_t localY;
    int32_t relativeDx;
    int32_t relativeDy;
    int32_t outputWidth;
    int32_t outputHeight;
    int32_t virtualX;
    int32_t virtualY;
    uint8_t buttonMask;
    int16_t wheelDelta;
    bool sourceHeadKnown;
    bool sourceMonitorKnown;
    bool fallbackUsed;
    bool clamped;
    bool valid;
    char mappingReason[64];

    DisplayPointerEvent();
};

class DisplayInputMapper {
public:
    DisplayInputMapper();

    void reset();
    void configureVirtualDesktop(int32_t left, int32_t top,
                                 int32_t right, int32_t bottom);
    bool setMonitor(uint8_t slot, int32_t id, int32_t virtualX,
                    int32_t virtualY, int32_t width, int32_t height,
                    bool primary, bool enabled = true,
                    int32_t assignedX = 0, int32_t assignedY = 0,
                    int32_t assignedWidth = 0, int32_t assignedHeight = 0);

    // Relative devices update one global cursor. The cursor is never reset
    // when it crosses a monitor boundary.
    DisplayPointerEvent mapRelativePointer(PointerSourceType source,
                                            int32_t dx, int32_t dy,
                                            uint8_t buttons,
                                            int16_t wheelDelta = 0);

    // Absolute coordinates whose range is local to a known source head.
    DisplayPointerEvent mapHeadLocalToVirtual(
        PointerSourceType source, int32_t sourceHead, int32_t localX,
        int32_t localY, int32_t outputWidth, int32_t outputHeight,
        uint8_t buttons, int16_t wheelDelta = 0);

    // Absolute coordinates in an arbitrary device range. The range is
    // validated before overflow-safe scaling to the selected monitor.
    DisplayPointerEvent mapNormalizedAbsolute(
        PointerSourceType source, int32_t sourceHead, int32_t rawX,
        int32_t rawY, int32_t rawMinX, int32_t rawMaxX, int32_t rawMinY,
        int32_t rawMaxY, int32_t outputWidth, int32_t outputHeight,
        uint8_t buttons, int16_t wheelDelta = 0);

    // Absolute input without head identity uses the last active monitor,
    // then the primary monitor. It never guesses from a local raw range.
    DisplayPointerEvent mapUnknownHeadAbsolute(
        PointerSourceType source, int32_t rawX, int32_t rawY,
        int32_t rawMinX, int32_t rawMaxX, int32_t rawMinY,
        int32_t rawMaxY, int32_t outputWidth, int32_t outputHeight,
        uint8_t buttons, int16_t wheelDelta = 0);

    int32_t cursorX() const { return m_cursorX; }
    int32_t cursorY() const { return m_cursorY; }
    void setCursor(int32_t x, int32_t y);
    int32_t virtualLeft() const { return m_left; }
    int32_t virtualTop() const { return m_top; }
    int32_t virtualRight() const { return m_right; }
    int32_t virtualBottom() const { return m_bottom; }
    int32_t virtualWidth() const { return m_right > m_left ? m_right - m_left : 0; }
    int32_t virtualHeight() const { return m_bottom > m_top ? m_bottom - m_top : 0; }

    int monitorFromPoint(int32_t x, int32_t y) const;
    int monitorFromRect(int32_t left, int32_t top,
                        int32_t right, int32_t bottom) const;
    int monitorSlotFromId(int32_t monitorId) const;
    const DisplayInputMonitor* monitorAtSlot(uint8_t slot) const;
    const DisplayInputMonitor* monitorById(int32_t monitorId) const;
    int32_t lastActiveMonitorId() const { return m_lastActiveMonitorId; }
    const DisplayPointerEvent& lastEvent() const { return m_lastEvent; }
    const DisplayInputMapperCounters& counters() const { return m_counters; }

private:
    DisplayInputMonitor m_monitors[kDisplayInputMaxMonitors];
    uint8_t m_monitorCount;
    int32_t m_left;
    int32_t m_top;
    int32_t m_right;
    int32_t m_bottom;
    int32_t m_cursorX;
    int32_t m_cursorY;
    int32_t m_lastActiveMonitorId;
    uint32_t m_sequence;
    DisplayPointerEvent m_lastEvent;
    DisplayInputMapperCounters m_counters;

    int primaryMonitorSlot() const;
    void recomputeBounds();
    void clampVirtualPoint(int32_t& x, int32_t& y, bool& clamped) const;
    DisplayPointerEvent beginEvent(PointerSourceType source,
                                   PointerCoordinateMode mode,
                                   int32_t sourceHead, uint8_t buttons,
                                   int16_t wheelDelta);
    DisplayPointerEvent finishAbsoluteEvent(DisplayPointerEvent event,
                                            int monitorSlot, int32_t localX,
                                            int32_t localY, bool fallbackUsed,
                                            const char* reason);
    static int32_t clampInt32(int32_t value, int32_t low, int32_t high,
                              bool& clamped);
    static int32_t scaleAbsolute(int32_t raw, int32_t rawMin,
                                 int32_t rawMax, int32_t outputSize,
                                 int32_t destinationSize, bool& valid,
                                 bool& clamped);
    static void copyReason(char* destination, const char* source);
};

const char* pointerSourceName(PointerSourceType source);
const char* pointerCoordinateModeName(PointerCoordinateMode mode);

} // namespace display_input
} // namespace kernel

#endif // KERNEL_DISPLAY_INPUT_MAPPER_H
