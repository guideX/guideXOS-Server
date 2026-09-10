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
    char architecture[16];
    uint64_t executableBytes;
    bool startMenuVisible;
};

// Scan the mounted /Apps tree for supported NativeElf packages.
void discover();

// Invalidate the bounded discovery table after Developer Studio publishes a
// package. The next lookup performs a normal App Model rescan.
void refresh();

// Launch a discovered package by display name, id, package directory, or the
// short PacMan compatibility label.  Returns only after the app exits.
bool launch(const char* appName);

// The ARM64 Phase-5 negative control reports this separately from a normal
// application return so the harness can prove rejection before entry.
bool last_launch_rejected_wrong_architecture();

// Whether a supported external NativeElf package was discovered.
bool is_available(const char* appName);

// Resolve a package through the same discovery table used by launch().  A
// non-null result means the package has a real bare-metal target, not merely a
// manifest entry.
const PackageInfo* lookup_package(const char* appName);

// Enumerate the same validated package table used by lookup_package().
uint32_t package_count();
const PackageInfo* package_at(uint32_t index);

/* Phase-9 concurrent runtime driver.  The loader remains architecture
 * specific, while the lifetime, event, quota, and scheduler contracts are
 * common kernel services used by both ARM64 application tasks. */
bool phase9_run_application(const char* appName, bool autoClose);
void phase9_service();
bool phase9_all_complete();
bool phase9_app_a_image_primed();
bool phase9_app_b_survived_a_close();
uint32_t phase9_app_a_launches();
uint64_t phase9_app_a_waits();
uint64_t phase9_app_a_wakes();
uint64_t phase9_app_b_waits();
uint64_t phase9_app_b_wakes();
bool phase9_vfs_exclusive();
void phase9_vfs_enter();
void phase9_vfs_exit();

/* Phase-10 uses the hardened Phase-9 runtime with a normal application-ID
 * launch.  The caller supplies no architecture-specific executable path. */
bool phase10_run_application(const char* appName, bool autoClose);
bool phase10_all_complete();
uint32_t phase10_launches();

// Phase 11 uses the same Phase-9 runtime and package resolver for the
// Developer Studio client and the freshly generated proof application.
bool phase11_run_developer_studio();
bool phase11_run_application(const char* appName, bool autoClose);

} // namespace native_elf
} // namespace kernel

#endif // KERNEL_NATIVE_ELF_BAREMETAL_H
