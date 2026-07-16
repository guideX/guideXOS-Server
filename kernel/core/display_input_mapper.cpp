// Backend-independent virtual-desktop pointer mapping.

#include "include/kernel/display_input_mapper.h"

namespace kernel {
namespace display_input {

static void clear_bytes(void* destination, uint32_t length)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (uint32_t i = 0; i < length; ++i) bytes[i] = 0;
}

DisplayPointerEvent::DisplayPointerEvent()
    : sequence(0), sourceType(PointerSourceType::Unknown), sourceHead(-1),
      sourceMonitor(-1), coordinateMode(PointerCoordinateMode::Unknown),
      rawX(0), rawY(0), rawMinX(0), rawMaxX(0), rawMinY(0), rawMaxY(0),
      localX(0), localY(0), relativeDx(0), relativeDy(0), outputWidth(0),
      outputHeight(0), virtualX(0), virtualY(0), buttonMask(0), wheelDelta(0),
      sourceHeadKnown(false), sourceMonitorKnown(false), fallbackUsed(false),
      clamped(false), valid(false), mappingReason{}
{
}

DisplayInputMapper::DisplayInputMapper()
    : m_monitors{}, m_monitorCount(0), m_left(0), m_top(0), m_right(0),
      m_bottom(0), m_cursorX(0), m_cursorY(0), m_lastActiveMonitorId(-1),
      m_sequence(0), m_lastEvent(), m_counters()
{
    reset();
}

void DisplayInputMapper::copyReason(char* destination, const char* source)
{
    if (destination == nullptr) return;
    uint32_t index = 0;
    if (source != nullptr) {
        while (source[index] != '\0' && index < 63u) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

void DisplayInputMapper::reset()
{
    clear_bytes(m_monitors, sizeof(m_monitors));
    m_monitorCount = 0;
    m_left = 0;
    m_top = 0;
    m_right = 0;
    m_bottom = 0;
    m_cursorX = 0;
    m_cursorY = 0;
    m_lastActiveMonitorId = -1;
    m_sequence = 0;
    m_lastEvent = DisplayPointerEvent();
    m_counters = DisplayInputMapperCounters();
}

void DisplayInputMapper::configureVirtualDesktop(int32_t left, int32_t top,
                                                  int32_t right, int32_t bottom)
{
    m_left = left;
    m_top = top;
    m_right = right > left ? right : left;
    m_bottom = bottom > top ? bottom : top;
    bool ignored = false;
    clampVirtualPoint(m_cursorX, m_cursorY, ignored);
}

void DisplayInputMapper::setCursor(int32_t x, int32_t y)
{
    bool clamped = false;
    m_cursorX = x;
    m_cursorY = y;
    clampVirtualPoint(m_cursorX, m_cursorY, clamped);
}

bool DisplayInputMapper::setMonitor(uint8_t slot, int32_t id,
                                     int32_t virtualX, int32_t virtualY,
                                     int32_t width, int32_t height,
                                     bool primary, bool enabled,
                                     int32_t assignedX, int32_t assignedY,
                                     int32_t assignedWidth,
                                     int32_t assignedHeight)
{
    if (slot >= kDisplayInputMaxMonitors || width <= 0 || height <= 0) {
        return false;
    }

    DisplayInputMonitor& monitor = m_monitors[slot];
    monitor.id = id;
    monitor.virtualX = virtualX;
    monitor.virtualY = virtualY;
    monitor.width = width;
    monitor.height = height;
    monitor.assignedX = assignedX;
    monitor.assignedY = assignedY;
    monitor.assignedWidth = assignedWidth > 0 ? assignedWidth : width;
    monitor.assignedHeight = assignedHeight > 0 ? assignedHeight : height;
    monitor.primary = primary;
    monitor.enabled = enabled;
    if (slot >= m_monitorCount) m_monitorCount = static_cast<uint8_t>(slot + 1u);
    recomputeBounds();
    return true;
}

void DisplayInputMapper::recomputeBounds()
{
    bool haveBounds = false;
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        const DisplayInputMonitor& monitor = m_monitors[i];
        if (!monitor.isActive()) continue;
        const int32_t right = monitor.virtualX + monitor.width;
        const int32_t bottom = monitor.virtualY + monitor.height;
        if (!haveBounds) {
            m_left = monitor.virtualX;
            m_top = monitor.virtualY;
            m_right = right;
            m_bottom = bottom;
            haveBounds = true;
        } else {
            if (monitor.virtualX < m_left) m_left = monitor.virtualX;
            if (monitor.virtualY < m_top) m_top = monitor.virtualY;
            if (right > m_right) m_right = right;
            if (bottom > m_bottom) m_bottom = bottom;
        }
    }
    if (!haveBounds && (m_right < m_left || m_bottom < m_top)) {
        m_left = m_top = m_right = m_bottom = 0;
    }
}

int DisplayInputMapper::primaryMonitorSlot() const
{
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        if (m_monitors[i].isActive() && m_monitors[i].primary) return i;
    }
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        if (m_monitors[i].isActive()) return i;
    }
    return -1;
}

