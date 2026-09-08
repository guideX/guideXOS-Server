#pragma once

#include <stdint.h>

namespace kernel {
namespace input {

// Platform discovery data passed from an architecture/platform driver to the
// common input provider.  The common layer never interprets the device bus.
struct PlatformInputDevice {
    uint64_t base;
    uint64_t size;
    uint32_t irq;
    uint32_t reserved;
};

} // namespace input
} // namespace kernel
