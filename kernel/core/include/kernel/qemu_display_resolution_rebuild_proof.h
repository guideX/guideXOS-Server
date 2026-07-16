#pragma once

namespace kernel {
namespace qemu_display_resolution_rebuild_proof {

// Bounded QEMU-only coordinator for logical-resolution resource rebuilds.
// Every mutation uses the typed display-configuration service.
void run();

} // namespace qemu_display_resolution_rebuild_proof
} // namespace kernel