int DisplayInputMapper::monitorFromPoint(int32_t x, int32_t y) const
{
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        if (m_monitors[i].containsVirtualPoint(x, y)) return i;
    }
    return -1;
}

int DisplayInputMapper::monitorFromRect(int32_t left, int32_t top,
                                        int32_t right, int32_t bottom) const
{
    int best = -1;
    int64_t bestArea = 0;
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        const DisplayInputMonitor& monitor = m_monitors[i];
        if (!monitor.isActive()) continue;
        const int32_t overlapLeft = left > monitor.virtualX ? left : monitor.virtualX;
        const int32_t overlapTop = top > monitor.virtualY ? top : monitor.virtualY;
        const int32_t monitorRight = monitor.virtualX + monitor.width;
        const int32_t monitorBottom = monitor.virtualY + monitor.height;
        const int32_t overlapRight = right < monitorRight ? right : monitorRight;
        const int32_t overlapBottom = bottom < monitorBottom ? bottom : monitorBottom;
        if (overlapRight <= overlapLeft || overlapBottom <= overlapTop) continue;
        const int64_t area = static_cast<int64_t>(overlapRight - overlapLeft) *
                             static_cast<int64_t>(overlapBottom - overlapTop);
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    if (best >= 0) return best;
    const int centerX = left + (right - left) / 2;
    const int centerY = top + (bottom - top) / 2;
    const int centerMonitor = monitorFromPoint(centerX, centerY);
    return centerMonitor >= 0 ? centerMonitor : primaryMonitorSlot();
}

int DisplayInputMapper::monitorSlotFromId(int32_t monitorId) const
{
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        if (m_monitors[i].isActive() && m_monitors[i].id == monitorId) return i;
    }
    return -1;
}

const DisplayInputMonitor* DisplayInputMapper::monitorAtSlot(uint8_t slot) const
{
    if (slot >= m_monitorCount || !m_monitors[slot].isActive()) return nullptr;
    return &m_monitors[slot];
}

const DisplayInputMonitor* DisplayInputMapper::monitorById(int32_t monitorId) const
{
    const int slot = monitorSlotFromId(monitorId);
    return slot >= 0 ? &m_monitors[slot] : nullptr;
}

void DisplayInputMapper::clampVirtualPoint(int32_t& x, int32_t& y,
                                            bool& clamped) const
{
    if (monitorFromPoint(x, y) >= 0) return;

    // The virtual bounds are a bounding box. Mixed-height Extend can leave
    // holes in that box, so clamp to the nearest point in the union of active
    // monitor rectangles.
    bool found = false;
    int64_t bestDistance = 0;
    int32_t bestX = x;
    int32_t bestY = y;
    for (uint8_t i = 0; i < m_monitorCount; ++i) {
        const DisplayInputMonitor& monitor = m_monitors[i];
        if (!monitor.isActive()) continue;
        const int32_t monitorRight = monitor.virtualX + monitor.width;
        const int32_t monitorBottom = monitor.virtualY + monitor.height;
        const int32_t candidateX = x < monitor.virtualX ? monitor.virtualX
            : (x >= monitorRight ? monitorRight - 1 : x);
        const int32_t candidateY = y < monitor.virtualY ? monitor.virtualY
            : (y >= monitorBottom ? monitorBottom - 1 : y);
        const int64_t dx = static_cast<int64_t>(x) - candidateX;
        const int64_t dy = static_cast<int64_t>(y) - candidateY;
        const int64_t distance = dx * dx + dy * dy;
        if (!found || distance < bestDistance) {
            found = true;
            bestDistance = distance;
            bestX = candidateX;
            bestY = candidateY;
        }
    }
    if (found) {
        if (x != bestX || y != bestY) clamped = true;
        x = bestX;
        y = bestY;
    }
}

