#pragma once

#include "guidexos_event.h"
#include "guidexos_scheduler_wait.h"

#if defined(GXOS_BARE_METAL)

namespace gxos {
namespace runtime {
namespace baremetal {

// Compatibility aliases for callers that used the previous experimental
// installation point.  The implementation is now the generic scheduler wait
// contract; no event-specific scheduler state exists here.
using EventSchedulerHooks = scheduler_wait::SchedulerWaitHooks;

inline void installEventSchedulerHooks(const EventSchedulerHooks* hooks) {
    scheduler_wait::installSchedulerWaitHooks(hooks);
}

inline bool eventSchedulerHooksAvailable() {
    return scheduler_wait::schedulerWaitAvailable();
}

} // namespace baremetal
} // namespace runtime
} // namespace gxos

#endif
