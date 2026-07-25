#include "../platform/guidexos_nativeaot_pal_abi_bridge.h"

#include <stdint.h>
#include <stdio.h>

#if defined(_MSC_VER) && defined(_M_AMD64)
#define GXOS_PROBE_WIN64 __cdecl
#elif defined(__GNUC__) && defined(__x86_64__)
#define GXOS_PROBE_WIN64 __attribute__((ms_abi))
#else
#define GXOS_PROBE_WIN64
#endif

static void* g_expected_context = reinterpret_cast<void*>(static_cast<uintptr_t>(0x12345678));
static void* g_seen_value = nullptr;

extern "C" uintptr_t GXOS_PROBE_WIN64
probe_worker_callback(void* context) {
    return context == g_expected_context ? static_cast<uintptr_t>(0x1234) : 0;
}

extern "C" void GXOS_PROBE_WIN64
probe_detach_callback(void* value) {
    g_seen_value = value;
}

int main() {
    const uintptr_t result = guidexos_nativeaot_pal_bridge_invoke_worker(
        probe_worker_callback, g_expected_context);
    guidexos_nativeaot_pal_bridge_invoke_detach(
        probe_detach_callback, g_expected_context);

    const bool passed = result == static_cast<uintptr_t>(0x1234) &&
                        g_seen_value == g_expected_context;
    printf("abi-bridge worker=%s detach=%s result=0x%llx\n",
           result == static_cast<uintptr_t>(0x1234) ? "PASS" : "FAIL",
           g_seen_value == g_expected_context ? "PASS" : "FAIL",
           static_cast<unsigned long long>(result));
    return passed ? 0 : 1;
}
