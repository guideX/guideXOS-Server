#include "../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_thread_adapter.h"

#include <cstdint>

int main() {
    std::uintptr_t contextValue = 0xA07A0u;
    guidexos::nativeaot::HelperThreadProbe* probe =
        guidexos::nativeaot::createHelperThreadProbe(&contextValue);
    if (probe == nullptr) {
        return 1;
    }

    if (guidexos::nativeaot::startHelperThreadProbe(probe) !=
            gxos::runtime::EventStatus::Ok) {
        (void)guidexos::nativeaot::destroyHelperThreadProbe(probe);
        return 2;
    }

    std::uintptr_t result = 0;
    if (guidexos::nativeaot::joinHelperThreadProbe(
            probe, gxos::runtime::WaitTimeout::infinite(), &result) !=
            gxos::runtime::WaitResult::Signaled ||
        result != reinterpret_cast<std::uintptr_t>(&contextValue)) {
        (void)guidexos::nativeaot::destroyHelperThreadProbe(probe);
        return 3;
    }

    if (guidexos::nativeaot::destroyHelperThreadProbe(probe) !=
        gxos::runtime::ThreadResult::Ok) {
        return 4;
    }

    // This is only an adapter mapping probe.  It intentionally performs no
    // runtime startup, managed-thread registration, finalization, or collect.
    return 0;
}