int32_t DisplayInputMapper::clampInt32(int32_t value, int32_t low,
                                        int32_t high, bool& clamped)
{
    if (high < low) {
        clamped = true;
        return low;
    }
    if (value < low) { clamped = true; return low; }
    if (value > high) { clamped = true; return high; }
    return value;
}

int32_t DisplayInputMapper::scaleAbsolute(int32_t raw, int32_t rawMin,
                                          int32_t rawMax, int32_t outputSize,
                                          int32_t destinationSize, bool& valid,
                                          bool& clamped)
{
    if (rawMax <= rawMin || outputSize <= 0 || destinationSize <= 0) {
        valid = false;
        return 0;
    }
    if (raw < rawMin) { raw = rawMin; clamped = true; }
    if (raw > rawMax) { raw = rawMax; clamped = true; }

    // Use signed 64-bit intermediates. The range is validated first and the
    // destination is positive, so this cannot wrap during normal HID ranges.
    const int64_t numerator = (static_cast<int64_t>(raw) -
                               static_cast<int64_t>(rawMin)) *
                              static_cast<int64_t>(destinationSize - 1);
    const int64_t denominator = static_cast<int64_t>(rawMax) -
                                static_cast<int64_t>(rawMin);
    int64_t scaled = numerator / denominator;
    if (scaled < 0) { scaled = 0; clamped = true; }
    if (scaled >= destinationSize) {
        scaled = destinationSize - 1;
        clamped = true;
    }
    valid = true;
    (void)outputSize; // kept in the API for diagnostic raw-output reporting
    return static_cast<int32_t>(scaled);
}

DisplayPointerEvent DisplayInputMapper::beginEvent(PointerSourceType source,
                                                    PointerCoordinateMode mode,
                                                    int32_t sourceHead,
                                                    uint8_t buttons,
                                                    int16_t wheelDelta)
{
    ++m_counters.eventsSeen;
    DisplayPointerEvent event;
    event.sequence = ++m_sequence;
    event.sourceType = source;
    event.coordinateMode = mode;
    event.sourceHead = sourceHead;
    event.sourceHeadKnown = sourceHead >= 0;
    event.buttonMask = buttons;
    event.wheelDelta = wheelDelta;
    return event;
}

DisplayPointerEvent DisplayInputMapper::finishAbsoluteEvent(
    DisplayPointerEvent event, int monitorSlot, int32_t localX,
    int32_t localY, bool fallbackUsed, const char* reason)
{
    event.fallbackUsed = fallbackUsed;
    event.localX = localX;
    event.localY = localY;
    if (monitorSlot < 0 || monitorSlot >= m_monitorCount ||
        !m_monitors[monitorSlot].isActive()) {
        event.valid = false;
        copyReason(event.mappingReason, "no-active-monitor");
        ++m_counters.invalidEvents;
        m_lastEvent = event;
        return event;
    }

    const DisplayInputMonitor& monitor = m_monitors[monitorSlot];
    bool clamped = event.clamped;
    event.localX = clampInt32(localX, 0, monitor.width - 1, clamped);
    event.localY = clampInt32(localY, 0, monitor.height - 1, clamped);
    event.virtualX = monitor.virtualX + event.localX;
    event.virtualY = monitor.virtualY + event.localY;
    clampVirtualPoint(event.virtualX, event.virtualY, clamped);
    event.clamped = clamped;
    event.sourceMonitor = monitor.id;
    event.sourceMonitorKnown = true;
    event.valid = true;
    copyReason(event.mappingReason, reason);
    m_cursorX = event.virtualX;
    m_cursorY = event.virtualY;
    m_lastActiveMonitorId = monitor.id;
    ++m_counters.validEvents;
    if (clamped) ++m_counters.clampedEvents;
    m_lastEvent = event;
    return event;
}

