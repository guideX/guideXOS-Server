#include <guidexos/abi.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

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
static_assert(offsetof(gx_host_calls, build_project_start) == 168, "build start slot changed");
static_assert(offsetof(gx_host_calls, build_project_poll) == 176, "build poll slot changed");
static_assert(offsetof(gx_host_calls, build_project_release) == 184, "build release slot changed");
static_assert(offsetof(gx_host_calls, development_run_prepare) == 192, "development run prepare slot changed");
static_assert(offsetof(gx_host_calls, development_run_start) == 200, "development run start slot changed");
static_assert(offsetof(gx_host_calls, development_run_poll) == 208, "development run poll slot changed");
static_assert(offsetof(gx_host_calls, development_run_request_close) == 216, "development run close slot changed");
static_assert(offsetof(gx_host_calls, development_run_release) == 224, "development run release slot changed");
static_assert(offsetof(gx_host_calls, development_debug) == 232, "development debug slot changed");
static_assert(sizeof(gx_host_calls) == 240, "gx_host_calls size changed");
static_assert(sizeof(gx_development_run_request) == 72, "development run request size changed");
static_assert(offsetof(gx_development_run_request, projectRoot) == 8, "development run request project root offset changed");
static_assert(offsetof(gx_development_run_request, artifactSha256) == 56, "development run request artifact hash offset changed");
static_assert(offsetof(gx_development_run_request, flags) == 64, "development run request flags offset changed");
static_assert(offsetof(gx_development_run_request, reserved) == 68, "development run request reserved offset changed");
static_assert(sizeof(gx_development_debug_request) == 88, "development debug request size changed");
static_assert(offsetof(gx_development_debug_request, threadId) == 72, "development debug thread id offset changed");
static_assert(offsetof(gx_development_debug_request, stopGeneration) == 80, "development debug stop generation offset changed");
static_assert(sizeof(gx_development_debug_register_context) == 192, "development debug register context size changed");
static_assert(sizeof(gx_development_debug_snapshot) == 416, "development debug snapshot size changed");
static_assert(sizeof(gx_development_run_snapshot) == 448, "development run snapshot size changed");
static_assert(offsetof(gx_development_run_snapshot, processId) == 24, "development run process id offset changed");
static_assert(offsetof(gx_development_run_snapshot, applicationId) == 56, "development run application id offset changed");
static_assert(sizeof(gx_build_request) == 72, "build request size changed");
static_assert(offsetof(gx_build_request, projectRoot) == 8, "build request project root offset changed");
static_assert(offsetof(gx_build_request, configuration) == 64, "build request configuration offset changed");
static_assert(sizeof(gx_build_output_line) == 260, "build output line size changed");
static_assert(offsetof(gx_build_snapshot, handle) == 8, "build snapshot handle offset changed");
static_assert(offsetof(gx_build_snapshot, artifactPath) == 72, "build snapshot artifact offset changed");
static_assert(sizeof(gx_build_snapshot) == 8784, "build snapshot size changed");

int main() {
    const bool appended = offsetof(gx_host_calls, get_ticks_ms) >
                          offsetof(gx_host_calls, present_frame);
    const bool workspaceAppended = offsetof(gx_host_calls, file_write_all) >
                                   offsetof(gx_host_calls, file_list);
    const bool buildAppended = offsetof(gx_host_calls, build_project_start) >
                               offsetof(gx_host_calls, file_remove);
    const bool runAppended = offsetof(gx_host_calls, development_run_prepare) >
                             offsetof(gx_host_calls, build_project_release);
    if (!appended || !workspaceAppended || !buildAppended || !runAppended) return 1;
    std::cout << "Native ABI layout test PASS\n";
    return 0;
}
