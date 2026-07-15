#include "guidexos_nativeaot_event_adapter.h"

#include <new>

namespace guidexos {
namespace nativeaot {

struct EventHandle {
    explicit EventHandle(gxos::runtime::EventMode mode, bool initiallySignaled)
        : event(mode, initiallySignaled) {
    }

    gxos::runtime::Event event;
};

namespace {
    EventHandle* createEvent(gxos::runtime::EventMode mode, bool initiallySignaled) {
        EventHandle* handle = new (std::nothrow) EventHandle(mode, initiallySignaled);
        if (handle == nullptr || !handle->event.isInitialized()) {
            delete handle;
            return nullptr;
        }
        return handle;
    }
}

EventHandle* createAutoResetEvent(bool initiallySignaled) {
    return createEvent(gxos::runtime::EventMode::AutoReset, initiallySignaled);
}

EventHandle* createManualResetEvent(bool initiallySignaled) {
    return createEvent(gxos::runtime::EventMode::ManualReset, initiallySignaled);
}

gxos::runtime::EventStatus setEvent(EventHandle* handle) {
    return handle == nullptr ? gxos::runtime::EventStatus::Invalid : handle->event.signal();
}

gxos::runtime::EventStatus resetEvent(EventHandle* handle) {
    return handle == nullptr ? gxos::runtime::EventStatus::Invalid : handle->event.reset();
}

gxos::runtime::WaitResult waitIndefinitely(EventHandle* handle) {
    return handle == nullptr
        ? gxos::runtime::WaitResult::Invalid
        : handle->event.wait();
}

gxos::runtime::WaitResult waitMilliseconds(EventHandle* handle, std::int64_t milliseconds) {
    if (handle == nullptr) {
        return gxos::runtime::WaitResult::Invalid;
    }
    return handle->event.wait(gxos::runtime::WaitTimeout::signedMilliseconds(milliseconds));
}

gxos::runtime::EventStatus destroyEvent(EventHandle* handle) {
    if (handle == nullptr) {
        return gxos::runtime::EventStatus::Invalid;
    }
    const gxos::runtime::EventStatus status = handle->event.close();
    delete handle;
    return status;
}

} // namespace nativeaot
} // namespace guidexos

