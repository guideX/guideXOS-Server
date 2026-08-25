// Bounded provenance context for the kernel executable-text write guard.

#ifndef KERNEL_KERNEL_TEXT_GUARD_H
#define KERNEL_KERNEL_TEXT_GUARD_H

extern "C" void gxos_kernel_text_guard_set_context(const char* scenario, const char* stage);

#endif
