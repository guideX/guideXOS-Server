#pragma once

namespace kernel {
namespace qemu_display_configuration_control_proof {

// Bounded QEMU-only smoke coordinator. Every operation goes through the same
// public typed service endpoint used by bare-metal Display Options.
void run();

} // namespace qemu_display_configuration_control_proof
} // namespace kernel
