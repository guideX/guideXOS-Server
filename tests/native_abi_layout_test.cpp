#include <guidexos/abi.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

static_assert(offsetof(gx_host_calls, get_ticks_ms) > offsetof(gx_host_calls, present_frame),
              "get_ticks_ms must be appended to the ABI table");
static_assert(sizeof(uint64_t) == 8, "Native ABI ticks must remain 64-bit");

int main() {
    const bool appended = offsetof(gx_host_calls, get_ticks_ms) >
                          offsetof(gx_host_calls, present_frame);
    if (!appended) return 1;
    std::cout << "Native ABI layout test PASS\n";
    return 0;
}
