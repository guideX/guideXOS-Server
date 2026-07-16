#pragma once

// Inactive NativeAOT platform-thread mapping probe.  This file is intentionally
// kept inside the runtime-pack adapter; the generic Server API has no knowledge
// of this type or its eventual runtime consumer.

#include <cstdint>

#include "../../../../../runtime/synchronization/guidexos_event.h"
#include "../../../../../runtime/thread/guidexos_native_thread.h"

namespace guidexos {
namespace nativeaot {

struct HelperThreadProbe;

HelperThreadProbe* createHelperThreadProbe(void* runtimeContext);
gxos::runtime::EventStatus startHelperThreadProbe(HelperThreadProbe* probe);
gxos::runtime::WaitResult joinHelperThreadProbe(
    HelperThreadProbe* probe,
    const gxos::runtime::WaitTimeout& timeout,
    std::uintptr_t* result);
gxos::runtime::ThreadResult destroyHelperThreadProbe(HelperThreadProbe* probe);

} // namespace nativeaot
} // namespace guidexos

