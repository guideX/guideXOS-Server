#include <type_traits>

#include "../src/probes/guidexos_nativeaot_rh_initialize_contract.h"

extern "C" bool __cdecl RhInitialize(bool isDll);

using RhInitializeNativeAotPrototype = bool (__cdecl *)(bool);
static_assert(sizeof(bool) == 1, "NativeAOT RhInitialize bool must be byte-wide");
static_assert(std::is_same<decltype(&RhInitialize),
                           RhInitializeNativeAotPrototype>::value,
              "RhInitialize must use the pinned bool(bool) C ABI");

int main() {
    const guidexos_nativeaot_rh_initialize_observation succeeded =
        guidexos_nativeaot_observe_rh_initialize(true);
    if (succeeded.native_boolean != 1u || succeeded.adapter_status != 0) {
        return 1;
    }

    const guidexos_nativeaot_rh_initialize_observation failed =
        guidexos_nativeaot_observe_rh_initialize(false);
    if (failed.native_boolean != 0u || failed.adapter_status != 1) {
        return 2;
    }

    return 0;
}
