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
    char icon[64];
    char executable[160];
    char entryPoint[48];
    char abi[64];
    uint64_t executableBytes;
    bool startMenuVisible;
    // Whether the manifest grants "audio.output". Parsed boundedly at
    // discovery; gates the play_pcm host call like the hosted runtime.
    bool hasAudioOutput;
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

// Enumerate the same validated package table used by lookup_package().
uint32_t package_count();
const PackageInfo* package_at(uint32_t index);

// Kernel main-loop audio pump (MC6, always available, safe when idle).
void app_audio_pump();

// Kernel slide base for App Model audio DMA translation
// (base + (virt - 0x100000), same convention as nic/virtio/mmio).
void audio_set_kernel_physical_base(uint64_t physicalBase);

#if defined(GXOS_AUDIO_BOOT_SELFTEST)
// Opt-in bare-metal audio proof (MC6): runs the app-audio backend
// self-test against real hardware at boot and reports PASS/FAIL over
// serial. Defined in kernel/core/native_elf_baremetal.cpp; enabled via
// EXTRA_CFLAGS=-DGXOS_AUDIO_BOOT_SELFTEST (QEMU proof builds only).
void app_audio_boot_selftest();
// Opt-in app-level proof (MC6, same flag): launches the real AudioBeep
// sample and its permission-denied twin through the production path.
void app_audio_app_proof();
#endif

} // namespace native_elf
} // namespace kernel

#endif // KERNEL_NATIVE_ELF_BAREMETAL_H
