#include <guidexos/abi.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

static_assert(offsetof(gx_host_calls, get_ticks_ms) > offsetof(gx_host_calls, present_frame),
              "get_ticks_ms must be appended to the ABI table");
static_assert(sizeof(uint64_t) == 8, "Native ABI ticks must remain 64-bit");
static_assert(offsetof(gx_host_calls, file_stat) > offsetof(gx_host_calls, get_ticks_ms),
              "workspace file_stat must be appended to the ABI table");
static_assert(offsetof(gx_host_calls, file_read_workspace) > offsetof(gx_host_calls, file_stat),
              "workspace file_read must be appended after file_stat");
static_assert(offsetof(gx_host_calls, file_list) > offsetof(gx_host_calls, file_read_workspace),
              "workspace file_list must be appended after workspace file_read");
static_assert(offsetof(gx_host_calls, file_write_all) > offsetof(gx_host_calls, file_list),
              "workspace file_write must be appended after file_list");

int main() {
    const bool appended = offsetof(gx_host_calls, get_ticks_ms) >
                          offsetof(gx_host_calls, present_frame);
    const bool workspaceAppended = offsetof(gx_host_calls, file_write_all) >
                                   offsetof(gx_host_calls, file_list);
    if (!appended || !workspaceAppended) return 1;
    std::cout << "Native ABI layout test PASS\n";
    return 0;
}
