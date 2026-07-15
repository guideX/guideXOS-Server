#include "../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_event_adapter.h"

namespace {
    using gxos::runtime::EventStatus;
    using gxos::runtime::WaitResult;

    bool expect(bool condition) {
        return condition;
    }
}

int main() {
    using namespace guidexos::nativeaot;

    EventHandle* autoEvent = createAutoResetEvent(false);
    if (!expect(autoEvent != nullptr) ||
        !expect(waitMilliseconds(autoEvent, 0) == WaitResult::TimedOut) ||
        !expect(setEvent(autoEvent) == EventStatus::Ok) ||
        !expect(waitMilliseconds(autoEvent, 0) == WaitResult::Signaled) ||
        !expect(waitMilliseconds(autoEvent, 0) == WaitResult::TimedOut) ||
        !expect(destroyEvent(autoEvent) == EventStatus::Ok)) {
        return 1;
    }

    EventHandle* manualEvent = createManualResetEvent(true);
    if (!expect(manualEvent != nullptr) ||
        !expect(waitMilliseconds(manualEvent, 0) == WaitResult::Signaled) ||
        !expect(waitMilliseconds(manualEvent, 0) == WaitResult::Signaled) ||
        !expect(resetEvent(manualEvent) == EventStatus::Ok) ||
        !expect(waitMilliseconds(manualEvent, 0) == WaitResult::TimedOut) ||
        !expect(destroyEvent(manualEvent) == EventStatus::Ok)) {
        return 2;
    }

    // This probe constructs only the inactive adapter and exercises its
    // event mapping.  It does not call any runtime startup or collector entry.
    return 0;
}