DisplayPointerEvent DisplayInputMapper::mapRelativePointer(
    PointerSourceType source, int32_t dx, int32_t dy, uint8_t buttons,
    int16_t wheelDelta)
{
    DisplayPointerEvent event = beginEvent(source, PointerCoordinateMode::Relative,
                                            -1, buttons, wheelDelta);
    event.relativeDx = dx;
    event.relativeDy = dy;
    event.rawX = dx;
    event.rawY = dy;
    bool clamped = false;
    int64_t nextX = static_cast<int64_t>(m_cursorX) + dx;
    int64_t nextY = static_cast<int64_t>(m_cursorY) + dy;
    int32_t x = nextX < -2147483648LL ? -2147483648LL :
                nextX > 2147483647LL ? 2147483647LL : static_cast<int32_t>(nextX);
    int32_t y = nextY < -2147483648LL ? -2147483648LL :
                nextY > 2147483647LL ? 2147483647LL : static_cast<int32_t>(nextY);
    clampVirtualPoint(x, y, clamped);
    event.virtualX = x;
    event.virtualY = y;
    event.clamped = clamped;
    event.valid = (m_right > m_left && m_bottom > m_top);
    const int monitorSlot = monitorFromPoint(x, y);
    if (monitorSlot >= 0) {
        event.sourceMonitor = m_monitors[monitorSlot].id;
        event.sourceMonitorKnown = true;
        event.localX = x - m_monitors[monitorSlot].virtualX;
        event.localY = y - m_monitors[monitorSlot].virtualY;
        m_lastActiveMonitorId = event.sourceMonitor;
        copyReason(event.mappingReason, clamped ? "relative-global-clamped" : "relative-global");
    } else {
        copyReason(event.mappingReason, "relative-global-monitor-fallback");
    }
    m_cursorX = x;
    m_cursorY = y;
    ++m_counters.relativeEvents;
    if (event.valid) ++m_counters.validEvents;
    else ++m_counters.invalidEvents;
    if (clamped) ++m_counters.clampedEvents;
    m_lastEvent = event;
    return event;
}

DisplayPointerEvent DisplayInputMapper::mapHeadLocalToVirtual(
    PointerSourceType source, int32_t sourceHead, int32_t localX,
    int32_t localY, int32_t outputWidth, int32_t outputHeight,
    uint8_t buttons, int16_t wheelDelta)
{
    DisplayPointerEvent event = beginEvent(source, PointerCoordinateMode::HeadLocalAbsolute,
                                            sourceHead, buttons, wheelDelta);
    event.rawX = localX;
    event.rawY = localY;
    event.outputWidth = outputWidth;
    event.outputHeight = outputHeight;
    const int monitorSlot = sourceHead >= 0 && sourceHead < m_monitorCount
        ? sourceHead : -1;
    if (monitorSlot < 0) {
        event.valid = false;
        copyReason(event.mappingReason, "source-head-no-monitor");
        ++m_counters.invalidEvents;
        m_lastEvent = event;
        return event;
    }

    const DisplayInputMonitor& monitor = m_monitors[monitorSlot];
    bool clamped = false;
    int32_t mappedX = localX;
    int32_t mappedY = localY;
    if (outputWidth > 0 && outputWidth != monitor.width) {
        bool valid = true;
        mappedX = scaleAbsolute(localX, 0, outputWidth - 1, outputWidth,
                                monitor.width, valid, clamped);
    }
    if (outputHeight > 0 && outputHeight != monitor.height) {
        bool valid = true;
        mappedY = scaleAbsolute(localY, 0, outputHeight - 1, outputHeight,
                                monitor.height, valid, clamped);
    }
    event.clamped = clamped;
    ++m_counters.headAbsoluteEvents;
    return finishAbsoluteEvent(event, monitorSlot, mappedX, mappedY, false,
                                "head-local-to-monitor-virtual");
}

DisplayPointerEvent DisplayInputMapper::mapNormalizedAbsolute(
    PointerSourceType source, int32_t sourceHead, int32_t rawX,
    int32_t rawY, int32_t rawMinX, int32_t rawMaxX, int32_t rawMinY,
    int32_t rawMaxY, int32_t outputWidth, int32_t outputHeight,
    uint8_t buttons, int16_t wheelDelta)
{
    DisplayPointerEvent event = beginEvent(source, PointerCoordinateMode::NormalizedAbsolute,
                                            sourceHead, buttons, wheelDelta);
    event.rawX = rawX;
    event.rawY = rawY;
    event.rawMinX = rawMinX;
    event.rawMaxX = rawMaxX;
    event.rawMinY = rawMinY;
    event.rawMaxY = rawMaxY;
    event.outputWidth = outputWidth;
    event.outputHeight = outputHeight;
    const int monitorSlot = sourceHead >= 0 && sourceHead < m_monitorCount
        ? sourceHead : -1;
    if (monitorSlot < 0) {
        event.valid = false;
        copyReason(event.mappingReason, "source-head-no-monitor");
        ++m_counters.invalidEvents;
        m_lastEvent = event;
        return event;
    }
    const DisplayInputMonitor& monitor = m_monitors[monitorSlot];
    bool validX = true;
    bool validY = true;
    bool clamped = false;
    const int32_t localX = scaleAbsolute(rawX, rawMinX, rawMaxX,
                                         outputWidth, monitor.width,
                                         validX, clamped);
    const int32_t localY = scaleAbsolute(rawY, rawMinY, rawMaxY,
                                         outputHeight, monitor.height,
                                         validY, clamped);
    event.clamped = clamped;
    ++m_counters.normalizedAbsoluteEvents;
    if (!validX || !validY) {
        event.valid = false;
        copyReason(event.mappingReason, "invalid-absolute-range");
        ++m_counters.invalidEvents;
        m_lastEvent = event;
        return event;
    }
    return finishAbsoluteEvent(event, monitorSlot, localX, localY, false,
                                "normalized-absolute-to-monitor-virtual");
}

