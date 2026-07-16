#pragma once

namespace kernel {
namespace qemu_display_configuration_persistence_proof {

// QEMU-only two-process coordinator. It uses the public display configuration
// service; it never edits the persisted store and never speaks QMP.
void run();

} // namespace qemu_display_configuration_persistence_proof
} // namespace kernel
