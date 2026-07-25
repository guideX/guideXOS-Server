#include "guidexos_nativeaot_pal_abi_bridge.h"

#if defined(__GNUC__) && defined(__x86_64__)
#define GXOS_NO_INLINE __attribute__((noinline))
#else
#define GXOS_NO_INLINE
#endif

extern "C" GXOS_NO_INLINE uintptr_t
guidexos_nativeaot_pal_bridge_invoke_worker(
    guidexos_nativeaot_pal_win64_worker_entry entry,
    void* context) {
    return entry == nullptr ? 0 : entry(context);
}

extern "C" GXOS_NO_INLINE void
guidexos_nativeaot_pal_bridge_invoke_detach(
    guidexos_nativeaot_pal_win64_detach_callback callback,
    void* value) {
    if (callback != nullptr) callback(value);
}
