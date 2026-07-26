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
static_assert(offsetof(gx_event, size) == 0, "gx_event size offset changed");
static_assert(offsetof(gx_event, type) == 4, "gx_event type offset changed");
static_assert(offsetof(gx_event, window) == 8, "gx_event window offset changed");
static_assert(offsetof(gx_event, param1) == 16, "gx_event param1 offset changed");
static_assert(sizeof(gx_event) == 32, "gx_event size changed");
static_assert(offsetof(gx_file_info, type) == 0, "gx_file_info type offset changed");
static_assert(offsetof(gx_file_info, reserved) == 4, "gx_file_info reserved offset changed");
static_assert(offsetof(gx_file_info, size) == 8, "gx_file_info size offset changed");
static_assert(sizeof(gx_file_info) == 16, "gx_file_info size changed");
static_assert(offsetof(gx_file_entry, type) == 0, "gx_file_entry type offset changed");
static_assert(offsetof(gx_file_entry, reserved) == 4, "gx_file_entry reserved offset changed");
static_assert(offsetof(gx_file_entry, size) == 8, "gx_file_entry size offset changed");
static_assert(offsetof(gx_file_entry, name) == 16, "gx_file_entry name offset changed");
static_assert(sizeof(gx_file_entry) == 144, "gx_file_entry size changed");
static_assert(offsetof(gx_host_calls, log) == 8, "log slot changed");
static_assert(offsetof(gx_host_calls, get_api_version) == 16, "get_api_version slot changed");
static_assert(offsetof(gx_host_calls, request_window) == 24, "request_window slot changed");
static_assert(offsetof(gx_host_calls, file_read_all) == 72, "file_read_all slot changed");
static_assert(offsetof(gx_host_calls, file_exists) == 80, "file_exists slot changed");
static_assert(offsetof(gx_host_calls, request_window_ex) == 88, "request_window_ex slot changed");
static_assert(offsetof(gx_host_calls, file_read) == 96, "file_read slot changed");
static_assert(offsetof(gx_host_calls, present_frame) == 104, "present_frame slot changed");
static_assert(offsetof(gx_host_calls, get_ticks_ms) == 112, "get_ticks_ms slot changed");
static_assert(offsetof(gx_host_calls, file_stat) > offsetof(gx_host_calls, get_ticks_ms),
              "workspace file_stat must be appended to the ABI table");
static_assert(offsetof(gx_host_calls, file_read_workspace) > offsetof(gx_host_calls, file_stat),
              "workspace file_read must be appended after file_stat");
static_assert(offsetof(gx_host_calls, file_list) > offsetof(gx_host_calls, file_read_workspace),
              "workspace file_list must be appended after workspace file_read");
static_assert(offsetof(gx_host_calls, file_write_all) > offsetof(gx_host_calls, file_list),
              "workspace file_write must be appended after file_list");
static_assert(offsetof(gx_host_calls, file_stat) == 120, "file_stat slot changed");
static_assert(offsetof(gx_host_calls, file_read_workspace) == 128, "file_read_workspace slot changed");
static_assert(offsetof(gx_host_calls, file_list) == 136, "file_list slot changed");
static_assert(offsetof(gx_host_calls, file_write_all) == 144, "file_write_all slot changed");
static_assert(offsetof(gx_host_calls, file_create_directory) == 152, "file_create_directory slot changed");
static_assert(offsetof(gx_host_calls, file_remove) == 160, "file_remove slot changed");
static_assert(sizeof(gx_host_calls) == 168, "gx_host_calls size changed");

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
