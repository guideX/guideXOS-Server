#pragma once

#include <stdint.h>

struct guidexos_nativeaot_rh_initialize_observation {
    uint32_t native_boolean;
    int32_t adapter_status;
};

// The NativeAOT ABI uses true for success. The isolated probe exposes a
// conventional zero-success integer status to its kernel caller.
constexpr guidexos_nativeaot_rh_initialize_observation
guidexos_nativeaot_observe_rh_initialize(bool native_result) {
    return {
        native_result ? 1u : 0u,
        native_result ? 0 : 1,
    };
}
