#pragma once

#include <cstdint>

#include "../../../../../runtime/synchronization/guidexos_event.h"

namespace guidexos {
namespace nativeaot {

struct EventHandle;

// These names are runtime-internal adapter operations.  They are not part of
// the generic Server interface and intentionally do not expose host API
// constants or host handle types.
EventHandle* createAutoResetEvent(bool initiallySignaled);
EventHandle* createManualResetEvent(bool initiallySignaled);
gxos::runtime::EventStatus setEvent(EventHandle* handle);
gxos::runtime::EventStatus resetEvent(EventHandle* handle);
gxos::runtime::WaitResult waitIndefinitely(EventHandle* handle);
gxos::runtime::WaitResult waitMilliseconds(EventHandle* handle, std::int64_t milliseconds);
gxos::runtime::EventStatus destroyEvent(EventHandle* handle);

} // namespace nativeaot
} // namespace guidexos
