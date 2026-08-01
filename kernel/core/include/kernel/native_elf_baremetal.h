// Bare-metal Native ELF package discovery and execution.
//
// This is the small external-app bridge used when the desktop is running
// directly in the UEFI kernel.  It consumes the same /Apps package manifest
// and guidexos-c-abi-v1 table as the hosted runtime, but resolves package
// files through the kernel VFS and renders through the kernel compositor.

#ifndef KERNEL_NATIVE_ELF_BAREMETAL_H
#define KERNEL_NATIVE_ELF_BAREMETAL_H

#include <stdint.h>

namespace kernel {
namespace native_elf {

// Stable App Model metadata for a package that has passed manifest, target,
// ABI, and VFS executable validation.  The strings point at the discovered
// package table and remain valid until the next discovery pass.
struct PackageInfo {
    bool valid;
    char directory[64];
    char root[128];
    char id[80];
    char displayName[80];
    char executable[160];
    char entryPoint[48];
    char abi[64];
    uint64_t executableBytes;
};

// Scan the mounted /Apps tree for supported NativeElf packages.
void discover();

// Launch a discovered package by display name, id, package directory, or the
// short PacMan compatibility label.  Returns only after the app exits.
bool launch(const char* appName);

// Whether a supported external NativeElf package was discovered.
bool is_available(const char* appName);

// Resolve a package through the same discovery table used by launch().  A
// non-null result means the package has a real bare-metal target, not merely a
// manifest entry.
const PackageInfo* lookup_package(const char* appName);

} // namespace native_elf
} // namespace kernel

#endif // KERNEL_NATIVE_ELF_BAREMETAL_H
