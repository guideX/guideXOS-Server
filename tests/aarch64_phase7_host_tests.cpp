#include <stdint.h>

#include <cstdio>

#include "kernel/input_queue.h"
#include "kernel/display_input_mapper.h"

int main()
{
    using namespace kernel::input_queue;
    initialize();

    if (!push_pointer(10, 20, 0, 0, OriginVirtioHardware) ||
        !push_pointer(30, 40, 0, 0, OriginVirtioHardware) ||
        events_coalesced() != 1 || size() != 1) {
        std::fprintf(stderr, "pointer coalescing control failed\n");
        return 1;
    }
    Event event;
    if (!pop(&event) || event.x != 30 || event.y != 40 || event.origin != OriginVirtioHardware) {
        std::fprintf(stderr, "coalesced event control failed\n");
        return 2;
    }

    initialize();
    for (uint32_t i = 0; i < kCapacity; ++i) {
        if (!push_key(EventType::KeyDown, 'a' + (i & 1), OriginVirtioHardware)) {
            std::fprintf(stderr, "queue filled too early\n");
            return 3;
        }
    }
    if (push_key(EventType::KeyDown, 'z', OriginVirtioHardware) ||
        events_dropped() != 1 || high_water_mark() != kCapacity) {
        std::fprintf(stderr, "drop-newest overflow control failed\n");
        return 4;
    }
    if (push_key(static_cast<EventType>(99), 0, OriginSynthetic) || events_dropped() != 2) {
        std::fprintf(stderr, "invalid event control failed\n");
        return 5;
    }
    uint32_t count = 0;
    while (pop(&event)) ++count;
    if (count != kCapacity) {
        std::fprintf(stderr, "queue ordering/capacity control failed\n");
        return 6;
    }

    kernel::display_input::DisplayInputMapper mapper;
    mapper.configureVirtualDesktop(0, 0, 800, 600);
    mapper.setMonitor(0, 1, 0, 0, 800, 600, true);
    mapper.setCursor(400, 300);
    const auto relative = mapper.mapRelativePointer(
        kernel::display_input::PointerSourceType::VirtioInputRelative,
        INT32_MAX, INT32_MIN, 1, 0);
    if (!relative.valid || relative.virtualX != 799 || relative.virtualY != 0 || !relative.clamped) {
        std::fprintf(stderr, "signed/clipped pointer control failed\n");
        return 7;
    }
    const auto absolute = mapper.mapUnknownHeadAbsolute(
        kernel::display_input::PointerSourceType::VirtioInputAbsolute,
        32767, 32767, 0, 32767, 0, 32767, 800, 600, 0, 0);
    if (!absolute.valid || absolute.virtualX != 799 || absolute.virtualY != 599) {
        std::fprintf(stderr, "absolute pointer control failed\n");
        return 8;
    }
    std::puts("AARCH64_PHASE7_HOST_CONTROLS_PASS");
    return 0;
}