DisplayPointerEvent DisplayInputMapper::mapUnknownHeadAbsolute(
    PointerSourceType source, int32_t rawX, int32_t rawY,
    int32_t rawMinX, int32_t rawMaxX, int32_t rawMinY,
    int32_t rawMaxY, int32_t outputWidth, int32_t outputHeight,
    uint8_t buttons, int16_t wheelDelta)
{
    int monitorSlot = monitorSlotFromId(m_lastActiveMonitorId);
    bool fallback = false;
    const char* reason = "unknown-head-last-active-monitor";
    if (monitorSlot < 0) {
        monitorSlot = primaryMonitorSlot();
        fallback = true;
        reason = "unknown-head-primary-monitor-fallback";
    }

    DisplayPointerEvent event = beginEvent(source, PointerCoordinateMode::NormalizedAbsolute,
                                            -1, buttons, wheelDelta);
    event.rawX = rawX;
    event.rawY = rawY;
    event.rawMinX = rawMinX;
    event.rawMaxX = rawMaxX;
    event.rawMinY = rawMinY;
    event.rawMaxY = rawMaxY;
    event.outputWidth = outputWidth;
    event.outputHeight = outputHeight;
    event.sourceHeadKnown = false;
    if (fallback) ++m_counters.unknownHeadFallbacks;
    if (monitorSlot < 0) {
        event.valid = false;
        copyReason(event.mappingReason, "unknown-head-no-active-monitor");
        ++m_counters.invalidEvents;
        m_lastEvent = event;
        return event;
    }

    const DisplayInputMonitor& monitor = m_monitors[monitorSlot];
    bool validX = true;
    bool validY = true;
    bool clamped = false;
    const int32_t localX = scaleAbsolute(rawX, rawMinX, rawMaxX,
                                         outputWidth, monitor.width,
                                         validX, clamped);
    const int32_t localY = scaleAbsolute(rawY, rawMinY, rawMaxY,
                                         outputHeight, monitor.height,
                                         validY, clamped);
    event.clamped = clamped;
    ++m_counters.normalizedAbsoluteEvents;
    if (!validX || !validY) {
        event.valid = false;
        copyReason(event.mappingReason, "invalid-absolute-range");
        ++m_counters.invalidEvents;
        m_lastEvent = event;
        return event;
    }
    return finishAbsoluteEvent(event, monitorSlot, localX, localY, fallback, reason);
}

const char* pointerSourceName(PointerSourceType source)
{
    switch (source) {
        case PointerSourceType::Ps2Relative: return "ps2";
        case PointerSourceType::UsbRelative: return "usb-relative";
        case PointerSourceType::UsbTabletAbsolute: return "usb-tablet";
        case PointerSourceType::VirtioInputRelative: return "virtio-input-relative";
        case PointerSourceType::VirtioInputAbsolute: return "virtio-input-absolute";
        case PointerSourceType::PlatformRelative: return "platform-relative";
        case PointerSourceType::PlatformAbsolute: return "platform-absolute";
        default: return "unknown";
    }
}

const char* pointerCoordinateModeName(PointerCoordinateMode mode)
{
    switch (mode) {
        case PointerCoordinateMode::Relative: return "relative";
        case PointerCoordinateMode::HeadLocalAbsolute: return "head-local-absolute";
        case PointerCoordinateMode::NormalizedAbsolute: return "normalized-absolute";
        default: return "unknown";
    }
}

} // namespace display_input
} // namespace kernel
