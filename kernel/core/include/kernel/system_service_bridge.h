#ifndef KERNEL_SYSTEM_SERVICE_BRIDGE_H
#define KERNEL_SYSTEM_SERVICE_BRIDGE_H

namespace kernel {
namespace system_service_bridge {

// Initializes the trusted host-runtime service channel on the second 16550
// UART (COM2). Requests are serviced from the normal kernel main loop.
void init();
void poll();

} // namespace system_service_bridge
} // namespace kernel

#endif
