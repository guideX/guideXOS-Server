#include "guidexos_nativeaot_pal_contract.h"

#include <stdint.h>

// Keep the exact minipal symbol names while replacing the Windows QPC/SleepEx
// calls with the guideXOS monotonic clock and delay contract.
extern "C" int64_t minipal_hires_ticks() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_counter(&value) == 0
        ? static_cast<int64_t>(value) : 0;
}

extern "C" int64_t minipal_hires_tick_frequency() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_frequency(&value) == 0
        ? static_cast<int64_t>(value) : 0;
}

extern "C" void minipal_microdelay(uint32_t usecs, uint32_t* usecs_since_yield) {
    if (usecs > 1000u) {
        (void)guidexos_nativeaot_pal_sleep(usecs / 1000u);
        if (usecs_since_yield != nullptr) {
            *usecs_since_yield = 0;
        }
        return;
    }

    (void)guidexos_nativeaot_pal_yield();
    if (usecs_since_yield != nullptr) {
        *usecs_since_yield += usecs;
    }
}
