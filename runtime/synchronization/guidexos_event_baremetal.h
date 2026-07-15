#pragma once

#include "guidexos_event.h"

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {
namespace baremetal {

using EventCriticalEnter = void* (*)(void* context);
using EventCriticalLeave = void (*)(void* context, void* token);
using EventBlock = WaitResult (*)(void* context, Event* event, const WaitTimeout& timeout);
using EventWake = void (*)(void* context, Event* event);

// The current kernel does not yet provide these hooks.  They are deliberately
// explicit so an event cannot silently fall back to polling or to an unsafe
// interrupt-only approximation.
struct EventSchedulerHooks {
    void* context;
    EventCriticalEnter enterCritical;
    EventCriticalLeave leaveCritical;
    EventBlock block;
    EventWake wakeOne;
    EventWake wakeAll;
};

void installEventSchedulerHooks(const EventSchedulerHooks* hooks);
bool eventSchedulerHooksAvailable();

} // namespace baremetal
} // namespace runtime
} // namespace gxos

#endif

