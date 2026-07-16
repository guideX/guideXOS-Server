#include "guidexos_nativeaot_thread_adapter.h"

#include <new>

namespace guidexos {
namespace nativeaot {

struct HelperThreadProbe {
    explicit HelperThreadProbe(void* value)
        : start(gxos::runtime::EventMode::AutoReset, false),
          runtimeContext(value),
          thread{},
          joined(false) {
    }

    gxos::runtime::Event start;
    void* runtimeContext;
    gxos::runtime::ThreadHandle thread;
    bool joined;
};

namespace {
    // This is deliberately a plain native entry.  It neither attaches to a
    // managed thread store nor invokes a collector or finalization routine.
    std::uintptr_t helperThreadEntry(void* raw) {
        HelperThreadProbe* probe = static_cast<HelperThreadProbe*>(raw);
        if (probe->start.wait(gxos::runtime::WaitTimeout::infinite()) !=
            gxos::runtime::WaitResult::Signaled) {
            return 0;
        }
        return reinterpret_cast<std::uintptr_t>(probe->runtimeContext);
    }
}

HelperThreadProbe* createHelperThreadProbe(void* runtimeContext) {
    HelperThreadProbe* probe = new (std::nothrow) HelperThreadProbe(runtimeContext);
    if (probe == nullptr) {
        return nullptr;
    }

    gxos::runtime::ThreadCreateOptions options;
    options.debugName = "runtime-pack-helper-probe";
    if (gxos::runtime::createThread(helperThreadEntry, probe, options,
                                    &probe->thread) != gxos::runtime::ThreadResult::Ok) {
        delete probe;
        return nullptr;
    }
    return probe;
}

gxos::runtime::EventStatus startHelperThreadProbe(HelperThreadProbe* probe) {
    return probe == nullptr
        ? gxos::runtime::EventStatus::Invalid
        : probe->start.signal();
}

gxos::runtime::WaitResult joinHelperThreadProbe(
    HelperThreadProbe* probe,
    const gxos::runtime::WaitTimeout& timeout,
    std::uintptr_t* result) {
    if (probe == nullptr || probe->joined) {
        return gxos::runtime::WaitResult::Invalid;
    }
    const gxos::runtime::WaitResult waitResult =
        gxos::runtime::joinThread(probe->thread, timeout, result);
    if (waitResult == gxos::runtime::WaitResult::Signaled) {
        probe->joined = true;
    }
    return waitResult;
}

gxos::runtime::ThreadResult destroyHelperThreadProbe(HelperThreadProbe* probe) {
    if (probe == nullptr) {
        return gxos::runtime::ThreadResult::InvalidArgument;
    }
    if (!probe->joined) {
        const gxos::runtime::EventStatus startStatus = probe->start.signal();
        if (startStatus != gxos::runtime::EventStatus::Ok) {
            return gxos::runtime::ThreadResult::ProcessTeardown;
        }
        if (joinHelperThreadProbe(probe, gxos::runtime::WaitTimeout::infinite(), nullptr) !=
            gxos::runtime::WaitResult::Signaled) {
            return gxos::runtime::ThreadResult::ProcessTeardown;
        }
    }
    (void)probe->start.close();
    delete probe;
    return gxos::runtime::ThreadResult::Ok;
}

} // namespace nativeaot
} // namespace guidexos

