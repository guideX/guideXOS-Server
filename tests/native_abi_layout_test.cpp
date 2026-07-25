#include <guidexos/abi.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

static_assert(sizeof(gx_event) == 32, "gx_event size must remain 32 bytes on amd64");
static_assert(alignof(gx_event) == 8, "gx_event alignment must remain 8 bytes on amd64");
static_assert(offsetof(gx_event, size) == 0, "gx_event.size offset changed");
static_assert(offsetof(gx_event, type) == 4, "gx_event.type offset changed");
static_assert(offsetof(gx_event, window) == 8, "gx_event.window offset changed");
static_assert(offsetof(gx_event, param1) == 16, "gx_event.param1 offset changed");
static_assert(offsetof(gx_event, param4) == 28, "gx_event.param4 offset changed");
static_assert(sizeof(gx_host_calls) == 120, "gx_host_calls size must remain 120 bytes on amd64");
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

constexpr uint64_t kPacManFrameWidth = 448;
constexpr uint64_t kPacManFrameHeight = 553;
constexpr uint64_t kPacManFrameStride = kPacManFrameWidth * 4;
constexpr uint64_t kPacManFrameBytes = kPacManFrameStride * kPacManFrameHeight;
static_assert(kPacManFrameStride == 1792, "PacMan retained-frame stride changed");
static_assert(kPacManFrameBytes == 990976, "PacMan retained-frame byte count changed");
static_assert(kPacManFrameBytes <= 0xFFFFFFFFull, "PacMan retained-frame size must fit ABI byte count");

int main() {
    const bool appended = offsetof(gx_host_calls, get_ticks_ms) >
                          offsetof(gx_host_calls, present_frame);
    const bool workspaceAppended = offsetof(gx_host_calls, file_write_all) >
                                   offsetof(gx_host_calls, file_list);
    if (!appended || !workspaceAppended) return 1;
    std::cout << "Native ABI layout test PASS\n";
    return 0;
}
