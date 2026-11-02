#include <guidexos/abi.h>

#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(void*) == 8, "Phase 12 SDK targets require 64-bit pointers");
static_assert(alignof(gx_host_calls) == 8, "v0.5 host-call table alignment changed");
static_assert(offsetof(gx_host_calls, size) == 0, "host-call size prefix moved");
static_assert(offsetof(gx_host_calls, version) == 4, "host-call version prefix moved");
static_assert(offsetof(gx_host_calls, bare_metal_build_project_start) == 240,
              "v0.5 bare-metal build slot changed");
static_assert(offsetof(gx_host_calls, get_document_activation_path) == 448,
              "v0.5 document activation slot changed");
static_assert(offsetof(gx_host_calls, bare_metal_build_project_cancel) == 456,
              "v0.5 bare-metal Build cancellation slot changed");
static_assert(sizeof(gx_host_calls) == 464, "v0.5 host-call table size changed");

int main() {
    return 0;
}
