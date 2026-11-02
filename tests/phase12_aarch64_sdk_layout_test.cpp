#include <guidexos/abi.h>

#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(void*) == 8, "AARCH64 SDK targets require 64-bit pointers");
static_assert(alignof(gx_host_calls) == 8, "AARCH64 host-call table alignment changed");
static_assert(offsetof(gx_host_calls, size) == 0, "host-call size prefix moved");
static_assert(offsetof(gx_host_calls, version) == 4, "host-call version prefix moved");
static_assert(offsetof(gx_host_calls, guiVersion) == 240, "AARCH64 GUI version slot changed");
static_assert(offsetof(gx_host_calls, wait_event) == 280, "AARCH64 wait event slot changed");
static_assert(offsetof(gx_host_calls, bare_metal_build_project_start) == 288,
              "AARCH64 bare-metal build slot changed");
static_assert(offsetof(gx_host_calls, bare_metal_development_run_release) == 392,
              "AARCH64 final current callback changed");
static_assert(sizeof(gx_host_calls) == 400, "AARCH64 host-call table size changed");

int main() {
    return 0;
}
