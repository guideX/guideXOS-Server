#define GXOS_BARE_METAL 1

#include "runtime/synchronization/guidexos_event.h"
#include "runtime/synchronization/guidexos_event_baremetal.h"

extern "C" int guidexos_event_baremetal_compile_probe() {
    gxos::runtime::Event event(gxos::runtime::EventMode::AutoReset, false);
    if (event.wait(gxos::runtime::WaitTimeout::zero()) != gxos::runtime::WaitResult::TimedOut) {
        return 1;
    }
    if (event.signal() != gxos::runtime::EventStatus::Ok) {
        return 2;
    }
    if (event.wait(gxos::runtime::WaitTimeout::zero()) != gxos::runtime::WaitResult::Signaled) {
        return 3;
    }
    if (event.wait() != gxos::runtime::WaitResult::Invalid) {
        return 4;
    }
    return event.reset() == gxos::runtime::EventStatus::Ok ? 0 : 5;
}

