// VirtIO RNG Driver
//
// Narrow PCI transitional/legacy virtio-rng support for secure entropy on
// bare-metal x86/AMD64 QEMU guests.
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace virtio {
namespace rng {

enum Status : uint8_t {
    STATUS_NOT_INITIALIZED = 0,
    STATUS_DEVICE_NOT_FOUND,
    STATUS_UNSUPPORTED_ARCH,
    STATUS_UNSUPPORTED_VIRTIO_MODE,
    STATUS_QUEUE_SETUP_FAILED,
    STATUS_REQUEST_TIMEOUT,
    STATUS_SHORT_READ,
    STATUS_DEVICE_ERROR,
    STATUS_SUCCESS,
};

void set_kernel_physical_base(uint64_t physicalBase);
bool init();
bool fill(void* buffer, size_t len);
bool detected();
bool ready();
Status last_status();
const char* last_status_name();
const char* backend_name();

} // namespace rng
} // namespace virtio
} // namespace kernel
