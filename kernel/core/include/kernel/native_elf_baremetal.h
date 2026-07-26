// Bare-metal Native ELF package discovery and execution.
//
// This is the small external-app bridge used when the desktop is running
// directly in the UEFI kernel.  It consumes the same /Apps package manifest
// and guidexos-c-abi-v1 table as the hosted runtime, but resolves package
// files through the kernel VFS and renders through the kernel compositor.

#ifndef KERNEL_NATIVE_ELF_BAREMETAL_H
#define KERNEL_NATIVE_ELF_BAREMETAL_H

namespace kernel {
namespace native_elf {

// Scan the mounted /Apps tree for supported NativeElf packages.
void discover();

// Launch a discovered package by display name, id, package directory, or the
// short PacMan compatibility label.  Returns only after the app exits.
bool launch(const char* appName);

// Whether a supported external NativeElf package was discovered.
bool is_available(const char* appName);

} // namespace native_elf
} // namespace kernel

#endif // KERNEL_NATIVE_ELF_BAREMETAL_H
