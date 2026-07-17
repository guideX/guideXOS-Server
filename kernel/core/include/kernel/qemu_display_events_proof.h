#pragma once

namespace kernel {
namespace qemu_display_events_proof {

// Bounded QEMU-only observer/rescan/injected-diff proof. It never applies a
// detected topology and never writes events_read.
void run();

} // namespace qemu_display_events_proof
} // namespace kernel
